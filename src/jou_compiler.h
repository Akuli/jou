#ifndef JOU_COMPILER_H
#define JOU_COMPILER_H

#include <stdbool.h>
#include <stdnoreturn.h>
#include <llvm-c/Core.h>
#include "util.h"

// don't like repeating "struct" outside this header file
typedef struct Location Location;
typedef struct Token Token;
typedef struct Type Type;
typedef struct Signature Signature;
typedef struct Constant Constant;

typedef struct AstType AstType;
typedef struct AstSignature AstSignature;
typedef struct AstBody AstBody;
typedef struct AstCall AstCall;
typedef struct AstConditionAndBody AstConditionAndBody;
typedef struct AstExpression AstExpression;
typedef struct AstForLoop AstForLoop;
typedef struct AstVarDeclaration AstVarDeclaration;
typedef struct AstIfStatement AstIfStatement;
typedef struct AstStatement AstStatement;
typedef struct AstToplevelNode AstToplevelNode;
typedef struct AstFunctionDef AstFunctionDef;

typedef struct CfBlock CfBlock;
typedef struct CfGraph CfGraph;
typedef struct CfGraphFile CfGraphFile;
typedef struct CfInstruction CfInstruction;
typedef struct CfVariable CfVariable;


struct Location {
    const char *filename;
    int lineno;
};

#ifdef __GNUC__
    void show_warning(Location location, const char *fmt, ...) __attribute__((format(printf,2,3)));
    noreturn void fail_with_error(Location location, const char *fmt, ...) __attribute__((format(printf,2,3)));
#else
    void show_warning(Location location, const char *fmt, ...);
    noreturn void fail_with_error(Location location, const char *fmt, ...);
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
        TOKEN_END_OF_FILE,  // Marks the end of an array of Token
    } type;
    Location location;
    union {
        int int_value;  // TOKEN_INT
        char char_value;  // TOKEN_CHAR
        char *string_value;  // TOKEN_STRING
        int indentation_level;  // TOKEN_NEWLINE, indicates how many spaces after newline
        char name[100];  // TOKEN_NAME and TOKEN_KEYWORD
        char operator[4];  // TOKEN_OPERATOR
    } data;
};


// Constants can appear in AST and also compilation steps after AST.
struct Constant {
    enum ConstantKind {
        CONSTANT_INTEGER,
        CONSTANT_STRING,
        CONSTANT_NULL,
        CONSTANT_BOOL,
    } kind;
    union {
        struct { int width_in_bits; bool is_signed; long long value; } integer;
        bool boolean;
        char *str;
    } data;
};
#define copy_constant(c) ( (c)->kind==CONSTANT_STRING ? (Constant){ CONSTANT_STRING, {.str=strdup((c)->data.str)} } : *(c) )


/*
There is AstType and Type. The distinction is that AstType only contains
the name of the type (e.g. "int"), whereas Type contains more
information (e.g. 32-bit signed integer) that is figured out separately
after the code has been parsed. This is important for structs.

AstType can also represent "void" even though that is not a valid type.
It simply appears as a type with name "void".
*/
struct AstType {
    Location location;
    char name[100];
    int npointers;  // example: 2 means foo**
};

struct AstSignature {
    char funcname[100];
    int nargs;
    AstType *argtypes;
    char (*argnames)[100];
    bool takes_varargs;  // true for functions like printf()
    AstType returntype;  // can represent void
};

struct AstCall {
    char funcname[100];
    AstExpression *args;
    int nargs;
};

struct AstExpression {
    Location location;

    enum AstExpressionKind {
        AST_EXPR_CONSTANT,
        AST_EXPR_CALL,
        AST_EXPR_GET_VARIABLE,
        AST_EXPR_ADDRESS_OF,
        AST_EXPR_DEREFERENCE,
        AST_EXPR_ASSIGN,
        AST_EXPR_AND,
        AST_EXPR_OR,
        AST_EXPR_NOT,
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
        AST_EXPR_PRE_INCREMENT,  // ++foo
        AST_EXPR_PRE_DECREMENT,  // --foo
        AST_EXPR_POST_INCREMENT,  // foo++
        AST_EXPR_POST_DECREMENT,  // foo--
    } kind;
    union {
        Constant constant;   // AST_EXPR_CONSTANT
        char varname[100];          // AST_EXPR_GET_VARIABLE
        AstCall call;        // AST_EXPR_CALL
        /*
        The "operands" pointer is an array of 1 to 2 expressions.
        A couple examples to hopefully give you an idea of how it works in general:

            * For AST_EXPR_DEREFERENCE, it is the dereferenced value: the "foo" of "*foo".
            * For AST_EXPR_ADD, it is an array of the two things being added.
            * For AST_EXPR_ASSIGN, these are the left and right side of the assignment.
        */
        AstExpression *operands;
    } data;
};

struct AstBody {
    AstStatement *statements;
    int nstatements;
};
struct AstConditionAndBody {
    AstExpression condition;
    AstBody body;
};
struct AstForLoop {
    // for init; cond; incr:
    //     ...body...
    AstExpression init, cond, incr;
    AstBody body;
};
struct AstIfStatement {
    AstConditionAndBody *if_and_elifs;
    int n_if_and_elifs;  // Always >= 1 for the initial "if"
    AstBody elsebody;  // Empty (0 statements) means no else
};
struct AstVarDeclaration {
    char name[100];
    AstType type;
    AstExpression *initial_value; // can be NULL
};

struct AstStatement {
    Location location;
    enum AstStatementKind {
        AST_STMT_RETURN_VALUE,
        AST_STMT_RETURN_WITHOUT_VALUE,
        AST_STMT_IF,
        AST_STMT_WHILE,
        AST_STMT_FOR,
        AST_STMT_BREAK,
        AST_STMT_CONTINUE,
        AST_STMT_DECLARE_LOCAL_VAR,
        AST_STMT_EXPRESSION_STATEMENT,  // Evaluate an expression and discard the result.
    } kind;
    union {
        AstExpression expression;    // for AST_STMT_EXPRESSION_STATEMENT, AST_STMT_RETURN
        AstConditionAndBody whileloop;
        AstIfStatement ifstatement;
        AstForLoop forloop;
        AstVarDeclaration vardecl;
    } data;
};

struct AstFunctionDef {
    AstSignature signature;
    AstBody body;
};

// Toplevel = outermost in the nested structure i.e. what the file consists of
struct AstToplevelNode {
    Location location;
    enum AstToplevelNodeKind {
        AST_TOPLEVEL_END_OF_FILE,  // indicates end of array of AstToplevelNodeKind
        AST_TOPLEVEL_DECLARE_FUNCTION,
        AST_TOPLEVEL_DEFINE_FUNCTION,
    } kind;
    union {
        AstSignature decl_signature;  // for AST_TOPLEVEL_DECLARE_FUNCTION
        AstFunctionDef funcdef;  // for AST_TOPLEVEL_DEFINE_FUNCTION
    } data;
};


struct Type {
    char name[100];   // All types have a name for error messages and debugging.
    enum TypeKind {
        TYPE_SIGNED_INTEGER,
        TYPE_UNSIGNED_INTEGER,
        TYPE_BOOL,
        TYPE_POINTER,
        TYPE_VOID_POINTER,
    } kind;
    union {
        int width_in_bits;  // TYPE_SIGNED_INTEGER, TYPE_UNSIGNED_INTEGER
        Type *valuetype;  // TYPE_POINTER
    } data;
};

// Built-in types, for convenience.
// Named with a differentNamingConvention compared to everything else,
// so you recognize these instead of wondering where they are defined.
extern const Type boolType;      // bool
extern const Type intType;       // int (32-bit signed)
extern const Type byteType;      // byte (8-bit unsigned)
extern const Type stringType;    // byte*
extern const Type voidPtrType;   // void*

// create_pointer_type(...) returns a type whose .data.valuetype must be free()d
// copy_type() is a recursive/deep copy and should be used together with free_type()
Type create_pointer_type(const Type *elem_type, Location error_location);
Type create_integer_type(int size_in_bits, bool is_signed);
Type copy_type(const Type *t);
bool is_integer_type(const Type *t);  // includes signed and unsigned
bool is_pointer_type(const Type *t);  // includes void pointers
bool same_type(const Type *a, const Type *b);
Type type_of_constant(const Constant *c);


struct Signature {
    char funcname[100];
    int nargs;
    Type *argtypes;
    char (*argnames)[100];
    bool takes_varargs;  // true for functions like printf()
    Type *returntype;  // NULL, if does not return a value
    Location returntype_location;  // meaningful even if returntype is NULL
};

char *signature_to_string(const Signature *sig, bool include_return_type);
Signature copy_signature(const Signature *sig);


// Control Flow Graph.
// Struct names not prefixed with Cfg because it looks too much like "config" to me
struct CfVariable {
    char name[100];  // Prefixed with $ for values that are anonymous in Jou code
    Type type;
    // First n variables are always the arguments
    bool is_argument;
};
struct CfInstruction {
    Location location;
    enum CfInstructionKind {
        CF_CONSTANT,
        CF_CALL,
        CF_ADDRESS_OF_VARIABLE,
        CF_PTR_STORE,  // *op1 = op2 (does not use destvar, takes 2 operands)
        CF_PTR_LOAD,  // aka dereference
        CF_PTR_EQ,
        CF_INT_ADD,
        CF_INT_SUB,
        CF_INT_MUL,
        CF_INT_SDIV, // signed division, example with 8 bits: 255 / 2 = (-1) / 2 = 0
        CF_INT_UDIV, // unsigned division: 255 / 2 = 127
        CF_INT_EQ,
        CF_INT_LT,
        CF_INT_SCAST_TO_BIGGER,  // cast to bigger signed int, e.g. 8->16: 0xFF = -1 --> 0xFFFF
        CF_INT_UCAST_TO_BIGGER,  // cast to bigger unsigned int, e.g. 8->16: 0xFF = 255 --> 0x00FF
        CF_BOOL_NEGATE,  // TODO: get rid of this?
        CF_VARCPY, // similar to assignment statements: var1 = var2
        CF_CAST_POINTER,
    } kind;
    union CfInstructionData {
        Constant constant;  // CF_CONSTANT
        char funcname[100];     // CF_CALL
    } data;
    const CfVariable **operands;  // e.g. numbers to add, function arguments
    int noperands;
    const CfVariable *destvar;  // NULL when it doesn't make sense, e.g. functions that return void
    bool hide_unreachable_warning; // usually false, can be set to true to avoid unreachable warning false positives
};

struct CfBlock {
    List(CfInstruction) instructions;
    const CfVariable *branchvar;  // boolean value used to decide where to jump next
    CfBlock *iftrue;
    CfBlock *iffalse;
};

struct CfGraph {
    CfBlock start_block;  // First block
    CfBlock end_block;  // Always empty. Return statement jumps here.
    List(CfBlock *) all_blocks;
    List(CfVariable *) variables;   // First n variables are the function arguments
};

struct CfGraphFile {
    const char *filename;
    int nfuncs;
    Signature *signatures;
    CfGraph **graphs;  // NULL means function is only declared, not defined
};


/*
The compiling functions, i.e. how to go from source code to LLVM IR.
Each function's result is fed into the next.

Make sure that the filename passed to tokenize() stays alive throughout the
entire compilation. It is used in error messages.
*/
Token *tokenize(const char *filename);
AstToplevelNode *parse(const Token *tokens);
CfGraphFile build_control_flow_graphs(AstToplevelNode *ast);
void simplify_control_flow_graphs(const CfGraphFile *cfgfile);
LLVMModuleRef codegen(const CfGraphFile *cfgfile);

/*
Use these to clean up return values of compiling functions.

Even though arrays are typically allocated with malloc(), you shouldn't simply
free() them. For example, free(topnodelist) would free the list of AST nodes,
but not any of the data contained within individual nodes.
*/
void free_type(const Type *type);
void free_constant(const Constant *c);
void free_tokens(Token *tokenlist);
void free_ast(AstToplevelNode *topnodelist);
void free_control_flow_graphs(const CfGraphFile *cfgfile);
void free_control_flow_graph_block(const CfGraph *cfg, CfBlock *b);
// To free LLVM IR, use LLVMDisposeModule

/*
Functions for printing intermediate data for debugging and exploring the compiler.
Most of these take the data for an entire program.
*/
void print_token(const Token *token);
void print_tokens(const Token *tokenlist);
void print_ast(const AstToplevelNode *topnodelist);
void print_control_flow_graph(const CfGraph *cfg);
void print_control_flow_graphs(const CfGraphFile *cfgfile);
void print_llvm_ir(LLVMModuleRef module);

#endif
