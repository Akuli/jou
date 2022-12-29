#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include "jou_compiler.h"
#include "util.h"

struct LocalVariable {
    char name[100];
    LLVMValueRef pointer;
};

struct State {
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    const struct AstFunctionDef *ast_func;
    LLVMValueRef llvm_func;
    LLVMValueRef *llvm_locals;
};

static LLVMValueRef get_pointer_to_local_var(const struct State *st, const char *name)
{
    for (int i = 0; st->ast_func->locals[i].name[0]; i++)
        if (!strcmp(st->ast_func->locals[i].name, name))
            return st->llvm_locals[i];
    assert(0);
}

static LLVMTypeRef codegen_type(const struct State *st, const struct Type *type)
{
    switch(type->kind) {
    case TYPE_POINTER:
        return LLVMPointerType(codegen_type(st, type->data.valuetype), 0);
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
        return LLVMIntType(type->data.width_in_bits);
    case TYPE_BOOL:
        return LLVMInt1Type();
    case TYPE_UNKNOWN:
        assert(0);
    }
    assert(0);
}

static LLVMValueRef codegen_implicit_cast(
    const struct State *st,
    LLVMValueRef obj,
    const struct Type *from,
    const struct Type *to)
{
    if (same_type(from, to))
        return obj;

    switch(to->kind) {
    case TYPE_SIGNED_INTEGER:
        return LLVMBuildSExt(st->builder, obj, codegen_type(st, to), "implicit_cast");
    case TYPE_UNSIGNED_INTEGER:
        return LLVMBuildZExt(st->builder, obj, codegen_type(st, to), "implicit_cast");
    default:
        assert(0);
    }
}

static LLVMValueRef codegen_function_decl(const struct State *st, const struct AstFunctionSignature *sig)
{
    LLVMTypeRef *argtypes = malloc(sig->nargs * sizeof(argtypes[0]));  // NOLINT
    for (int i = 0; i < sig->nargs; i++)
        argtypes[i] = codegen_type(st, &sig->argtypes[i]);

    LLVMTypeRef returntype;
    if (sig->returntype == NULL)
        returntype = LLVMVoidType();
    else
        returntype = codegen_type(st, sig->returntype);

    LLVMTypeRef functype = LLVMFunctionType(returntype, argtypes, sig->nargs, sig->takes_varargs);
    free(argtypes);

    return LLVMAddFunction(st->module, sig->funcname, functype);
}

static LLVMValueRef make_a_string_constant(const struct State *st, const char *s)
{
    LLVMValueRef array = LLVMConstString(s, strlen(s), false);
    LLVMValueRef global_var = LLVMAddGlobal(st->module, LLVMTypeOf(array), "string_literal");
    LLVMSetLinkage(global_var, LLVMPrivateLinkage);  // This makes it a static global variable
    LLVMSetInitializer(global_var, array);

    LLVMTypeRef string_type = LLVMPointerType(LLVMInt8Type(), 0);
    return LLVMBuildBitCast(st->builder, global_var, string_type, "string_ptr");
}

// forward-declare
static LLVMValueRef codegen_call(const struct State *st, const struct AstCall *call, struct Location location);

static LLVMValueRef codegen_expression(const struct State *st, const struct AstExpression *expr)
{
    LLVMValueRef result;

    switch(expr->kind) {
    case AST_EXPR_CALL:
        result = codegen_call(st, &expr->data.call, expr->location);
        break;
    case AST_EXPR_ADDRESS_OF:
        switch(expr->data.operands[0].kind) {
        case AST_EXPR_GET_VARIABLE:
            result = get_pointer_to_local_var(st, expr->data.operands[0].data.varname);
            break;
        case AST_EXPR_DEREFERENCE:
            // &*foo --> just evaluate foo
            result = codegen_expression(st, &expr->data.operands[0].data.operands[0]);
            break;
        default:
            assert(0);
        }
        break;
    case AST_EXPR_GET_VARIABLE:
        result = LLVMBuildLoad(st->builder, get_pointer_to_local_var(st, expr->data.varname), expr->data.varname);
        break;
    case AST_EXPR_DEREFERENCE:
        result = LLVMBuildLoad(st->builder, codegen_expression(st, &expr->data.operands[0]), "deref");
        break;
    case AST_EXPR_INT_CONSTANT:
        result = LLVMConstInt(LLVMInt32Type(), expr->data.int_value, false);
        break;
    case AST_EXPR_CHAR_CONSTANT:
        result = LLVMConstInt(LLVMInt8Type(), expr->data.char_value, false);
        break;
    case AST_EXPR_STRING_CONSTANT:
        result = make_a_string_constant(st, expr->data.string_value);
        break;
    case AST_EXPR_TRUE:
        result = LLVMConstInt(LLVMInt1Type(), 1, false);
        break;
    case AST_EXPR_FALSE:
        result = LLVMConstInt(LLVMInt1Type(), 0, false);
        break;
    case AST_EXPR_ADD:
    case AST_EXPR_SUB:
    case AST_EXPR_MUL:
    case AST_EXPR_DIV:
    case AST_EXPR_EQ:
    case AST_EXPR_NE:
    case AST_EXPR_GT:
    case AST_EXPR_GE:
    case AST_EXPR_LT:
    case AST_EXPR_LE:
        {
            // careful with C's evaluation order........
            LLVMValueRef lhs = codegen_expression(st, &expr->data.operands[0]);
            LLVMValueRef rhs = codegen_expression(st, &expr->data.operands[1]);
            assert(same_type(
                &expr->data.operands[0].type_after_implicit_cast,
                &expr->data.operands[1].type_after_implicit_cast));
            bool is_signed = expr->data.operands[0].type_after_implicit_cast.kind == TYPE_SIGNED_INTEGER;

            switch(expr->kind) {
                case AST_EXPR_ADD: result = LLVMBuildAdd(st->builder, lhs, rhs, "add"); break;
                case AST_EXPR_SUB: result = LLVMBuildSub(st->builder, lhs, rhs, "sub"); break;
                case AST_EXPR_MUL: result = LLVMBuildMul(st->builder, lhs, rhs, "mul"); break;
                case AST_EXPR_DIV: result = (is_signed ? LLVMBuildSDiv : LLVMBuildUDiv)(st->builder, lhs, rhs, "div"); break;
                case AST_EXPR_EQ: result = LLVMBuildICmp(st->builder, LLVMIntEQ, lhs, rhs, "eq"); break;
                case AST_EXPR_NE: result = LLVMBuildICmp(st->builder, LLVMIntNE, lhs, rhs, "ne"); break;
                case AST_EXPR_LT: result = LLVMBuildICmp(st->builder, is_signed ? LLVMIntSLT : LLVMIntULT, lhs, rhs, "lt"); break;
                case AST_EXPR_LE: result = LLVMBuildICmp(st->builder, is_signed ? LLVMIntSLE : LLVMIntULE, lhs, rhs, "le"); break;
                case AST_EXPR_GT: result = LLVMBuildICmp(st->builder, is_signed ? LLVMIntSGT : LLVMIntUGT, lhs, rhs, "gt"); break;
                case AST_EXPR_GE: result = LLVMBuildICmp(st->builder, is_signed ? LLVMIntSGE : LLVMIntUGE, lhs, rhs, "ge"); break;
                default: assert(0);
            }
        }
    }

    return codegen_implicit_cast(st, result, &expr->type_before_implicit_cast, &expr->type_after_implicit_cast);
}

static LLVMValueRef codegen_call(const struct State *st, const struct AstCall *call, struct Location location)
{
    // TODO: do something with the location, debug info?
    (void)location;

    LLVMValueRef function = LLVMGetNamedFunction(st->module, call->funcname);
    assert(function);
    assert(LLVMGetTypeKind(LLVMTypeOf(function)) == LLVMPointerTypeKind);
    LLVMTypeRef function_type = LLVMGetElementType(LLVMTypeOf(function));
    assert(LLVMGetTypeKind(function_type) == LLVMFunctionTypeKind);

    LLVMValueRef *args = malloc(call->nargs * sizeof(args[0]));  // NOLINT
    for (int i = 0; i < call->nargs; i++)
        args[i] = codegen_expression(st, &call->args[i]);

    char debug_name[100] = {0};
    if (LLVMGetTypeKind(LLVMGetReturnType(function_type)) != LLVMVoidTypeKind)
        snprintf(debug_name, sizeof debug_name, "%s_return_value", call->funcname);

    LLVMValueRef return_value = LLVMBuildCall2(st->builder, function_type, function, args, call->nargs, debug_name);

    free(args);
    return return_value;
}

static void codegen_body(const struct State *st, const struct AstBody *body);

static void codegen_if_statement(const struct State *st, const struct AstIfStatement *ifstatement)
{
    LLVMValueRef condition = codegen_expression(st, &ifstatement->condition);
    assert(LLVMTypeOf(condition) == LLVMInt1Type());

    LLVMBasicBlockRef then = LLVMAppendBasicBlock(st->llvm_func, "then");
    LLVMBasicBlockRef after_if = LLVMAppendBasicBlock(st->llvm_func, "after_if");
    LLVMBuildCondBr(st->builder, condition, then, after_if);

    LLVMPositionBuilderAtEnd(st->builder, then);
    codegen_body(st, &ifstatement->body);
    LLVMBuildBr(st->builder, after_if);

    LLVMPositionBuilderAtEnd(st->builder, after_if);
}

static void codegen_statement(const struct State *st, const struct AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_IF:
        codegen_if_statement(st, &stmt->data.ifstatement);
        break;

    case AST_STMT_CALL:
        codegen_call(st, &stmt->data.call, stmt->location);
        break;

    case AST_STMT_SETVAR:
        LLVMBuildStore(
            st->builder,
            codegen_expression(st, &stmt->data.setvar.value),
            get_pointer_to_local_var(st, stmt->data.setvar.varname));
        break;

    case AST_STMT_RETURN_VALUE:
        LLVMBuildRet(st->builder, codegen_expression(st, &stmt->data.returnvalue));
        break;

    case AST_STMT_RETURN_WITHOUT_VALUE:
        LLVMBuildRetVoid(st->builder);
        break;
    }
}

static void codegen_body(const struct State *st, const struct AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        codegen_statement(st, &body->statements[i]);
}

static void codegen_function_def(struct State *st, const struct AstFunctionDef *funcdef)
{
    assert(st->ast_func == NULL);
    assert(st->llvm_func == NULL);
    assert(st->llvm_locals == NULL);

    st->ast_func = funcdef;
    st->llvm_func = codegen_function_decl(st, &funcdef->signature);

    LLVMBasicBlockRef block = LLVMAppendBasicBlockInContext(LLVMGetGlobalContext(), st->llvm_func, "function_start");
    LLVMPositionBuilderAtEnd(st->builder, block);

    List(LLVMValueRef) locals = {0};
    for (int i = 0; funcdef->locals[i].name[0]; i++) {
        LLVMTypeRef vartype = codegen_type(st, &funcdef->locals[i].type);
        Append(&locals, LLVMBuildAlloca(st->builder, vartype, funcdef->locals[i].name));
    }
    st->llvm_locals = locals.ptr;

    for (int i = 0; i < funcdef->signature.nargs; i++) {
        LLVMValueRef value = LLVMGetParam(st->llvm_func, i);
        LLVMValueRef pointer = get_pointer_to_local_var(st, funcdef->signature.argnames[i]);
        LLVMBuildStore(st->builder, value, pointer);
    }

    codegen_body(st, &funcdef->body);

    if (funcdef->signature.returntype == NULL)
        LLVMBuildRetVoid(st->builder);
    else
        LLVMBuildUnreachable(st->builder);  // TODO: compiler error if this is reachable

    st->ast_func = NULL;
    st->llvm_func = NULL;
    free(st->llvm_locals);
    st->llvm_locals = NULL;
}

LLVMModuleRef codegen(const struct AstToplevelNode *ast)
{
    struct State st = {
        .module = LLVMModuleCreateWithName(""),  // TODO: pass module name?
        .builder = LLVMCreateBuilder(),
    };
    LLVMSetSourceFileName(st.module, ast->location.filename, strlen(ast->location.filename));

    for(;;ast++){
        switch(ast->kind) {
        case AST_TOPLEVEL_CDECL_FUNCTION:
            codegen_function_decl(&st, &ast->data.decl_signature);
            break;

        case AST_TOPLEVEL_DEFINE_FUNCTION:
            codegen_function_def(&st, &ast->data.funcdef);
            break;

        case AST_TOPLEVEL_END_OF_FILE:
            LLVMDisposeBuilder(st.builder);
            return st.module;
        }
    }
}
