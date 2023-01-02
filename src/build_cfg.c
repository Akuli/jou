#include "jou_compiler.h"


struct State {
    struct CfGraphFile *cfgfile;
    struct CfGraph *cfg;
    const struct Signature *signature;
    struct CfBlock *current_block;
    List(struct CfBlock *) breakstack;
    List(struct CfBlock *) continuestack;
};

static struct CfVariable *add_variable(const struct State *st, const struct Type *t, const char *name)
{
    struct CfVariable *var = calloc(1, sizeof *var);
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

static void add_jump(struct State *st, const struct CfVariable *branchvar, struct CfBlock *iftrue, struct CfBlock *iffalse, struct CfBlock *new_current_block)
{
    assert(iftrue);
    assert(iffalse);
    if (iftrue != iffalse) {
        assert(branchvar);
        assert(same_type(&branchvar->type, &boolType));
    }

    st->current_block->branchvar = branchvar;
    st->current_block->iftrue = iftrue;
    st->current_block->iffalse = iffalse;
    st->current_block = new_current_block ? new_current_block : add_block(st);
}

static void add_instruction(
    const struct State *st,
    struct Location location,
    enum CfInstructionKind k,
    const union CfInstructionData *dat,
    int noperands,
    const struct CfVariable **operands,
    const struct CfVariable *destvar)
{
    struct CfInstruction ins = { .location=location, .kind=k, .noperands=noperands, .destvar=destvar };
    if (dat)
        ins.data=*dat;

    if (noperands) {
        size_t nbytes = sizeof(ins.operands[0]) * noperands;  // NOLINT
        ins.operands = malloc(nbytes);
        memcpy(ins.operands, operands, nbytes);
    }

    Append(&st->current_block->instructions, ins);
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
        add_instruction(st, location, CF_CAST_TO_BIGGER_SIGNED_INT, NULL, 1, &obj, result);
        return result;
    }

    // Casting to bigger unsigned int: original value has to be unsigned as well.
    if (from->kind == TYPE_UNSIGNED_INTEGER
        && to->kind == TYPE_UNSIGNED_INTEGER
        && from->data.width_in_bits < to->data.width_in_bits)
    {
        const struct CfVariable *result = add_variable(st, to, "$implicit_cast");
        add_instruction(st, location, CF_CAST_TO_BIGGER_UNSIGNED_INT, NULL, 1, &obj, result);
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

    enum CfInstructionKind k;
    const char *debugname;
    bool is_signed = cast_type.kind == TYPE_SIGNED_INTEGER;
    bool negate = false;
    bool swap = false;

    switch(op) {
        case AST_EXPR_ADD: debugname = "$add"; k = CF_INT_ADD; break;
        case AST_EXPR_SUB: debugname = "$sub"; k = CF_INT_SUB; break;
        case AST_EXPR_MUL: debugname = "$mul"; k = CF_INT_MUL; break;
        case AST_EXPR_DIV: debugname = (is_signed ? "$sdiv" : "$udiv"); k = (is_signed ? CF_INT_SDIV : CF_INT_UDIV); break;
        case AST_EXPR_EQ: debugname = "$eq"; k = CF_INT_EQ; break;
        case AST_EXPR_NE: debugname = "$ne"; k = CF_INT_EQ; negate=true; break;
        case AST_EXPR_LT: debugname = "$lt"; k = CF_INT_LT; break;
        case AST_EXPR_GT: debugname = "$gt"; k = CF_INT_LT; swap=true; break;
        case AST_EXPR_LE: debugname = "$le"; k = CF_INT_LT; negate=true; swap=true; break;
        case AST_EXPR_GE: debugname = "$ge"; k = CF_INT_LT; negate=true; break;
        default: assert(0);
    }

    const struct CfVariable *destvar = add_variable(st, &result_type, debugname);
    const struct CfVariable *operands[2] = { swap?rhs:lhs, swap?lhs:rhs };
    add_instruction(st, location, k, NULL, 2, operands, destvar);

    if (!negate)
        return destvar;

    const struct CfVariable *negated = add_variable(st, &boolType, debugname);
    add_instruction(st, location, CF_BOOL_NEGATE, NULL, 1, &destvar, negated);
    return negated;
}

static const struct CfVariable *build_address_of_expression(const struct State *st, const struct AstExpression *address_of_what, bool is_assignment);

enum PreOrPost { PRE, POST };

const struct CfVariable *build_increment_or_decrement(
    const struct State *st,
    struct Location location,
    const struct AstExpression *inner,
    enum PreOrPost pop,
    int diff)
{
    assert(diff==1 || diff==-1);  // 1=increment, -1=decrement

    const struct CfVariable *addr = build_address_of_expression(st, inner, true);
    assert(addr->type.kind == TYPE_POINTER);
    const struct Type *t = addr->type.data.valuetype;
    if (!is_integer_type(t))
        fail_with_error(location, "cannot %s a value of type %s", diff==1?"increment":"decrement", t->name);

    const struct CfVariable *old_value = add_variable(st, t, "$old_value");
    const struct CfVariable *new_value = add_variable(st, t, "$new_value");
    const struct CfVariable *diffvar = add_variable(st, t, "$diff");
    add_instruction(st, location, CF_INT_CONSTANT, &(union CfInstructionData){ .int_value=diff }, 0, NULL, diffvar);
    add_instruction(st, location, CF_LOAD_FROM_POINTER, NULL, 1, &addr, old_value);
    add_instruction(st, location, CF_INT_ADD, NULL, 2, (const struct CfVariable*[]){old_value,diffvar}, new_value);
    add_instruction(st, location, CF_STORE_TO_POINTER, NULL, 2, (const struct CfVariable*[]){addr,new_value}, NULL);

    switch(pop) {
        case PRE: return new_value;
        case POST: return old_value;
    }
    assert(0);
}

static const struct CfVariable *build_call(const struct State *st, const struct AstCall *call, struct Location location);

static void check_dereferenced_pointer_type(struct Location location, const struct Type *t)
{
    if (t->kind != TYPE_POINTER)
        fail_with_error(location, "the dereference operator '*' is only for pointers, not for %s", t->name);
}

static const struct CfVariable *build_expression(
    const struct State *st,
    const struct AstExpression *expr,
    const struct Type *implicit_cast_to,  // can be NULL, there will be no implicit casting
    const char *casterrormsg,
    bool needvalue)  // Usually true. False means that calls to "-> void" functions are acceptable.
{
    const struct CfVariable *result, *temp;
    union CfInstructionData data = {0};

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
        result = build_address_of_expression(st, &expr->data.operands[0], false);
        break;
    case AST_EXPR_GET_VARIABLE:
        result = find_variable(st, expr->data.varname, &expr->location);
        break;
    case AST_EXPR_DEREFERENCE:
        temp = build_expression(st, &expr->data.operands[0], NULL, NULL, true);
        check_dereferenced_pointer_type(expr->location, &temp->type);
        result = add_variable(st, temp->type.data.valuetype, "$deref");
        add_instruction(st, expr->location, CF_LOAD_FROM_POINTER, NULL, 1, &temp, result);
        break;
    case AST_EXPR_INT_CONSTANT:
        result = add_variable(st, &intType, "$intconstant");
        data.int_value = expr->data.int_value;
        add_instruction(st, expr->location, CF_INT_CONSTANT, &data, 0, NULL, result);
        break;
    case AST_EXPR_CHAR_CONSTANT:
        result = add_variable(st, &byteType, "$byteconstant");
        data.int_value = expr->data.char_value;
        add_instruction(st, expr->location, CF_INT_CONSTANT, &data, 0, NULL, result);
        break;
    case AST_EXPR_STRING_CONSTANT:
        result = add_variable(st, &stringType, "$strconstant");
        data.string_value = strdup(expr->data.string_value);
        add_instruction(st, expr->location, CF_STRING_CONSTANT, &data, 0, NULL, result);
        break;
    case AST_EXPR_TRUE:
        result = add_variable(st, &boolType, "$true");
        add_instruction(st, expr->location, CF_TRUE, NULL, 0, NULL, result);
        break;
    case AST_EXPR_FALSE:
        result = add_variable(st, &boolType, "$false");
        add_instruction(st, expr->location, CF_FALSE, NULL, 0, NULL, result);
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
                add_instruction(st, expr->location, CF_VARCPY, NULL, 1, &result, var);
            } else {
                // Convert value to the type of an existing variable or other assignment target.
                // TODO: is this evaluation order good?
                const struct CfVariable *target = build_address_of_expression(st, targetexpr, true);
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
                const struct CfVariable *operands[2] = { target, casted_result };
                add_instruction(st, expr->location, CF_STORE_TO_POINTER, NULL, 2, operands, NULL);
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
            break;
        }
    case AST_EXPR_PRE_INCREMENT:
    case AST_EXPR_PRE_DECREMENT:
    case AST_EXPR_POST_INCREMENT:
    case AST_EXPR_POST_DECREMENT:
        {
            enum PreOrPost pop;
            int diff;

            switch(expr->kind) {
                case AST_EXPR_PRE_INCREMENT: pop=PRE; diff=1; break;
                case AST_EXPR_PRE_DECREMENT: pop=PRE; diff=-1; break;
                case AST_EXPR_POST_INCREMENT: pop=POST; diff=1; break;
                case AST_EXPR_POST_DECREMENT: pop=POST; diff=-1; break;
                default: assert(0);
            }
            result = build_increment_or_decrement(st, expr->location, &expr->data.operands[0], pop, diff);
            break;
        }
    }

    if (implicit_cast_to == NULL) {
        assert(!casterrormsg);
        return result;
    }
    assert(casterrormsg);
    return build_implicit_cast(st, result, implicit_cast_to, expr->location, casterrormsg);
}

static const struct CfVariable *build_address_of_expression(const struct State *st, const struct AstExpression *address_of_what, bool is_assignment)
{
    const char *cant_take_address_of;

    switch(address_of_what->kind) {
    case AST_EXPR_GET_VARIABLE:
    {
        const struct CfVariable *var = find_variable(st, address_of_what->data.varname, &address_of_what->location);
        struct Type t = create_pointer_type(&var->type, (struct Location){0});
        const struct CfVariable *addr = add_variable(st, &t, "$address_of_var");
        free(t.data.valuetype);
        add_instruction(st, address_of_what->location, CF_ADDRESS_OF_VARIABLE, NULL, 1, &var, addr);
        return addr;
    }
    case AST_EXPR_DEREFERENCE:
    {
        // &*foo --> just evaluate foo, but make sure it is a pointer
        const struct CfVariable *result = build_expression(st, &address_of_what->data.operands[0], NULL, NULL, true);
        check_dereferenced_pointer_type(address_of_what->location, &result->type);
        return result;
    }

    /*
    The & operator can't go in front of most expressions.
    You can't do &(1 + 2), for example.

    The same rules apply to assignments: "foo = bar" is treated as setting the
    value of the pointer &foo to bar.
    */
    case AST_EXPR_INT_CONSTANT:
    case AST_EXPR_CHAR_CONSTANT:
    case AST_EXPR_STRING_CONSTANT:
    case AST_EXPR_TRUE:
    case AST_EXPR_FALSE:
        cant_take_address_of = "a constant";
        break;
    case AST_EXPR_PRE_INCREMENT:
    case AST_EXPR_POST_INCREMENT:
        cant_take_address_of = "the result of incrementing a value";
        break;
    case AST_EXPR_PRE_DECREMENT:
    case AST_EXPR_POST_DECREMENT:
        cant_take_address_of = "the result of decrementing a value";
        break;
    case AST_EXPR_ASSIGN:
        cant_take_address_of = "an assignment";
        break;
    case AST_EXPR_ADDRESS_OF:
    case AST_EXPR_CALL:
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
        cant_take_address_of = "a newly calculated value";
        break;
    }

    if (is_assignment)
        fail_with_error(address_of_what->location, "cannot assign to %s", cant_take_address_of);
    else
        fail_with_error(address_of_what->location, "the address-of operator '&' cannot be used with %s", cant_take_address_of);
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

    union CfInstructionData data;
    safe_strcpy(data.funcname, call->funcname);
    add_instruction(st, location, CF_CALL, &data, call->nargs, args, return_value);

    free(sigstr);
    free(args);
    return return_value;
}

static void build_body(struct State *st, const struct AstBody *body);

static void build_if_statement(struct State *st, const struct AstIfStatement *ifstmt)
{
    assert(ifstmt->n_if_and_elifs >= 1);

    struct CfBlock *done = add_block(st);
    for (int i = 0; i < ifstmt->n_if_and_elifs; i++) {
        const char *errmsg;
        if (i == 0)
            errmsg = "'if' condition must be a boolean, not FROM";
        else
            errmsg = "'elif' condition must be a boolean, not FROM";

        const struct CfVariable *cond = build_expression(
            st, &ifstmt->if_and_elifs[i].condition, &boolType, errmsg, true);
        struct CfBlock *then = add_block(st);
        struct CfBlock *otherwise = add_block(st);

        add_jump(st, cond, then, otherwise, then);
        build_body(st, &ifstmt->if_and_elifs[i].body);
        add_jump(st, NULL, done, done, otherwise);
    }

    build_body(st, &ifstmt->elsebody);
    add_jump(st, NULL, done, done, done);
}

// for init; cond; incr:
//     ...body...
//
// While loop is basically a special case of for loop, so it uses this too.
static void build_loop(
    struct State *st,
    const char *loopname,
    const struct AstExpression *init,
    const struct AstExpression *cond,
    const struct AstExpression *incr,
    const struct AstBody *body)
{
    assert(strlen(loopname) < 10);
    char errormsg[100];
    sprintf(errormsg, "'%s' condition must be a boolean, not FROM", loopname);

    struct CfBlock *condblock = add_block(st);  // evaluate condition and go to bodyblock or doneblock
    struct CfBlock *bodyblock = add_block(st);  // run loop body and go to incrblock
    struct CfBlock *incrblock = add_block(st);  // run incr and go to condblock
    struct CfBlock *doneblock = add_block(st);  // rest of the code goes here
    struct CfBlock *tmp;

    if (init)
        build_expression(st, init, NULL, NULL, false);

    // Evaluate condition. Jump to loop body or skip to after loop.
    add_jump(st, NULL, condblock, condblock, condblock);
    const struct CfVariable *condvar = build_expression(st, cond, &boolType, errormsg, true);
    add_jump(st, condvar, bodyblock, doneblock, bodyblock);

    // Run loop body: 'break' skips to after loop, 'continue' goes to incr.
    Append(&st->breakstack, doneblock);
    Append(&st->continuestack, incrblock);
    build_body(st, body);
    tmp = Pop(&st->breakstack); assert(tmp == doneblock);
    tmp = Pop(&st->continuestack); assert(tmp == incrblock);

    // Run incr and jump back to condition.
    add_jump(st, NULL, incrblock, incrblock, incrblock);
    if (incr)
        build_expression(st, incr, NULL, NULL, false);
    add_jump(st, NULL, condblock, condblock, doneblock);
}

static void build_statement(struct State *st, const struct AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_IF:
        build_if_statement(st, &stmt->data.ifstatement);
        break;

    case AST_STMT_WHILE:
        build_loop(
            st, "while",
            NULL, &stmt->data.whileloop.condition, NULL,
            &stmt->data.whileloop.body);
        break;

    case AST_STMT_FOR:
        build_loop(
            st, "for",
            &stmt->data.forloop.init, &stmt->data.forloop.cond, &stmt->data.forloop.incr,
            &stmt->data.forloop.body);
        break;

    case AST_STMT_BREAK:
        if (!st->breakstack.len)
            fail_with_error(stmt->location, "'break' can only be used inside a loop");
        add_jump(st, NULL, End(st->breakstack)[-1], End(st->breakstack)[-1], NULL);
        break;

    case AST_STMT_CONTINUE:
        if (!st->continuestack.len)
            fail_with_error(stmt->location, "'continue' can only be used inside a loop");
        add_jump(st, NULL, End(st->continuestack)[-1], End(st->continuestack)[-1], NULL);
        break;

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
        add_instruction(st, stmt->location, CF_VARCPY, NULL, 1, &retvalue, retvariable);

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

    for (int i = 0; i < sig->nargs; i++) {
        struct CfVariable *v = add_variable(st, &sig->argtypes[i], sig->argnames[i]);
        v->is_argument = true;
    }
    if (sig->returntype) 
        add_variable(st, sig->returntype, "return");

    assert(st->breakstack.len == 0 && st->continuestack.len == 0);
    build_body(st, body);
    assert(st->breakstack.len == 0 && st->continuestack.len == 0);

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
            check_signature(&st, &ast->data.decl_signature);
            result.signatures[result.nfuncs] = copy_signature(&ast->data.decl_signature);
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

    free(st.breakstack.ptr);
    free(st.continuestack.ptr);
    return result;
}
