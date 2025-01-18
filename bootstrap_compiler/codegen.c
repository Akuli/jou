#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include "jou_compiler.h"
#include "util.h"

/*
LLVM doesn't have a built-in union type, and you're supposed to abuse other types for that:
https://mapping-high-level-constructs-to-llvm-ir.readthedocs.io/en/latest/basic-constructs/unions.html

My first idea was to use an array of bytes that is big enough to fit anything.
However, that might not be aligned properly.

Then I tried choosing the member type that has the biggest align, and making a large enough array of it.
Because the align is always a power of two, the memory will be suitably aligned for all member types.
But it didn't work for some reason I still don't understand.

Then I figured out how clang does it and did it the same way.
We make a struct that contains:
- the most aligned type as chosen before
- array of i8 as padding to make it the right size.
But for some reason that didn't work either.

As a "last resort" I just use an array of i64 large enough and hope it's aligned as needed.
*/
static LLVMTypeRef codegen_union_type(const LLVMTypeRef *types, int ntypes)
{
    // For some reason uncommenting this makes stuff compile almost 2x slower...
    //if (ntypes == 1)
    //    return types[0];

    unsigned long long sizeneeded = 0;
    for (int i = 0; i < ntypes; i++) {
        unsigned long long size1 = LLVMABISizeOfType(get_target()->target_data_ref, types[i]);
        unsigned long long size2 = LLVMStoreSizeOfType(get_target()->target_data_ref, types[i]);

        // If this assert fails, you need to figure out which of the size functions should be used.
        // I don't know what their difference is.
        // And if you need the alignment, there's 3 different functions for that...
        assert(size1 == size2);
        sizeneeded = max(sizeneeded, size1);
    }
    return LLVMArrayType(LLVMInt64Type(), (sizeneeded+7)/8);
}

static LLVMTypeRef type_to_llvm(const Type *type)
{
    switch(type->kind) {
    case TYPE_ARRAY:
        return LLVMArrayType(type_to_llvm(type->data.array.membertype), type->data.array.len);
    case TYPE_POINTER:
    case TYPE_VOID_POINTER:
        // Element type doesn't matter in new LLVM versions.
        return LLVMPointerType(LLVMInt8Type(), 0);
    case TYPE_FLOATING_POINT:
        switch(type->data.width_in_bits) {
            case 32: return LLVMFloatType();
            case 64: return LLVMDoubleType();
            default: assert(0);
        }
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
        return LLVMIntType(type->data.width_in_bits);
    case TYPE_BOOL:
        return LLVMInt1Type();
    case TYPE_OPAQUE_CLASS:
        assert(0);
    case TYPE_CLASS:
        {
            int n = type->data.classdata.fields.len;

            LLVMTypeRef *flat_elems = malloc(sizeof(flat_elems[0]) * n);  // NOLINT
            for (int i = 0; i < n; i++)
                flat_elems[i] = type_to_llvm(type->data.classdata.fields.ptr[i].type);

            // Combine together fields of the same union.
            LLVMTypeRef *combined = malloc(sizeof(combined[0]) * n);  // NOLINT
            int combinedlen = 0;
            int start, end;
            for (start=0; start<n; start=end) {
                end = start+1;
                while (end < n && type->data.classdata.fields.ptr[start].union_id == type->data.classdata.fields.ptr[end].union_id)
                    end++;
                combined[combinedlen++] = codegen_union_type(&flat_elems[start], end-start);
            }

            LLVMTypeRef result = LLVMStructType(combined, combinedlen, false);
            free(flat_elems);
            free(combined);
            return result;
        }
    case TYPE_ENUM:
        return LLVMInt32Type();
    }
    assert(0);
}

struct LocalVar {
    char name[100];
    // All local variables are represented as pointers to stack space, even
    // if they are never reassigned. LLVM will optimize the mess.
    LLVMValueRef ptr;
};

struct State {
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    struct LocalVar locals[500];
    int nlocals;
    bool is_main_file;
    const FileTypes *filetypes;
    const FunctionOrMethodTypes *fomtypes;
    LLVMValueRef llvm_func;
};

// Return value may become invalid when adding more local vars.
static const struct LocalVar *find_local_var(const struct State *st, const char *name)
{
    for (int i = 0; i < st->nlocals; i++) {
        if (!strcmp(st->locals[i].name, name))
            return &st->locals[i];
    }
    return NULL;
}

static struct LocalVar *add_local_var(struct State *st, const Type *t, const char *name)
{
    if (!name)
        name = "";
    if (name[0])
        assert(find_local_var(st, name) == NULL);

    assert(st->nlocals < (int)(sizeof(st->locals) / sizeof(st->locals[0])));
    struct LocalVar *v = &st->locals[st->nlocals++];

    assert(strlen(name) < sizeof(v->name));
    strcpy(v->name, name);
    v->ptr = LLVMBuildAlloca(st->builder, type_to_llvm(t), name);
    return v;
}

static const Type *type_of_expr(const AstExpression *expr)
{
    assert(expr->types.type);
    if (expr->types.implicit_cast_type)
        return expr->types.implicit_cast_type;
    else
        return expr->types.type;
}

//static LLVMValueRef build_expression(struct State *st, const AstExpression *expr);
//static LLVMValueRef build_address_of_expression(struct State *st, const AstExpression *address_of_what);

static LLVMTypeRef signature_to_llvm(const Signature *sig)
{
    assert(sig->nargs < 50);
    LLVMTypeRef argtypes[50];
    for (int i = 0; i < sig->nargs; i++)
        argtypes[i] = type_to_llvm(sig->argtypes[i]);

    LLVMTypeRef returntype;
    if (sig->returntype == NULL)
        returntype = LLVMVoidType();
    else
        returntype = type_to_llvm(sig->returntype);

    return LLVMFunctionType(returntype, argtypes, sig->nargs, sig->takes_varargs);
}

static LLVMValueRef declare_function_or_method(const struct State *st, const Signature *sig)
{
    char fullname[200];
    if (get_self_class(sig))
        snprintf(fullname, sizeof fullname, "%s.%s", get_self_class(sig)->name, sig->name);
    else
        safe_strcpy(fullname, sig->name);

    // Make it so that this can be called many times without issue
    LLVMValueRef func = LLVMGetNamedFunction(st->module, fullname);
    if (func)
        return func;

    return LLVMAddFunction(st->module, fullname, signature_to_llvm(sig));
}

static void build_function_or_method(
    struct State *st,
    const Type *self_type,
    const Signature *sig,
    const AstBody *body,
    bool public)
{
    // Methods are always public for now
    if (self_type)
        assert(public);

    LLVMValueRef func = declare_function_or_method(st, sig);
    if (!public)
        LLVMSetLinkage(func, LLVMPrivateLinkage);

    assert(st->llvm_func == NULL);
    st->llvm_func = func;

    // TODO: body

    assert(st->llvm_func == func);
    st->llvm_func = NULL;
}

LLVMModuleRef codegen(const AstFile *ast, const FileTypes *ft, bool is_main_file)
{
    struct State st = {
        .filetypes = ft,
        .module = LLVMModuleCreateWithName(ast->path),
        .builder = LLVMCreateBuilder(),
        .is_main_file = is_main_file,
    };

    LLVMSetTarget(st.module, get_target()->triple);
    LLVMSetDataLayout(st.module, get_target()->data_layout);

    for (GlobalVariable *v = ft->globals.ptr; v < End(ft->globals); v++) {
        LLVMTypeRef t = type_to_llvm(v->type);
        LLVMValueRef globalptr = LLVMAddGlobal(st.module, t, v->name);
        if (v->defined_in_current_file)
            LLVMSetInitializer(globalptr, LLVMConstNull(t));
    }

    for (int i = 0; i < ast->body.nstatements; i++) {
        const AstStatement *stmt = &ast->body.statements[i];
        if(stmt->kind == AST_STMT_FUNCTION_DEF) {
            const Signature *sig = NULL;
            for (const Signature *s = ft->functions.ptr; s < End(ft->functions); s++) {
                if (!strcmp(s->name, stmt->data.function.signature.name)) {
                    sig = s;
                    break;
                }
            }
            assert(sig);
            build_function_or_method(&st, NULL, sig, &stmt->data.function.body, stmt->data.function.public);
        }

        if (stmt->kind == AST_STMT_DEFINE_CLASS) {
            const Type *classtype = NULL;
            for (Type **t = ft->owned_types.ptr; t < End(ft->owned_types); t++) {
                if (!strcmp((*t)->name, stmt->data.classdef.name)) {
                    classtype = *t;
                    break;
                }
            }
            assert(classtype);

            for (AstClassMember *m = stmt->data.classdef.members.ptr; m < End(stmt->data.classdef.members); m++) {
                if (m->kind == AST_CLASSMEMBER_METHOD) {
                    const Signature *sig = NULL;
                    for (const Signature *s = classtype->data.classdata.methods.ptr; s < End(classtype->data.classdata.methods); s++) {
                        if (!strcmp(s->name, m->data.method.signature.name)) {
                            sig = s;
                            break;
                        }
                    }
                    assert(sig);
                    build_function_or_method(&st, classtype, sig, &m->data.method.body, true);
                }
            }
        }
    }

    LLVMDisposeBuilder(st.builder);
    return st.module;
}
