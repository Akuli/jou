#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include "jou_compiler.h"

struct State {
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    const struct AstFunctionSignature *current_func_signature;
};

static void codegen_void_return(const struct State *st, struct Location location)
{
    if (st->current_func_signature->returns_a_value) {
        // TODO: get the name of the return type here
        fail_with_error(
            location,
            "a return value is needed, because the return type of function \"%s\" is \"int\"",
            st->current_func_signature->funcname);
    }

    // main function returns 0 if declared as having no return value
    if (!strcmp(st->current_func_signature->funcname, "main"))
        LLVMBuildRet(st->builder, LLVMConstInt(LLVMInt32Type(), 0, false));
    else
        LLVMBuildRetVoid(st->builder);
}

static LLVMValueRef codegen_function_decl(const struct State *st, const struct AstFunctionSignature *sig)
{
    if (LLVMGetNamedFunction(st->module, sig->funcname))
        fail_with_error(sig->location, "a function named \"%s\" already exists", sig->funcname);

    LLVMTypeRef i32type = LLVMInt32Type();

    LLVMTypeRef *argtypes = malloc(sig->nargs * sizeof(argtypes[0]));  // NOLINT
    for (int i = 0; i < sig->nargs; i++)
        argtypes[i] = i32type;

    LLVMTypeRef returntype;
    if (sig->returns_a_value)
        returntype = i32type ; // TODO
    else if (!strcmp(sig->funcname, "main")) {
        // main function returns 0 if declared as having no return value
        returntype = LLVMInt32Type();
    } else {
        returntype = LLVMVoidType();
    }

    LLVMTypeRef functype = LLVMFunctionType(returntype, argtypes, sig->nargs, false);
    free(argtypes);

    return LLVMAddFunction(st->module, sig->funcname, functype);
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
        args[i] = LLVMConstInt(i32type, call->args[i], false);

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
                    "return type must be written on the first line of the function definition, after a '->'");
            }
            LLVMBuildRet(st->builder, LLVMConstInt(LLVMInt32Type(), stmt->data.returnvalue, false));
            break;

        case AST_STMT_RETURN_WITHOUT_VALUE:
            codegen_void_return(st, stmt->location);
            break;
    }
}

static void codegen_function_def(struct State *st, const struct AstFunctionDef *funcdef)
{
    LLVMValueRef function = codegen_function_decl(st, &funcdef->signature);
    LLVMBasicBlockRef block = LLVMAppendBasicBlockInContext(LLVMGetGlobalContext(), function, "function_block");
    LLVMPositionBuilderAtEnd(st->builder, block);

    st->current_func_signature = &funcdef->signature;
    for (int i = 0; i < funcdef->body.nstatements; i++)
        codegen_statement(st, &funcdef->body.statements[i]);

    if (funcdef->signature.returns_a_value)
        LLVMBuildUnreachable(st->builder);  // TODO: add an error if this is reachable
    else {
        assert(funcdef->body.nstatements > 0);
        codegen_void_return(st, funcdef->body.statements[funcdef->body.nstatements-1].location);
    }

    assert(st->current_func_signature == &funcdef->signature);
    st->current_func_signature = NULL;
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
