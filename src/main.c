#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jou_compiler.h"
#include <llvm-c/Analysis.h>

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

    fill_types(ast);
    if(verbose)
        print_ast(ast);

    LLVMModuleRef module = codegen(ast);
    free_ast(ast);
    if(verbose)
        print_llvm_ir(module);

    // TODO: currently this doesn't work
    //LLVMVerifyModule(module, LLVMAbortProcessAction, NULL);

    // TODO: this is a ridiculous way to run the IR, figure out something better
    system("mkdir -p /tmp/jou");
    FILE *f = fopen("/tmp/jou/ir.bc", "wb");
    assert(f);
    char *s = LLVMPrintModuleToString(module);
    fprintf(f, "%s", s);
    LLVMDisposeMessage(s);
    fclose(f);

    LLVMDisposeModule(module);

    const char *command = "clang-11 -Wno-override-module -o /tmp/jou/exe /tmp/jou/ir.bc && /tmp/jou/exe";
    if(verbose)
        puts(command);
    return !!system(command);
}
