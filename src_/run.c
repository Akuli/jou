#include "jou_compiler.h"
#include <llvm-c/ExecutionEngine.h>

int run_program(LLVMModuleRef module, const CommandLineFlags *flags)
{
    if (flags->verbose)
        printf("Initializing JIT\n");

    if (LLVMInitializeNativeTarget()) {
        fprintf(stderr, "LLVMInitializeNativeTarget() failed\n");
        return 1;
    }

    // No idea what this function does, but https://stackoverflow.com/a/38801376
    if (LLVMInitializeNativeAsmPrinter()) {
        fprintf(stderr, "LLVMInitializeNativeAsmPrinter() failed\n");
        return 1;
    }

    LLVMExecutionEngineRef jit;
    char *errormsg = NULL;

    if (LLVMCreateJITCompilerForModule(&jit, module, flags->optlevel, &errormsg)) {
        fprintf(stderr, "LLVMCreateJITCompilerForModule() failed: %s\n", errormsg);
        return 1;
    }
    assert(jit);
    assert(!errormsg);

    LLVMValueRef main = LLVMGetNamedFunction(module, "main");
    if (!main) {
        fprintf(stderr, "error: main() function not found\n");
        return 1;
    }

    if (flags->verbose)
        printf("Running with JIT\n\n");

    extern char **environ;
    int result = LLVMRunFunctionAsMain(jit, main, 1, (const char*[]){"jou-program"}, (const char *const*)environ);

    LLVMDisposeExecutionEngine(jit);
    return result;
}
