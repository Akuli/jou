#include "jou_compiler.h"


struct State {
    struct CfGraphFile *cfgfile;
    struct CfGraph *cfg;
    const struct Signature *signature;
    struct CfBlock *current_block;
};

static const struct CfVariable *add_variable(const struct State *st, const struct Type *t, const char *name)
{
    struct CfVariable *var = malloc(sizeof *var);
    var->type = copy_type(t);
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

// If error_location is NULL, this will return NULL when variable is not found.
static const struct CfVariable *find_variable(const struct State *st, const char *name, const struct Location *error_location)
{
    for (struct CfVariable **var = st->cfg->variables.ptr; var < End(st->cfg->variables); var++)
        if (!strcmp((*var)->name, name))
            return *var;

    if (!error_location)
        return NULL;
    fail_with_error(*error_location, "no local variable named '%s'", name);
}

static const struct Signature *find_function(const struct State *st, const char *name)
{
    for (int i = 0; i < st->cfgfile->nfuncs; i++)
        if (!strcmp(st->cfgfile->signatures[i].funcname, name))
            return &st->cfgfile->signatures[i];
    return NULL;
}

static struct CfBlock *add_block(const struct State *st)
{
    struct CfBlock *block = calloc(1, sizeof *block);
    Append(&st->cfg->all_blocks, block);
    return block;
}

/*
Implicit casts are used in many places, e.g. function arguments.

When you pass an argument of the wrong type, it's best to give an error message
that says so, instead of some generic "expected type foo, got object of type bar"
kind of message.

The template can contain "FROM" and "TO". They will be substituted with names
of types. We cannot use printf() style functions because the arguments can be in
any order.
*/
noreturn void fail_with_implicit_cast_error(struct Location location, const char *template, const struct Type *from, const struct Type *to)
{
    List(char) msg = {0};
    while(*template){
        if (!strncmp(template, "FROM", 4)) {
            AppendStr(&msg, from->name);
            template += 4;
        } else if (!strncmp(template, "TO", 2)) {
            AppendStr(&msg, to->name);
            template += 2;
        } else {
            Append(&msg, template[0]);
            template++;
        }
    }
    fail_with_error(location, "%.*s", msg.len, msg.ptr);
}

static const struct CfVariable *build_implicit_cast(
    const struct State *st,
    const struct CfVariable *obj,
    const struct Type *to,
    struct Location location,
    const char *err_template)
{
    const struct Type *from = &obj->type;

    if (same_type(from, to))
        return obj;

    // Casting to bigger signed int applies when "to" is signed and bigger.
    // Doesn't cast from unsigned to same size signed: with 8 bits, 255 does not implicitly cast to -1.
    // TODO: does this surely work e.g. how would 8-bit 11111111 cast to 32 bit?
    if (is_integer_type(from)
        && to->kind == TYPE_SIGNED_INTEGER
        && from->data.width_in_bits < to->data.width_in_bits)
    {
        const struct CfVariable *result = add_variable(st, to, "$implicit_cast");
        Append(&st->current_block->instructions, (struct CfInstruction){
            .location = location,
            .kind = CF_CAST_TO_BIGGER_SIGNED_INT,
            .data.operands[0] = obj,
            .destvar = result,
        });
        return result;
    }

    // Casting to bigger unsigned int: original value has to be unsigned as well.
    if (from->kind == TYPE_UNSIGNED_INTEGER
        && to->kind == TYPE_UNSIGNED_INTEGER
        && from->data.width_in_bits < to->data.width_in_bits)
    {
        const struct CfVariable *result = add_variable(st, to, "$implicit_cast");
        Append(&st->current_block->instructions, (struct CfInstruction){
            .location = location,
            .kind = CF_CAST_TO_BIGGER_UNSIGNED_INT,
            .data.operands[0] = obj,
            .destvar = result,
        });
        return result;
    }

    fail_with_implicit_cast_error(location, err_template, from, to);
}

static const struct CfVariable *build_binop(
    const struct State *st,
    enum AstExpressionKind op,
    struct Location location,
    const struct CfVariable *lhs,
    const struct CfVariable *rhs)
{
    const char *do_what;
    switch(op) {
    case AST_EXPR_ADD: do_what = "add"; break;
    case AST_EXPR_SUB: do_what = "subtract"; break;
    case AST_EXPR_MUL: do_what = "multiply"; break;
    case AST_EXPR_DIV: do_what = "divide"; break;

    case AST_EXPR_EQ:
    case AST_EXPR_NE:
    case AST_EXPR_GT:
    case AST_EXPR_GE:
    case AST_EXPR_LT:
    case AST_EXPR_LE:
        do_what = "compare";
        break;

    default:
        assert(0);
    }

    if (!is_integer_type(&lhs->type) || !is_integer_type(&rhs->type))
        fail_with_error(location, "wrong types: cannot %s %s and %s", do_what, lhs->type.name, rhs->type.name);

    // TODO: is this a good idea?
    struct Type cast_type = create_integer_type(
        max(lhs->type.data.width_in_bits, rhs->type.data.width_in_bits),
        lhs->type.kind == TYPE_SIGNED_INTEGER || lhs->type.kind == TYPE_SIGNED_INTEGER
    );
    // It shouldn't be possible to make these fail.
    // TODO: i think it is, with adding same size signed and unsigned for example
    lhs = build_implicit_cast(st, lhs, &cast_type, location, NULL);
    rhs = build_implicit_cast(st, rhs, &cast_type, location, NULL);

    struct Type result_type;
    switch(op) {
    case AST_EXPR_ADD:
    case AST_EXPR_SUB:
    case AST_EXPR_MUL:
    case AST_EXPR_DIV:
        result_type = cast_type;
        break;

    case AST_EXPR_EQ:
    case AST_EXPR_NE:
    case AST_EXPR_GT:
    case AST_EXPR_GE:
    case AST_EXPR_LT:
    case AST_EXPR_LE:
        result_type = boolType;
        break;

    default:
        assert(0);
    }

    struct CfInstruction ins = { .location = location } ;
    const char *debugname;
    bool is_signed = cast_type.kind == TYPE_SIGNED_INTEGER;
    bool negate = false;
    bool swap = false;

    switch(op) {
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

    ins.destvar = add_variable(st, &result_type, debugname);
    Append(&st->current_block->instructions, ins);

    if (!negate)
        return ins.destvar;

    const struct CfVariable *negated = add_variable(st, &boolType, debugname);
    Append(&st->current_block->instructions, (struct CfInstruction){
        .location = location,
        .kind = CF_BOOL_NEGATE,
        .data.operands = {ins.destvar},
        .destvar = negated,
    });
    return negated;
}

static const struct CfVariable *build_call(const struct State *st, const struct AstCall *call, struct Location location);
static const struct CfVariable *build_address_of_expression(const struct State *st, const struct AstExpression *address_of_what);

static const struct CfVariable *build_expression(
    const struct State *st,
    const struct AstExpression *expr,
    const struct Type *implicit_cast_to,  // can be NULL, there will be no implicit casting
    const char *casterrormsg,
    bool needvalue)  // Usually true. False means that calls to "-> void" functions are acceptable.
{
    const struct CfVariable *result, *temp;

    switch(expr->kind) {
    case AST_EXPR_CALL:
        result = build_call(st, &expr->data.call, expr->location);
        if (result == NULL) {
            if (!needvalue)
                return NULL;
            // TODO: add a test for this
            fail_with_error(expr->location, "function '%s' does not return a value", expr->data.call.funcname);
        }
        break;
    case AST_EXPR_ADDRESS_OF:
        result = build_address_of_expression(st, &expr->data.operands[0]);
        break;
    case AST_EXPR_GET_VARIABLE:
        result = find_variable(st, expr->data.varname, &expr->location);
        break;
    case AST_EXPR_DEREFERENCE:
        temp = build_expression(st, &expr->data.operands[0], NULL, NULL, true);
        if (temp->type.kind != TYPE_POINTER)
            fail_with_error(expr->location, "the dereference operator '*' is only for pointers, not for %s", temp->type.name);
        result = add_variable(st, temp->type.data.valuetype, "$deref");
        Append(&st->current_block->instructions, (struct CfInstruction) {
            .location = expr->location,
            .kind = CF_LOAD_FROM_POINTER,
            .data.operands[0] = temp,
            .destvar = result,
        });
        break;
    case AST_EXPR_INT_CONSTANT:
        result = add_variable(st, &intType, "$intconstant");
        Append(&st->current_block->instructions, (struct CfInstruction) {
            .location = expr->location,
            .kind = CF_INT_CONSTANT,
            .data.int_value = expr->data.int_value,
            .destvar = result,
        });
        break;
    case AST_EXPR_CHAR_CONSTANT:
        result = add_variable(st, &byteType, "$byteconstant");
        Append(&st->current_block->instructions, (struct CfInstruction) {
            .location = expr->location,
            .kind = CF_CHAR_CONSTANT,
            .data.char_value = expr->data.char_value,
            .destvar = result,
        });
        break;
    case AST_EXPR_STRING_CONSTANT:
        result = add_variable(st, &stringType, "$strconstant");
        Append(&st->current_block->instructions, (struct CfInstruction){
            .location = expr->location,
            .kind = CF_STRING_CONSTANT,
            .data.string_value = strdup(expr->data.string_value),
            .destvar = result,
        });
        break;
    case AST_EXPR_TRUE:
        result = add_variable(st, &boolType, "$true");
        Append(&st->current_block->instructions, (struct CfInstruction){
            .location = expr->location,
            .kind = CF_TRUE,
            .destvar = result,
        });
        break;
    case AST_EXPR_FALSE:
        result = add_variable(st, &boolType, "$false");
        Append(&st->current_block->instructions, (struct CfInstruction){
            .location = expr->location,
            .kind = CF_FALSE,
            .destvar = result,
        });
        break;
    case AST_EXPR_ASSIGN:
        {
            struct AstExpression *targetexpr = &expr->data.operands[0];
            struct AstExpression *valueexpr = &expr->data.operands[1];
            if (targetexpr->kind == AST_EXPR_GET_VARIABLE && !find_variable(st, targetexpr->data.varname, NULL))
            {
                // Making a new variable. Use the type of the value being assigned.
                result = build_expression(st, valueexpr, NULL, NULL, true);
                const struct CfVariable *var = add_variable(st, &result->type, targetexpr->data.varname);
                Append(&st->current_block->instructions, (struct CfInstruction){
                    .location = expr->location,
                    .kind = CF_VARCPY,
                    .data.operands[0] = result,
                    .destvar = var,
                });
            } else {
                // Convert value to the type of an existing variable or other assignment target.
                // TODO: is this evaluation order good?
                const struct CfVariable *target = build_address_of_expression(st, targetexpr);
                assert(target->type.kind == TYPE_POINTER);
                const char *errmsg;
                switch(targetexpr->kind) {
                    case AST_EXPR_GET_VARIABLE: errmsg = "cannot assign a value of type FROM to variable of type TO"; break;
                    case AST_EXPR_DEREFERENCE: errmsg = "cannot assign a value of type FROM into a pointer of type TO*"; break;
                    default: assert(0);
                }
                /*
                result cannot be casted to type of the variable. It would break this:

                   some_byte_variable = some_int_variable = 'x'

                because (some_int_variable = 'x') would cast to type int, and then the int
                would be assigned to some_byte_variable.
                */
                result = build_expression(st, valueexpr, NULL, NULL, true);
                const struct CfVariable *casted_result = build_implicit_cast(
                    st, result, target->type.data.valuetype, expr->location, errmsg);
                Append(&st->current_block->instructions, (struct CfInstruction){
                    .location = expr->location,
                    .kind = CF_STORE_TO_POINTER,
                    .data.operands = {target, casted_result},
                    .destvar = NULL,
                });
            }
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
            const struct CfVariable *lhs = build_expression(st, &expr->data.operands[0], NULL, NULL, true);
            const struct CfVariable *rhs = build_expression(st, &expr->data.operands[1], NULL, NULL, true);
            result = build_binop(st, expr->kind, expr->location, lhs, rhs);
        }
    }

    if (implicit_cast_to == NULL) {
        assert(!casterrormsg);
        return result;
    }
    assert(casterrormsg);
    return build_implicit_cast(st, result, implicit_cast_to, expr->location, casterrormsg);
}

static const struct CfVariable *build_address_of_expression(const struct State *st, const struct AstExpression *address_of_what)
{
    switch(address_of_what->kind) {
    case AST_EXPR_GET_VARIABLE:
        {
            const struct CfVariable *var = find_variable(st, address_of_what->data.varname, &address_of_what->location);
            struct Type t = create_pointer_type(&var->type, (struct Location){0});
            const struct CfVariable *addr = add_variable(st, &t, "$address_of_var");
            free(t.data.valuetype);
            Append(&st->current_block->instructions, (struct CfInstruction){
                .location = address_of_what->location,
                .kind = CF_ADDRESS_OF_VARIABLE,
                .data.operands[0] = var,
                .destvar = addr,
            });
            return addr;
        }
    case AST_EXPR_DEREFERENCE:
        // &*foo --> just evaluate foo
        return build_expression(st, &address_of_what->data.operands[0], NULL, NULL, true);
    default:
        assert(0);
    }
}

const char *nth(int n)
{
    assert(n >= 1);

    const char *first_few[] = { NULL, "first", "second", "third", "fourth", "fifth", "sixth" };
    if (n < (int)(sizeof(first_few)/sizeof(first_few[0])))
        return first_few[n];

    static char result[100];
    sprintf(result, "%dth", n);
    return result;
}

// returns NULL if the function doesn't return anything
static const struct CfVariable *build_call(const struct State *st, const struct AstCall *call, struct Location location)
{
    const struct Signature *sig = find_function(st, call->funcname);
    if (!sig)
        fail_with_error(location, "function \"%s\" not found", call->funcname);
    char *sigstr = signature_to_string(sig, false);

    if (call->nargs < sig->nargs || (call->nargs > sig->nargs && !sig->takes_varargs)) {
        fail_with_error(
            location,
            "function %s takes %d argument%s, but it was called with %d argument%s",
            sigstr,
            sig->nargs,
            sig->nargs==1?"":"s",
            call->nargs,
            call->nargs==1?"":"s"
        );
    }

    const struct CfVariable **args = malloc(call->nargs * sizeof(args[0]));  // NOLINT
    for (int i = 0; i < sig->nargs; i++) {
        // This is a common error, so worth spending some effort to get a good error message.
        char msg[500];
        snprintf(msg, sizeof msg, "%s argument of function %s should have type TO, not FROM", nth(i+1), sigstr);
        args[i] = build_expression(st, &call->args[i], &sig->argtypes[i], msg, true);
    }
    free(sigstr);
    for (int i = sig->nargs; i < call->nargs; i++) {
        // This code runs for varargs, e.g. the things to format in printf().
        args[i] = build_expression(st, &call->args[i], NULL, NULL, true);
    }

    const struct CfVariable *return_value;
    if (sig->returntype) {
        char debugname[100];
        snprintf(debugname, sizeof debugname, "$%s_ret", call->funcname);
        return_value = add_variable(st, sig->returntype, debugname);
    }else
        return_value = NULL;

    struct CfInstruction ins = {
        .location = location,
        .kind = CF_CALL,
        .data.call.args = args,
        .data.call.nargs = call->nargs,
        .destvar = return_value,
    };
    safe_strcpy(ins.data.call.funcname, call->funcname);
    Append(&st->current_block->instructions, ins);

    return return_value;
}

static void build_body(struct State *st, const struct AstBody *body);

static void build_statement(struct State *st, const struct AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_IF:
    {
        const struct CfVariable *cond = build_expression(
            st, &stmt->data.ifstatement.condition,
            &boolType, "'if' condition must be a boolean, not FROM", true);
        struct CfBlock *thenblock = add_block(st);
        struct CfBlock *afterblock = add_block(st);
        st->current_block->branchvar = cond;
        st->current_block->iftrue = thenblock;
        st->current_block->iffalse = afterblock;
        st->current_block = thenblock;
        build_body(st, &stmt->data.ifstatement.body);
        st->current_block->iftrue = afterblock;
        st->current_block->iffalse = afterblock;
        st->current_block = afterblock;
        break;
    }
    case AST_STMT_WHILE:
    {
        // condblock: evaluate condition and go to bodyblock or doneblock
        // bodyblock: start of loop body
        // doneblock: rest of the code goes here
        struct CfBlock *condblock = add_block(st);
        struct CfBlock *bodyblock = add_block(st);
        struct CfBlock *doneblock = add_block(st);
        st->current_block->iftrue = condblock;
        st->current_block->iffalse = condblock;
        st->current_block = condblock;
        const struct CfVariable *cond = build_expression(
            st, &stmt->data.ifstatement.condition,
            &boolType, "'while' condition must be a boolean, not FROM", true);
        st->current_block->branchvar = cond;
        st->current_block->iftrue = bodyblock;
        st->current_block->iffalse = doneblock;
        st->current_block = bodyblock;
        build_body(st, &stmt->data.ifstatement.body);
        st->current_block->iftrue = condblock;
        st->current_block->iffalse = condblock;
        st->current_block = doneblock;
        break;
    }

    case AST_STMT_RETURN_VALUE:
    {
        if(!st->signature->returntype){
            fail_with_error(
                stmt->location,
                "function '%s' cannot return a value because it was defined with '-> void'",
                st->signature->funcname);
        }

        char msg[200];
        snprintf(msg, sizeof msg,
            "attempting to return a value of type FROM from function '%s' defined with '-> TO'",
            st->signature->funcname);
        const struct CfVariable *retvalue = build_expression(
            st, &stmt->data.expression, st->signature->returntype, msg, true);
        const struct CfVariable *retvariable = find_variable(st, "return", NULL);
        assert(retvariable);
        Append(&st->current_block->instructions, (struct CfInstruction){
            .location = stmt->location,
            .kind=CF_VARCPY,
            .data.operands[0] = retvalue,
            .destvar = retvariable,
        });

        st->current_block->iftrue = &st->cfg->end_block;
        st->current_block->iffalse = &st->cfg->end_block;
        st->current_block = add_block(st);  // an unreachable block
        break;
    }

    case AST_STMT_RETURN_WITHOUT_VALUE:
        if (st->signature->returntype) {
            fail_with_error(
                stmt->location,
                "a return value is needed, because the return type of function '%s' is %s",
                st->signature->funcname,
                st->signature->returntype->name);
        }
        st->current_block->iftrue = &st->cfg->end_block;
        st->current_block->iffalse = &st->cfg->end_block;
        st->current_block = add_block(st);  // an unreachable block
        break;

    case AST_STMT_EXPRESSION_STATEMENT:
        build_expression(st, &stmt->data.expression, NULL, NULL, false);
        break;
    }
}

static void build_body(struct State *st, const struct AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        build_statement(st, &body->statements[i]);
}

static struct CfGraph *build_function(struct State *st, const struct Signature *sig, const struct AstBody *body)
{
    st->signature = sig;
    st->cfg = calloc(1, sizeof *st->cfg);
    Append(&st->cfg->all_blocks, &st->cfg->start_block);
    Append(&st->cfg->all_blocks, &st->cfg->end_block);

    st->current_block = &st->cfg->start_block;

    for (int i = 0; i < sig->nargs; i++)
        add_variable(st, &sig->argtypes[i], sig->argnames[i]);
    if (sig->returntype) 
        add_variable(st, sig->returntype, "return");

    build_body(st, body);

    // Implicit return at the end of the function
    st->current_block->iftrue = &st->cfg->end_block;
    st->current_block->iffalse = &st->cfg->end_block;

    return st->cfg;
}

static void check_signature(const struct State *st, const struct Signature *sig)
{
    if (find_function(st, sig->funcname))
        fail_with_error(sig->location, "a function named '%s' already exists", sig->funcname);

    if (!strcmp(sig->funcname, "main") &&
        (sig->returntype == NULL || !same_type(sig->returntype, &intType)))
    {
        fail_with_error(sig->location, "the main() function must return int");
    }
}

struct CfGraphFile build_control_flow_graphs(struct AstToplevelNode *ast)
{
    struct CfGraphFile result = { .filename = ast->location.filename };
    struct State st = { .cfgfile = &result };

    int n = 0;
    while (ast[n].kind!=AST_TOPLEVEL_END_OF_FILE) n++;
    result.graphs = malloc(sizeof(result.graphs[0]) * n);  // NOLINT
    result.signatures = malloc(sizeof(result.signatures[0]) * n);

    while (ast->kind != AST_TOPLEVEL_END_OF_FILE) {
        switch(ast->kind) {
        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        case AST_TOPLEVEL_CDECL_FUNCTION:
            check_signature(&st, &ast[result.nfuncs].data.decl_signature);
            result.signatures[result.nfuncs] = copy_signature(&ast[result.nfuncs].data.decl_signature);
            result.graphs[result.nfuncs] = NULL;
            result.nfuncs++;
            break;
        case AST_TOPLEVEL_DEFINE_FUNCTION:
            check_signature(&st, &ast->data.funcdef.signature);
            struct Signature sig = copy_signature(&ast->data.funcdef.signature);
            result.signatures[result.nfuncs] = sig;
            result.nfuncs++;  // Make signature of current function usable in function calls (recursion)
            result.graphs[result.nfuncs-1] = build_function(&st, &sig, &ast->data.funcdef.body);
            break;
        }
        ast++;
    }

    return result;
}
