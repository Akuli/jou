#include "ast.h"
#include <stdbool.h>
#include <string.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>


LLVMModuleRef codegen(const struct AstStatement *ast)
{
    // TODO: pass better name?
    LLVMModuleRef module = LLVMModuleCreateWithName("my_module");
    LLVMSetSourceFileName(module, ast->location.filename, strlen(ast->location.filename));

    LLVMBuilderRef builder = LLVMCreateBuilder();

    // TODO: way too hard-coded
    assert(ast->kind == AST_STMT_CIMPORT_FUNCTION);
    assert(!strcmp(ast->data.cimport.funcname, "putchar"));
    LLVMTypeRef i32type = LLVMInt32TypeInContext(LLVMGetGlobalContext());
    LLVMTypeRef putchar_type = LLVMFunctionType(i32type, &i32type, 1, false);
    LLVMValueRef putchar_function = LLVMAddFunction(module, ast->data.cimport.funcname, putchar_type);
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
    LLVMDisposeBuilder(builder);
    return module;
}

