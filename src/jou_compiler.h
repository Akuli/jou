#ifndef JOU_COMPILER_H
#define JOU_COMPILER_H

#include <stdbool.h>
#include <stdnoreturn.h>
#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>
#include "util.h"

void update_jou_compiler(void);

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
typedef struct AstAssignment AstAssignment;
typedef struct AstForLoop AstForLoop;
typedef struct AstNameTypeValue AstNameTypeValue;
typedef struct AstIfStatement AstIfStatement;
typedef struct AstStatement AstStatement;
typedef struct AstToplevelNode AstToplevelNode;
typedef struct AstFunctionDef AstFunctionDef;
typedef struct AstClassDef AstClassDef;
typedef struct AstEnumDef AstEnumDef;
typedef struct AstImport AstImport;

typedef struct GlobalVariable GlobalVariable;
typedef struct LocalVariable LocalVariable;
typedef struct ExpressionTypes ExpressionTypes;
typedef struct ExportSymbol ExportSymbol;
typedef struct FileTypes FileTypes;
typedef struct FunctionOrMethodTypes FunctionOrMethodTypes;

typedef struct CfBlock CfBlock;
typedef struct CfGraph CfGraph;
typedef struct CfGraphFile CfGraphFile;
typedef struct CfInstruction CfInstruction;


// Command-line arguments are a global variable because I like it.
extern struct CommandLineArgs {
    const char *argv0;  // Program name
    int verbosity;  // How much debug/progress info to print, how many times -v/--verbose passed
    bool tokenize_only;  // If true, tokenize the file passed on command line and don't actually compile anything
    bool parse_only;  // If true, parse the file passed on command line and don't actually compile anything
    int optlevel;  // Optimization level (0 don't optimize, 3 optimize a lot)
    const char *infile;  // The "main" Jou file (can import other files)
    const char *outfile;  // If not NULL, where to output executable
    const char *linker_flags;  // String that is appended to linking command
} command_line_args;

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
        TOKEN_LONG,
        TOKEN_FLOAT,
        TOKEN_DOUBLE,
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
        int32_t int_value;  // TOKEN_INT
        int64_t long_value;  // TOKEN_LONG
        char char_value;  // TOKEN_CHAR
        char *string_value;  // TOKEN_STRING
        int indentation_level;  // TOKEN_NEWLINE, indicates how many spaces after newline
        char name[100];  // TOKEN_NAME and TOKEN_KEYWORD. Also TOKEN_DOUBLE & TOKEN_FLOAT (LLVM wants a string anyway)
        char operator[4];  // TOKEN_OPERATOR
    } data;
};

// Constants can appear in AST and also compilation steps after AST.
struct Constant {
    enum ConstantKind {
        CONSTANT_ENUM_MEMBER,
        CONSTANT_INTEGER,
        CONSTANT_FLOAT,
        CONSTANT_DOUBLE,
        CONSTANT_STRING,
        CONSTANT_NULL,
        CONSTANT_BOOL,
    } kind;
    union {
        struct { int width_in_bits; bool is_signed; long long value; } integer;
        char *str;
        char double_or_float_text[100];  // convenient because LLVM wants a string anyway
        bool boolean;
        struct { const Type *enumtype; int memberidx; } enum_member;
    } data;
};
#define copy_constant(c) ( (c)->kind==CONSTANT_STRING ? (Constant){ CONSTANT_STRING, {.str=strdup((c)->data.str)} } : *(c) )
#define int_constant(Type, Val) (\
    assert(is_integer_type((Type))), \
    (Constant){ \
        .kind = CONSTANT_INTEGER, \
        .data.integer = { \
            .width_in_bits = (Type)->data.width_in_bits, \
            .is_signed = (Type)->kind==TYPE_SIGNED_INTEGER, \
            .value = (Val), \
        } \
    } \
)

/*
There is AstType and Type. The distinction is that Type contains more
information, e.g. AstType could just contain the name "int" while the
Type knows that it is a 32-bit signed integer. This is important for
e.g. structs: we only know the name of a struct when parsing, but we
will eventually need to know a lot more.

AstType can also represent "void" even though that is not a valid type.
It simply appears as a named type with name "void".
*/
struct AstType {
    enum AstTypeKind { AST_TYPE_NAMED, AST_TYPE_POINTER, AST_TYPE_ARRAY } kind;
    Location location;
    union {
        char name[100];  // AST_TYPE_NAMED;
        AstType *valuetype;  // AST_TYPE_POINTER
        struct { AstType *membertype; AstExpression *len; } array;  // AST_TYPE_ARRAY
    } data;
};

struct AstSignature {
    Location name_location;
    char name[100];
    List(AstNameTypeValue) args;
    bool takes_varargs;  // true for functions like printf()
    AstType returntype;  // can represent void
};

struct AstCall {
    char calledname[100];  // e.g. function name, method name, struct name (instantiation)
    char (*argnames)[100];  // NULL when arguments are not named, e.g. function calls
    AstExpression *args;
    int nargs;
};

struct AstExpression {
    Location location;

    enum AstExpressionKind {
        AST_EXPR_CONSTANT,
        AST_EXPR_GET_ENUM_MEMBER,  // Cannot be just a Constant because ast doesn't know about Types.
        AST_EXPR_FUNCTION_CALL,
        AST_EXPR_BRACE_INIT,
        AST_EXPR_ARRAY,
        AST_EXPR_GET_FIELD,     // foo.bar
        AST_EXPR_DEREF_AND_GET_FIELD,  // foo->bar (shorthand for (*foo).bar)
        AST_EXPR_CALL_METHOD,  // foo.bar()
        AST_EXPR_DEREF_AND_CALL_METHOD,  // foo->bar()
        AST_EXPR_INDEXING,  // foo[bar]
        AST_EXPR_AS,  // foo as SomeType
        AST_EXPR_GET_VARIABLE,
        AST_EXPR_ADDRESS_OF,
        AST_EXPR_SIZEOF,
        AST_EXPR_DEREFERENCE,
        AST_EXPR_AND,
        AST_EXPR_OR,
        AST_EXPR_NOT,
        AST_EXPR_ADD,
        AST_EXPR_SUB,
        AST_EXPR_NEG,
        AST_EXPR_MUL,
        AST_EXPR_DIV,
        AST_EXPR_MOD,
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
        Constant constant;  // AST_EXPR_CONSTANT
        char varname[100];  // AST_EXPR_GET_VARIABLE
        AstCall call;       // AST_EXPR_CALL, AST_EXPR_BRACE_INIT
        struct { int count; AstExpression *items; } array;  // AST_EXPR_ARRAY
        struct { AstExpression *obj; AstType type; } as;    // AST_EXPR_AS
        struct { AstExpression *obj; struct AstCall call; } methodcall; // AST_EXPR_CALL_METHOD, AST_EXPR_DEREF_AND_CALL_METHOD
        struct { AstExpression *obj; char fieldname[100]; } classfield; // AST_EXPR_GET_FIELD, AST_EXPR_DEREF_AND_GET_FIELD
        struct { char enumname[100]; char membername[100]; } enummember; // AST_EXPR_GET_ENUM_MEMBER
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
    /*
    for init; cond; incr:
        ...body...

    init and incr must be pointers because this struct goes inside AstStatement.
    */
    AstStatement *init;
    AstExpression cond;
    AstStatement *incr;
    AstBody body;
};
struct AstIfStatement {
    AstConditionAndBody *if_and_elifs;
    int n_if_and_elifs;  // Always >= 1 for the initial "if"
    AstBody elsebody;  // Empty (0 statements) means no else
};
struct AstNameTypeValue {
    // name: type = value
    char name[100];
    Location name_location;
    AstType type;
    AstExpression *value; // can be NULL if value is missing
};
struct AstAssignment {
    // target = value
    AstExpression target;
    AstExpression value;
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
        AST_STMT_ASSIGN,
        AST_STMT_INPLACE_ADD, // x += y
        AST_STMT_INPLACE_SUB,
        AST_STMT_INPLACE_MUL,
        AST_STMT_INPLACE_DIV,
        AST_STMT_INPLACE_MOD,
        AST_STMT_EXPRESSION_STATEMENT,  // Evaluate an expression and discard the result.
    } kind;
    union {
        AstExpression expression;    // for AST_STMT_EXPRESSION_STATEMENT, AST_STMT_RETURN
        AstConditionAndBody whileloop;
        AstIfStatement ifstatement;
        AstForLoop forloop;
        AstNameTypeValue vardecl;
        AstAssignment assignment;  // also used for inplace operations
    } data;
};

struct AstFunctionDef {
    AstSignature signature;
    AstBody body;
};

struct AstClassDef {
    char name[100];
    List(AstNameTypeValue) fields;
    List(AstFunctionDef) methods;
};

struct AstEnumDef {
    char name[100];
    char (*membernames)[100];
    int nmembers;
};

struct AstImport {
    char *path;  // Relative to current working directory, so e.g. "blah/stdlib/io.jou"
    char symbolname[100];
    bool found, used;    // For errors/warnings
};

// Toplevel = outermost in the nested structure i.e. what the file consists of
struct AstToplevelNode {
    Location location;
    enum AstToplevelNodeKind {
        AST_TOPLEVEL_END_OF_FILE,  // indicates end of array of AstToplevelNodeKind
        AST_TOPLEVEL_DECLARE_FUNCTION,
        AST_TOPLEVEL_DECLARE_GLOBAL_VARIABLE,
        AST_TOPLEVEL_DEFINE_FUNCTION,
        AST_TOPLEVEL_DEFINE_GLOBAL_VARIABLE,
        AST_TOPLEVEL_DEFINE_CLASS,
        AST_TOPLEVEL_DEFINE_ENUM,
        AST_TOPLEVEL_IMPORT,
    } kind;
    union {
        AstNameTypeValue globalvar;  // AST_TOPLEVEL_DECLARE_GLOBAL_VARIABLE
        AstFunctionDef funcdef;  // AST_TOPLEVEL_DECLARE_FUNCTION, AST_TOPLEVEL_DEFINE_FUNCTION (body is empty for declaring)
        AstClassDef classdef;  // AST_TOPLEVEL_DEFINE_CLASS
        AstEnumDef enumdef;     // AST_TOPLEVEL_DEFINE_ENUM
        AstImport import;       // AST_TOPLEVEL_IMPORT
    } data;
};


struct ClassData {
    List(struct ClassField { char name[100]; const Type *type; }) fields;
    List(Signature) methods;
};

struct Type {
    char name[500];   // All types have a name for error messages and debugging.
    enum TypeKind {
        TYPE_SIGNED_INTEGER,
        TYPE_UNSIGNED_INTEGER,
        TYPE_BOOL,
        TYPE_FLOATING_POINT,  // float or double
        TYPE_POINTER,
        TYPE_VOID_POINTER,
        TYPE_ARRAY,
        TYPE_CLASS,
        TYPE_OPAQUE_CLASS,  // struct with unknown members
        TYPE_ENUM,
    } kind;
    union {
        int width_in_bits;  // TYPE_SIGNED_INTEGER, TYPE_UNSIGNED_INTEGER, TYPE_FLOATING_POINT
        const Type *valuetype;  // TYPE_POINTER
        struct ClassData classdata;  // TYPE_CLASS
        struct { const Type *membertype; int len; } array;  // TYPE_ARRAY
        struct { int count; char (*names)[100]; } enummembers;
    } data;
};

/*
Types are cached into global state. This makes a lot of things easier
because you don't need to copy and free the types everywhere. This is
important: previously it was a lot of work to find forgotten copies and
frees with valgrind.

This also simplifies checking whether two types are the same type: you
can simply use "==" between two "const Type *" pointers.

Class types are a bit different. When you make a class, you get a
pointer that you must pass to free_type() later. You can still "=="
compare types, because two different classes with the same members are
not the same type.
*/
extern const Type *boolType;      // bool
extern const Type *intType;       // int (32-bit signed)
extern const Type *longType;      // long (64-bit signed)
extern const Type *sizeType;      // size_t (unsigned)
extern const Type *byteType;      // byte (8-bit unsigned)
extern const Type *floatType;     // float (32-bit)
extern const Type *doubleType;    // double (64-bit)
extern const Type *voidPtrType;   // void*
void init_types();  // Called once when compiler starts
const Type *get_integer_type(int size_in_bits, bool is_signed);
const Type *get_pointer_type(const Type *t);  // result lives as long as t
const Type *get_array_type(const Type *t, int len);  // result lives as long as t
const Type *type_of_constant(const Constant *c);
Type *create_opaque_struct(const char *name);
Type *create_enum(const char *name, int membercount, char (*membernames)[100]);
void free_type(Type *type);

bool is_integer_type(const Type *t);  // includes signed and unsigned
bool is_number_type(const Type *t);  // integers, floats, doubles
bool is_pointer_type(const Type *t);  // includes void pointers

struct Signature {
    char name[100];  // Function or method name. For methods it does not include the name of the class.
    int nargs;
    const Type **argtypes;
    char (*argnames)[100];
    bool takes_varargs;  // true for functions like printf()
    const Type *returntype;  // NULL, if does not return a value
    Location returntype_location;  // meaningful even if returntype is NULL
};

void free_signature(const Signature *sig);
const Type *get_self_class(const Signature *sig);  // NULL for functions, a class for methods
char *signature_to_string(const Signature *sig, bool include_return_type);
Signature copy_signature(const Signature *sig);


struct GlobalVariable {
    char name[100];  // Same as in user's code, never empty
    const Type *type;
    bool defined_in_current_file;  // not declare-only (e.g. stdout) or imported
    bool *usedptr;  // If non-NULL, set to true when the variable is used. This is how we detect unused imports.
};
struct LocalVariable {
    int id;  // Unique, but you can also compare pointers to Variable.
    char name[100];  // Same name as in user's code, empty for temporary variables created by compiler
    const Type *type;
    bool is_argument;    // First n variables are always the arguments
};

struct ExpressionTypes {
    const AstExpression *expr;
    const Type *type;
    const Type *type_after_cast;  // NULL for no implicit cast
};

struct ExportSymbol {
    enum ExportSymbolKind { EXPSYM_FUNCTION, EXPSYM_TYPE, EXPSYM_GLOBAL_VAR } kind;
    char name[200];  // For methods this is "StructName.method_name"
    union {
        Signature funcsignature;
        const Type *type;  // EXPSYM_TYPE and EXPSYM_GLOBAL_VAR
    } data;
};

// Type information about a function or method defined in the current file.
struct FunctionOrMethodTypes {
    Signature signature;
    List(ExpressionTypes *) expr_types;
    List(LocalVariable *) locals;
};

// Type information about a file.
struct FileTypes {
    FunctionOrMethodTypes *current_fom_types;  // conceptually this is internal to typecheck.c
    List(FunctionOrMethodTypes) fomtypes;
    List(GlobalVariable *) globals;  // TODO: probably doesn't need to has pointers
    List(Type *) owned_types;   // These will be freed later
    List(struct TypeAndUsedPtr { const Type *type; bool *usedptr; }) types;
    List(struct SignatureAndUsedPtr { Signature signature; bool *usedptr; }) functions;
};

/*
Type checking is split into several stages:
    1. Create types. After this, structs defined in Jou exist, but
       they are opaque and contain no members. Enums exist and contain
       their members (although it doesn't really matter whether enum
       members are handled in step 1 or 2).
    2. Check signatures, global variables and struct bodies. This step
       assumes that all types exist, but doesn't need to know what
       fields each struct has.
    3. Check function bodies.

Each stage is ran on all files before we move on to the next stage. This
ensures that different files can import things from each other. You
also never need to forward-declare a struct: the struct exists by the
time we check the function signature or struct body that uses it.

Steps 1 and 2 return a list of ExportSymbols for other files to use.
The list is terminated with (ExportSymbol){0}, which you can detect by
checking if the name of the ExportSymbol is empty.
*/
ExportSymbol *typecheck_stage1_create_types(FileTypes *ft, const AstToplevelNode *ast);
ExportSymbol *typecheck_stage2_signatures_globals_structbodies(FileTypes *ft, const AstToplevelNode *ast);
void typecheck_stage3_function_and_method_bodies(FileTypes *ft, const AstToplevelNode *ast);


// Control Flow Graph.
// Struct names not prefixed with Cfg because it looks too much like "config" to me
struct CfInstruction {
    Location location;
    enum CfInstructionKind {
        CF_CONSTANT,
        CF_CALL,  // function or method call, depending on whether self_type is NULL (see below)
        CF_ADDRESS_OF_LOCAL_VAR,
        CF_ADDRESS_OF_GLOBAL_VAR,
        CF_SIZEOF,
        CF_PTR_MEMSET_TO_ZERO,  // takes one operand, a pointer: memset(ptr, 0, sizeof(*ptr))
        CF_PTR_STORE,  // *op1 = op2 (does not use destvar, takes 2 operands)
        CF_PTR_LOAD,  // aka dereference
        CF_PTR_EQ,
        CF_PTR_CLASS_FIELD,  // takes 1 operand (pointer), sets destvar to &op->fieldname
        CF_PTR_CAST,
        CF_PTR_ADD_INT,
        // Left and right side of number operations must be of the same type (except CF_NUM_CAST).
        CF_NUM_ADD,
        CF_NUM_SUB,
        CF_NUM_MUL,
        CF_NUM_DIV,
        CF_NUM_MOD,
        CF_NUM_EQ,
        CF_NUM_LT,
        CF_NUM_CAST,
        CF_ENUM_TO_INT32,
        CF_INT32_TO_ENUM,
        CF_BOOL_NEGATE,  // TODO: get rid of this?
        CF_VARCPY, // similar to assignment statements: var1 = var2
    } kind;
    union CfInstructionData {
        Constant constant;      // CF_CONSTANT
        Signature signature;    // CF_CALL
        char fieldname[100];    // CF_PTR_CLASS_FIELD
        char globalname[100];   // CF_ADDRESS_OF_GLOBAL_VAR
        const Type *type;       // CF_SIZEOF
    } data;
    const LocalVariable **operands;  // e.g. numbers to add, function arguments
    int noperands;
    const LocalVariable *destvar;  // NULL when it doesn't make sense, e.g. functions that return void
    bool hide_unreachable_warning; // usually false, can be set to true to avoid unreachable warning false positives
};

struct CfBlock {
    List(CfInstruction) instructions;
    const LocalVariable *branchvar;  // boolean value used to decide where to jump next
    CfBlock *iftrue;
    CfBlock *iffalse;
};

struct CfGraph {
    Signature signature;
    CfBlock start_block;  // First block
    CfBlock end_block;  // Always empty. Return statement jumps here.
    List(CfBlock *) all_blocks;
    List(LocalVariable *) locals;   // First n variables are the function arguments
};

struct CfGraphFile {
    const char *filename;
    List(CfGraph*) graphs;  // only for defined functions
};


/*
LLVM makes a mess of how to define what kind of computer will run the
compiled programs. Sometimes it wants a target triple, sometimes a
data layout. Sometimes it wants a string, sometimes an object
representing the thing.

This struct aims to provide everything you may ever need. Hopefully it
will make the mess slightly less miserable to you.
*/
struct Target {
    char triple[100];
    char data_layout[500];
    LLVMTargetRef target_ref;
    LLVMTargetMachineRef target_machine_ref;
    LLVMTargetDataRef target_data_ref;
};
void init_target(void);
const struct Target *get_target(void);

/*
The compiling functions, i.e. how to go from source code to LLVM IR and
eventually running the LLVM IR. Each function's result is fed into the next.

Make sure that the filename passed to tokenize() stays alive throughout the
entire compilation. It is used in error messages.
*/
Token *tokenize(FILE *f, const char *filename);
AstToplevelNode *parse(const Token *tokens, const char *stdlib_path);
// Type checking happens between parsing and building CFGs.
CfGraphFile build_control_flow_graphs(AstToplevelNode *ast, FileTypes *ft);
void simplify_control_flow_graphs(const CfGraphFile *cfgfile);
LLVMModuleRef codegen(const CfGraphFile *cfgfile, const FileTypes *ft);
char *compile_to_object_file(LLVMModuleRef module);
char *get_default_exe_path(void);
void run_linker(const char *const *objpaths, const char *exepath);
int run_exe(const char *exepath);

/*
Use these to clean up return values of compiling functions.

Even though arrays are typically allocated with malloc(), you shouldn't simply
free() them. For example, free(topnodelist) would free the list of AST nodes,
but not any of the data contained within individual nodes.
*/
void free_constant(const Constant *c);
void free_tokens(Token *tokenlist);
void free_ast(AstToplevelNode *topnodelist);
void free_file_types(const FileTypes *ft);
void free_export_symbol(const ExportSymbol *es);
void free_control_flow_graphs(const CfGraphFile *cfgfile);
void free_control_flow_graph_block(const CfGraph *cfg, CfBlock *b);

/*
Functions for printing intermediate data for debugging and exploring the compiler.
Most of these take the data for an entire program.
*/
void print_token(const Token *token);
void print_tokens(const Token *tokenlist);
void print_ast(const AstToplevelNode *topnodelist);
void print_control_flow_graph(const CfGraph *cfg);
void print_control_flow_graphs(const CfGraphFile *cfgfile);
void print_llvm_ir(LLVMModuleRef module, bool is_optimized);

#endif
