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

static LLVMTypeRef codegen_type(const struct State *st, const struct Type *type, struct Location location)
{
    switch(type->kind) {
    case TYPE_POINTER:
        return LLVMPointerType(codegen_type(st, type->data.valuetype, location), 0);
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

static LLVMValueRef codegen_implicit_conversion(
    const struct State *st,
    LLVMValueRef obj,
    const struct Type *objtype,
    LLVMTypeRef target_type)
{
    char name[200];
    snprintf(name, sizeof name, "cast_from_%s", objtype->name);

    switch(objtype->kind) {
    case TYPE_SIGNED_INTEGER:
        return LLVMBuildSExt(st->builder, obj, target_type, name);
    case TYPE_UNSIGNED_INTEGER:
        return LLVMBuildZExt(st->builder, obj, target_type, name);
    default:
        // assumed to already be of the right type
        return obj;
    }
}

static LLVMValueRef codegen_function_decl(const struct State *st, const struct AstFunctionSignature *sig)
{
    LLVMTypeRef *argtypes = malloc(sig->nargs * sizeof(argtypes[0]));  // NOLINT
    for (int i = 0; i < sig->nargs; i++)
        argtypes[i] = codegen_type(st, &sig->argtypes[i], sig->location);

    LLVMTypeRef returntype;
    if (sig->returntype == NULL)
        returntype = LLVMVoidType();
    else
        returntype = codegen_type(st, sig->returntype, sig->location);

    LLVMTypeRef functype = LLVMFunctionType(returntype, argtypes, sig->nargs, false);
    free(argtypes);

    return LLVMAddFunction(st->module, sig->funcname, functype);
}

static LLVMValueRef make_a_string_constant(const struct State *st, const char *s)
{
    // https://stackoverflow.com/a/37906139
    LLVMValueRef array = LLVMConstString(s, strlen(s), false);
    LLVMValueRef stack_space_ptr = LLVMBuildAlloca(st->builder, LLVMTypeOf(array), "string_data");
    LLVMBuildStore(st->builder, array, stack_space_ptr);
    LLVMTypeRef string_type = LLVMPointerType(LLVMInt8Type(), 0);
    return LLVMBuildBitCast(st->builder, stack_space_ptr, string_type, "string_ptr");
}

// forward-declare
static LLVMValueRef codegen_call(const struct State *st, const struct AstCall *call, struct Location location);

static LLVMValueRef codegen_expression(const struct State *st, const struct AstExpression *expr)
{
    switch(expr->kind) {
    case AST_EXPR_CALL:
        return codegen_call(st, &expr->data.call, expr->location);
    case AST_EXPR_ADDRESS_OF_VARIABLE:
        return get_pointer_to_local_var(st, expr->data.varname);
    case AST_EXPR_GET_VARIABLE:
        return LLVMBuildLoad(st->builder, get_pointer_to_local_var(st, expr->data.varname), expr->data.varname);
    case AST_EXPR_DEREFERENCE:
        return LLVMBuildLoad(st->builder, codegen_expression(st, expr->data.pointerexpr), "deref");
    case AST_EXPR_INT_CONSTANT:
        return LLVMConstInt(LLVMInt32Type(), expr->data.int_value, false);
    case AST_EXPR_CHAR_CONSTANT:
        return LLVMConstInt(LLVMInt8Type(), expr->data.char_value, false);
    case AST_EXPR_STRING_CONSTANT:
        return make_a_string_constant(st, expr->data.string_value);
    case AST_EXPR_TRUE:
        return LLVMConstInt(LLVMInt1Type(), 1, false);
    case AST_EXPR_FALSE:
        return LLVMConstInt(LLVMInt1Type(), 0, false);
    }
    assert(0);
}

static LLVMValueRef codegen_expression_with_implicit_conversion(
    const struct State *st,
    const struct AstExpression *expr,
    LLVMTypeRef target_type)
{
    return codegen_implicit_conversion(st, codegen_expression(st,expr), &expr->type, target_type);
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
    assert(call->nargs == (int)LLVMCountParamTypes(function_type));

    LLVMValueRef *args = malloc(call->nargs * sizeof(args[0]));  // NOLINT

    LLVMTypeRef *paramtypes = malloc(call->nargs * sizeof(paramtypes[0]));  // NOLINT
    LLVMGetParamTypes(function_type, paramtypes);
    for (int i = 0; i < call->nargs; i++)
        args[i] = codegen_expression_with_implicit_conversion(st, &call->args[i], paramtypes[i]);
    free(paramtypes);

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
    {
        LLVMValueRef pointer = get_pointer_to_local_var(st, stmt->data.setvar.varname);
        LLVMTypeRef var_type = LLVMGetElementType(LLVMTypeOf(pointer));
        LLVMValueRef value = codegen_expression_with_implicit_conversion(st, &stmt->data.setvar.value, var_type);
        LLVMBuildStore(st->builder, value, pointer);
        break;
    }

    case AST_STMT_RETURN_VALUE:
    {
        LLVMTypeRef returntype = codegen_type(st, st->ast_func->signature.returntype, stmt->location);
        LLVMValueRef returnvalue = codegen_expression_with_implicit_conversion(st, &stmt->data.returnvalue, returntype);
        LLVMBuildRet(st->builder, returnvalue);
        break;
    }

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
        LLVMTypeRef vartype = codegen_type(st, &funcdef->locals[i].type, funcdef->signature.location);
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
