#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "compile_steps.h"

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

    LLVMModuleRef module = codegen(ast);
    free(ast);

    char *s = LLVMPrintModuleToString(module);
    if(verbose)
        printf("--- LLVM IR for file \"%s\" ---\n%s", filename, s);

    // TODO: this is a ridiculous way to run the IR, figure out something better
    FILE *f = fopen("/tmp/newlang-temp.bc", "wb");
    assert(f);
    fprintf(f, "%s", s);
    fclose(f);

    LLVMDisposeMessage(s);
    LLVMDisposeModule(module);

    return !!system("cd /tmp && clang-11 -Wno-override-module -o newlang-temp newlang-temp.bc && ./newlang-temp");
}
