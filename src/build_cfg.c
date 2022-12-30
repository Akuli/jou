#include "jou_compiler.h"


struct State {
    struct CfGraph *cfg;
    struct CfBlock *current_block;  // NULL for unreachable code
};

static struct CfVariable *add_variable(const struct State *st, const struct Type *t, const char *name)
{
    struct CfVariable *var = malloc(sizeof *var);
    var->type = *t;
    if (name[0] == '$') {
        // Anonymous in the user's code, make unique name
        snprintf(var->name, sizeof var->name, "%s_%d", name, st->cfg->variables.len);
    } else {
        assert(strlen(name) < sizeof var->name);
        strcpy(var->name, name);
    }
    Append(&st->cfg->variables, var);
    return var;
}

static struct CfVariable *find_variable(const struct State *st, const char *name)
{
    for (struct CfVariable **var = st->cfg->variables.ptr; var < End(st->cfg->variables); var++)
        if (!strcmp((*var)->name, name))
            return *var;
    assert(0);
}

static struct CfBlock *add_block(const struct State *st)
{
    struct CfBlock *block = calloc(1, sizeof *block);
    Append(&st->cfg->all_blocks, block);
    return block;
}

static struct CfVariable *build_cfg_for_implicit_cast(
    const struct State *st,
    struct CfVariable *obj,
    const struct Type *resulttype)
{
    if (same_type(&obj->type, resulttype))
        return obj;

    struct CfVariable *destvar = add_variable(st, resulttype, "$cast");

    switch(resulttype->kind) {
    case TYPE_SIGNED_INTEGER:
        Append(&st->current_block->instructions, (struct CfInstruction){
            .kind = CF_CAST_TO_BIGGER_SIGNED_INT,
            .data.operands[0] = obj,
            .destvar = destvar,
        });
        break;
    case TYPE_UNSIGNED_INTEGER:
        Append(&st->current_block->instructions, (struct CfInstruction){
            .kind = CF_CAST_TO_BIGGER_UNSIGNED_INT,
            .data.operands[0] = obj,
            .destvar = destvar,
        });
        break;
    default:
        assert(0);
    }

    return destvar;
}

// forward-declare
static struct CfVariable *build_cfg_for_call(const struct State *st, const struct AstCall *call, const struct Type *t);
static struct CfVariable *build_cfg_for_address_of_expression(const struct State *st, const struct AstExpression *address_of_what);

static struct CfVariable *build_cfg_for_expression(const struct State *st, const struct AstExpression *expr)
{
    struct CfVariable *result, *temp;

    switch(expr->kind) {
    case AST_EXPR_CALL:
        result = build_cfg_for_call(st, &expr->data.call, expr->type_before_implicit_cast.kind == TYPE_UNKNOWN ? NULL : &expr->type_before_implicit_cast);
        break;
    case AST_EXPR_ADDRESS_OF:
        result = build_cfg_for_address_of_expression(st, &expr->data.operands[0]);
        break;
    case AST_EXPR_GET_VARIABLE:
        result = find_variable(st, expr->data.varname);
        break;
    case AST_EXPR_DEREFERENCE:
        result = add_variable(st, &expr->type_before_implicit_cast, "$deref");
        // Assigning to temp variable first is needed because evaluating the
        // argument of Append() must not grow the list we're appending into.
        temp = build_cfg_for_expression(st, &expr->data.operands[0]);
        Append(&st->current_block->instructions, (struct CfInstruction) {
            .kind = CF_LOAD_FROM_POINTER,
            .data.operands[0] = temp,
            .destvar = result,
        });
        break;
    case AST_EXPR_INT_CONSTANT:
        result = add_variable(st, &intType, "$intconstant");
        Append(&st->current_block->instructions, (struct CfInstruction) {
            .kind = CF_INT_CONSTANT,
            .data.int_value = expr->data.int_value,
            .destvar = result,
        });
        break;
    case AST_EXPR_CHAR_CONSTANT:
        result = add_variable(st, &byteType, "$byteconstant");
        Append(&st->current_block->instructions, (struct CfInstruction) {
            .kind = CF_CHAR_CONSTANT,
            .data.char_value = expr->data.char_value,
            .destvar = result,
        });
        break;
    case AST_EXPR_STRING_CONSTANT:
        result = add_variable(st, &stringType, "$strconstant");
        Append(&st->current_block->instructions, (struct CfInstruction){
            .kind = CF_STRING_CONSTANT,
            .data.string_value = strdup(expr->data.string_value),
            .destvar = result,
        });
        break;
    case AST_EXPR_TRUE:
        result = add_variable(st, &boolType, "$true");
        Append(&st->current_block->instructions, (struct CfInstruction){
            .kind = CF_TRUE,
            .destvar = result,
        });
        break;
    case AST_EXPR_FALSE:
        result = add_variable(st, &boolType, "$false");
        Append(&st->current_block->instructions, (struct CfInstruction){
            .kind = CF_FALSE,
            .destvar = result,
        });
        break;
    case AST_EXPR_ASSIGN:
        {
            // TODO: this evaluation order good? sometimes confuses python programmers, seen it in 2022 /r/adventofcode
            struct CfVariable *lhsptr = build_cfg_for_address_of_expression(st, &expr->data.operands[0]);
            struct CfVariable *rhs = build_cfg_for_expression(st, &expr->data.operands[1]);
            Append(&st->current_block->instructions, (struct CfInstruction){
                .kind = CF_STORE_TO_POINTER,
                .data.operands = {lhsptr,rhs },
                .destvar = NULL,
            });
            result = rhs;
            break;
        }
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
            // Refactoring note: Make sure to evaluate lhs first. C doesn't guarantee evaluation
            // order of function arguments.
            struct CfVariable *lhs = build_cfg_for_expression(st, &expr->data.operands[0]);
            struct CfVariable *rhs = build_cfg_for_expression(st, &expr->data.operands[1]);
            assert(same_type(
                &expr->data.operands[0].type_after_implicit_cast,
                &expr->data.operands[1].type_after_implicit_cast));
            bool is_signed = expr->data.operands[0].type_after_implicit_cast.kind == TYPE_SIGNED_INTEGER;

            struct CfInstruction ins;
            const char *debugname;
            bool negate = false;
            bool swap = false;

            switch(expr->kind) {
                case AST_EXPR_ADD: debugname = "$add"; ins.kind = CF_INT_ADD; break;
                case AST_EXPR_SUB: debugname = "$sub"; ins.kind = CF_INT_SUB; break;
                case AST_EXPR_MUL: debugname = "$mul"; ins.kind = CF_INT_MUL; break;
                case AST_EXPR_DIV: debugname = (is_signed ? "$sdiv" : "$udiv"); ins.kind = (is_signed ? CF_INT_SDIV : CF_INT_UDIV); break;
                case AST_EXPR_EQ: debugname = "$eq"; ins.kind = CF_INT_EQ; break;
                case AST_EXPR_NE: debugname = "$ne"; ins.kind = CF_INT_EQ; negate=true; break;
                case AST_EXPR_LT: debugname = "$lt"; ins.kind = CF_INT_LT; break;
                case AST_EXPR_GT: debugname = "$gt"; ins.kind = CF_INT_LT; swap=true; break;
                case AST_EXPR_LE: debugname = "$le"; ins.kind = CF_INT_LT; negate=true; swap=true; break;
                case AST_EXPR_GE: debugname = "$ge"; ins.kind = CF_INT_LT; negate=true; break;
                default: assert(0);
            }

            ins.data.operands[0] = swap?rhs:lhs;
            ins.data.operands[1] = swap?lhs:rhs;

            ins.destvar = result = add_variable(st, &expr->type_before_implicit_cast, debugname);
            Append(&st->current_block->instructions, ins);

            if (negate) {
                struct CfVariable *result2 = add_variable(st, &boolType, debugname);
                Append(&st->current_block->instructions, (struct CfInstruction){
                    .kind = CF_BOOL_NEGATE,
                    .data.operands = {result},
                    .destvar = result2,
                });
                result = result2;
            }
        }
    }

    if (result == NULL) {
        // call to function with '-> void'
        assert(expr->kind == AST_EXPR_CALL);
        return NULL;
    }

    assert(same_type(&result->type, &expr->type_before_implicit_cast));
    return build_cfg_for_implicit_cast(st, result, &expr->type_after_implicit_cast);
}

static struct CfVariable *build_cfg_for_address_of_expression(const struct State *st, const struct AstExpression *address_of_what)
{
    switch(address_of_what->kind) {
    case AST_EXPR_GET_VARIABLE:
        {
            struct CfVariable *var = find_variable(st, address_of_what->data.varname);
            // TODO: shouldn't need to create a new type here
            struct Type t = create_pointer_type(&var->type, (struct Location){0});
            struct CfVariable *addr = add_variable(st, &t, "$address_of_var");
            Append(&st->current_block->instructions, (struct CfInstruction){
                .kind = CF_ADDRESS_OF_VARIABLE,
                .data.operands[0] = var,
                .destvar = addr,
            });
            return addr;
        }
    case AST_EXPR_DEREFERENCE:
        // &*foo --> just evaluate foo
        return build_cfg_for_expression(st, &address_of_what->data.operands[0]);
    default:
        assert(0);
    }
}

// returntype can be NULL, this function returns NULL in that case
static struct CfVariable *build_cfg_for_call(const struct State *st, const struct AstCall *call, const struct Type *returntype)
{
    struct CfVariable **args = malloc(call->nargs * sizeof(args[0]));  // NOLINT
    for (int i = 0; i < call->nargs; i++)
        args[i] = build_cfg_for_expression(st, &call->args[i]);

    struct CfVariable *return_value;
    if (returntype) {
        char debugname[100];
        snprintf(debugname, sizeof debugname, "$%s_ret", call->funcname);
        return_value = add_variable(st, returntype, debugname);
    }else
        return_value = NULL;

    struct CfInstruction ins = {
        .kind = CF_CALL,
        .data.call.args = args,
        .data.call.nargs = call->nargs,
        .destvar = return_value,
    };
    safe_strcpy(ins.data.call.funcname, call->funcname);
    Append(&st->current_block->instructions, ins);

    return return_value;
}

static void build_cfg_for_body(struct State *st, const struct AstBody *body);

static void build_cfg_for_statement(struct State *st, const struct AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_IF:
        {
        struct CfVariable *cond = build_cfg_for_expression(st, &stmt->data.ifstatement.condition);
        struct CfBlock *thenblock = add_block(st);
        struct CfBlock *afterblock = add_block(st);
        st->current_block->branchvar = cond;
        st->current_block->iftrue = thenblock;
        st->current_block->iffalse = afterblock;
        st->current_block = thenblock;
        build_cfg_for_body(st, &stmt->data.ifstatement.body);
        st->current_block->iftrue = afterblock;
        st->current_block->iffalse = afterblock;
        st->current_block = afterblock;
        break;
        }

    case AST_STMT_RETURN_VALUE:
        {
            struct CfVariable *ret = build_cfg_for_expression(st, &stmt->data.expression);
            Append(&st->current_block->instructions, (struct CfInstruction){
                .kind=CF_VARCPY,
                .data.operands[0] = ret,
                .destvar = find_variable(st, "return"),
            });
        }
        // fall through
    case AST_STMT_RETURN_WITHOUT_VALUE:
        st->current_block->iftrue = &st->cfg->end_block;
        st->current_block->iffalse = &st->cfg->end_block;
        st->current_block = add_block(st);  // an unreachable block
        break;

    case AST_STMT_EXPRESSION_STATEMENT:
        build_cfg_for_expression(st, &stmt->data.expression);
        break;
    }
}

static void build_cfg_for_body(struct State *st, const struct AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        build_cfg_for_statement(st, &body->statements[i]);
}

static struct CfGraph *build_cfg_for_function(const struct Signature *sig, const struct AstLocalVariable *locals, const struct AstBody *body)
{
    struct CfGraph *cfg = calloc(1, sizeof *cfg);
    Append(&cfg->all_blocks, &cfg->start_block);
    Append(&cfg->all_blocks, &cfg->end_block);

    struct State st = { .cfg = cfg };
    st.current_block = &st.cfg->start_block;

    for (int i = 0; locals[i].name[0]; i++)
        add_variable(&st, &locals[i].type, locals[i].name);
    if (sig->returntype) 
        add_variable(&st, sig->returntype, "return");

    build_cfg_for_body(&st, body);

    // Implicit return at the end of the function
    st.current_block->iftrue = &cfg->end_block;
    st.current_block->iffalse = &cfg->end_block;

    return cfg;
}

struct CfGraphFile build_control_flow_graphs(struct AstToplevelNode *ast)
{
    struct CfGraphFile result = { .filename = ast->location.filename };
    while (ast[result.nfuncs].kind != AST_TOPLEVEL_END_OF_FILE)
        result.nfuncs++;
    result.graphs = malloc(sizeof(result.graphs[0]) * result.nfuncs);  // NOLINT
    result.signatures = malloc(sizeof(result.signatures[0]) * result.nfuncs);

    for (int i = 0; i < result.nfuncs; i++) {
        switch(ast[i].kind) {
        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        case AST_TOPLEVEL_CDECL_FUNCTION:
            result.signatures[i] = copy_signature(&ast[i].data.decl_signature);
            result.graphs[i] = NULL;
            break;
        case AST_TOPLEVEL_DEFINE_FUNCTION:
            result.signatures[i] = copy_signature(&ast[i].data.funcdef.signature);
            result.graphs[i] = build_cfg_for_function(&result.signatures[i], ast[i].data.funcdef.locals, &ast[i].data.funcdef.body);
            break;
        }
    }

    return result;
}
