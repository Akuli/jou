#include "jou_compiler.h"


struct State {
    struct CfGraph *cfg;
    struct CfBlock *current_block;  // NULL for unreachable code
};

void build_cfg_for_statement(struct State *st, const struct AstStatement *stmt);

void build_cfg_for_body(struct State *st, const struct AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        build_cfg_for_statement(st, &body->statements[i]);
}

void build_cfg_for_statement(struct State *st, const struct AstStatement *stmt)
{
    if (!st->current_block)
        fail_with_error(stmt->location, "statement is unreachable, it can never run");

    switch(stmt->kind) {
    case AST_STMT_IF:
        Append(&st->current_block->expressions, stmt->data.ifstatement.condition);
        struct CfBlock *thenblock = calloc(1, sizeof(*thenblock));
        struct CfBlock *afterblock = calloc(1, sizeof(*afterblock));
        st->current_block->iftrue = thenblock;
        st->current_block->iffalse = afterblock;
        st->current_block = thenblock;
        build_cfg_for_body(st, &stmt->data.ifstatement.body);
        if (st->current_block) {
            st->current_block->iftrue = afterblock;
            st->current_block->iffalse = afterblock;
        }
        st->current_block = afterblock;
        break;

    case AST_STMT_RETURN_VALUE:
        // TODO: figure out and comment how this will work
        Append(&st->current_block->expressions, stmt->data.expression);
        // fall through
    case AST_STMT_RETURN_WITHOUT_VALUE:
        st->current_block->iftrue = &st->cfg->end_block;
        st->current_block->iffalse = &st->cfg->end_block;
        st->current_block = NULL;
        break;

    case AST_STMT_EXPRESSION_STATEMENT:
        Append(&st->current_block->expressions, stmt->data.expression);
        break;
    }
}

void build_cfg_for_function(struct CfGraph *cfg, const struct AstFunctionSignature *sig, const struct AstBody *body)
{
    memset(cfg, 0, sizeof *cfg);
    struct State st = { .cfg = cfg };
    st.current_block = &st.cfg->start_block;
    build_cfg_for_body(&st, body);

    // Implicit return at the end of the function
    if(st.current_block) {
        st.current_block->iftrue = &cfg->end_block;
        st.current_block->iffalse = &cfg->end_block;
    }
}

void build_control_flow_graphs(struct AstToplevelNode *ast)
{
    while(1) {
        switch(ast->kind) {
        case AST_TOPLEVEL_END_OF_FILE:
            return;
        case AST_TOPLEVEL_CDECL_FUNCTION:
            break;
        case AST_TOPLEVEL_DEFINE_FUNCTION:
            assert(!ast->data.funcdef.cfg);
            ast->data.funcdef.cfg = malloc(sizeof *ast->data.funcdef.cfg);
            build_cfg_for_function(ast->data.funcdef.cfg, &ast->data.funcdef.signature, &ast->data.funcdef.body);
            break;
        }
        ast++;
    }
}
