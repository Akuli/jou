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
    //const FunctionOrMethodTypes *fomtypes;
    LLVMValueRef llvm_func;
    const Signature *signature;
};

// forward declarations
static LLVMValueRef build_expression(struct State *st, const AstExpression *expr);
static void build_body(struct State *st, const AstBody *body);

// Return value may become invalid when adding more local vars.
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

static LLVMValueRef build_call(struct State *st, const AstCall *call, const Signature *sig)
{
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
    const AstExpression *lhs_expr,
    const AstExpression *rhs_expr)
{
    LLVMValueRef lhs = build_expression(st, lhs_expr);
    LLVMValueRef rhs = build_expression(st, rhs_expr);
    const Type *lhstype = type_of_expr(lhs_expr);
    const Type *rhstype = type_of_expr(rhs_expr);

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
            case AST_EXPR_GT: return LLVMBuildICmp(st->builder, LLVMIntSGT, lhs, rhs, "ugt");
            case AST_EXPR_GE: return LLVMBuildICmp(st->builder, LLVMIntSGE, lhs, rhs, "uge");
            case AST_EXPR_LT: return LLVMBuildICmp(st->builder, LLVMIntSLT, lhs, rhs, "ult");
            case AST_EXPR_LE: return LLVMBuildICmp(st->builder, LLVMIntSLE, lhs, rhs, "ule");
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
            case AST_EXPR_GT: return LLVMBuildICmp(st->builder, LLVMIntUGT, lhs, rhs, "igt");
            case AST_EXPR_GE: return LLVMBuildICmp(st->builder, LLVMIntUGE, lhs, rhs, "ige");
            case AST_EXPR_LT: return LLVMBuildICmp(st->builder, LLVMIntULT, lhs, rhs, "ilt");
            case AST_EXPR_LE: return LLVMBuildICmp(st->builder, LLVMIntULE, lhs, rhs, "ile");
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

static LLVMValueRef build_cast(struct State *st, LLVMValueRef obj, const Type *from, const Type *to)
{
    assert(0);
}

static LLVMValueRef build_expression_without_implicit_cast(struct State *st, const AstExpression *expr)
{
    switch(expr->kind) {
    case AST_EXPR_CONSTANT:
        return build_constant(st, &expr->data.constant);
    case AST_EXPR_GET_ENUM_MEMBER:
        assert(0); // TODO
        break;
    case AST_EXPR_FUNCTION_CALL:
        {
            const Signature *sig = NULL;
            for (const Signature *s = st->filetypes->functions.ptr; s < End(st->filetypes->functions); s++) {
                if (!strcmp(s->name, expr->data.call.calledname)) {
                    sig = s;
                    break;
                }
            }
            assert(sig);
            return build_call(st, &expr->data.call, sig);
        }
    case AST_EXPR_BRACE_INIT:
        assert(0); // TODO
        break;
    case AST_EXPR_ARRAY:
        assert(0); // TODO
        break;
    case AST_EXPR_GET_FIELD:
        assert(0); // TODO
        break;
    case AST_EXPR_DEREF_AND_GET_FIELD:
        assert(0); // TODO
        break;
    case AST_EXPR_CALL_METHOD:
        assert(0); // TODO
        break;
    case AST_EXPR_DEREF_AND_CALL_METHOD:
        assert(0); // TODO
        break;
    case AST_EXPR_INDEXING:
        assert(0); // TODO
        break;
    case AST_EXPR_AS:
        assert(0); // TODO
        break;
    case AST_EXPR_GET_VARIABLE:
        return LLVMBuildLoad2(
            st->builder,
            type_to_llvm(type_of_expr(expr)),
            get_local_var(st, expr->data.varname)->ptr,
            expr->data.varname);
    case AST_EXPR_ADDRESS_OF:
        assert(0); // TODO
        break;
    case AST_EXPR_SIZEOF:
        assert(0); // TODO
        break;
    case AST_EXPR_DEREFERENCE:
        assert(0); // TODO
        break;
    case AST_EXPR_AND:
    case AST_EXPR_OR:
    case AST_EXPR_NOT:
        assert(0); // TODO
        break;
    case AST_EXPR_NEG:
        assert(0); // TODO
        break;
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
        return build_binop(st, expr->kind, &expr->data.operands[0], &expr->data.operands[1]);
    case AST_EXPR_PRE_INCREMENT:
    case AST_EXPR_PRE_DECREMENT:
    case AST_EXPR_POST_INCREMENT:
    case AST_EXPR_POST_DECREMENT:
        assert(0); // TODO
        break;
    }
    assert(0);
}

static LLVMValueRef build_expression(struct State *st, const AstExpression *expr)
{
    LLVMValueRef before_cast = build_expression_without_implicit_cast(st, expr);
    if (expr->types.implicit_cast_type == NULL)
        return before_cast;
    return build_cast(st, before_cast, expr->types.type, expr->types.implicit_cast_type);
}

// TODO: This function can be replaced with LLVMBuildStore() once we drop LLVM 14 support.
static void store(LLVMBuilderRef b, LLVMValueRef value, LLVMValueRef ptr)
{
    ptr = LLVMBuildBitCast(b, ptr, LLVMPointerType(LLVMTypeOf(value), 0), "legacy_llvm14_cast");
    LLVMBuildStore(b, value, ptr);
}

static LLVMValueRef build_address_of_expression(struct State *st, const AstExpression *expr)
{
    switch(expr->kind) {
    case AST_EXPR_DEREF_AND_GET_FIELD:
        assert(0); // TODO
        break;
    case AST_EXPR_GET_FIELD:
        assert(0); // TODO
        break;
    case AST_EXPR_GET_VARIABLE:
        return get_local_var(st, expr->data.varname)->ptr;
    case AST_EXPR_INDEXING:
        assert(0); // TODO
        break;
    case AST_EXPR_CALL_METHOD:
    case AST_EXPR_DEREF_AND_CALL_METHOD:
    case AST_EXPR_SIZEOF:
    case AST_EXPR_ADDRESS_OF:
    case AST_EXPR_DEREFERENCE:
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
        assert(0); // TODO
        break;
    case AST_STMT_FOR:
        assert(0); // TODO
        break;
    case AST_STMT_MATCH:
        assert(0); // TODO
        break;
    case AST_STMT_BREAK:
        assert(0); // TODO
        break;
    case AST_STMT_CONTINUE:
        assert(0); // TODO
        break;
    case AST_STMT_DECLARE_LOCAL_VAR:
        assert(0); // TODO
        break;
    case AST_STMT_ASSIGN:
    {
        // Refactoring note: Needs separate variables because evaluation order
        // of arguments is not guaranteed in C.
        LLVMValueRef destptr = build_address_of_expression(st, &stmt->data.assignment.target);
        LLVMValueRef value = build_expression(st, &stmt->data.assignment.value);
        store(st->builder, value, destptr);
        break;
    }
    case AST_STMT_INPLACE_ADD:
        assert(0); // TODO
        break;
    case AST_STMT_INPLACE_SUB:
        assert(0); // TODO
        break;
    case AST_STMT_INPLACE_MUL:
        assert(0); // TODO
        break;
    case AST_STMT_INPLACE_DIV:
        assert(0); // TODO
        break;
    case AST_STMT_INPLACE_MOD:
        assert(0); // TODO
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
