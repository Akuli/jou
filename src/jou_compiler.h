#ifndef JOU_COMPILER_H
#define JOU_COMPILER_H

#include <stdbool.h>
#include <stdnoreturn.h>
#include <llvm-c/Core.h>

struct Location {
    const char *filename;
    int lineno;
};
noreturn void fail_with_error(struct Location location, const char *fmt, ...);


struct Token {
    enum TokenType {
        TOKEN_INT,
        TOKEN_CHAR,
        TOKEN_NAME,
        TOKEN_KEYWORD,
        TOKEN_NEWLINE,
        TOKEN_INDENT,
        TOKEN_DEDENT,
        TOKEN_END_OF_FILE,  // Marks the end of an array of struct Token
        // operators
        TOKEN_OPENPAREN,
        TOKEN_CLOSEPAREN,
        TOKEN_COLON,
        TOKEN_ARROW,
        TOKEN_STAR,
        TOKEN_AMP,
    } type;
    struct Location location;
    union {
        char char_value;  // TOKEN_CHAR
        int int_value;  // TOKEN_INT
        int indentation_level;  // TOKEN_NEWLINE, indicates how many spaces after newline
        char name[100];  // TOKEN_NAME and TOKEN_KEYWORD
    } data;
};


// The AST contains types only where the types were specified by the user,
// e.g. in function declarations. This is a bit weird, but I don't want a
// "not-yet-known type" struct that would be used only in AST.
struct Type {
    enum TypeKind {
        TYPE_SIGNED_INTEGER,
        TYPE_UNSIGNED_INTEGER,
        TYPE_BOOL,
        TYPE_POINTER,
    } kind;
    union {
        int width_in_bits;  // TYPE_SIGNED_INTEGER, TYPE_UNSIGNED_INTEGER
        struct Type *valuetype;  // TYPE_POINTER
    } data;

    // All types have a name for error messages and debugging.
    char name[100];
};


// returnvalue.data.valuetype must be free()d
struct Type create_pointer_type(const struct Type *elem_type, struct Location error_location);

bool same_type(const struct Type *a, const struct Type *b);
bool can_implicitly_convert(const struct Type *from, const struct Type *to);


struct AstCall {
    char funcname[100];
    struct AstExpression *args;
    int nargs;
};

struct AstExpression {
    struct Location location;
    enum AstExpressionKind {
        AST_EXPR_INT_CONSTANT,
        AST_EXPR_CHAR_CONSTANT,
        AST_EXPR_CALL,
        AST_EXPR_GET_VARIABLE,
        AST_EXPR_ADDRESS_OF_VARIABLE,
        AST_EXPR_DEREFERENCE,
        AST_EXPR_TRUE,
        AST_EXPR_FALSE,
    } kind;
    union {
        int int_value;          // AST_EXPR_INT_CONSTANT
        char char_value;        // AST_EXPR_CHAR_CONSTANT
        char varname[100];      // AST_EXPR_GET_VARIABLE, AST_EXPR_ADDRESS_OF_VARIABLE
        struct AstCall call;    // AST_EXPR_CALL
        struct AstExpression *pointerexpr;  // AST_EXPR_DEREFERENCE
    } data;
};

// TODO: currently hard-coded: all arguments have type int
struct AstFunctionSignature {
    struct Location location;
    char funcname[100];
    int nargs;
    struct Type *argtypes;
    char (*argnames)[100];
    struct Type *returntype;  // NULL, if does not return a value
};

struct AstBody {
    struct AstStatement *statements;
    int nstatements;
};

struct AstStatement {
    struct Location location;
    enum AstStatementKind {
        AST_STMT_CALL,
        AST_STMT_RETURN_VALUE,
        AST_STMT_RETURN_WITHOUT_VALUE,
        AST_STMT_IF,
    } kind;
    union {
        struct AstCall call;                // for AST_STMT_CALL
        struct AstExpression returnvalue;   // for AST_STMT_RETURN
        struct AstIfStatement {
            struct AstExpression condition;
            struct AstBody body;
        } ifstatement;
    } data;
};

struct AstFunctionDef {
    struct AstFunctionSignature signature;
    struct AstBody body;
};

// Toplevel = outermost in the nested structure i.e. what the file consists of
struct AstToplevelNode {
    struct Location location;
    enum AstToplevelNodeKind {
        AST_TOPLEVEL_END_OF_FILE,  // indicates end of array of AstToplevelNodeKind
        AST_TOPLEVEL_CDECL_FUNCTION,
        AST_TOPLEVEL_DEFINE_FUNCTION,
    } kind;
    union {
        struct AstFunctionSignature decl_signature;  // for AST_TOPLEVEL_CDECL_FUNCTION
        struct AstFunctionDef funcdef;  // for AST_TOPLEVEL_DEFINE_FUNCTION
    } data;
};


/*
The compiling functions, i.e. how to go from source code to LLVM IR.
Each function's result is fed into the next.

Make sure that the filename passed to tokenize() stays alive throughout the
entire compilation. It is used in error messages.
*/
struct Token *tokenize(const char *filename);
struct AstToplevelNode *parse(const struct Token *tokens);
void typecheck(const struct AstToplevelNode *ast);
LLVMModuleRef codegen(const struct AstToplevelNode *ast);

/*
Use these to clean up return values of compiling functions.

Even though arrays are typically allocated with malloc(), you shouldn't simply
free() them. For example, free(topnodelist) would free the list of AST nodes,
but not any of the data contained within individual nodes.
*/
void free_tokens(struct Token *tokenlist);
void free_ast(struct AstToplevelNode *topnodelist);
// To free LLVM IR, use LLVMDisposeModule

/*
Functions for printing intermediate data for debugging and exploring the compiler.
Most of these take the data for an entire program.
*/
void print_token(const struct Token *token);
void print_tokens(const struct Token *tokenlist);
void print_ast(const struct AstToplevelNode *topnodelist);
void print_llvm_ir(LLVMModuleRef module);

#endif
