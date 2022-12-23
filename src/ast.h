#ifndef AST_H
#define AST_H

#include "misc.h"
#include "token.h"

struct AstCall {
    char funcname[100];
    int arg;
};

// TODO: currently hard-coded: all arguments have type int, returntype int
struct AstFunctionSignature {
    struct Location location;
    char funcname[100];
    int nargs;
};

enum AstStatementKind {
    AST_STMT_CALL,
    AST_STMT_RETURN,
};

struct AstStatement {
    struct Location location;
    enum AstStatementKind kind;
    union {
        struct AstCall call;    // for AST_STMT_CALL
        int returnvalue;        // for AST_STMT_RETURN
    } data;
};

struct AstBody {
    struct AstStatement *statements;
    int nstatements;
};

struct AstFunctionDef {
    struct AstFunctionSignature signature;
    struct AstBody body;
};

// Toplevel = outermost in the nested structure i.e. what the file consists of
enum AstToplevelNodeKind {
    AST_TOPLEVEL_END_OF_FILE,  // indicates end of array of AstToplevelNodeKind
    AST_TOPLEVEL_CIMPORT_FUNCTION,
    AST_TOPLEVEL_DEFINE_FUNCTION,
};

struct AstToplevelNode {
    struct Location location;
    enum AstToplevelNodeKind kind;
    union {
        struct AstFunctionSignature cimport_signature;  // for AST_TOPLEVEL_CIMPORT_FUNCTION
        struct AstFunctionDef funcdef;  // for AST_TOPLEVEL_DEFINE_FUNCTION
    } data;
};

#endif
