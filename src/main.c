#include <stdio.h>
#include <stdlib.h>
#include "tokenizer.h"

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s FILENAME\n", argv[0]);
        return 2;
    }

    struct Token *tokens = tokenize(argv[1]);
    print_tokens(tokens);
    free_tokens(tokens);
}
