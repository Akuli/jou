#ifndef TOKEN_H
#define TOKEN_H

#include "misc.h"

enum TokenType {
    TOKEN_INT,
    TOKEN_NAME,
    TOKEN_NEWLINE,
    TOKEN_INDENT,
    TOKEN_DEDENT,
    TOKEN_END_OF_FILE,  // Marks the end of an array of struct Token

    // operators
    TOKEN_OPENPAREN,
    TOKEN_CLOSEPAREN,
    TOKEN_COLON,

    // keywords
    TOKEN_RETURN,
    TOKEN_CIMPORT,
};

struct Token {
    enum TokenType type;
    struct Location location;
    union {
        int int_value;  // TOKEN_INT
        int indentation_level;  // TOKEN_NEWLINE has this to indicate how many spaces after newline
        char name[100];  // TOKEN_NAME
    } data;
};

#endif
