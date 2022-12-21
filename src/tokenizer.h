#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "error.h"

enum TokenType {
    TOKENTYPE_INT,
    TOKENTYPE_OPENPAREN,
    TOKENTYPE_CLOSEPAREN,
    TOKENTYPE_NAME,
    TOKENTYPE_NEWLINE,
    TOKENTYPE_END_OF_FILE,
};

struct Token {
    enum TokenType type;
    struct Location location;
    union {
        int value;  // TOKENTYPE_INT
        char *string;  // TOKENTYPE_NAME
    } data;
};

// Resulting tokens will point at the filename passed here.
// The filename must therefore be somewhat long-living.
//
// Last token will have TOKENTYPE_END_OF_FILE.
// You need to use free_tokens()
struct Token *tokenize(const char *filename);

// tokens list should be similar to what tokenize() returns
void print_tokens(const struct Token *tokens);

void free_tokens(struct Token *tokenlist);

#endif
