#include "jou_compiler.h"
#include <llvm-c/TargetMachine.h>

void compile_to_object_file(LLVMModuleRef module, const char *path, const CommandLineFlags *flags)
{
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmParsers();
    LLVMInitializeAllAsmPrinters();

    char *triple = LLVMGetDefaultTargetTriple();
    assert(triple);
    if (flags->verbose)
        printf("Emitting object file \"%s\" for %s\n", path, triple);

    // The CPU name is the first component of the target triple.
    // But it's not quite that simple, achthually the typical cpu name is x86-64 instead of x86_64...
    char cpu[100];
    snprintf(cpu, sizeof cpu, "%.*s", (int)strcspn(triple, "-"), triple);
    if (!strcmp(cpu, "x86_64"))
        strcpy(cpu, "x86-64");

    LLVMTargetRef target = LLVMGetTargetFromName(cpu);
    assert(target);

    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target, triple, cpu, "", LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault);
    assert(machine);

    char *error = NULL;
    char *tmppath = strdup(path);
    if (LLVMTargetMachineEmitToFile(machine, module, tmppath, LLVMObjectFile, &error)) {
        assert(error);
        fprintf(stderr, "failed to emit object file \"%s\": %s\n", path, error);
        exit(1);
    }
    free(tmppath);
    assert(!error);

    LLVMDisposeTargetMachine(machine);
    LLVMDisposeMessage(triple);
}

int run_program(LLVMModuleRef module, const CommandLineFlags *flags)
{
    assert(0);
}
