#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jou_compiler.h"
#include <llvm-c/Analysis.h>

// TODO: test invalid ways to pass arguments, passing non-existent file, etc
int main(int argc, char **argv)
{
    bool verbose;
    const char *filename;

    if (argc == 3 && !strcmp(argv[1], "--verbose")) {
        verbose = true;
        filename = argv[2];
    } else if (argc == 2) {
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

    build_control_flow_graphs(ast);
    if(verbose)
        print_ast(ast);

    LLVMModuleRef module = codegen(ast);
    free_ast(ast);
    if(verbose)
        print_llvm_ir(module);

    // TODO: currently this doesn't work
    //LLVMVerifyModule(module, LLVMAbortProcessAction, NULL);

    // TODO: this is a ridiculous way to run the IR, figure out something better
    FILE *f = fopen("/tmp/jou-temp.bc", "wb");
    assert(f);
    char *s = LLVMPrintModuleToString(module);
    fprintf(f, "%s", s);
    LLVMDisposeMessage(s);
    fclose(f);

    LLVMDisposeModule(module);

    const char *command = "cd /tmp && clang-11 -Wno-override-module -o jou-temp jou-temp.bc && ./jou-temp";
    if(verbose)
        puts(command);
    return !!system(command);
}
