#ifndef JOU_COMPILER_H
#define JOU_COMPILER_H

#include <stdbool.h>
#include <stdnoreturn.h>
#include <llvm-c/Core.h>
#include "util.h"

struct Location {
    const char *filename;
    int lineno;
};

#ifdef __GNUC__
    void show_warning(struct Location location, const char *fmt, ...) __attribute__((format(printf,2,3)));
    noreturn void fail_with_error(struct Location location, const char *fmt, ...) __attribute__((format(printf,2,3)));
#else
    void show_warning(struct Location location, const char *fmt, ...);
    noreturn void fail_with_error(struct Location location, const char *fmt, ...);
#endif


struct Token {
    enum TokenType {
        TOKEN_INT,
        TOKEN_CHAR,
        TOKEN_STRING,
        TOKEN_NAME,
        TOKEN_KEYWORD,
        TOKEN_NEWLINE,
        TOKEN_INDENT,
        TOKEN_DEDENT,
        TOKEN_OPERATOR,
        TOKEN_END_OF_FILE,  // Marks the end of an array of struct Token
    } type;
    struct Location location;
    union {
        int int_value;  // TOKEN_INT
        char char_value;  // TOKEN_CHAR
        char *string_value;  // TOKEN_STRING
        int indentation_level;  // TOKEN_NEWLINE, indicates how many spaces after newline
        char name[100];  // TOKEN_NAME and TOKEN_KEYWORD
        char operator[4];  // TOKEN_OPERATOR
    } data;
};


struct Type {
    char name[100];   // All types have a name for error messages and debugging.
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
};

// Built-in types, for convenience.
// Named with a differentNamingConvention compared to everything else,
// so you recognize these instead of wondering where they are defined.
extern const struct Type boolType;      // bool
extern const struct Type intType;       // int (32-bit signed)
extern const struct Type byteType;      // byte (8-bit unsigned)
extern const struct Type stringType;    // byte*


// create_pointer_type(...) returns a type whose .data.valuetype must be free()d
struct Type create_pointer_type(const struct Type *elem_type, struct Location error_location);
struct Type create_integer_type(int size_in_bits, bool is_signed);
struct Type copy_type(const struct Type *t);
bool is_integer_type(const struct Type *t);
bool same_type(const struct Type *a, const struct Type *b);

struct Signature {
    struct Location location;
    char funcname[100];
    int nargs;
    struct Type *argtypes;
    char (*argnames)[100];
    bool takes_varargs;  // true for functions like printf()
    struct Type *returntype;  // NULL, if does not return a value
};
char *signature_to_string(const struct Signature *sig, bool include_return_type);
struct Signature copy_signature(const struct Signature *sig);


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
        AST_EXPR_STRING_CONSTANT,
        AST_EXPR_CALL,
        AST_EXPR_GET_VARIABLE,
        AST_EXPR_ADDRESS_OF,
        AST_EXPR_DEREFERENCE,
        AST_EXPR_ASSIGN,
        AST_EXPR_TRUE,
        AST_EXPR_FALSE,
        AST_EXPR_ADD,
        AST_EXPR_SUB,
        AST_EXPR_MUL,
        AST_EXPR_DIV,
        AST_EXPR_EQ,
        AST_EXPR_NE,
        // We need all of gt,ge,lt,le (>,>=,<,<=) because a<b and b>a do different
        // things: a<b evaluates a first, but b>a evaluates b first.
        AST_EXPR_GT,
        AST_EXPR_GE,
        AST_EXPR_LT,
        AST_EXPR_LE,
    } kind;
    union {
        int int_value;          // AST_EXPR_INT_CONSTANT
        char char_value;        // AST_EXPR_CHAR_CONSTANT
        char *string_value;     // AST_EXPR_STRING_CONSTANT
        char varname[100];      // AST_EXPR_GET_VARIABLE
        struct AstCall call;    // AST_EXPR_CALL
        /*
        The "operands" pointer is an array of 1 to 2 expressions.
        A couple examples to hopefully give you an idea of how it works in general:

            * For AST_EXPR_DEREFERENCE, it is the dereferenced value: the "foo" of "*foo".
            * For AST_EXPR_ADD, it is an array of the two things being added.
            * For AST_EXPR_ASSIGN, these are the left and right side of the assignment.
        */
        struct AstExpression *operands;
    } data;
};

struct AstBody {
    struct AstStatement *statements;
    int nstatements;
};

struct AstStatement {
    struct Location location;
    enum AstStatementKind {
        AST_STMT_EXPRESSION_STATEMENT,  // Evaluate an expression and discard the result.
        AST_STMT_RETURN_VALUE,
        AST_STMT_RETURN_WITHOUT_VALUE,
        AST_STMT_IF,
        AST_STMT_WHILE,
    } kind;
    union {
        struct AstExpression expression;    // for AST_STMT_EXPRESSION_STATEMENT, AST_STMT_RETURN
        struct AstIfStatement {
            struct AstExpression condition;
            struct AstBody body;
        } ifstatement;
        struct AstWhileLoop {
            struct AstExpression condition;
            struct AstBody body;
        } whileloop;
    } data;
};

struct AstFunctionDef {
    struct Signature signature;
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
        struct Signature decl_signature;  // for AST_TOPLEVEL_CDECL_FUNCTION
        struct AstFunctionDef funcdef;  // for AST_TOPLEVEL_DEFINE_FUNCTION
    } data;
};


// Control Flow Graph.
// Struct names not prefixed with Cfg because it looks too much like "config" to me
struct CfVariable {
    char name[100];  // Prefixed with $ for values that are anonymous in Jou code
    struct Type type;
};
struct CfInstruction {
    struct Location location;
    enum CfInstructionKind {
        CF_INT_CONSTANT,
        CF_CHAR_CONSTANT,  // TODO: delete (can use int constant + cast)
        CF_STRING_CONSTANT,
        CF_TRUE,
        CF_FALSE,
        CF_CALL,
        CF_ADDRESS_OF_VARIABLE,
        CF_STORE_TO_POINTER,  // *foo = bar (does not use destvar, see below)
        CF_LOAD_FROM_POINTER,  // aka dereference
        CF_INT_ADD,
        CF_INT_SUB,
        CF_INT_MUL,
        CF_INT_SDIV, // signed division, example with 8 bits: 255 / 2 = (-1) / 2 = 0
        CF_INT_UDIV, // unsigned division: 255 / 2 = 127
        CF_INT_EQ,
        CF_INT_LT,
        CF_BOOL_NEGATE,  // TODO: get rid of this?
        CF_VARCPY, // similar to assignment statements: var1 = var2
        CF_CAST_TO_BIGGER_SIGNED_INT,
        CF_CAST_TO_BIGGER_UNSIGNED_INT,
    } kind;
    union {
        int int_value;          // CF_INT_CONSTANT
        char char_value;        // CF_CHAR_CONSTANT
        char *string_value;     // CF_STRING_CONSTANT
        const struct CfVariable *operands[2];  // e.g. numbers to add
        // TODO: replace nargs with NULL terminated?
        struct { char funcname[100]; const struct CfVariable **args; int nargs; } call; // CF_CALL
    } data;
    const struct CfVariable *destvar;  // NULL when it doesn't make sense, e.g. functions that return void
};

struct CfBlock {
    List(struct CfInstruction) instructions;
    const struct CfVariable *branchvar;  // boolean value used to decide where to jump next
    struct CfBlock *iftrue;
    struct CfBlock *iffalse;
};

struct CfGraph {
    struct CfBlock start_block;  // First block
    struct CfBlock end_block;  // Always empty. Return statement jumps here.
    List(struct CfBlock *) all_blocks;
    List(struct CfVariable *) variables;   // First n variables are the function arguments
};

struct CfGraphFile {
    const char *filename;
    int nfuncs;
    struct Signature *signatures;
    struct CfGraph **graphs;  // NULL means function is only declared, not defined
};


/*
The compiling functions, i.e. how to go from source code to LLVM IR.
Each function's result is fed into the next.

Make sure that the filename passed to tokenize() stays alive throughout the
entire compilation. It is used in error messages.
*/
struct Token *tokenize(const char *filename);
struct AstToplevelNode *parse(const struct Token *tokens);
struct CfGraphFile build_control_flow_graphs(struct AstToplevelNode *ast);
void simplify_control_flow_graphs(const struct CfGraphFile *cfgfile);
LLVMModuleRef codegen(const struct CfGraphFile *cfgfile);

/*
Use these to clean up return values of compiling functions.

Even though arrays are typically allocated with malloc(), you shouldn't simply
free() them. For example, free(topnodelist) would free the list of AST nodes,
but not any of the data contained within individual nodes.
*/
void free_tokens(struct Token *tokenlist);
void free_ast(struct AstToplevelNode *topnodelist);
void free_control_flow_graphs(const struct CfGraphFile *cfgfile);
void free_control_flow_graph_block(const struct CfGraph *cfg, struct CfBlock *b);
// To free LLVM IR, use LLVMDisposeModule

/*
Functions for printing intermediate data for debugging and exploring the compiler.
Most of these take the data for an entire program.
*/
void print_token(const struct Token *token);
void print_tokens(const struct Token *tokenlist);
void print_ast(const struct AstToplevelNode *topnodelist);
void print_control_flow_graphs(const struct CfGraphFile *cfgfile);
void print_llvm_ir(LLVMModuleRef module);

#endif
