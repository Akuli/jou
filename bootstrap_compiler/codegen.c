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
    assert(type);

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
    const FileTypes *filetypes;
    bool is_main_file;

    struct LocalVar locals[500];
    int nlocals;
    LLVMValueRef llvm_func;
    const Signature *signature;

    unsigned nloops;
    LLVMBasicBlockRef breaks[20];
    LLVMBasicBlockRef continues[20];
};

static LLVMValueRef build_expression(struct State *st, const AstExpression *expr);
static LLVMValueRef build_address_of_expression(struct State *st, const AstExpression *expr);
static void build_statement(struct State *st, const AstStatement *stmt);
static void build_body(struct State *st, const AstBody *body);

// TODO: Casts are unnecessary and these can be removed once we drop LLVM 14 support.
static void store(LLVMBuilderRef b, LLVMValueRef value, LLVMValueRef ptr)
{
    ptr = LLVMBuildBitCast(b, ptr, LLVMPointerType(LLVMTypeOf(value), 0), "legacy_llvm14_cast");
    LLVMBuildStore(b, value, ptr);
}
static LLVMValueRef stack_alloc(LLVMBuilderRef b, LLVMTypeRef type, const char *name)
{
    LLVMValueRef ptr = LLVMBuildAlloca(b, type, name);
    return LLVMBuildBitCast(b, ptr, type_to_llvm(voidPtrType), "legacy_llvm14_cast");  // TODO: is this needed?
}
static LLVMValueRef gep(LLVMBuilderRef b, LLVMTypeRef type, LLVMValueRef ptr, LLVMValueRef *indices, unsigned num_indices, const char *name)
{
    ptr = LLVMBuildBitCast(b, ptr, LLVMPointerType(type, 0), "legacy_llvm14_cast");  // TODO: is this needed?
    LLVMValueRef result = LLVMBuildGEP2(b, type, ptr, indices, num_indices, name);
    return LLVMBuildBitCast(b, result, type_to_llvm(voidPtrType), "legacy_llvm14_cast");  // TODO: is this needed?
}
static LLVMValueRef struct_gep(LLVMBuilderRef b, LLVMTypeRef type, LLVMValueRef ptr, unsigned idx, const char *name)
{
    ptr = LLVMBuildBitCast(b, ptr, LLVMPointerType(type, 0), "legacy_llvm14_cast");  // TODO: is this needed?
    LLVMValueRef result = LLVMBuildStructGEP2(b, type, ptr, idx, name);
    return LLVMBuildBitCast(b, result, type_to_llvm(voidPtrType), "legacy_llvm14_cast");  // TODO: is this needed?
}

static bool local_var_exists(const struct State *st, const char *name)
{
    for (int i = 0; i < st->nlocals; i++)
        if (!strcmp(st->locals[i].name, name))
            return true;
    return false;
}

static const struct LocalVar *get_local_var(const struct State *st, const char *name)
{
    for (int i = 0; i < st->nlocals; i++)
        if (!strcmp(st->locals[i].name, name))
            return &st->locals[i];
    assert(false);
}

static struct LocalVar *add_local_var(struct State *st, const Type *t, const char *name)
{
    assert(name && name[0]);
    for (int i = 0; i < st->nlocals; i++)
        assert(strcmp(st->locals[i].name, name) != 0);

    assert(st->nlocals < (int)(sizeof(st->locals) / sizeof(st->locals[0])));
    struct LocalVar *v = &st->locals[st->nlocals++];

    assert(strlen(name) < sizeof(v->name));
    strcpy(v->name, name);
    v->ptr = stack_alloc(st->builder, type_to_llvm(t), name);
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


static LLVMTypeRef signature_to_llvm(const Signature *sig)
{
    LLVMTypeRef *argtypes = malloc(sizeof(argtypes[0]) * sig->nargs);
    for (int i = 0; i < sig->nargs; i++)
        argtypes[i] = type_to_llvm(sig->argtypes[i]);

    LLVMTypeRef returntype;
    if (sig->returntype == NULL)
        returntype = LLVMVoidType();
    else
        returntype = type_to_llvm(sig->returntype);

    LLVMTypeRef result = LLVMFunctionType(returntype, argtypes, sig->nargs, sig->takes_varargs);
    free(argtypes);
    return result;
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

static LLVMValueRef build_function_call(struct State *st, const AstCall *call)
{
    const Signature *sig = NULL;
    for (const Signature *s = st->filetypes->functions.ptr; s < End(st->filetypes->functions); s++) {
        if (!strcmp(s->name, call->calledname)) {
            sig = s;
            break;
        }
    }
    assert(sig);

    assert(call->nargs < 100);
    LLVMValueRef args[100];
    for (int i = 0; i < call->nargs; i++) {
        args[i] = build_expression(st, &call->args[i]);
    }

    char debug_name[100] = "";
    if (sig->returntype != NULL)
        snprintf(debug_name, sizeof(debug_name), "%s_return_value", sig->name);

    LLVMValueRef func = declare_function_or_method(st, sig);
    LLVMTypeRef functype = signature_to_llvm(sig);

    return LLVMBuildCall2(st->builder, functype, func, args, call->nargs, debug_name);
}

static LLVMValueRef build_method_call(struct State *st, const AstCall *call, const AstExpression *self)
{
    assert(self);

    const Type *selfclass = type_of_expr(self);
    bool self_is_a_pointer = selfclass->kind == TYPE_POINTER;
    if (self_is_a_pointer)
        selfclass = selfclass->data.valuetype;
    assert(selfclass->kind == TYPE_CLASS);

    const Signature *sig = NULL;
    for (const Signature *s = selfclass->data.classdata.methods.ptr; s < End(selfclass->data.classdata.methods); s++) {
        assert(get_self_class(s) == selfclass);
        if (!strcmp(s->name, call->calledname)) {
            sig = s;
            break;
        }
    }
    assert(sig);

    // leave room for self
    assert(call->nargs <= 45);
    LLVMValueRef args[50];

    int k = 0;

    if (sig->argtypes[0] == get_pointer_type(selfclass) && type_of_expr(self) == selfclass) {
        // take address to pass self as pointer
        args[k++] = build_address_of_expression(st, self);
    } else if (sig->argtypes[0] == selfclass && type_of_expr(self) == get_pointer_type(selfclass)) {
        // dereference to pass self by value
        LLVMValueRef self_ptr = build_expression(st, self);
        args[k++] = LLVMBuildLoad2(st->builder, type_to_llvm(selfclass), self_ptr, "self");
    } else {
        assert(sig->argtypes[0] == type_of_expr(self));
        args[k++] = build_expression(st, self);
    }

    for (int i = 0; i < call->nargs; i++)
        args[k++] = build_expression(st, &call->args[i]);

    char debug_name[100] = "";
    if (sig->returntype != NULL)
        snprintf(debug_name, sizeof(debug_name), "%s_return_value", sig->name);

    LLVMValueRef func = declare_function_or_method(st, sig);
    LLVMTypeRef functype = signature_to_llvm(sig);

    return LLVMBuildCall2(st->builder, functype, func, args, k, debug_name);
}

static LLVMValueRef build_string_constant(const struct State *st, const char *s)
{
    LLVMValueRef array = LLVMConstString(s, strlen(s), false);
    LLVMValueRef global_var = LLVMAddGlobal(st->module, LLVMTypeOf(array), "string_literal");
    LLVMSetLinkage(global_var, LLVMPrivateLinkage);  // This makes it a static global variable
    LLVMSetInitializer(global_var, array);

    LLVMTypeRef string_type = LLVMPointerType(LLVMInt8Type(), 0);
    return LLVMBuildBitCast(st->builder, global_var, string_type, "string_ptr");
}

static LLVMValueRef build_constant(const struct State *st, const Constant *c)
{
    switch(c->kind) {
    case CONSTANT_BOOL:
        return LLVMConstInt(LLVMInt1Type(), c->data.boolean, false);
    case CONSTANT_INTEGER:
        return LLVMConstInt(type_to_llvm(type_of_constant(c)), c->data.integer.value, c->data.integer.is_signed);
    case CONSTANT_FLOAT:
    case CONSTANT_DOUBLE:
        return LLVMConstRealOfString(type_to_llvm(type_of_constant(c)), c->data.double_or_float_text);
    case CONSTANT_NULL:
        return LLVMConstNull(type_to_llvm(voidPtrType));
    case CONSTANT_STRING:
        return build_string_constant(st, c->data.str);
    case CONSTANT_ENUM_MEMBER:
        return LLVMConstInt(LLVMInt32Type(), c->data.enum_member.memberidx, false);
    }
    assert(0);
}

static LLVMValueRef build_signed_mod(LLVMBuilderRef builder, LLVMValueRef lhs, LLVMValueRef rhs)
{
    // Jou's % operator ensures that a%b has same sign as b:
    // jou_mod(a, b) = llvm_mod(llvm_mod(a, b) + b, b)
    LLVMValueRef llmod = LLVMBuildSRem(builder, lhs, rhs, "smod_tmp");
    LLVMValueRef sum = LLVMBuildAdd(builder, llmod, rhs, "smod_tmp");
    return LLVMBuildSRem(builder, sum, rhs, "smod");
}

static LLVMValueRef build_signed_div(LLVMBuilderRef builder, LLVMValueRef lhs, LLVMValueRef rhs)
{
    /*
    LLVM's provides two divisions. One truncates, the other is an "exact div"
    that requires there is no remainder. Jou uses floor division which is
    neither of the two, but is quite easy to implement:

        floordiv(a, b) = exact_div(a - jou_mod(a, b), b)
    */
    LLVMValueRef top = LLVMBuildSub(builder, lhs, build_signed_mod(builder, lhs, rhs), "sdiv_tmp");
    return LLVMBuildExactSDiv(builder, top, rhs, "sdiv");
}

static LLVMValueRef build_binop(
    struct State *st,
    enum AstExpressionKind op,
    LLVMValueRef lhs,
    const Type *lhstype,
    LLVMValueRef rhs,
    const Type *rhstype)
{
    // Always treat enums as integers
    if (lhstype->kind == TYPE_ENUM)
        lhstype = intType;
    if (rhstype->kind == TYPE_ENUM)
        rhstype = intType;

    bool got_bools = lhstype == boolType && rhstype == boolType;
    bool got_numbers = is_number_type(lhstype) && is_number_type(rhstype);
    bool got_ints = is_integer_type(lhstype) && is_integer_type(rhstype);
    bool got_uints = lhstype->kind == TYPE_UNSIGNED_INTEGER && rhstype->kind == TYPE_UNSIGNED_INTEGER;
    bool got_pointers = is_pointer_type(lhstype) && is_pointer_type(rhstype);

    assert(got_bools || got_numbers || got_pointers);
    if (got_uints)
        assert(got_ints);
    if (got_ints)
        assert(got_numbers);

    if (got_uints || got_bools) {
        switch(op) {
            case AST_EXPR_EQ: return LLVMBuildICmp(st->builder, LLVMIntEQ, lhs, rhs, "ueq");
            case AST_EXPR_NE: return LLVMBuildICmp(st->builder, LLVMIntNE, lhs, rhs, "une");
            case AST_EXPR_GT: return LLVMBuildICmp(st->builder, LLVMIntUGT, lhs, rhs, "ugt");
            case AST_EXPR_GE: return LLVMBuildICmp(st->builder, LLVMIntUGE, lhs, rhs, "uge");
            case AST_EXPR_LT: return LLVMBuildICmp(st->builder, LLVMIntULT, lhs, rhs, "ult");
            case AST_EXPR_LE: return LLVMBuildICmp(st->builder, LLVMIntULE, lhs, rhs, "ule");
            case AST_EXPR_ADD: return LLVMBuildAdd(st->builder, lhs, rhs, "uadd");
            case AST_EXPR_SUB: return LLVMBuildSub(st->builder, lhs, rhs, "usub");
            case AST_EXPR_MUL: return LLVMBuildMul(st->builder, lhs, rhs, "umul");
            case AST_EXPR_DIV: return LLVMBuildUDiv(st->builder, lhs, rhs, "udiv");
            case AST_EXPR_MOD: return LLVMBuildURem(st->builder, lhs, rhs, "urem");
            default: assert(0);
        }
    }

    if (got_ints) {
        assert(!got_uints);
        switch(op) {
            case AST_EXPR_EQ: return LLVMBuildICmp(st->builder, LLVMIntEQ, lhs, rhs, "ieq");
            case AST_EXPR_NE: return LLVMBuildICmp(st->builder, LLVMIntNE, lhs, rhs, "ine");
            case AST_EXPR_GT: return LLVMBuildICmp(st->builder, LLVMIntSGT, lhs, rhs, "igt");
            case AST_EXPR_GE: return LLVMBuildICmp(st->builder, LLVMIntSGE, lhs, rhs, "ige");
            case AST_EXPR_LT: return LLVMBuildICmp(st->builder, LLVMIntSLT, lhs, rhs, "ilt");
            case AST_EXPR_LE: return LLVMBuildICmp(st->builder, LLVMIntSLE, lhs, rhs, "ile");
            case AST_EXPR_ADD: return LLVMBuildAdd(st->builder, lhs, rhs, "iadd");
            case AST_EXPR_SUB: return LLVMBuildSub(st->builder, lhs, rhs, "isub");
            case AST_EXPR_MUL: return LLVMBuildMul(st->builder, lhs, rhs, "imul");
            case AST_EXPR_DIV: return build_signed_div(st->builder, lhs, rhs);
            case AST_EXPR_MOD: return build_signed_mod(st->builder, lhs, rhs);
            default: assert(0);
        }
    }

    if (got_numbers) {
        assert(!got_ints);
        switch(op) {
            case AST_EXPR_EQ: return LLVMBuildFCmp(st->builder, LLVMRealOEQ, lhs, rhs, "feq");
            case AST_EXPR_NE: return LLVMBuildFCmp(st->builder, LLVMRealONE, lhs, rhs, "fne");
            case AST_EXPR_GT: return LLVMBuildFCmp(st->builder, LLVMRealOGT, lhs, rhs, "fgt");
            case AST_EXPR_GE: return LLVMBuildFCmp(st->builder, LLVMRealOGE, lhs, rhs, "fge");
            case AST_EXPR_LT: return LLVMBuildFCmp(st->builder, LLVMRealOLT, lhs, rhs, "flt");
            case AST_EXPR_LE: return LLVMBuildFCmp(st->builder, LLVMRealOLE, lhs, rhs, "fle");
            case AST_EXPR_ADD: return LLVMBuildFAdd(st->builder, lhs, rhs, "fadd");
            case AST_EXPR_SUB: return LLVMBuildFSub(st->builder, lhs, rhs, "fsub");
            case AST_EXPR_MUL: return LLVMBuildFMul(st->builder, lhs, rhs, "fmul");
            case AST_EXPR_DIV: return LLVMBuildFDiv(st->builder, lhs, rhs, "fdiv");
            case AST_EXPR_MOD: return LLVMBuildFRem(st->builder, lhs, rhs, "fmod");
            default: assert(0);
        }
    }

    assert(0);
}

static void build_inplace_binop(
    struct State *st,
    enum AstExpressionKind op,
    const AstExpression *lhs,
    const AstExpression *rhs)
{
    LLVMValueRef lhsptr = build_address_of_expression(st, lhs);
    LLVMValueRef rhsvalue = build_expression(st, rhs);

    LLVMValueRef old_value = LLVMBuildLoad2(st->builder, type_to_llvm(type_of_expr(lhs)), lhsptr, "old_value");
    LLVMValueRef new_value = build_binop(st, op, old_value, type_of_expr(lhs), rhsvalue, type_of_expr(rhs));
    store(st->builder, new_value, lhsptr);
}

static LLVMValueRef build_class_field_pointer(struct State *st, const Type *classtype, LLVMValueRef instanceptr, const char *fieldname)
{
    for (struct ClassField *f = classtype->data.classdata.fields.ptr; f < End(classtype->data.classdata.fields); f++)
        if (!strcmp(f->name, fieldname))
            return struct_gep(st->builder, type_to_llvm(classtype), instanceptr, f->union_id, fieldname);

    assert(0);
}

static LLVMValueRef build_brace_init(struct State *st, const AstCall *call)
{
    const Type *classtype = NULL;
    for (const Type **t = st->filetypes->types.ptr; t < End(st->filetypes->types); t++) {
        if (!strcmp((*t)->name, call->calledname)) {
            classtype = *t;
            break;
        }
    }

    assert(classtype != NULL);
    assert(classtype->kind == TYPE_CLASS);

    LLVMValueRef ptr = stack_alloc(st->builder, type_to_llvm(classtype), "new_instance_ptr");

    LLVMValueRef size = LLVMSizeOf(type_to_llvm(classtype));
    LLVMBuildMemSet(st->builder, ptr, LLVMConstInt(LLVMInt8Type(), 0, false), size, 0);

    for (int i = 0; i < call->nargs; i++) {
        LLVMValueRef fieldptr = build_class_field_pointer(st, classtype, ptr, call->argnames[i]);
        LLVMValueRef fieldval = build_expression(st, &call->args[i]);
        store(st->builder, fieldval, fieldptr);
    }

    return LLVMBuildLoad2(st->builder, type_to_llvm(classtype), ptr, "new_instance");
}

static LLVMValueRef build_cast(struct State *st, LLVMValueRef obj, const Type *from, const Type *to)
{
    assert(from != NULL);
    assert(to != NULL);

    // Always treat enums as ints
    if (from->kind == TYPE_ENUM)
        from = intType;
    if (to->kind == TYPE_ENUM)
        to = intType;

    if (from == to)
        return obj;

    if (is_pointer_type(from) && is_pointer_type(to)) {
        // All pointers are the same type in LLVM
        return obj;
    }

    if (is_number_type(from) && is_number_type(to)) {
        if (is_integer_type(from) && is_integer_type(to)) {
            // Examples:
            //  signed 8-bit 0xFF (-1) --> 16-bit 0xFFFF (-1 or max value)
            //  unsigned 8-bit 0xFF (255) --> 16-bit 0x00FF (255)
            return LLVMBuildIntCast2(st->builder, obj, type_to_llvm(to), from->kind == TYPE_SIGNED_INTEGER, "cast");
        }
        if (is_integer_type(from) && to->kind == TYPE_FLOATING_POINT) {
            // integer --> double/float
            if (from->kind == TYPE_SIGNED_INTEGER)
                return LLVMBuildSIToFP(st->builder, obj, type_to_llvm(to), "cast");
            else
                return LLVMBuildUIToFP(st->builder, obj, type_to_llvm(to), "cast");
        }
        if (from->kind == TYPE_FLOATING_POINT && is_integer_type(to)) {
            // double/float --> integer
            if (to->kind == TYPE_SIGNED_INTEGER)
                return LLVMBuildFPToSI(st->builder, obj, type_to_llvm(to), "cast");
            else
                return LLVMBuildFPToUI(st->builder, obj, type_to_llvm(to), "cast");
        }
        if (from->kind == TYPE_FLOATING_POINT && to->kind == TYPE_FLOATING_POINT) {
            // double/float --> double/float
            return LLVMBuildFPCast(st->builder, obj, type_to_llvm(to), "cast");
        }
        assert(0);
    }

    if (is_integer_type(from) && is_pointer_type(to))
        return LLVMBuildIntToPtr(st->builder, obj, type_to_llvm(to), "cast");
    if (is_pointer_type(from) && is_integer_type(to))
        return LLVMBuildPtrToInt(st->builder, obj, type_to_llvm(to), "cast");

    if (from == boolType && is_integer_type(to))
        return LLVMBuildIntCast2(st->builder, obj, type_to_llvm(to), false, "cast");

    printf("unimpl cast %s --> %s\n", from->name, to->name);
    assert(0);
}

static LLVMValueRef build_and(struct State *st, const AstExpression *lhsexpr, const AstExpression *rhsexpr)
{
    /*
    Must be careful with side effects.

        # lhs returning False means we don't evaluate rhs
        if lhs:
            result = rhs
        else:
            result = False
    */
    LLVMBasicBlockRef lhstrue = LLVMAppendBasicBlock(st->llvm_func, "lhstrue");
    LLVMBasicBlockRef lhsfalse = LLVMAppendBasicBlock(st->llvm_func, "lhsfalse");
    LLVMBasicBlockRef done = LLVMAppendBasicBlock(st->llvm_func, "done");

    LLVMValueRef resultptr = stack_alloc(st->builder, type_to_llvm(boolType), "and_ptr");

    // if lhs:
    LLVMBuildCondBr(st->builder, build_expression(st, lhsexpr), lhstrue, lhsfalse);
    LLVMPositionBuilderAtEnd(st->builder, lhstrue);
    // result = rhs
    store(st->builder, build_expression(st, rhsexpr), resultptr);
    // end if
    LLVMBuildBr(st->builder, done);
    // else:
    LLVMPositionBuilderAtEnd(st->builder, lhsfalse);
    // result = False
    store(st->builder, LLVMConstInt(LLVMInt1Type(), false, false), resultptr);
    // end else
    LLVMBuildBr(st->builder, done);
    LLVMPositionBuilderAtEnd(st->builder, done);

    return LLVMBuildLoad2(st->builder, LLVMInt1Type(), resultptr, "and");
}

static LLVMValueRef build_or(struct State *st, const AstExpression *lhsexpr, const AstExpression *rhsexpr)
{
    /*
    Must be careful with side effects.

        # lhs returning True means we don't evaluate rhs
        if lhs:
            result = True
        else:
            result = rhs
    */
    LLVMBasicBlockRef lhstrue = LLVMAppendBasicBlock(st->llvm_func, "lhstrue");
    LLVMBasicBlockRef lhsfalse = LLVMAppendBasicBlock(st->llvm_func, "lhsfalse");
    LLVMBasicBlockRef done = LLVMAppendBasicBlock(st->llvm_func, "done");

    LLVMValueRef resultptr = stack_alloc(st->builder, type_to_llvm(boolType), "or_ptr");

    // if lhs:
    LLVMBuildCondBr(st->builder, build_expression(st, lhsexpr), lhstrue, lhsfalse);
    LLVMPositionBuilderAtEnd(st->builder, lhstrue);
    // result = True
    store(st->builder, LLVMConstInt(LLVMInt1Type(), true, false), resultptr);
    // end if
    LLVMBuildBr(st->builder, done);
    // else:
    LLVMPositionBuilderAtEnd(st->builder, lhsfalse);
    // result = rhs
    store(st->builder, build_expression(st, rhsexpr), resultptr);
    // end else
    LLVMBuildBr(st->builder, done);
    LLVMPositionBuilderAtEnd(st->builder, done);

    return LLVMBuildLoad2(st->builder, LLVMInt1Type(), resultptr, "or");
}

static LLVMValueRef build_increment_or_decrement(struct State *st, const AstExpression *inner, bool pre, int diff)
{
    assert(diff==1 || diff==-1);  // 1=increment, -1=decrement

    const Type *t = type_of_expr(inner);
    assert(is_integer_type(t) || is_pointer_type(t));

    LLVMValueRef ptr = build_address_of_expression(st, inner);
    LLVMValueRef old_value = LLVMBuildLoad2(st->builder, type_to_llvm(t), ptr, "old_value");

    LLVMValueRef new_value;
    if (is_integer_type(t)) {
        LLVMValueRef diff_llvm = LLVMConstInt(type_to_llvm(t), diff, true);
        new_value = LLVMBuildAdd(st->builder, old_value, diff_llvm, "new_value");
    } else if (is_number_type(t)) {
        assert(t == floatType || t == doubleType);
        LLVMValueRef diff_llvm = LLVMConstRealOfString(type_to_llvm(t), diff>0 ? "1" : "-1");
        new_value = LLVMBuildFAdd(st->builder, old_value, diff_llvm, "new_value");
    } else if (t->kind == TYPE_POINTER) {
        LLVMValueRef diff_llvm = LLVMConstInt(LLVMInt64Type(), diff, true);
        new_value = gep(st->builder, type_to_llvm(t->data.valuetype), old_value, &diff_llvm, 1, "new_value");
    } else {
        assert(false);
    }

    store(st->builder, new_value, ptr);
    return pre ? new_value : old_value;
}

static int find_enum_member(const Type *enumtype, const char *name)
{
    for (int i = 0; i < enumtype->data.enummembers.count; i++)
        if (!strcmp(enumtype->data.enummembers.names[i], name))
            return i;
    assert(0);
}

static LLVMValueRef build_expression_without_implicit_cast(struct State *st, const AstExpression *expr)
{
    switch(expr->kind) {
    case AST_EXPR_CONSTANT:
        return build_constant(st, &expr->data.constant);
    case AST_EXPR_GET_ENUM_MEMBER:
        return LLVMConstInt(LLVMInt32Type(), find_enum_member(type_of_expr(expr), expr->data.enummember.membername), false);
    case AST_EXPR_FUNCTION_CALL:
        return build_function_call(st, &expr->data.call);
    case AST_EXPR_CALL_METHOD:
    case AST_EXPR_DEREF_AND_CALL_METHOD:
        return build_method_call(st, &expr->data.methodcall.call, expr->data.methodcall.obj);
    case AST_EXPR_BRACE_INIT:
        return build_brace_init(st, &expr->data.call);
    case AST_EXPR_ARRAY:
        assert(0); // TODO
        break;
    case AST_EXPR_GET_FIELD:
        {
            // Evaluate foo.bar as (&temp)->bar, where temp is a temporary copy of foo.
            // We need to copy, because it's not always possible to evaluate &foo.
            // For example, consider evaluating some_function().foo
            const Type *t = type_of_expr(expr->data.classfield.obj);
            assert(t->kind == TYPE_CLASS);
            LLVMValueRef obj = build_expression(st, expr->data.classfield.obj);
            LLVMValueRef ptr = stack_alloc(st->builder, type_to_llvm(t), "temp_copy");
            store(st->builder, obj, ptr);
            LLVMValueRef fieldptr = build_class_field_pointer(st, t, ptr, expr->data.classfield.fieldname);
            return LLVMBuildLoad2(st->builder, type_to_llvm(type_of_expr(expr)), fieldptr, "field");
        }
    case AST_EXPR_DEREF_AND_GET_FIELD:
    case AST_EXPR_INDEXING:
        /*
        ptr->foo can always be evaluated as *(&ptr->foo).

        We can't always do this. For example, this doesn't work with the '.' operator.
        When evaluating some_function().foo, we can't evaluate &some_function().
        */
        return LLVMBuildLoad2(
            st->builder,
            type_to_llvm(expr->types.type),
            build_address_of_expression(st, expr),
            "dereffed");
    case AST_EXPR_GET_VARIABLE:
        {
            int c = get_special_constant(expr->data.varname);
            if (c == 0 || c == 1)
                return LLVMConstInt(LLVMInt1Type(), c, false);
        }
        return LLVMBuildLoad2(
            st->builder,
            type_to_llvm(expr->types.type),
            build_address_of_expression(st, expr),
            "dereffed");
    case AST_EXPR_AS:
        return build_cast(st, build_expression(st, expr->data.as.obj), type_of_expr(expr->data.as.obj), expr->types.type);
        return LLVMBuildLoad2(
            st->builder,
            type_to_llvm(expr->types.type),
            get_local_var(st, expr->data.varname)->ptr,
            expr->data.varname);
    case AST_EXPR_ADDRESS_OF:
        return build_address_of_expression(st, &expr->data.operands[0]);
    case AST_EXPR_SIZEOF:
        return LLVMSizeOf(type_to_llvm(type_of_expr(&expr->data.operands[0])));
    case AST_EXPR_DEREFERENCE:
        return LLVMBuildLoad2(
            st->builder,
            type_to_llvm(expr->types.type),
            build_expression(st, &expr->data.operands[0]),
            "dereference");
        break;
    case AST_EXPR_AND:
        return build_and(st, &expr->data.operands[0], &expr->data.operands[1]);
    case AST_EXPR_OR:
        return build_or(st, &expr->data.operands[0], &expr->data.operands[1]);
    case AST_EXPR_NOT:
        assert(0); // TODO
        break;
    case AST_EXPR_NEG:
        if (type_of_expr(&expr->data.operands[0])->kind == TYPE_FLOATING_POINT)
            return LLVMBuildFNeg(st->builder, build_expression(st, &expr->data.operands[0]), "fneg");
        else
            return LLVMBuildNeg(st->builder, build_expression(st, &expr->data.operands[0]), "ineg");
    case AST_EXPR_ADD:
    case AST_EXPR_SUB:
    case AST_EXPR_MUL:
    case AST_EXPR_DIV:
    case AST_EXPR_MOD:
    case AST_EXPR_EQ:
    case AST_EXPR_NE:
    case AST_EXPR_GT:
    case AST_EXPR_GE:
    case AST_EXPR_LT:
    case AST_EXPR_LE:
    {
        // Evaluation order of arguments isn't guaranteed in C, but is in Jou.
        // Make sure to evaluate lhs first.
        LLVMValueRef lhs = build_expression(st, &expr->data.operands[0]);
        LLVMValueRef rhs = build_expression(st, &expr->data.operands[1]);
        const Type *lhstype = type_of_expr(&expr->data.operands[0]);
        const Type *rhstype = type_of_expr(&expr->data.operands[1]);
        return build_binop(st, expr->kind, lhs, lhstype, rhs, rhstype);
    }
    case AST_EXPR_PRE_INCREMENT:
        return build_increment_or_decrement(st, &expr->data.operands[0], true, 1);
    case AST_EXPR_PRE_DECREMENT:
        return build_increment_or_decrement(st, &expr->data.operands[0], true, -1);
    case AST_EXPR_POST_INCREMENT:
        return build_increment_or_decrement(st, &expr->data.operands[0], false, 1);
    case AST_EXPR_POST_DECREMENT:
        return build_increment_or_decrement(st, &expr->data.operands[0], false, -1);
    }
    assert(0);
}

static LLVMValueRef build_expression(struct State *st, const AstExpression *expr)
{
    if (expr->types.implicit_array_to_pointer_cast)
        return build_address_of_expression(st, expr);

    if (expr->types.implicit_string_to_array_cast) {
        assert(expr->types.implicit_cast_type->kind == TYPE_ARRAY);
        assert(expr->types.implicit_cast_type->data.array.membertype == byteType);
        assert(expr->kind == AST_EXPR_CONSTANT);
        assert(expr->data.constant.kind == CONSTANT_STRING);

        size_t arrlen = expr->types.implicit_cast_type->data.array.len;
        const char *str = expr->data.constant.data.str;

        assert(strlen(str) < arrlen);
        char *padded = calloc(1, arrlen);
        strcpy(padded, str);

        LLVMValueRef result = LLVMConstString(padded, arrlen, true);
        free(padded);
        return result;
    }

    LLVMValueRef before_cast = build_expression_without_implicit_cast(st, expr);
    if (expr->types.implicit_cast_type == NULL)
        return before_cast;
    return build_cast(st, before_cast, expr->types.type, expr->types.implicit_cast_type);
}

static LLVMValueRef build_address_of_expression(struct State *st, const AstExpression *expr)
{
    switch(expr->kind) {
    case AST_EXPR_DEREF_AND_GET_FIELD:
    {
        // &ptr->field = ptr + memory offset, first evaluate ptr
        LLVMValueRef ptr = build_expression(st, expr->data.classfield.obj);
        const Type *t = type_of_expr(expr->data.classfield.obj);
        assert(t->kind == TYPE_POINTER);
        assert(t->data.valuetype->kind == TYPE_CLASS);
        return build_class_field_pointer(st, t->data.valuetype, ptr, expr->data.classfield.fieldname);
    }
    case AST_EXPR_GET_FIELD:
    {
        // &obj.field = &obj + memory offset
        LLVMValueRef ptr = build_address_of_expression(st, expr->data.classfield.obj);
        const Type *t = type_of_expr(expr->data.classfield.obj);
        assert(t->kind == TYPE_CLASS);
        return build_class_field_pointer(st, t, ptr, expr->data.classfield.fieldname);
    }
    case AST_EXPR_GET_VARIABLE:
        if (local_var_exists(st, expr->data.varname))
            return get_local_var(st, expr->data.varname)->ptr;
        else
            return LLVMGetNamedGlobal(st->module, expr->data.varname);
    case AST_EXPR_INDEXING:
    {
        // &ptr[index] = ptr + memory offset
        LLVMValueRef ptr = build_expression(st, &expr->data.operands[0]);
        LLVMValueRef index = build_expression(st, &expr->data.operands[1]);
        const Type *t = type_of_expr(&expr->data.operands[0]);
        assert(t->kind == TYPE_POINTER);
        return gep(st->builder, type_to_llvm(t->data.valuetype), ptr, &index, 1, "indexing");
    }
    case AST_EXPR_DEREFERENCE:
        // &*foo = foo
        return build_expression(st, &expr->data.operands[0]);
    case AST_EXPR_CALL_METHOD:
    case AST_EXPR_DEREF_AND_CALL_METHOD:
    case AST_EXPR_SIZEOF:
    case AST_EXPR_ADDRESS_OF:
    case AST_EXPR_AND:
    case AST_EXPR_OR:
    case AST_EXPR_NOT:
    case AST_EXPR_ADD:
    case AST_EXPR_SUB:
    case AST_EXPR_MUL:
    case AST_EXPR_DIV:
    case AST_EXPR_MOD:
    case AST_EXPR_NEG:
    case AST_EXPR_EQ:
    case AST_EXPR_NE:
    case AST_EXPR_GT:
    case AST_EXPR_GE:
    case AST_EXPR_LT:
    case AST_EXPR_LE:
    case AST_EXPR_PRE_INCREMENT:
    case AST_EXPR_PRE_DECREMENT:
    case AST_EXPR_POST_INCREMENT:
    case AST_EXPR_POST_DECREMENT:
    case AST_EXPR_AS:
    case AST_EXPR_CONSTANT:
    case AST_EXPR_GET_ENUM_MEMBER:
    case AST_EXPR_FUNCTION_CALL:
    case AST_EXPR_BRACE_INIT:
    case AST_EXPR_ARRAY:
        assert(0);
    }
    assert(0);
}

static void build_if_statement(struct State *st, const AstIfStatement *ifst)
{
    LLVMBasicBlockRef done = LLVMAppendBasicBlock(st->llvm_func, "done");
    for (int i = 0; i < ifst->n_if_and_elifs; i++) {
        LLVMValueRef cond = build_expression(st, &ifst->if_and_elifs[i].condition);
        LLVMBasicBlockRef then = LLVMAppendBasicBlock(st->llvm_func, "then");
        LLVMBasicBlockRef otherwise = LLVMAppendBasicBlock(st->llvm_func, "otherwise");

        LLVMBuildCondBr(st->builder, cond, then, otherwise);
        LLVMPositionBuilderAtEnd(st->builder, then);
        build_body(st, &ifst->if_and_elifs[i].body);
        LLVMBuildBr(st->builder, done);
        LLVMPositionBuilderAtEnd(st->builder, otherwise);
    }

    build_body(st, &ifst->elsebody);
    LLVMBuildBr(st->builder, done);
    LLVMPositionBuilderAtEnd(st->builder, done);
}

static void build_loop(
    struct State *st,
    const AstStatement *init,
    const AstExpression *cond,
    const AstStatement *incr,
    const AstBody *body)
{
    LLVMBasicBlockRef condblock, bodyblock, incrblock, doneblock;

    condblock = LLVMAppendBasicBlock(st->llvm_func, "cond");  // evaluate condition and go to bodyblock or doneblock
    bodyblock = LLVMAppendBasicBlock(st->llvm_func, "body");  // run loop body and go to incrblock
    incrblock = LLVMAppendBasicBlock(st->llvm_func, "incr");  // run incr and go to condblock
    doneblock = LLVMAppendBasicBlock(st->llvm_func, "done");  // rest of the code goes here

    // When entering loop, start with init and then jump to condition
    if (init)
        build_statement(st, init);
    LLVMBuildBr(st->builder, condblock);

    // Evaluate condition and then jump to loop body or skip to after loop.
    LLVMPositionBuilderAtEnd(st->builder, condblock);
    LLVMBuildCondBr(st->builder, build_expression(st, cond), bodyblock, doneblock);

    // Within loop body, 'break' skips to after loop, 'continue' goes to incr.
    assert(st->nloops < sizeof(st->breaks)/sizeof(st->breaks[0]));
    assert(st->nloops < sizeof(st->continues)/sizeof(st->continues[0]));
    st->breaks[st->nloops] = doneblock;
    st->continues[st->nloops] = incrblock;
    st->nloops++;

    // Run loop body. When done, go to incr.
    LLVMPositionBuilderAtEnd(st->builder, bodyblock);
    build_body(st, body);
    LLVMBuildBr(st->builder, incrblock);

    // 'break' and 'continue' are not allowed after the loop body.
    assert(st->nloops > 0);
    st->nloops--;

    // Run incr and jump back to condition.
    LLVMPositionBuilderAtEnd(st->builder, incrblock);
    if (incr)
        build_statement(st, incr);
    LLVMBuildBr(st->builder, condblock);

    // Code after the loop goes to "loop done" part.
    LLVMPositionBuilderAtEnd(st->builder, doneblock);
}

static void build_match_statament(struct State *st, const AstMatchStatement *match_stmt)
{
    const AstExpression *matchobj_ast = &match_stmt->match_obj;
    LLVMValueRef matchobj = build_expression(st, matchobj_ast);
    LLVMBasicBlockRef done = LLVMAppendBasicBlock(st->llvm_func, "done");

    for (int i = 0; i < match_stmt->ncases; i++) {
    for (AstExpression *caseobj_ast = match_stmt->cases[i].case_objs; caseobj_ast < &match_stmt->cases[i].case_objs[match_stmt->cases[i].n_case_objs]; caseobj_ast++) {
        LLVMValueRef caseobj = build_expression(st, caseobj_ast);
        LLVMValueRef cond = build_binop(st, AST_EXPR_EQ, matchobj, type_of_expr(matchobj_ast), caseobj, type_of_expr(caseobj_ast));

        LLVMBasicBlockRef then = LLVMAppendBasicBlock(st->llvm_func, "then");
        LLVMBasicBlockRef otherwise = LLVMAppendBasicBlock(st->llvm_func, "otherwise");
        LLVMBuildCondBr(st->builder, cond, then, otherwise);

        LLVMPositionBuilderAtEnd(st->builder, then);
        build_body(st, &match_stmt->cases[i].body);
        LLVMBuildBr(st->builder, done);
        LLVMPositionBuilderAtEnd(st->builder, otherwise);
    }
    }

    build_body(st, &match_stmt->case_underscore);
    LLVMPositionBuilderAtEnd(st->builder, done);
}

static void build_statement(struct State *st, const AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_ASSERT:
        assert(0); // TODO
        break;
    case AST_STMT_RETURN:
        if (stmt->data.returnvalue)
            LLVMBuildRet(st->builder, build_expression(st, stmt->data.returnvalue));
        else
            LLVMBuildRetVoid(st->builder);
        // Put the rest of the code into an unreachable block
        LLVMPositionBuilderAtEnd(st->builder, LLVMAppendBasicBlock(st->llvm_func, "after_return"));
        break;
    case AST_STMT_IF:
        build_if_statement(st, &stmt->data.ifstatement);
        break;
    case AST_STMT_WHILE:
        build_loop(
            st, NULL, &stmt->data.whileloop.condition, NULL,
            &stmt->data.whileloop.body);
        break;
    case AST_STMT_FOR:
        build_loop(
            st, stmt->data.forloop.init, &stmt->data.forloop.cond, stmt->data.forloop.incr,
            &stmt->data.forloop.body);
        break;
    case AST_STMT_MATCH:
        build_match_statament(st, &stmt->data.match);
        break;
    case AST_STMT_BREAK:
        assert(0); // TODO
        break;
    case AST_STMT_CONTINUE:
        assert(0); // TODO
        break;
    case AST_STMT_DECLARE_LOCAL_VAR:
        if (stmt->data.vardecl.value) {
            const struct LocalVar *var = get_local_var(st, stmt->data.vardecl.name);
            LLVMValueRef value = build_expression(st, stmt->data.vardecl.value);
            store(st->builder, value, var->ptr);
        }
        break;
    case AST_STMT_ASSIGN:
    {
        // Refactoring note: Needs separate variables because evaluation order
        // of arguments is not guaranteed in C.
        LLVMValueRef lhsptr = build_address_of_expression(st, &stmt->data.assignment.target);
        LLVMValueRef rhs = build_expression(st, &stmt->data.assignment.value);
        store(st->builder, rhs, lhsptr);
        break;
    }
    case AST_STMT_INPLACE_ADD:
        build_inplace_binop(st, AST_EXPR_ADD, &stmt->data.assignment.target, &stmt->data.assignment.value);
        break;
    case AST_STMT_INPLACE_SUB:
        build_inplace_binop(st, AST_EXPR_SUB, &stmt->data.assignment.target, &stmt->data.assignment.value);
        break;
    case AST_STMT_INPLACE_MUL:
        build_inplace_binop(st, AST_EXPR_MUL, &stmt->data.assignment.target, &stmt->data.assignment.value);
        break;
    case AST_STMT_INPLACE_DIV:
        build_inplace_binop(st, AST_EXPR_DIV, &stmt->data.assignment.target, &stmt->data.assignment.value);
        break;
    case AST_STMT_INPLACE_MOD:
        build_inplace_binop(st, AST_EXPR_MOD, &stmt->data.assignment.target, &stmt->data.assignment.value);
        break;
    case AST_STMT_EXPRESSION_STATEMENT:
        build_expression(st, &stmt->data.expression);
        break;
    case AST_STMT_PASS:
        break; // nothing to do
    case AST_STMT_DEFINE_CLASS:
    case AST_STMT_DEFINE_ENUM:
    case AST_STMT_DECLARE_GLOBAL_VAR:
    case AST_STMT_DEFINE_GLOBAL_VAR:
    case AST_STMT_FUNCTION_DEF:
    case AST_STMT_FUNCTION_DECLARE:
        assert(0); // should never occur inside a function
        break;
    }
}

static void build_body(struct State *st, const AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        build_statement(st, &body->statements[i]);
}

#if defined(_WIN32) || defined(__APPLE__) || defined(__NetBSD__)
static void codegen_call_to_the_special_startup_function(const struct State *st)
{
    LLVMTypeRef functype = LLVMFunctionType(LLVMVoidType(), NULL, 0, false);
    LLVMValueRef func = LLVMAddFunction(st->module, "_jou_startup", functype);
    LLVMBuildCall2(st->builder, functype, func, NULL, 0, "");
}
#endif

static void build_function_or_method(
    struct State *st, const Signature *sig, const AstBody *body, bool public)
{
    // Methods are always public for now
    if (get_self_class(sig))
        assert(public);

    assert(st->signature == NULL);
    assert(st->llvm_func == NULL);
    st->signature = sig;
    st->llvm_func = declare_function_or_method(st, sig);
    assert(st->llvm_func != NULL);

    bool implicitly_public = !get_self_class(sig) && !strcmp(sig->name, "main") && st->is_main_file;
    if (!(public || implicitly_public))
        LLVMSetLinkage(st->llvm_func, LLVMPrivateLinkage);

    LLVMBasicBlockRef start = LLVMAppendBasicBlock(st->llvm_func, "start");
    LLVMPositionBuilderAtEnd(st->builder, start);

    // Allocate local variables
    const FunctionOrMethodTypes *fomtypes = NULL;
    for (FunctionOrMethodTypes *f = st->filetypes->fomtypes.ptr; f < End(st->filetypes->fomtypes); f++) {
        if (!strcmp(f->signature.name, sig->name) && get_self_class(&f->signature) == get_self_class(sig)) {
            fomtypes = f;
            break;
        }
    }
    assert(fomtypes);
    assert(st->nlocals == 0);
    for (LocalVariable **lv = fomtypes->locals.ptr; lv < End(fomtypes->locals); lv++) {
        add_local_var(st, (*lv)->type, (*lv)->name);
    }

    // Place arguments into the first n local variables.
    assert(st->nlocals >= sig->nargs);
    for (int i = 0; i < sig->nargs; i++)
        store(st->builder, LLVMGetParam(st->llvm_func, i), st->locals[i].ptr);

#if defined(_WIN32) || defined(__APPLE__) || defined(__NetBSD__)
    if (!get_self_class(sig) && !strcmp(sig->name, "main"))
        codegen_call_to_the_special_startup_function(st);
#endif

    build_body(st, body);

    if (sig->returntype)
        LLVMBuildUnreachable(st->builder);
    else
        LLVMBuildRetVoid(st->builder);

    assert(st->signature != NULL);
    assert(st->llvm_func != NULL);
    st->signature = NULL;
    st->llvm_func = NULL;
    st->nlocals = 0;
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
            build_function_or_method(&st, sig, &stmt->data.function.body, stmt->data.function.public);
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
                    build_function_or_method(&st, sig, &m->data.method.body, true);
                }
            }
        }
    }

    LLVMDisposeBuilder(st.builder);
    return st.module;
}
