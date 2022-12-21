#ifndef AST_H
#define AST_H

#include "misc.h"
#include "token.h"

struct AstCall {
    char funcname[100];
    int arg;
};

// TODO: currently hard-coded: 1 int argument, returntype int
struct AstCImport {
    char funcname[100];
};

enum AstStatementKind {
    AST_STMT_CIMPORT_FUNCTION,
    AST_STMT_CALL,
    AST_STMT_END_OF_FILE,
};

struct AstStatement {
    struct Location location;
    enum AstStatementKind kind;
    union {
        struct AstCImport cimport;
        struct AstCall call;
    } data;
};

#endif
