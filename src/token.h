#ifndef TOKEN_H
#define TOKEN_H

#include "misc.h"

enum TokenType {
    TOKEN_INT,
    TOKEN_OPENPAREN,
    TOKEN_CLOSEPAREN,
    TOKEN_NAME,
    TOKEN_NEWLINE,
    TOKEN_END_OF_FILE,  // Marks the end of an array of struct Token
    TOKEN_CIMPORT,
};

struct Token {
    enum TokenType type;
    struct Location location;
    union {
        int int_value;  // TOKEN_INT
        char name[100];  // TOKEN_NAME
    } data;
};

#endif
