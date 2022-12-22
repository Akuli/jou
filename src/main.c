#include <stdio.h>
#include <stdlib.h>

#include "compile_steps.h"

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s FILENAME\n", argv[0]);
        return 2;
    }

    struct Token *tokens = tokenize(argv[1]);
    print_tokens(tokens);

    struct AstStatement *ast = parse(tokens);
    free_tokens(tokens);
    print_ast(ast);

    LLVMModuleRef module = codegen(ast);
    free(ast);

    char *s = LLVMPrintModuleToString(module);
    printf("LLVM IR:\n\n%s", s);
    LLVMDisposeMessage(s);
}
