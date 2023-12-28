#include <stdlib.h>
#include "jou_compiler.h"

static struct Target target = {0};

static void cleanup(void)
{
    LLVMDisposeTargetMachine(target.target_machine_ref);
    LLVMDisposeTargetData(target.target_data_ref);
}

void init_target(void)
{
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmParser();
    LLVMInitializeX86AsmPrinter();

#ifdef _WIN32
    // Default is x86_64-pc-windows-msvc
    strcpy(target.triple, "x86_64-pc-windows-gnu");
#else
    char *triple = LLVMGetDefaultTargetTriple();
    assert(strlen(triple) < sizeof target.triple);
    strcpy(target.triple, triple);
    LLVMDisposeMessage(triple);
#endif

    char *error = NULL;
    if (LLVMGetTargetFromTriple(target.triple, &target.target_ref, &error)) {
        assert(error);
        fprintf(stderr, "LLVMGetTargetFromTriple(\"%s\") failed: %s\n", target.triple, error);
        exit(1);
    }
    assert(!error);
    assert(target.target_ref);

    target.target_machine_ref = LLVMCreateTargetMachine(
        target.target_ref, target.triple, "x86-64", "", LLVMCodeGenLevelDefault, LLVMRelocPIC, LLVMCodeModelDefault);
    assert(target.target_machine_ref);

    target.target_data_ref = LLVMCreateTargetDataLayout(target.target_machine_ref);
    assert(target.target_data_ref);

    char *tmp = LLVMCopyStringRepOfTargetData(target.target_data_ref);
    assert(strlen(tmp) < sizeof target.data_layout);
    strcpy(target.data_layout, tmp);
    LLVMDisposeMessage(tmp);

    atexit(cleanup);
}

const struct Target *get_target(void)
{
    return &target;
}
