#include <stdio.h>
#include "jou_compiler.h"

void print_token(const struct Token *token)
{
    switch(token->type) {
        #define f(x) case x: printf(#x); break
        f(TOKEN_INT);
        f(TOKEN_OPENPAREN);
        f(TOKEN_CLOSEPAREN);
        f(TOKEN_NAME);
        f(TOKEN_NEWLINE);
        f(TOKEN_END_OF_FILE);
        f(TOKEN_CDECL);
        f(TOKEN_COLON);
        f(TOKEN_INDENT);
        f(TOKEN_DEDENT);
        f(TOKEN_RETURN);
        f(TOKEN_ARROW);
        f(TOKEN_DEF);
        #undef f
    }

    switch(token->type) {
    case TOKEN_INT:
        printf(" value=%d", token->data.int_value);
        break;
    case TOKEN_NAME:
        printf(" name=\"%s\"", token->data.name);
        break;
    case TOKEN_NEWLINE:
        printf(" indentation_level=%d", token->data.indentation_level);
        break;

    // These tokens don't have any associated data to be printed here.
    //
    // Listed explicitly instead of "default" so that you get a compiler error
    // coming from here after adding a new token type. That should remind you
    // to keep this function up to date.
    case TOKEN_CDECL:
    case TOKEN_OPENPAREN:
    case TOKEN_CLOSEPAREN:
    case TOKEN_COLON:
    case TOKEN_END_OF_FILE:
    case TOKEN_INDENT:
    case TOKEN_DEDENT:
    case TOKEN_RETURN:
    case TOKEN_ARROW:
    case TOKEN_DEF:
        break;
    }

    printf("\n");
}

void print_tokens(const struct Token *tokens)
{
    printf("--- Tokens for file \"%s\" ---\n", tokens->location.filename);
    int lastlineno = -1;
    do {
        if (tokens->location.lineno != lastlineno) {
            printf("Line %d:\n", tokens->location.lineno);
            lastlineno = tokens->location.lineno;
        }
        printf("  ");
        print_token(tokens);
    } while (tokens++->type != TOKEN_END_OF_FILE);

    printf("\n");
}

static void print_ast_function_signature(const struct AstFunctionSignature *sig, int indent)
{
    printf("%*sfunction signature (on line %d): %s(", indent, "", sig->location.lineno, sig->funcname);
    for (int i = 0; i < sig->nargs; i++) {
        if(i) printf(", ");
        printf("int");
    }
    printf(") -> int\n");
}

static void print_ast_statement(const struct AstStatement *stmt, int indent)
{
    printf("%*s(line %d) ", indent, "", stmt->location.lineno);
    switch(stmt->kind) {
        #define f(x) case x: printf(#x); break
        f(AST_STMT_CALL);
        f(AST_STMT_RETURN);
        #undef f
    }

    switch(stmt->kind) {
        case AST_STMT_CALL:
            printf(" funcname=\"%s\" arg=%d\n", stmt->data.call.funcname, stmt->data.call.arg);
            break;
        case AST_STMT_RETURN:
            printf(" returnvalue=%d\n", stmt->data.returnvalue);
            break;
    }
}

static void print_ast_body(const struct AstBody *body, int indent)
{
    printf("%*sbody:\n", indent, "");
    for (int i = 0; i < body->nstatements; i++)
        print_ast_statement(&body->statements[i], indent+2);
}

void print_ast(const struct AstToplevelNode *topnodelist)
{
    printf("--- AST for file \"%s\" ---\n", topnodelist->location.filename);

    do {
        printf("line %d: ", topnodelist->location.lineno);

        switch(topnodelist->kind) {
            #define f(x) case x: printf(#x); break
            f(AST_TOPLEVEL_DEFINE_FUNCTION);
            f(AST_TOPLEVEL_CDECL_FUNCTION);
            f(AST_TOPLEVEL_END_OF_FILE);
            #undef f
        }
        printf("\n");

        switch(topnodelist->kind) {
            case AST_TOPLEVEL_CDECL_FUNCTION:
                print_ast_function_signature(&topnodelist->data.decl_signature, 2);
                break;
            case AST_TOPLEVEL_DEFINE_FUNCTION:
                print_ast_function_signature(&topnodelist->data.funcdef.signature, 2);
                print_ast_body(&topnodelist->data.funcdef.body, 2);
                break;
            case AST_TOPLEVEL_END_OF_FILE:
                break;
        }
        printf("\n");

    } while (topnodelist++->kind != AST_TOPLEVEL_END_OF_FILE);
}

void print_llvm_ir(LLVMModuleRef module)
{
    size_t len;
    const char *filename = LLVMGetSourceFileName(module, &len);
    printf("--- LLVM IR for file \"%.*s\" ---\n", (int)len, filename);

    char *s = LLVMPrintModuleToString(module);
    puts(s);
    LLVMDisposeMessage(s);
}
