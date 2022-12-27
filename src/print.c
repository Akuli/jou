#include <ctype.h>
#include <stdio.h>
#include "jou_compiler.h"
#include "util.h"

static void print_byte(char b)
{
    printf("%#02x", b);
    if (isprint(b))
        printf(" '%c'", b);
}

static void print_string(const char *s)
{
    putchar('"');
    for (int i = 0; s[i]; i++) {
        if (isprint(s[i]))
            putchar(s[i]);
        else if (s[i] == '\n')
            printf("\\n");
        else
            printf("\\x%02x", s[i]);     // TODO: \x is not yet recognized by the tokenizer
    }
    putchar('"');
}

void print_token(const struct Token *token)
{
    switch(token->type) {
        #define f(x) case x: printf(#x); break
        f(TOKEN_INT);
        f(TOKEN_CHAR);
        f(TOKEN_STRING);
        f(TOKEN_OPENPAREN);
        f(TOKEN_CLOSEPAREN);
        f(TOKEN_NAME);
        f(TOKEN_NEWLINE);
        f(TOKEN_END_OF_FILE);
        f(TOKEN_KEYWORD);
        f(TOKEN_COLON);
        f(TOKEN_INDENT);
        f(TOKEN_DEDENT);
        f(TOKEN_ARROW);
        f(TOKEN_STAR);
        f(TOKEN_AMP);
        f(TOKEN_EQUAL_SIGN);
        #undef f
    }

    switch(token->type) {
    case TOKEN_INT:
        printf(" value=%d\n", token->data.int_value);
        break;
    case TOKEN_CHAR:
        printf(" value=");
        print_byte(token->data.char_value);
        printf("\n");
        break;
    case TOKEN_STRING:
        printf(" value=");
        print_string(token->data.string_value);
        printf("\n");
        break;
    case TOKEN_NAME:
    case TOKEN_KEYWORD:
        printf(" name=\"%s\"\n", token->data.name);
        break;
    case TOKEN_NEWLINE:
        printf(" indentation_level=%d\n", token->data.indentation_level);
        break;

    // These tokens don't have any associated data to be printed here.
    //
    // Listed explicitly instead of "default" so that you get a compiler error
    // coming from here after adding a new token type. That should remind you
    // to keep this function up to date.
    case TOKEN_OPENPAREN:
    case TOKEN_CLOSEPAREN:
    case TOKEN_COLON:
    case TOKEN_END_OF_FILE:
    case TOKEN_INDENT:
    case TOKEN_DEDENT:
    case TOKEN_ARROW:
    case TOKEN_STAR:
    case TOKEN_AMP:
    case TOKEN_EQUAL_SIGN:
        printf("\n");
        break;
    }
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
        printf("%s: %s", sig->argnames[i], sig->argtypes[i].name);
    }
    if (sig->returntype)
        printf(") -> %s\n", sig->returntype->name);
    else
        printf(") -> void\n");
}

static void print_ast_call(const struct AstCall *call, int indent);

static void print_ast_expression(const struct AstExpression *expr, int indent)
{
    printf("%*s(line %d) ", indent, "", expr->location.lineno);
    switch(expr->kind) {
        #define f(x) case x: printf(#x); break
        f(AST_EXPR_CALL);
        f(AST_EXPR_GET_VARIABLE);
        f(AST_EXPR_INT_CONSTANT);
        f(AST_EXPR_CHAR_CONSTANT);
        f(AST_EXPR_STRING_CONSTANT);
        f(AST_EXPR_ADDRESS_OF_VARIABLE);
        f(AST_EXPR_DEREFERENCE);
        f(AST_EXPR_FALSE);
        f(AST_EXPR_TRUE);
        #undef f
    }
    printf(" types=[%s --> %s]", expr->type_before_implicit_cast.name, expr->type_after_implicit_cast.name);

    switch(expr->kind) {
    case AST_EXPR_CALL:
        printf("\n");
        print_ast_call(&expr->data.call, indent+2);
        break;
    case AST_EXPR_DEREFERENCE:
        printf("\n");
        print_ast_expression(expr->data.pointerexpr, indent+2);
        break;
    case AST_EXPR_GET_VARIABLE:
    case AST_EXPR_ADDRESS_OF_VARIABLE:
        printf(" varname=\"%s\"\n", expr->data.varname);
        break;
    case AST_EXPR_INT_CONSTANT:
        printf(" value=%d\n", expr->data.int_value);
        break;
    case AST_EXPR_CHAR_CONSTANT:
        printf(" value=");
        print_byte(expr->data.char_value);
        printf("\n");
        break;
    case AST_EXPR_STRING_CONSTANT:
        printf(" value=");
        print_string(expr->data.string_value);
        printf("\n");
        break;
    case AST_EXPR_TRUE:
    case AST_EXPR_FALSE:
        printf("\n");
        break;
    }
}

static void print_ast_call(const struct AstCall *call, int indent)
{
    printf("%*sAstCall: funcname=\"%s\" nargs=%d\n", indent, "", call->funcname, call->nargs);
    for (int i = 0; i < call->nargs; i++) {
        printf("%*s  argument %d:\n", indent, "", i);
        print_ast_expression(&call->args[i], indent+4);
    }
}

static void print_ast_body(const struct AstBody *body, int indent);

static void print_ast_statement(const struct AstStatement *stmt, int indent)
{
    printf("%*s(line %d) ", indent, "", stmt->location.lineno);
    switch(stmt->kind) {
        #define f(x) case x: printf(#x); break
        f(AST_STMT_CALL);
        f(AST_STMT_RETURN_VALUE);
        f(AST_STMT_RETURN_WITHOUT_VALUE);
        f(AST_STMT_IF);
        f(AST_STMT_SETVAR);
        #undef f
    }
    printf("\n");

    switch(stmt->kind) {
        case AST_STMT_CALL:
            print_ast_call(&stmt->data.call, indent+2);
            break;
        case AST_STMT_SETVAR:
            printf("%*s  varname = \"%s\"\n", indent, "", stmt->data.setvar.varname);
            printf("%*s  value:\n", indent, "");
            print_ast_expression(&stmt->data.setvar.value, indent+4);
            break;
        case AST_STMT_RETURN_VALUE:
            printf("%*s  return value:\n", indent, "");
            print_ast_expression(&stmt->data.returnvalue, indent+4);
            break;
        case AST_STMT_RETURN_WITHOUT_VALUE:
            break;
        case AST_STMT_IF:
            printf("%*s  condition:\n", indent, "");
            print_ast_expression(&stmt->data.ifstatement.condition, indent+4);
            print_ast_body(&stmt->data.ifstatement.body, indent+2);
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
                for (struct AstLocalVariable *var = topnodelist->data.funcdef.locals; var && var->name[0]; var++)
                    printf("  type of local variable \"%s\" is %s\n", var->name, var->type.name);
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
