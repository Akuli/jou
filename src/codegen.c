#include "ast.h"
#include <stdbool.h>
#include <string.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>

static void codegen_cimport(LLVMModuleRef module, const struct AstFunctionSignature *sig)
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

    LLVMAddFunction(module, sig->funcname, functype);
}

LLVMModuleRef codegen(const struct AstToplevelNode *ast)
{
    // TODO: pass better name?
    LLVMModuleRef module = LLVMModuleCreateWithName("my_module");
    LLVMSetSourceFileName(module, ast->location.filename, strlen(ast->location.filename));

    LLVMBuilderRef builder = LLVMCreateBuilder();

    for(;;ast++){
        switch(ast->kind) {
        case AST_TOPLEVEL_CIMPORT_FUNCTION:
            codegen_cimport(module, &ast->data.cimport_signature);
            break;

        case AST_TOPLEVEL_DEFINE_FUNCTION:
            // TODO
            break;

        case AST_TOPLEVEL_END_OF_FILE:
            LLVMDisposeBuilder(builder);
            return module;
        }
    }

    /*
    // TODO: way too hard-coded
    assert(ast->kind == AST_STMT_CIMPORT_FUNCTION);
    ast++;

    LLVMTypeRef main_type = LLVMFunctionType(i32type, NULL, 0, false);
    LLVMValueRef main_function = LLVMAddFunction(module, "main", main_type);

    LLVMBasicBlockRef block = LLVMAppendBasicBlockInContext(LLVMGetGlobalContext(), main_function, "block");
    LLVMPositionBuilderAtEnd(builder, block);

    // Evaluate the rest in an anonymous namespace.
    for ( ; ast->kind != AST_STMT_END_OF_FILE; ast++) {
        assert(ast->kind == AST_STMT_CALL);
        assert(!strcmp(ast->data.call.funcname, "putchar"));
        LLVMValueRef arg = LLVMConstInt(i32type, ast->data.call.arg, false);
        LLVMBuildCall2(builder, putchar_type, putchar_function, &arg, 1, "putchar_ret");
    }

    LLVMBuildRet(builder, LLVMConstInt(i32type, 0, false));
    */
}
