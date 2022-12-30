/*
This file contains boring boilerplate code to free up data structures
used in compilation.
*/

#include "jou_compiler.h"
#include <assert.h>
#include <stdlib.h>

void free_tokens(struct Token *tokenlist)
{
    for (struct Token *t = tokenlist; t->type != TOKEN_END_OF_FILE; t++)
        if (t->type == TOKEN_STRING)
            free(t->data.string_value);
    free(tokenlist);
}

static void free_type(const struct Type *type)
{
    switch(type->kind) {
    case TYPE_POINTER:
        free_type(type->data.valuetype);
        free(type->data.valuetype);
        break;
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
    case TYPE_BOOL:
    case TYPE_UNKNOWN:
        break;
    }
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
    case AST_EXPR_ASSIGN:
    case AST_EXPR_ADD:
    case AST_EXPR_SUB:
    case AST_EXPR_MUL:
    case AST_EXPR_DIV:
    case AST_EXPR_EQ:
    case AST_EXPR_NE:
    case AST_EXPR_GT:
    case AST_EXPR_GE:
    case AST_EXPR_LT:
    case AST_EXPR_LE:
        free_expression(&expr->data.operands[0]);
        free_expression(&expr->data.operands[1]);
        free(expr->data.operands);
        break;
    case AST_EXPR_STRING_CONSTANT:
        free(expr->data.string_value);
        break;
    case AST_EXPR_ADDRESS_OF:
        // fill_types() sets the type to a newly allocated pointer type.
        // Usually AST doesn't own its types, because that would result in lots of unnecessary allocations.
        assert(expr->type_before_implicit_cast.kind == TYPE_UNKNOWN
            || expr->type_before_implicit_cast.kind == TYPE_POINTER);
        if(expr->type_before_implicit_cast.kind == TYPE_POINTER)
            free(expr->type_before_implicit_cast.data.valuetype);
        // fall through
    case AST_EXPR_DEREFERENCE:
        free_expression(&expr->data.operands[0]);
        free(expr->data.operands);
        break;
    case AST_EXPR_INT_CONSTANT:
    case AST_EXPR_CHAR_CONSTANT:
    case AST_EXPR_GET_VARIABLE:
    case AST_EXPR_TRUE:
    case AST_EXPR_FALSE:
        break;
    }
}

static void free_body(const struct AstBody *body);

static void free_statement(const struct AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_IF:
        free_expression(&stmt->data.ifstatement.condition);
        free_body(&stmt->data.ifstatement.body);
        break;
    case AST_STMT_EXPRESSION_STATEMENT:
    case AST_STMT_RETURN_VALUE:
        free_expression(&stmt->data.expression);
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
    for (int i = 0; i < sig->nargs; i++)
        free_type(&sig->argtypes[i]);
    free(sig->argtypes);
    if (sig->returntype){
        free_type(sig->returntype);
        free(sig->returntype);
    }
    free(sig->argnames);
}

static void free_cfg(struct CfGraph *cfg)
{
    for (struct CfBlock **b = cfg->all_blocks.ptr; b < End(cfg->all_blocks); b++) {
        // TODO
        //free((*b)->expressions.ptr);
        if (*b != &cfg->start_block  && *b != &cfg->end_block)
            free(*b);
    }
    free(cfg->all_blocks.ptr);
    free(cfg);
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
            free(t->data.funcdef.locals);
            if (t->data.funcdef.cfg)
                free_cfg(t->data.funcdef.cfg);
            break;
        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        }
    }
    free(topnodelist);
}
