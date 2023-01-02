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

void free_type(const struct Type *type)
{
    switch(type->kind) {
    case TYPE_POINTER:
        free_type(type->data.valuetype);
        free(type->data.valuetype);
        break;
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
    case TYPE_BOOL:
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
        for (int i = 0; i < stmt->data.ifstatement.n_if_and_elifs; i++) {
            free_expression(&stmt->data.ifstatement.if_and_elifs[i].condition);
            free_body(&stmt->data.ifstatement.if_and_elifs[i].body);
        }
        free(stmt->data.ifstatement.if_and_elifs);
        free_body(&stmt->data.ifstatement.elsebody);
        break;
    case AST_STMT_WHILE:
        free_expression(&stmt->data.whileloop.condition);
        free_body(&stmt->data.whileloop.body);
        break;
    case AST_STMT_EXPRESSION_STATEMENT:
    case AST_STMT_RETURN_VALUE:
        free_expression(&stmt->data.expression);
        break;
    case AST_STMT_RETURN_WITHOUT_VALUE:
    case AST_STMT_BREAK:
    case AST_STMT_CONTINUE:
        break;
    }
}

static void free_body(const struct AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        free_statement(&body->statements[i]);
    free(body->statements);
}

static void free_signature(const struct Signature *sig)
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


void free_control_flow_graph_block(const struct CfGraph *cfg, struct CfBlock *b)
{
    for (const struct CfInstruction *ins = b->instructions.ptr; ins < End(b->instructions); ins++) {
        if (ins->kind == CF_STRING_CONSTANT)
            free(ins->data.string_value);
        free(ins->operands);
    }
    free(b->instructions.ptr);
    if (b != &cfg->start_block && b != &cfg->end_block)
        free(b);
}

static void free_cfg(struct CfGraph *cfg)
{
    for (struct CfBlock **b = cfg->all_blocks.ptr; b < End(cfg->all_blocks); b++)
        free_control_flow_graph_block(cfg, *b);

    for (struct CfVariable **v = cfg->variables.ptr; v < End(cfg->variables); v++) {
        free_type(&(*v)->type);
        free(*v);
    }

    free(cfg->all_blocks.ptr);
    free(cfg->variables.ptr);
    free(cfg);
}

void free_control_flow_graphs(const struct CfGraphFile *cfgfile)
{
    for (int i = 0; i < cfgfile->nfuncs; i++) {
        free_signature(&cfgfile->signatures[i]);
        if (cfgfile->graphs[i])
            free_cfg(cfgfile->graphs[i]);
    }

    free(cfgfile->signatures);
    free(cfgfile->graphs);
}
