// Boring boilerplate code to free up data structures used in compilation.

#include "jou_compiler.h"
#include <assert.h>
#include <stdlib.h>

void free_tokens(Token *tokenlist)
{
    for (Token *t = tokenlist; t->type != TOKEN_END_OF_FILE; t++)
        if (t->type == TOKEN_STRING)
            free(t->data.string_value);
    free(tokenlist);
}

void free_constant(const Constant *c)
{
    if (c->kind == CONSTANT_STRING)
        free(c->data.str);
}

static void free_expression(const AstExpression *expr);

static void free_ast_type(const AstType *t)
{
    switch(t->kind) {
    case AST_TYPE_ARRAY:
        free_expression(t->data.array.len);
        free(t->data.array.len);
        free_ast_type(t->data.array.membertype);
        free(t->data.array.membertype);
        break;
    case AST_TYPE_POINTER:
        free_ast_type(t->data.valuetype);
        free(t->data.valuetype);
        break;
    case AST_TYPE_NAMED:
        break;
    }
}

static void free_name_type_value(const AstNameTypeValue *ntv)
{
    free_ast_type(&ntv->type);
    if(ntv->value) {
        free_expression(ntv->value);
        free(ntv->value);
    }
}

static void free_ast_signature(const AstSignature *sig)
{
    for (const AstNameTypeValue *ntv = sig->args.ptr; ntv < End(sig->args); ntv++)
        free_name_type_value(ntv);
    free(sig->args.ptr);
    free_ast_type(&sig->returntype);
}

static void free_call(const AstCall *call)
{
    for (int i = 0; i < call->nargs; i++)
        free_expression(&call->args[i]);
    free(call->argnames);
    free(call->args);
}

static void free_expression(const AstExpression *expr)
{
    switch(expr->kind) {
    case AST_EXPR_FUNCTION_CALL:
    case AST_EXPR_BRACE_INIT:
        free_call(&expr->data.call);
        break;
    case AST_EXPR_GET_FIELD:
    case AST_EXPR_DEREF_AND_GET_FIELD:
        free_expression(expr->data.classfield.obj);
        free(expr->data.classfield.obj);
        break;
    case AST_EXPR_CALL_METHOD:
    case AST_EXPR_DEREF_AND_CALL_METHOD:
        free_expression(expr->data.methodcall.obj);
        free(expr->data.methodcall.obj);
        free_call(&expr->data.methodcall.call);
        break;
    case AST_EXPR_ARRAY:
        for (int i = 0; i < expr->data.array.count; i++)
            free_expression(&expr->data.array.items[i]);
        free(expr->data.array.items);
        break;
    case AST_EXPR_INDEXING:
    case AST_EXPR_ADD:
    case AST_EXPR_SUB:
    case AST_EXPR_MUL:
    case AST_EXPR_DIV:
    case AST_EXPR_MOD:
    case AST_EXPR_EQ:
    case AST_EXPR_NE:
    case AST_EXPR_GT:
    case AST_EXPR_GE:
    case AST_EXPR_LT:
    case AST_EXPR_LE:
    case AST_EXPR_AND:
    case AST_EXPR_OR:
        free_expression(&expr->data.operands[0]);
        free_expression(&expr->data.operands[1]);
        free(expr->data.operands);
        break;
    case AST_EXPR_NEG:
    case AST_EXPR_NOT:
    case AST_EXPR_ADDRESS_OF:
    case AST_EXPR_DEREFERENCE:
    case AST_EXPR_PRE_INCREMENT:
    case AST_EXPR_PRE_DECREMENT:
    case AST_EXPR_POST_INCREMENT:
    case AST_EXPR_POST_DECREMENT:
    case AST_EXPR_SIZEOF:
        free_expression(&expr->data.operands[0]);
        free(expr->data.operands);
        break;
    case AST_EXPR_AS:
        free_expression(expr->data.as.obj);
        free(expr->data.as.obj);
        free_ast_type(&expr->data.as.type);
        break;
    case AST_EXPR_CONSTANT:
        free_constant(&expr->data.constant);
        break;
    case AST_EXPR_GET_VARIABLE:
    case AST_EXPR_GET_ENUM_MEMBER:
        break;
    }
}

static void free_ast_body(const AstBody *body);

static void free_statement(const AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_IF:
        for (int i = 0; i < stmt->data.ifstatement.n_if_and_elifs; i++) {
            free_expression(&stmt->data.ifstatement.if_and_elifs[i].condition);
            free_ast_body(&stmt->data.ifstatement.if_and_elifs[i].body);
        }
        free(stmt->data.ifstatement.if_and_elifs);
        free_ast_body(&stmt->data.ifstatement.elsebody);
        break;
    case AST_STMT_WHILE:
        free_expression(&stmt->data.whileloop.condition);
        free_ast_body(&stmt->data.whileloop.body);
        break;
    case AST_STMT_FOR:
        free_statement(stmt->data.forloop.init);
        free_expression(&stmt->data.forloop.cond);
        free_statement(stmt->data.forloop.incr);
        free(stmt->data.forloop.init);
        free(stmt->data.forloop.incr);
        free_ast_body(&stmt->data.forloop.body);
        break;
    case AST_STMT_EXPRESSION_STATEMENT:
    case AST_STMT_RETURN_VALUE:
        free_expression(&stmt->data.expression);
        break;
    case AST_STMT_DECLARE_LOCAL_VAR:
        free_name_type_value(&stmt->data.vardecl);
        break;
    case AST_STMT_ASSIGN:
    case AST_STMT_INPLACE_ADD:
    case AST_STMT_INPLACE_SUB:
    case AST_STMT_INPLACE_MUL:
    case AST_STMT_INPLACE_DIV:
    case AST_STMT_INPLACE_MOD:
        free_expression(&stmt->data.assignment.target);
        free_expression(&stmt->data.assignment.value);
        break;
    case AST_STMT_RETURN_WITHOUT_VALUE:
    case AST_STMT_BREAK:
    case AST_STMT_CONTINUE:
        break;
    }
}

static void free_ast_body(const AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        free_statement(&body->statements[i]);
    free(body->statements);
}

void free_ast(AstToplevelNode *topnodelist)
{
    for (AstToplevelNode *t = topnodelist; t->kind != AST_TOPLEVEL_END_OF_FILE; t++) {
        switch(t->kind) {
        case AST_TOPLEVEL_DECLARE_FUNCTION:
            free_ast_signature(&t->data.funcdef.signature);
            break;
        case AST_TOPLEVEL_DEFINE_FUNCTION:
            free_ast_signature(&t->data.funcdef.signature);
            free_ast_body(&t->data.funcdef.body);
            break;
        case AST_TOPLEVEL_DECLARE_GLOBAL_VARIABLE:
        case AST_TOPLEVEL_DEFINE_GLOBAL_VARIABLE:
            free_name_type_value(&t->data.globalvar);
            break;
        case AST_TOPLEVEL_DEFINE_CLASS:
            for (const AstNameTypeValue *ntv = t->data.classdef.fields.ptr; ntv < End(t->data.classdef.fields); ntv++)
                free_name_type_value(ntv);
            free(t->data.classdef.fields.ptr);
            for (const AstFunctionDef *m = t->data.classdef.methods.ptr; m < End(t->data.classdef.methods); m++) {
                free_ast_signature(&m->signature);
                free_ast_body(&m->body);
            }
            free(t->data.classdef.methods.ptr);
            break;
        case AST_TOPLEVEL_DEFINE_ENUM:
            free(t->data.enumdef.membernames);
            break;
        case AST_TOPLEVEL_IMPORT:
            free(t->data.import.path);
            break;
        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        }
    }
    free(topnodelist);
}


void free_signature(const Signature *sig)
{
    free(sig->argnames);
    free(sig->argtypes);
}

void free_export_symbol(const ExportSymbol *es)
{
    if (es->kind == EXPSYM_FUNCTION)
        free_signature(&es->data.funcsignature);
}

void free_file_types(const FileTypes *ft)
{
    for (GlobalVariable **g = ft->globals.ptr; g < End(ft->globals); g++)
        free(*g);
    for (Type **t = ft->owned_types.ptr; t < End(ft->owned_types); t++)
        free_type(*t);
    for (struct SignatureAndUsedPtr *f = ft->functions.ptr; f < End(ft->functions); f++)
        free_signature(&f->signature);
    for (FunctionOrMethodTypes *f = ft->fomtypes.ptr; f < End(ft->fomtypes); f++) {
        for (ExpressionTypes **et = f->expr_types.ptr; et < End(f->expr_types); et++)
            free(*et);
        free(f->expr_types.ptr);
        free(f->locals.ptr);  // Don't free individual locals because they're owned by CFG now
        free_signature(&f->signature);
    }
    free(ft->globals.ptr);
    free(ft->types.ptr);
    free(ft->owned_types.ptr);
    free(ft->functions.ptr);
    free(ft->fomtypes.ptr);
}


void free_control_flow_graph_block(const CfGraph *cfg, CfBlock *b)
{
    for (const CfInstruction *ins = b->instructions.ptr; ins < End(b->instructions); ins++) {
        if (ins->kind == CF_CONSTANT)
            free_constant(&ins->data.constant);
        if (ins->kind == CF_CALL)
            free_signature(&ins->data.signature);
        free(ins->operands);
    }
    free(b->instructions.ptr);
    if (b != &cfg->start_block && b != &cfg->end_block)
        free(b);
}

static void free_cfg(CfGraph *cfg)
{
    free_signature(&cfg->signature);

    for (CfBlock **b = cfg->all_blocks.ptr; b < End(cfg->all_blocks); b++)
        free_control_flow_graph_block(cfg, *b);
    for (LocalVariable **v = cfg->locals.ptr; v < End(cfg->locals); v++)
        free(*v);

    free(cfg->all_blocks.ptr);
    free(cfg->locals.ptr);
    free(cfg);
}

void free_control_flow_graphs(const CfGraphFile *cfgfile)
{
    for (CfGraph **cfg = cfgfile->graphs.ptr; cfg < End(cfgfile->graphs); cfg++)
        free_cfg(*cfg);
    free(cfgfile->graphs.ptr);
}
