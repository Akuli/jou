#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jou_compiler.h"
#include <llvm-c/Analysis.h>

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

int main(int argc, char **argv)
{
    bool verbose;
    const char *filename;

    if (argc == 3 && !strcmp(argv[1], "--verbose")) {
        verbose = true;
        filename = argv[2];
    } else if (argc == 2 && argv[1][0] != '-') {
        verbose = false;
        filename = argv[1];
    } else {
        fprintf(stderr, "Usage: %s [--verbose] FILENAME\n", argv[0]);
        return 2;
    }

    struct Token *tokens = tokenize(filename);
    if(verbose)
        print_tokens(tokens);

    struct AstToplevelNode *ast = parse(tokens);
    free_tokens(tokens);
    if(verbose)
        print_ast(ast);

    struct CfGraphFile cfgfile = build_control_flow_graphs(ast);
    free_ast(ast);
    if(verbose)
        print_control_flow_graphs(&cfgfile);

    simplify_control_flow_graphs(&cfgfile);
    if(verbose)
        print_control_flow_graphs(&cfgfile);

    LLVMModuleRef module = codegen(&cfgfile);
    free_control_flow_graphs(&cfgfile);
    if(verbose)
        print_llvm_ir(module);

    LLVMVerifyModule(module, LLVMAbortProcessAction, NULL);

    // TODO: this is a ridiculous way to run the IR, figure out something better
    make_temp_dir();
    char irfilename[200];
    sprintf(irfilename, "%s/ir.bc", TempDir);
    FILE *f = fopen(irfilename, "wb");
    assert(f);
    char *s = LLVMPrintModuleToString(module);
    fprintf(f, "%s", s);
    LLVMDisposeMessage(s);
    fclose(f);

    LLVMDisposeModule(module);

    char command[200];
    sprintf(command, "clang-11 -Wno-override-module -o %s/exe %s/ir.bc && %s/exe",
        TempDir, TempDir, TempDir);
    if(verbose)
        puts(command);
    return !!system(command);
}
