#include "jou_compiler.h"


struct State {
    TypeContext typectx;
    CfGraph *cfg;
    CfBlock *current_block;
    List(CfBlock *) breakstack;
    List(CfBlock *) continuestack;
};

static const Variable *find_variable(const struct State *st, const char *name)
{
    for (Variable **var = st->typectx.variables.ptr; var < End(st->typectx.variables); var++)
        if (!strcmp((*var)->name, name))
            return *var;
    return NULL;
}

static const ExpressionTypes *get_expr_types(const struct State *st, const AstExpression *expr)
{
    // TODO: a fancy binary search algorithm
    for (int i = 0; i < st->typectx.expr_types.len; i++)
        if (st->typectx.expr_types.ptr[i]->expr == expr)
            return st->typectx.expr_types.ptr[i];
    return NULL;
}

static Variable *add_variable(struct State *st, const Type *t, const char *name)
{
    Variable *var = calloc(1, sizeof *var);
    var->id = st->typectx.variables.len;
    var->type = copy_type(t);
    if (name) {
        assert(!find_variable(st, name));
        assert(strlen(name) < sizeof var->name);
        strcpy(var->name, name);
    }
    Append(&st->typectx.variables, var);
    return var;
}

static const Signature *find_function(const struct State *st, const char *name)
{
    for (int i = 0; i < st->typectx.function_signatures.len; i++)
        if (!strcmp(st->typectx.function_signatures.ptr[i].funcname, name))
            return &st->typectx.function_signatures.ptr[i];
    return NULL;
}

static CfBlock *add_block(const struct State *st)
{
    CfBlock *block = calloc(1, sizeof *block);
    Append(&st->cfg->all_blocks, block);
    return block;
}

static void add_jump(struct State *st, const Variable *branchvar, CfBlock *iftrue, CfBlock *iffalse, CfBlock *new_current_block)
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

// returned pointer is only valid until next call to add_instruction()
static CfInstruction *add_instruction(
    const struct State *st,
    Location location,
    enum CfInstructionKind k,
    const union CfInstructionData *dat,
    const Variable **operands, // NULL terminated, or NULL for empty
    const Variable *destvar)
{
    CfInstruction ins = { .location=location, .kind=k, .destvar=destvar };
    if (dat)
        ins.data=*dat;

    while (operands && operands[ins.noperands])
        ins.noperands++;
    if (ins.noperands) {
        size_t nbytes = sizeof(ins.operands[0]) * ins.noperands;  // NOLINT
        ins.operands = malloc(nbytes);
        memcpy(ins.operands, operands, nbytes);
    }

    Append(&st->current_block->instructions, ins);
    return &st->current_block->instructions.ptr[st->current_block->instructions.len - 1];
}

// add_instruction() takes many arguments. Let's hide the mess a bit.
#define add_unary_op(st, loc, op, arg, target) \
    add_instruction((st), (loc), (op), NULL, (const Variable*[]){(arg),NULL}, (target))
#define add_binary_op(st, loc, op, lhs, rhs, target) \
    add_instruction((st), (loc), (op), NULL, (const Variable*[]){(lhs),(rhs),NULL}, (target))
#define add_constant(st, loc, c, target) \
    add_instruction((st), (loc), CF_CONSTANT, &(union CfInstructionData){ .constant=copy_constant(&(c)) }, NULL, (target))


static const Variable *build_cast(
    struct State *st, const Variable *obj, const Type *to, Location location)
{
    if (same_type(&obj->type, to))
        return obj;

    const Variable *result = add_variable(st, to, NULL);

    if (is_pointer_type(&obj->type) && is_pointer_type(to)) {
        add_unary_op(st, location, CF_PTR_CAST, obj, result);
        return result;
    }
    if (is_integer_type(&obj->type) && is_integer_type(to)) {
        add_unary_op(st, location, CF_INT_CAST, obj, result);
        return result;
    }
    assert(0);
}

// TODO: use get_expr_types()
static const Variable *build_binop(
    struct State *st,
    enum AstExpressionKind op,
    Location location,
    const Variable *lhs,
    const Variable *rhs)
{
    bool got_integers = is_integer_type(&lhs->type) && is_integer_type(&rhs->type);
    bool got_pointers = is_pointer_type(&lhs->type) && is_pointer_type(&rhs->type);
    assert(got_integers || got_pointers);
    assert(!(got_integers && got_pointers));

    // TODO: is this a good idea?
    Type cast_type;
    if (got_integers) {
        cast_type = create_integer_type(
            max(lhs->type.data.width_in_bits, rhs->type.data.width_in_bits),
            lhs->type.kind == TYPE_SIGNED_INTEGER || lhs->type.kind == TYPE_SIGNED_INTEGER
        );
    }
    if (got_pointers) {
        cast_type = voidPtrType;
    }

    lhs = build_cast(st, lhs, &cast_type, location);
    rhs = build_cast(st, rhs, &cast_type, location);

    Type result_type;
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
    bool is_signed = cast_type.kind == TYPE_SIGNED_INTEGER;
    bool negate = false;
    bool swap = false;

    switch(op) {
        case AST_EXPR_ADD: k = CF_INT_ADD; break;
        case AST_EXPR_SUB: k = CF_INT_SUB; break;
        case AST_EXPR_MUL: k = CF_INT_MUL; break;
        case AST_EXPR_DIV: k = (is_signed ? CF_INT_SDIV : CF_INT_UDIV); break;
        case AST_EXPR_EQ: k = got_pointers?CF_PTR_EQ:CF_INT_EQ; break;
        case AST_EXPR_NE: k = got_pointers?CF_PTR_EQ:CF_INT_EQ; negate=true; break;
        case AST_EXPR_LT: k = CF_INT_LT; break;
        case AST_EXPR_GT: k = CF_INT_LT; swap=true; break;
        case AST_EXPR_LE: k = CF_INT_LT; negate=true; swap=true; break;
        case AST_EXPR_GE: k = CF_INT_LT; negate=true; break;
        default: assert(0);
    }

    const Variable *destvar = add_variable(st, &result_type, NULL);
    add_binary_op(st, location, k, swap?rhs:lhs, swap?lhs:rhs, destvar);

    if (!negate)
        return destvar;

    const Variable *negated = add_variable(st, &boolType, NULL);
    add_unary_op(st, location, CF_BOOL_NEGATE, destvar, negated);
    return negated;
}

static const Variable *build_struct_field_pointer(
    struct State *st, const Variable *structinstance, const char *fieldname, Location location)
{
    assert(structinstance->type.kind == TYPE_POINTER);
    assert(structinstance->type.data.valuetype->kind == TYPE_STRUCT);
    const Type *structtype = structinstance->type.data.valuetype;

    for (int i = 0; i < structtype->data.structfields.count; i++) {
        char name[100];
        safe_strcpy(name, structtype->data.structfields.names[i]);
        const Type *type = &structtype->data.structfields.types[i];

        if (!strcmp(name, fieldname)) {
            const Type ptrtype = create_pointer_type(type, location);
            Variable* result = add_variable(st, &ptrtype, NULL);
            free(ptrtype.data.valuetype);

            union CfInstructionData dat;
            safe_strcpy(dat.fieldname, name);

            add_instruction(st, location, CF_PTR_STRUCT_FIELD, &dat, (const Variable*[]){structinstance,NULL}, result);
            return result;
        }
    }

    assert(0);
}

static const Variable *build_expression(struct State *st, const AstExpression *expr);
static const Variable *build_address_of_expression(
    struct State *st,
    const AstExpression *address_of_what,
    bool is_assignment);

enum PreOrPost { PRE, POST };

static const Variable *build_increment_or_decrement(
    struct State *st,
    Location location,
    const AstExpression *inner,
    enum PreOrPost pop,
    int diff)
{
    assert(diff==1 || diff==-1);  // 1=increment, -1=decrement

    const Variable *addr = build_address_of_expression(st, inner, true);
    assert(addr->type.kind == TYPE_POINTER);
    const Type *t = addr->type.data.valuetype;
    if (!is_integer_type(t) && !is_pointer_type(t))
        fail_with_error(location, "cannot %s a value of type %s", diff==1?"increment":"decrement", t->name);

    const Variable *old_value = add_variable(st, t, NULL);
    const Variable *new_value = add_variable(st, t, NULL);
    const Variable *diffvar = add_variable(st, is_integer_type(t)?t:&intType, NULL);

    Constant diffconst = {
        .kind = CONSTANT_INTEGER,
        .data.integer = {
            .width_in_bits = diffvar->type.data.width_in_bits,
            .is_signed = (diffvar->type.kind == TYPE_SIGNED_INTEGER),
            .value = diff,
        },
    };

    add_constant(st, location, diffconst, diffvar);
    add_unary_op(st, location, CF_PTR_LOAD, addr, old_value);
    add_binary_op(st, location, is_integer_type(t)?CF_INT_ADD:CF_PTR_ADD_INT, old_value, diffvar, new_value);
    add_binary_op(st, location, CF_PTR_STORE, addr, new_value, NULL);

    switch(pop) {
        case PRE: return new_value;
        case POST: return old_value;
    }
    assert(0);
}

// ptr[index]
static const Variable *build_indexing(struct State *st, const AstExpression *ptrexpr, const AstExpression *indexexpr)
{
    const Variable *ptr = build_expression(st, ptrexpr);
    const Variable *index = build_expression(st, indexexpr);
    assert(ptr->type.kind == TYPE_POINTER);
    assert(is_integer_type(&index->type));

    const Variable *ptr2 = add_variable(st, &ptr->type, NULL);
    const Variable *result = add_variable(st, ptr->type.data.valuetype, NULL);
    add_binary_op(st, ptrexpr->location, CF_PTR_ADD_INT, ptr, index, ptr2);
    add_unary_op(st, ptrexpr->location, CF_PTR_LOAD, ptr2, result);
    return result;
}

enum AndOr { AND, OR };

static const Variable *build_and_or(
    struct State *st, const AstExpression *lhsexpr, const AstExpression *rhsexpr, enum AndOr andor)
{
    /*
    Must be careful with side effects.

    and:
        # lhs returning False means we don't evaluate rhs
        if lhs:
            result = rhs
        else:
            result = False

    or:
        # lhs returning True means we don't evaluate rhs
        if lhs:
            result = True
        else:
            result = rhs
    */
    const Variable *lhs = build_expression(st, lhsexpr);
    const Variable *rhs;
    const Variable *result = add_variable(st, &boolType, NULL);
    CfInstruction *ins;

    CfBlock *lhstrue = add_block(st);
    CfBlock *lhsfalse = add_block(st);
    CfBlock *done = add_block(st);

    // if lhs:
    add_jump(st, lhs, lhstrue, lhsfalse, lhstrue);

    switch(andor) {
    case AND:
        // result = rhs
        rhs = build_expression(st, rhsexpr);
        add_unary_op(st, rhsexpr->location, CF_VARCPY, rhs, result);
        break;
    case OR:
        // result = True
        ins = add_constant(st, lhsexpr->location, ((Constant){CONSTANT_BOOL, {.boolean=true}}), result);
        ins->hide_unreachable_warning = true;
        break;
    }

    // else:
    add_jump(st, NULL, done, done, lhsfalse);

    switch(andor) {
    case AND:
        // result = False
        ins = add_constant(st, lhsexpr->location, ((Constant){CONSTANT_BOOL, {.boolean=false}}), result);
        ins->hide_unreachable_warning = true;
        break;
    case OR:
        // result = rhs
        rhs = build_expression(st, rhsexpr);
        add_unary_op(st, rhsexpr->location, CF_VARCPY, rhs, result);
        break;
    }

    add_jump(st, NULL, done, done, done);
    return result;
}

static const Variable *build_address_of_expression(struct State *st, const AstExpression *address_of_what, bool is_assignment)
{
    const char *cant_take_address_of;

    switch(address_of_what->kind) {
    case AST_EXPR_GET_VARIABLE:
    {
        const Variable *var = find_variable(st, address_of_what->data.varname);
        assert(var);
        Type t = create_pointer_type(&var->type, (Location){0});
        const Variable *addr = add_variable(st, &t, NULL);
        free(t.data.valuetype);
        add_unary_op(st, address_of_what->location, CF_ADDRESS_OF_VARIABLE, var, addr);
        return addr;
    }
    case AST_EXPR_DEREFERENCE:
    {
        // &*foo --> just evaluate foo
        return build_expression(st, &address_of_what->data.operands[0]);
    }
    case AST_EXPR_DEREF_AND_GET_FIELD:
    {
        // &obj->field aka &(obj->field)
        const Variable *obj = build_expression(st, address_of_what->data.field.obj);
        assert(obj->type.kind == TYPE_POINTER);
        assert(obj->type.data.valuetype->kind == TYPE_STRUCT);
        return build_struct_field_pointer(st, obj, address_of_what->data.field.fieldname, address_of_what->location);
    }
    case AST_EXPR_GET_FIELD:
    {
        // &obj.field aka &(obj.field), evaluate as &(&obj)->field
        const Variable *obj = build_address_of_expression(st, address_of_what->data.field.obj, false);
        assert(obj->type.kind == TYPE_POINTER);
        assert(obj->type.data.valuetype->kind == TYPE_STRUCT);
        return build_struct_field_pointer(st, obj, address_of_what->data.field.fieldname, address_of_what->location);
    }

    /*
    The & operator can't go in front of most expressions.
    You can't do &(1 + 2), for example.

    The same rules apply to assignments: "foo = bar" is treated as setting the
    value of the pointer &foo to bar.
    */
    case AST_EXPR_CONSTANT:
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
    default:
        cant_take_address_of = "a newly calculated value";
        break;
    }

    if (is_assignment)
        fail_with_error(address_of_what->location, "cannot assign to %s", cant_take_address_of);
    else
        fail_with_error(address_of_what->location, "the address-of operator '&' cannot be used with %s", cant_take_address_of);
}

// returns NULL if the function doesn't return anything
static const Variable *build_function_call(struct State *st, const AstCall *call, Location location)
{
    const Signature *sig = find_function(st, call->calledname);
    assert(sig);

    const Variable **args = calloc(call->nargs + 1, sizeof(args[0]));  // NOLINT
    for (int i = 0; i < call->nargs; i++)
        args[i] = build_expression(st, &call->args[i]);

    const Variable *return_value;
    if (sig->returntype)
        return_value = add_variable(st, sig->returntype, NULL);
    else
        return_value = NULL;

    union CfInstructionData data;
    safe_strcpy(data.funcname, call->calledname);
    add_instruction(st, location, CF_CALL, &data, args, return_value);

    free(args);
    return return_value;
}

static const Variable *build_struct_init(struct State *st, const Type *type, const AstCall *call, Location location)
{
    const Variable *instance = add_variable(st, type, NULL);
    Type p = create_pointer_type(type, location);
    const Variable *instanceptr = add_variable(st, &p, NULL);
    free(p.data.valuetype);

    add_unary_op(st, location, CF_ADDRESS_OF_VARIABLE, instance, instanceptr);
    add_unary_op(st, location, CF_PTR_MEMSET_TO_ZERO, instanceptr, NULL);

    for (int i = 0; i < call->nargs; i++) {
        const Variable *fieldptr = build_struct_field_pointer(st, instanceptr, call->argnames[i], call->args[i].location);
        const Variable *fieldval = build_expression(st, &call->args[i]);
        add_binary_op(st, location, CF_PTR_STORE, fieldptr, fieldval, NULL);
    }

    return instance;
}

static const Variable *build_expression(struct State *st, const AstExpression *expr)
{
    const ExpressionTypes *types = get_expr_types(st, expr);

    const Variable *result, *temp;
    Type temptype;

    switch(expr->kind) {
    case AST_EXPR_FUNCTION_CALL:
        result = build_function_call(st, &expr->data.call, expr->location);
        if (!result)
            return NULL;
        break;
    case AST_EXPR_BRACE_INIT:
        result = build_struct_init(st, &types->type, &expr->data.call, expr->location);
        break;
    case AST_EXPR_GET_FIELD:
    case AST_EXPR_DEREF_AND_GET_FIELD:
        // To evaluate foo.bar or foo->bar, we first evaluate &foo.bar or &foo->bar.
        // We can't do this with all expressions: &(1 + 2) doesn't work, for example.
        {
            temp = build_address_of_expression(st, expr, false);
            assert(temp->type.kind == TYPE_POINTER);
            result = add_variable(st, temp->type.data.valuetype, NULL);
            add_unary_op(st, expr->location, CF_PTR_LOAD, temp, result);
        }
        break;
    case AST_EXPR_INDEXING:
        result = build_indexing(st, &expr->data.operands[0], &expr->data.operands[1]);
        break;
    case AST_EXPR_ADDRESS_OF:
        result = build_address_of_expression(st, &expr->data.operands[0], false);
        break;
    case AST_EXPR_GET_VARIABLE:
        result = find_variable(st, expr->data.varname);
        assert(result);
        if (!types->type_after_cast) {
            // Must take a "snapshot" of this variable, as it may change soon.
            temp = result;
            result = add_variable(st, &temp->type, NULL);
            add_unary_op(st, expr->location, CF_VARCPY, temp, result);
        }
        break;
    case AST_EXPR_DEREFERENCE:
        temp = build_expression(st, &expr->data.operands[0]);
        assert(temp->type.kind == TYPE_POINTER);
        result = add_variable(st, temp->type.data.valuetype, NULL);
        add_unary_op(st, expr->location, CF_PTR_LOAD, temp, result);
        break;
    case AST_EXPR_CONSTANT:
        temptype = type_of_constant(&expr->data.constant);
        result = add_variable(st, &temptype, NULL);
        add_constant(st, expr->location, expr->data.constant, result);
        break;
    case AST_EXPR_AND:
        result = build_and_or(st, &expr->data.operands[0], &expr->data.operands[1], AND);
        break;
    case AST_EXPR_OR:
        result = build_and_or(st, &expr->data.operands[0], &expr->data.operands[1], OR);
        break;
    case AST_EXPR_NOT:
        temp = build_expression(st, &expr->data.operands[0]);
        result = add_variable(st, &boolType, NULL);
        add_unary_op(st, expr->location, CF_BOOL_NEGATE, temp, result);
        break;
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
            const Variable *lhs = build_expression(st, &expr->data.operands[0]);
            const Variable *rhs = build_expression(st, &expr->data.operands[1]);
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
    case AST_EXPR_AS:
        temp = build_expression(st, expr->data.as.obj);
        temptype = type_from_ast(&st->typectx, &expr->data.as.type);
        result = build_cast(st, temp, &temptype, expr->location);
        free_type(&temptype);
        break;
    }

    assert(types);
    assert(same_type(&result->type, &types->type));
    if (types->type_after_cast)
        return build_cast(st, result, types->type_after_cast, expr->location);
    else
        return result;
}

static void build_body(struct State *st, const AstBody *body);

static void build_if_statement(struct State *st, const AstIfStatement *ifstmt)
{
    assert(ifstmt->n_if_and_elifs >= 1);

    CfBlock *done = add_block(st);
    for (int i = 0; i < ifstmt->n_if_and_elifs; i++) {
        const Variable *cond = build_expression(
            st, &ifstmt->if_and_elifs[i].condition);
        CfBlock *then = add_block(st);
        CfBlock *otherwise = add_block(st);

        add_jump(st, cond, then, otherwise, then);
        build_body(st, &ifstmt->if_and_elifs[i].body);
        add_jump(st, NULL, done, done, otherwise);
    }

    build_body(st, &ifstmt->elsebody);
    add_jump(st, NULL, done, done, done);
}

static void build_statement(struct State *st, const AstStatement *stmt);

// for init; cond; incr:
//     ...body...
//
// While loop is basically a special case of for loop, so it uses this too.
static void build_loop(
    struct State *st,
    const char *loopname,
    const AstStatement *init,
    const AstExpression *cond,
    const AstStatement *incr,
    const AstBody *body)
{
    assert(strlen(loopname) < 10);

    CfBlock *condblock = add_block(st);  // evaluate condition and go to bodyblock or doneblock
    CfBlock *bodyblock = add_block(st);  // run loop body and go to incrblock
    CfBlock *incrblock = add_block(st);  // run incr and go to condblock
    CfBlock *doneblock = add_block(st);  // rest of the code goes here
    CfBlock *tmp;

    if (init)
        build_statement(st, init);

    // Evaluate condition. Jump to loop body or skip to after loop.
    add_jump(st, NULL, condblock, condblock, condblock);
    const Variable *condvar = build_expression(st, cond);
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
        build_statement(st, incr);
    add_jump(st, NULL, condblock, condblock, doneblock);
}

static void build_statement(struct State *st, const AstStatement *stmt)
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
            stmt->data.forloop.init, &stmt->data.forloop.cond, stmt->data.forloop.incr,
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

    case AST_STMT_ASSIGN:
        {
            const AstExpression *targetexpr = &stmt->data.assignment.target;
            const AstExpression *valueexpr = &stmt->data.assignment.value;

            // TODO: is this evaluation order good?
            const Variable *target = build_address_of_expression(st, targetexpr, true);
            assert(target->type.kind == TYPE_POINTER);
            const Variable *value = build_expression(st, valueexpr);
            add_binary_op(st, stmt->location, CF_PTR_STORE, target, value, NULL);
            break;
        }

    case AST_STMT_RETURN_VALUE:
    {
        const Variable *retvalue = build_expression(st, &stmt->data.expression);
        const Variable *retvariable = find_variable(st, "return");
        assert(retvariable);
        add_unary_op(st, stmt->location, CF_VARCPY, retvalue, retvariable);
    }  // fall through
    case AST_STMT_RETURN_WITHOUT_VALUE:
        st->current_block->iftrue = &st->cfg->end_block;
        st->current_block->iffalse = &st->cfg->end_block;
        st->current_block = add_block(st);  // an unreachable block
        break;

    case AST_STMT_DECLARE_LOCAL_VAR:
        if (stmt->data.vardecl.initial_value) {
            const Variable *v = find_variable(st, stmt->data.vardecl.name);
            assert(v);
            const Variable *cfvar = build_expression(st, stmt->data.vardecl.initial_value);
            add_unary_op(st, stmt->location, CF_VARCPY, cfvar, v);
        }
        break;

    case AST_STMT_EXPRESSION_STATEMENT:
        build_expression(st, &stmt->data.expression);
        break;
    }
}

static void build_body(struct State *st, const AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        build_statement(st, &body->statements[i]);
}

static CfGraph *build_function(struct State *st, const Signature *sig, const AstBody *body)
{
    st->typectx.current_function_signature = sig;
    for (int i = 0; i < sig->nargs; i++) {
        Variable *v = add_variable(st, &sig->argtypes[i], sig->argnames[i]);
        v->is_argument = true;
    }
    if (sig->returntype)
        add_variable(st, sig->returntype, "return");
    typecheck_function(&st->typectx, body);

    st->cfg = calloc(1, sizeof *st->cfg);
    Append(&st->cfg->all_blocks, &st->cfg->start_block);
    Append(&st->cfg->all_blocks, &st->cfg->end_block);

    st->current_block = &st->cfg->start_block;

    assert(st->breakstack.len == 0 && st->continuestack.len == 0);
    build_body(st, body);
    assert(st->breakstack.len == 0 && st->continuestack.len == 0);

    // Implicit return at the end of the function
    st->current_block->iftrue = &st->cfg->end_block;
    st->current_block->iffalse = &st->cfg->end_block;

    memcpy(&st->cfg->variables, &st->typectx.variables, sizeof st->typectx.variables);
    memset(&st->typectx.variables, 0, sizeof st->typectx.variables);
    st->typectx.current_function_signature = NULL;

    return st->cfg;
}

static Signature build_signature(const struct State *st, const AstSignature *astsig, Location location)
{
    for (int i = 0; i < st->typectx.function_signatures.len; i++)
        if (!strcmp(st->typectx.function_signatures.ptr[i].funcname, astsig->funcname))
            fail_with_error(location, "a function named '%s' already exists", astsig->funcname);

    Signature result = { .nargs = astsig->nargs, .takes_varargs = astsig->takes_varargs };
    safe_strcpy(result.funcname, astsig->funcname);

    size_t size = sizeof(result.argnames[0]) * result.nargs;
    result.argnames = malloc(size);
    memcpy(result.argnames, astsig->argnames, size);

    result.argtypes = malloc(sizeof(result.argtypes[0]) * result.nargs);
    for (int i = 0; i < result.nargs; i++)
        result.argtypes[i] = type_from_ast(&st->typectx, &astsig->argtypes[i]);

    result.returntype = type_or_void_from_ast(&st->typectx, &astsig->returntype);
    // TODO: validate main() parameters
    // TODO: test main() taking parameters
    if (!strcmp(astsig->funcname, "main") &&
        (result.returntype == NULL || !same_type(result.returntype, &intType)))
    {
        fail_with_error(astsig->returntype.location, "the main() function must return int");
    }

    result.returntype_location = astsig->returntype.location;
    return result;
}

static Type build_struct(struct State *st, const AstStructDef *structdef, Location location)
{
    for (Type *t = st->typectx.structs.ptr; t < End(st->typectx.structs); t++)
        if (!strcmp(t->name, structdef->name))
            fail_with_error(location, "a struct named '%s' already exists", t->name);

    Type result = { .kind = TYPE_STRUCT };
    safe_strcpy(result.name, structdef->name);

    int n = structdef->nfields;
    result.data.structfields.count = n;

    result.data.structfields.names = malloc(n * sizeof result.data.structfields.names[0]);
    memcpy(result.data.structfields.names, structdef->fieldnames, n * sizeof result.data.structfields.names[0]);

    result.data.structfields.types = malloc(n * sizeof result.data.structfields.types[0]);
    for (int i = 0; i < n; i++)
        result.data.structfields.types[i] = type_from_ast(&st->typectx, &structdef->fieldtypes[i]);

    return result;
}

CfGraphFile build_control_flow_graphs(AstToplevelNode *ast)
{
    CfGraphFile result = { .filename = ast->location.filename };
    struct State st = {0};

    int n = 0;
    while (ast[n].kind!=AST_TOPLEVEL_END_OF_FILE) n++;
    result.graphs = malloc(sizeof(result.graphs[0]) * n);  // NOLINT

    Signature sig;
    Type type;

    while (ast->kind != AST_TOPLEVEL_END_OF_FILE) {
        switch(ast->kind) {
        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        case AST_TOPLEVEL_DECLARE_FUNCTION:
            sig = build_signature(&st, &ast->data.decl_signature, ast->location);
            Append(&st.typectx.function_signatures, sig);
            result.graphs[result.nfuncs++] = NULL;
            break;
        case AST_TOPLEVEL_DEFINE_FUNCTION:
            sig = build_signature(&st, &ast->data.funcdef.signature, ast->location);
            // Make signature of current function usable in function calls (recursion)
            Append(&st.typectx.function_signatures, sig);
            result.graphs[result.nfuncs++] = build_function(&st, &sig, &ast->data.funcdef.body);
            break;
        case AST_TOPLEVEL_DEFINE_STRUCT:
            // careful with evaluation order in Append...
            type = build_struct(&st, &ast->data.structdef, ast->location);
            Append(&st.typectx.structs, type);
            break;
        }
        ast++;
    }

    free(st.breakstack.ptr);
    free(st.continuestack.ptr);
    /*for (Type *t = st.structs.ptr; t < End(st.structs); t++)
        free_type(t);
    free(st.structs.ptr);*/
    result.signatures = st.typectx.function_signatures.ptr;
    result.nfuncs = st.typectx.function_signatures.len;
    return result;
}
