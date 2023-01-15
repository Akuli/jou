#include <errno.h>
#include <string.h>
#include "jou_compiler.h"
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Transforms/PassManagerBuilder.h>

// TODO: this TempDir code is ridiculous
static char TempDir[50];

static void delete_temp_dir()
{
    char command[200];
    sprintf(command, "rm -rf '%s'", TempDir);
    system(command);
}

static void make_temp_dir()
{
    system("mkdir -p /tmp/jou");
    strcpy(TempDir, "/tmp/jou/XXXXXX");
    if (!mkdtemp(TempDir)){
        fprintf(stderr, "cannot create temporary directory: %s\n", strerror(errno));
        exit(1);
    }
    atexit(delete_temp_dir);
}

static const char *get_clang_path(void)
{
    // Makefile passes e.g. -DJOU_CLANG_PATH=/usr/lib/llvm-11/bin/clang
    // But retrieving the value is weird...
#define str(x) #x
#define str1(x) str(x)
    return str1(JOU_CLANG_PATH);
#undef str
#undef str1
}

static int run_main_with_clang(LLVMModuleRef module, const CommandLineFlags *flags)
{
    // TODO: this is a bit ridiculous...
    make_temp_dir();

    char filename[200];
    sprintf(filename, "%s/ir.bc", TempDir);

    char *errormsg;
    if (LLVMPrintModuleToFile(module, filename, &errormsg)) {
        fprintf(stderr, "writing LLVM IR to %s failed: %s\n", filename, errormsg);
        LLVMDisposeMessage(errormsg);
    }

    LLVMDisposeModule(module);

    char command[2000];
    snprintf(command, sizeof command, "%s -O%d -Wno-override-module -o %s/exe %s/ir.bc && %s/exe",
        get_clang_path(), flags->optlevel, TempDir, TempDir, TempDir);
    if (flags->verbose)
        puts(command);
    return !!system(command);
}

static void optimize(LLVMModuleRef module, int level)
{
    assert(0 <= level && level <= 3);

    LLVMPassManagerRef pm = LLVMCreatePassManager();

    LLVMPassManagerBuilderRef pmbuilder = LLVMPassManagerBuilderCreate();
    LLVMPassManagerBuilderSetOptLevel(pmbuilder, level);
    LLVMPassManagerBuilderPopulateModulePassManager(pmbuilder, pm);
    LLVMPassManagerBuilderDispose(pmbuilder);

    LLVMRunPassManager(pm, module);
    LLVMDisposePassManager(pm);
}

static int run_main_with_jit(LLVMModuleRef module, const CommandLineFlags *flags)
{
    optimize(module, flags->optlevel);

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
        printf("Running with JIT (optlevel %d)...\n", flags->optlevel);

    extern char **environ;
    int result = LLVMRunFunctionAsMain(jit, main, 1, (const char*[]){"jou-program"}, (const char *const*)environ);

    LLVMDisposeExecutionEngine(jit);
    return result;
}

int run_program(LLVMModuleRef module, const CommandLineFlags *flags)
{
    if (flags->jit)
        return run_main_with_jit(module, flags);
    else
        return run_main_with_clang(module, flags);
}
