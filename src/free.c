/*
This file contains boring boilerplate code to free up data structures
used in compilation.
*/

#include "jou_compiler.h"
#include <assert.h>
#include <stdlib.h>

void free_tokens(struct Token *tokenlist)
{
    // Currently individual tokens don't need freeing.
    // TODO: This will change once we have strings, because a string token
    //       will contain an arbitrary amount of data.
    free(tokenlist);
}

static void free_expression(const struct AstExpression *expr);

static void free_call(const struct AstCall *call)
{
    for (int i = 0; i < call->nargs; i++)
        free_expression(&call->args[i]);
    free(call->args);
}

static void free_expression(const struct AstExpression *expr)
{
    switch(expr->kind) {
    case AST_EXPR_CALL:
        free_call(&expr->data.call);
        break;
    case AST_EXPR_INT_CONSTANT:
    case AST_EXPR_GETVAR:
        break;
    }
}

static void free_statement(const struct AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_CALL:
        free_call(&stmt->data.call);
        break;
    case AST_STMT_RETURN_VALUE:
        free_expression(&stmt->data.returnvalue);
        break;
    case AST_STMT_RETURN_WITHOUT_VALUE:
        break;
    }
}

static void free_body(const struct AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        free_statement(&body->statements[i]);
    free(body->statements);
}

static void free_signature(const struct AstFunctionSignature *sig)
{
    free(sig->argnames);
}

void free_ast(struct AstToplevelNode *topnodelist)
{
    for (struct AstToplevelNode *t = topnodelist; t->kind != AST_TOPLEVEL_END_OF_FILE; t++) {
        switch(t->kind) {
        case AST_TOPLEVEL_CDECL_FUNCTION:
            free_signature(&t->data.decl_signature);
            break;
        case AST_TOPLEVEL_DEFINE_FUNCTION:
            free_signature(&t->data.funcdef.signature);
            free_body(&t->data.funcdef.body);
            break;
        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        }
    }
    free(topnodelist);
}
