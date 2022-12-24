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
    const struct AstFunctionSignature *current_func_signature;
    List(struct LocalVariable) local_vars;
};

static LLVMValueRef codegen_function_decl(const struct State *st, const struct AstFunctionSignature *sig)
{
    if (LLVMGetNamedFunction(st->module, sig->funcname))
        fail_with_error(sig->location, "a function named \"%s\" already exists", sig->funcname);

    if (!strcmp(sig->funcname, "main") && !sig->returns_a_value)
        fail_with_error(sig->location, "the main() function must return int");

    LLVMTypeRef i32type = LLVMInt32Type();

    LLVMTypeRef *argtypes = malloc(sig->nargs * sizeof(argtypes[0]));  // NOLINT
    for (int i = 0; i < sig->nargs; i++)
        argtypes[i] = i32type;

    LLVMTypeRef returntype;
    if (sig->returns_a_value)
        returntype = i32type ; // TODO
    else
        returntype = LLVMVoidType();

    LLVMTypeRef functype = LLVMFunctionType(returntype, argtypes, sig->nargs, false);
    free(argtypes);

    return LLVMAddFunction(st->module, sig->funcname, functype);
}

// forward-declare
LLVMValueRef codegen_call(const struct State *st, const struct AstCall *call, struct Location location);

LLVMValueRef codegen_expression(const struct State *st, const struct AstExpression *expr)
{
    switch(expr->kind) {
    case AST_EXPR_CALL:
        return codegen_call(st, &expr->data.call, expr->location);
    case AST_EXPR_GETVAR:
        for (struct LocalVariable *v = st->local_vars.ptr; v < End(st->local_vars); v++)
            if (!strcmp(v->name, expr->data.varname))
                return LLVMBuildLoad(st->builder, v->pointer, v->name);
        fail_with_error(expr->location, "no local variable named \"%s\"", expr->data.varname);
    case AST_EXPR_INT_CONSTANT:
        return LLVMConstInt(LLVMInt32Type(), expr->data.int_value, false);
    }
    assert(0);
}

LLVMValueRef codegen_call(const struct State *st, const struct AstCall *call, struct Location location)
{
    LLVMTypeRef i32type = LLVMInt32Type();

    LLVMValueRef function = LLVMGetNamedFunction(st->module, call->funcname);
    if (!function)
        fail_with_error(location, "function \"%s\" not found", call->funcname);

    assert(LLVMGetTypeKind(LLVMTypeOf(function)) == LLVMPointerTypeKind);
    LLVMTypeRef function_type = LLVMGetElementType(LLVMTypeOf(function));
    assert(LLVMGetTypeKind(function_type) == LLVMFunctionTypeKind);

    int nargs = LLVMCountParamTypes(function_type);
    if (nargs != call->nargs) {
        fail_with_error(
            location,
            "function \"%s\" takes %d argument%s, but it was called with %d argument%s",
            call->funcname,
            nargs,
            nargs==1?"":"s",
            call->nargs,
            call->nargs==1?"":"s"
        );
    }

    LLVMTypeRef *paramtypes = malloc(nargs * sizeof(paramtypes[0]));  // NOLINT
    LLVMGetParamTypes(function_type, paramtypes);
    for (int i = 0; i < nargs; i++) {
        if (paramtypes[i] != i32type) {
            // TODO: improve error message, display names of argument types?
            fail_with_error(location, "wrong argument types when calling function \"%s\"", call->funcname);
        }
    }
    free(paramtypes);

    LLVMValueRef *args = malloc(nargs * sizeof(args[0]));  // NOLINT
    for (int i = 0; i < call->nargs; i++)
        args[i] = codegen_expression(st, &call->args[i]);

    char debug_name[100] = {0};
    if (LLVMGetTypeKind(LLVMGetReturnType(function_type)) != LLVMVoidTypeKind)
        snprintf(debug_name, sizeof debug_name, "%s_return_value", call->funcname);

    LLVMValueRef return_value = LLVMBuildCall2(st->builder, function_type, function, args, call->nargs, debug_name);
    free(args);
    return return_value;
}

static void codegen_statement(const struct State *st, const struct AstStatement *stmt)
{
    switch(stmt->kind) {
        case AST_STMT_CALL:
            codegen_call(st, &stmt->data.call, stmt->location);
            break;

        case AST_STMT_RETURN_VALUE:
            if (!st->current_func_signature->returns_a_value) {
                fail_with_error(
                    stmt->location,
                    "function \"%s(...) -> void\" does not return a value",
                    st->current_func_signature->funcname
                );
            }
            LLVMBuildRet(st->builder, codegen_expression(st, &stmt->data.returnvalue));
            break;

        case AST_STMT_RETURN_WITHOUT_VALUE:
            if (st->current_func_signature->returns_a_value) {
                // TODO: get the name of the return type here
                fail_with_error(
                    stmt->location,
                    "a return value is needed, because the return type of function \"%s\" is \"int\"",
                    st->current_func_signature->funcname);
            }
            LLVMBuildRetVoid(st->builder);
            break;
    }
}

static void codegen_function_def(struct State *st, const struct AstFunctionDef *funcdef)
{
    LLVMValueRef function = codegen_function_decl(st, &funcdef->signature);
    LLVMBasicBlockRef block = LLVMAppendBasicBlockInContext(LLVMGetGlobalContext(), function, "function_block");
    LLVMPositionBuilderAtEnd(st->builder, block);

    st->current_func_signature = &funcdef->signature;
    for (int i = 0; i < funcdef->signature.nargs; i++) {
        LLVMValueRef value = LLVMGetParam(function, i);
        LLVMValueRef pointer = LLVMBuildAlloca(st->builder, LLVMTypeOf(value), funcdef->signature.argnames[i]);
        LLVMBuildStore(st->builder, value, pointer);
        struct LocalVariable local = { .pointer=pointer };
        safe_strcpy(local.name, funcdef->signature.argnames[i]);
        Append(&st->local_vars, local);
    }

    for (int i = 0; i < funcdef->body.nstatements; i++)
        codegen_statement(st, &funcdef->body.statements[i]);

    if (funcdef->signature.returns_a_value)
        LLVMBuildUnreachable(st->builder);  // TODO: add an error if this is reachable
    else
        LLVMBuildRetVoid(st->builder);

    assert(st->current_func_signature == &funcdef->signature);
    st->current_func_signature = NULL;
    free(st->local_vars.ptr);
    memset(&st->local_vars, 0, sizeof st->local_vars);
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
