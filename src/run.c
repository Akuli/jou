#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jou_compiler.h"
#include <llvm-c/Core.h>


static char TempDir[50];

static void cleanup()
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
    atexit(cleanup);
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

int run_program(LLVMModuleRef module, const CommandLineFlags *flags)
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

    char command[2000];
    snprintf(command, sizeof command, "%s -O%d -Wno-override-module -o %s/exe %s/ir.bc && %s/exe",
        get_clang_path(), flags->optlevel, TempDir, TempDir, TempDir);
    if (flags->verbose)
        puts(command);
    return !!system(command);
}
