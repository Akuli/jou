#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include "jou_compiler.h"

static LLVMValueRef codegen_function_decl(LLVMModuleRef module, const struct AstFunctionSignature *sig)
{
    // TODO: test this
    if (LLVMGetNamedFunction(module, sig->funcname))
        fail_with_error(sig->location, "a function named \"%s\" already exists", sig->funcname);

    LLVMTypeRef i32type = LLVMInt32TypeInContext(LLVMGetGlobalContext());

    LLVMTypeRef *argtypes = malloc(sig->nargs * sizeof(argtypes[0]));  // NOLINT
    for (int i = 0; i < sig->nargs; i++)
        argtypes[i] = i32type;
    LLVMTypeRef functype = LLVMFunctionType(i32type, argtypes, sig->nargs, false);
    free(argtypes);

    return LLVMAddFunction(module, sig->funcname, functype);
}

static void codegen_statement(LLVMBuilderRef builder, LLVMModuleRef module, const struct AstStatement *stmt)
{
    LLVMTypeRef i32type = LLVMInt32TypeInContext(LLVMGetGlobalContext());

    switch(stmt->kind) {
        case AST_STMT_CALL:
        {
            LLVMValueRef function = LLVMGetNamedFunction(module, stmt->data.call.funcname);
            if (!function)
                fail_with_error(stmt->location, "function \"%s\" not found", stmt->data.call.funcname);

            LLVMTypeRef function_type = LLVMTypeOf(function);
            // TODO: this doesn't work because LLVMTypeOf returns the type of a pointer, and
            // I can't figure out how to access the type that the pointer is pointing to
            /*
            assert(LLVMGetTypeKind(function_type) == LLVMFunctionTypeKind);

            // TODO: currently function calls hard-code 1 argument of type int
            if (LLVMCountParamTypes(function_type) != 1) {
                fail_with_error(
                    stmt->location,
                    "function \"%s\" takes %d arguments, but it was called with 1 argument",
                    stmt->data.call.funcname,
                    LLVMCountParamTypes(function_type));
            }

            LLVMTypeRef paramtype;
            LLVMGetParamTypes(function_type, &paramtype);
            if (paramtype != i32type) {
                // TODO: improve error message, display names of argument types
                fail_with_error(
                    stmt->location,
                    "wrong argument types when calling function \"%s\"",
                    stmt->data.call.funcname);
            }
            */

            LLVMValueRef arg = LLVMConstInt(i32type, stmt->data.call.arg, false);

            char debug_name[100];
            snprintf(debug_name, sizeof debug_name, "%s_return_value", stmt->data.call.funcname);
            LLVMBuildCall2(builder, function_type, function, &arg, 1, debug_name);

            break;
        }

        case AST_STMT_RETURN:
            LLVMBuildRet(builder, LLVMConstInt(i32type, stmt->data.call.arg, false));
            break;
    }
}

static void codegen_function_def(LLVMModuleRef module, LLVMBuilderRef builder, const struct AstFunctionDef *funcdef)
{
    LLVMValueRef function = codegen_function_decl(module, &funcdef->signature);

    LLVMBasicBlockRef block = LLVMAppendBasicBlockInContext(LLVMGetGlobalContext(), function, "function_block");
    LLVMPositionBuilderAtEnd(builder, block);
    for (int i = 0; i < funcdef->body.nstatements; i++)
        codegen_statement(builder, module, &funcdef->body.statements[i]);
}

LLVMModuleRef codegen(const struct AstToplevelNode *ast)
{
    // TODO: pass better name?
    LLVMModuleRef module = LLVMModuleCreateWithName("");
    LLVMSetSourceFileName(module, ast->location.filename, strlen(ast->location.filename));

    LLVMBuilderRef builder = LLVMCreateBuilder();

    for(;;ast++){
        switch(ast->kind) {
        case AST_TOPLEVEL_CDECL_FUNCTION:
            codegen_function_decl(module, &ast->data.decl_signature);
            break;

        case AST_TOPLEVEL_DEFINE_FUNCTION:
            codegen_function_def(module, builder, &ast->data.funcdef);
            break;

        case AST_TOPLEVEL_END_OF_FILE:
            LLVMDisposeBuilder(builder);
            return module;
        }
    }
}
