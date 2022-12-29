#include "jou_compiler.h"


struct State {
    struct CfGraph *cfg;
    struct CfVariable *nextvar;
    struct CfVariable *return_value;
    struct CfBlock *current_block;  // NULL for unreachable code
};

struct CfVariable *find_or_add_variable_by_name(struct State *st, const char *name, const struct Type *type)
{
    struct CfVariable *v;
    for (v = st->cfg->vars; v < st->nextvar; v++) {
        if (!strcmp(v->name, name)) {
            assert(same_type(&v->type, type));
            return v;
        }
    }

    v = st->nextvar++;
    assert(strlen(name) < sizeof v->name);
    strcpy(v->name, name);
    v->type = copy_type(type);
    return v;
}

struct CfVariable *build_cfg_for_expression(struct State *st, const struct AstExpression *expr)
{
    struct CfVariable *result;

    switch(expr->kind) {
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
        {
            // careful with C's evaluation order........
            struct CfVariable *lhs = build_cfg_for_expression(st, &expr->data.operands[0]);
            struct CfVariable *rhs = build_cfg_for_expression(st, &expr->data.operands[1]);
            assert(same_type(
                &expr->data.operands[0].type_after_implicit_cast,
                &expr->data.operands[1].type_after_implicit_cast));
            bool is_signed = expr->data.operands[0].type_after_implicit_cast.kind == TYPE_SIGNED_INTEGER;

            result = st->
        }
    }
    }
}

void build_cfg_for_body(struct State *st, const struct AstBody *body);

void build_cfg_for_statement(struct State *st, const struct AstStatement *stmt)
{
    if (!st->current_block)
        fail_with_error(stmt->location, "statement is unreachable, it can never run");

    struct CfVariable *v, *lhs, *rhs;

    switch(stmt->kind) {
    case AST_STMT_CALL:
        assert(0);

    case AST_STMT_IF:
        st->current_block->branchvar = build_cfg_for_expression(&stmt->data.ifstatement.condition);
        struct CfBlock *thenblock = calloc(1, sizeof(*thenblock));
        struct CfBlock *afterblock = calloc(1, sizeof(*afterblock));
        st->current_block->iftrue = thenblock;
        st->current_block->iffalse = afterblock;
        st->current_block = thenblock;
        build_cfg_for_body(st, &stmt->data.ifstatement.body);
        st->current_block->iftrue = afterblock;
        st->current_block->iffalse = afterblock;
        st->current_block = afterblock;
        break;

    case AST_STMT_RETURN_VALUE:
        v = build_cfg_for_expression(st, &stmt->data.returnvalue);
        Append(&st->current_block->instructions, (struct CfInstruction){
            .kind = CF_VARCPY,
            .data.operands[0] = v,
            .destvar = st->return_value,
        });
        // fall through

    case AST_STMT_RETURN_WITHOUT_VALUE:
        st->current_block->iftrue = &st->cfg->end_block;
        st->current_block->iffalse = &st->cfg->end_block;
        st->current_block = NULL;
        break;

    case AST_STMT_SETVAR:
        rhs = build_cfg_for_expression(st, &stmt->data.setvar.value);
        lhs = find_or_add_variable_by_name(st, stmt->data.setvar.varname, &rhs->type);
        Append(&st->current_block->instructions, (struct CfInstruction){
            .kind = CF_VARCPY,
            .data.operands[0] = rhs,
            .destvar = lhs,
        });
        break;
    }
}

void build_cfg_for_body(struct State *st, const struct AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        build_cfg_for_statement(st, &body->statements[i]);
}

void build_cfg_for_function(struct CfGraph *cfg, const struct AstFunctionSignature *sig, const struct AstBody *body)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->vars = calloc(sizeof(cfg->vars[0]), 4096);  // TODO: calculate size properly!!!

    struct State st = { .cfg = cfg, .nextvar = &cfg->vars[0] };

    // Add arguments
    for (int i = 0; i < sig->nargs; i++) {
        struct CfVariable *v = st.nextvar++;
        safe_strcpy(v->name, sig->argnames[i]);
        v->type = copy_type(&sig->argtypes[i]);
    }

    // Add return value, if any
    if (sig->returntype) {
        st.return_value = st.nextvar++;
        strcpy(st.return_value->name, "return");
        st.return_value->type = copy_type(sig->returntype);
    }

    st.current_block = &st.cfg->start_block;
    build_cfg_for_body(&st, body);

    // Implicit return at the end of the function
    st.current_block->iftrue = &cfg->end_block;
    st.current_block->iffalse = &cfg->end_block;
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
    while (ast->kind != AST_TOPLEVEL_END_OF_FILE) {
        if (ast->kind == AST_TOPLEVEL_DEFINE_FUNCTION) {
            ast->data.
        }
    }
}
