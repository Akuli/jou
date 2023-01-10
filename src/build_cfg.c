#include "jou_compiler.h"


struct State {
    CfGraphFile *cfgfile;
    CfGraph *cfg;
    const Signature *signature;
    CfBlock *current_block;
    List(CfBlock *) breakstack;
    List(CfBlock *) continuestack;
};

static CfVariable *add_variable(const struct State *st, const Type *t, const char *name)
{
    CfVariable *var = calloc(1, sizeof *var);
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
static const CfVariable *find_variable(const struct State *st, const char *name, const Location *error_location)
{
    for (CfVariable **var = st->cfg->variables.ptr; var < End(st->cfg->variables); var++)
        if (!strcmp((*var)->name, name))
            return *var;

    if (!error_location)
        return NULL;
    fail_with_error(*error_location, "no local variable named '%s'", name);
}

static const Signature *find_function(const struct State *st, const char *name)
{
    for (int i = 0; i < st->cfgfile->nfuncs; i++)
        if (!strcmp(st->cfgfile->signatures[i].funcname, name))
            return &st->cfgfile->signatures[i];
    return NULL;
}

static CfBlock *add_block(const struct State *st)
{
    CfBlock *block = calloc(1, sizeof *block);
    Append(&st->cfg->all_blocks, block);
    return block;
}

static void add_jump(struct State *st, const CfVariable *branchvar, CfBlock *iftrue, CfBlock *iffalse, CfBlock *new_current_block)
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
    const CfVariable **operands, // NULL terminated, or NULL for empty
    const CfVariable *destvar)
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
    add_instruction((st), (loc), (op), NULL, (const CfVariable*[]){(arg),NULL}, (target))
#define add_binary_op(st, loc, op, lhs, rhs, target) \
    add_instruction((st), (loc), (op), NULL, (const CfVariable*[]){(lhs),(rhs),NULL}, (target))
#define add_constant(st, loc, c, target) \
    add_instruction((st), (loc), CF_CONSTANT, &(union CfInstructionData){ .constant=copy_constant(&(c)) }, NULL, (target))


// NULL return value means it is void
static Type *build_type_or_void(const struct State *st, const AstType *asttype)
{
    (void)st;    // Currently not used. Will later be needed for struct names

    int npointers = asttype->npointers;
    Type t;

    if (!strcmp(asttype->name, "int"))
        t = intType;
    else if (!strcmp(asttype->name, "byte"))
        t = byteType;
    else if (!strcmp(asttype->name, "bool"))
        t = boolType;
    else if (!strcmp(asttype->name, "void")) {
        if (npointers == 0)
            return NULL;
        npointers--;
        t = voidPtrType;
    } else
        fail_with_error(asttype->location, "there is no type named '%s'", asttype->name);

    while (npointers--)
        t = create_pointer_type(&t, asttype->location);

    Type *ptr = malloc(sizeof *ptr);
    *ptr = t;
    return ptr;
}

static Type build_type(const struct State *st, const AstType *asttype)
{
    Type *ptr = build_type_or_void(st, asttype);
    if (!ptr)
        fail_with_error(asttype->location, "'void' cannot be used here because it is not a type");

    Type t = *ptr;
    free(ptr);
    return t;
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
static noreturn void fail_with_implicit_cast_error(
    Location location, const char *template, const Type *from, const Type *to)
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

static const CfVariable *build_implicit_cast(
    const struct State *st,
    const CfVariable *obj,
    const Type *to,
    Location location,
    const char *err_template)
{
    const Type *from = &obj->type;

    if (same_type(from, to))
        return obj;

    const CfVariable *result = add_variable(st, to, "$implicit_cast");

    // Casting to bigger signed int applies when "to" is signed and bigger.
    // Doesn't cast from unsigned to same size signed: with 8 bits, 255 does not implicitly cast to -1.
    // TODO: does this surely work e.g. how would 8-bit 11111111 cast to 32 bit?
    if (is_integer_type(from)
        && to->kind == TYPE_SIGNED_INTEGER
        && from->data.width_in_bits < to->data.width_in_bits)
    {
        add_unary_op(st, location, CF_INT_SCAST_TO_BIGGER, obj, result);
        return result;
    }

    // Casting to bigger unsigned int: original value has to be unsigned as well.
    if (from->kind == TYPE_UNSIGNED_INTEGER
        && to->kind == TYPE_UNSIGNED_INTEGER
        && from->data.width_in_bits < to->data.width_in_bits)
    {
        add_unary_op(st, location, CF_INT_UCAST_TO_BIGGER, obj, result);
        return result;
    }

    // Implicitly cast between void* and non-void pointer
    if ((from->kind == TYPE_POINTER && to->kind == TYPE_VOID_POINTER)
        || (from->kind == TYPE_VOID_POINTER && to->kind == TYPE_POINTER))
    {
        add_unary_op(st, location, CF_CAST_POINTER, obj, result);
        return result;
    }

    fail_with_implicit_cast_error(location, err_template, from, to);
}

static const CfVariable *build_binop(
    const struct State *st,
    enum AstExpressionKind op,
    Location location,
    const CfVariable *lhs,
    const CfVariable *rhs)
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

    bool got_integers = is_integer_type(&lhs->type) && is_integer_type(&rhs->type);
    bool got_pointers = (
        is_pointer_type(&lhs->type)
        && is_pointer_type(&rhs->type)
        && (
            // Ban comparisons like int* == byte*, unless one of the two types is void*
            same_type(&lhs->type, &rhs->type)
            || same_type(&lhs->type, &voidPtrType)
            || same_type(&rhs->type, &voidPtrType)
        )
    );

    if (!got_integers && !(got_pointers && (op == AST_EXPR_EQ || op == AST_EXPR_NE)))
        fail_with_error(location, "wrong types: cannot %s %s and %s", do_what, lhs->type.name, rhs->type.name);

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

    // It shouldn't be possible to make these fail.
    // TODO: i think it is, with adding same size signed and unsigned for example
    lhs = build_implicit_cast(st, lhs, &cast_type, location, NULL);
    rhs = build_implicit_cast(st, rhs, &cast_type, location, NULL);

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
    const char *debugname;
    bool is_signed = cast_type.kind == TYPE_SIGNED_INTEGER;
    bool negate = false;
    bool swap = false;

    switch(op) {
        case AST_EXPR_ADD: debugname = "$add"; k = CF_INT_ADD; break;
        case AST_EXPR_SUB: debugname = "$sub"; k = CF_INT_SUB; break;
        case AST_EXPR_MUL: debugname = "$mul"; k = CF_INT_MUL; break;
        case AST_EXPR_DIV: debugname = (is_signed ? "$sdiv" : "$udiv"); k = (is_signed ? CF_INT_SDIV : CF_INT_UDIV); break;
        case AST_EXPR_EQ: debugname = "$eq"; k = got_pointers?CF_PTR_EQ:CF_INT_EQ; break;
        case AST_EXPR_NE: debugname = "$ne"; k = got_pointers?CF_PTR_EQ:CF_INT_EQ; negate=true; break;
        case AST_EXPR_LT: debugname = "$lt"; k = CF_INT_LT; break;
        case AST_EXPR_GT: debugname = "$gt"; k = CF_INT_LT; swap=true; break;
        case AST_EXPR_LE: debugname = "$le"; k = CF_INT_LT; negate=true; swap=true; break;
        case AST_EXPR_GE: debugname = "$ge"; k = CF_INT_LT; negate=true; break;
        default: assert(0);
    }

    const CfVariable *destvar = add_variable(st, &result_type, debugname);
    add_binary_op(st, location, k, swap?rhs:lhs, swap?lhs:rhs, destvar);

    if (!negate)
        return destvar;

    const CfVariable *negated = add_variable(st, &boolType, debugname);
    add_unary_op(st, location, CF_BOOL_NEGATE, destvar, negated);
    return negated;
}

static const CfVariable *build_address_of_expression(struct State *st, const AstExpression *address_of_what, bool is_assignment);

enum PreOrPost { PRE, POST };

static const CfVariable *build_increment_or_decrement(
    struct State *st,
    Location location,
    const AstExpression *inner,
    enum PreOrPost pop,
    int diff)
{
    assert(diff==1 || diff==-1);  // 1=increment, -1=decrement

    const CfVariable *addr = build_address_of_expression(st, inner, true);
    assert(addr->type.kind == TYPE_POINTER);
    const Type *t = addr->type.data.valuetype;
    if (!is_integer_type(t))
        fail_with_error(location, "cannot %s a value of type %s", diff==1?"increment":"decrement", t->name);

    const CfVariable *old_value = add_variable(st, t, "$old_value");
    const CfVariable *new_value = add_variable(st, t, "$new_value");
    const CfVariable *diffvar = add_variable(st, t, "$diff");

    Constant diffconst = {
        .kind = CONSTANT_INTEGER,
        .data.integer = {
            .width_in_bits = t->data.width_in_bits,
            .is_signed = (t->kind == TYPE_SIGNED_INTEGER),
            .value = diff,
        },
    };

    add_constant(st, location, diffconst, diffvar);
    add_unary_op(st, location, CF_PTR_LOAD, addr, old_value);
    add_binary_op(st, location, CF_INT_ADD, old_value, diffvar, new_value);
    add_binary_op(st, location, CF_PTR_STORE, addr, new_value, NULL);

    switch(pop) {
        case PRE: return new_value;
        case POST: return old_value;
    }
    assert(0);
}

static void check_dereferenced_pointer_type(Location location, const Type *t)
{
    // TODO: improved error message for dereferencing void*
    if (t->kind != TYPE_POINTER)
        fail_with_error(location, "the dereference operator '*' is only for pointers, not for %s", t->name);
}

static const char *get_debug_name_for_constant(const Constant *c)
{
    switch(c->kind) {
    case CONSTANT_INTEGER:
        return "$intconstant";
    case CONSTANT_NULL:
        return "$null";
    case CONSTANT_STRING:
        return "$strconstant";
    case CONSTANT_BOOL:
        return c->data.boolean ? "$true" : "$false";
    }
    assert(0);
}

enum AndOr { AND, OR };

static const CfVariable *build_call(struct State *st, const AstCall *call, Location location);
static const CfVariable *build_and_or(struct State *st, const AstExpression *lhsexpr, const AstExpression *rhsexpr, enum AndOr andor);

static const CfVariable *build_expression(
    struct State *st,
    const AstExpression *expr,
    const Type *implicit_cast_to,  // can be NULL, there will be no implicit casting
    const char *casterrormsg,
    bool needvalue)  // Usually true. False means that calls to "-> void" functions are acceptable.
{
    const CfVariable *result, *temp;
    Type temptype;

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
        add_unary_op(st, expr->location, CF_PTR_LOAD, temp, result);
        break;
    case AST_EXPR_CONSTANT:
        temptype = type_of_constant(&expr->data.constant);
        result = add_variable(st, &temptype, get_debug_name_for_constant(&expr->data.constant));
        add_constant(st, expr->location, expr->data.constant, result);
        break;
    case AST_EXPR_ASSIGN:
        {
            AstExpression *targetexpr = &expr->data.operands[0];
            AstExpression *valueexpr = &expr->data.operands[1];
            if (targetexpr->kind == AST_EXPR_GET_VARIABLE && !find_variable(st, targetexpr->data.varname, NULL))
            {
                // Making a new variable. Use the type of the value being assigned.
                result = build_expression(st, valueexpr, NULL, NULL, true);
                const CfVariable *var = add_variable(st, &result->type, targetexpr->data.varname);
                add_unary_op(st, expr->location, CF_VARCPY, result, var);
            } else {
                // Convert value to the type of an existing variable or other assignment target.
                // TODO: is this evaluation order good?
                const CfVariable *target = build_address_of_expression(st, targetexpr, true);
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
                const CfVariable *casted_result = build_implicit_cast(
                    st, result, target->type.data.valuetype, expr->location, errmsg);
                add_binary_op(st, expr->location, CF_PTR_STORE, target, casted_result, NULL);
            }
            break;
        }
    case AST_EXPR_AND:
        result = build_and_or(st, &expr->data.operands[0], &expr->data.operands[1], AND);
        break;
    case AST_EXPR_OR:
        result = build_and_or(st, &expr->data.operands[0], &expr->data.operands[1], OR);
        break;
    case AST_EXPR_NOT:
        temp = build_expression(st, &expr->data.operands[0], &boolType, "value after 'not' must be a boolean, not FROM", true);
        result = add_variable(st, &boolType, "$not");
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
            const CfVariable *lhs = build_expression(st, &expr->data.operands[0], NULL, NULL, true);
            const CfVariable *rhs = build_expression(st, &expr->data.operands[1], NULL, NULL, true);
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

static const CfVariable *build_and_or(
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
    char errormsg[100];
    sprintf(errormsg, "'%s' only works with booleans, not FROM", andor==AND ? "and" : "or");

    const CfVariable *lhs = build_expression(st, lhsexpr, &boolType, errormsg, true);
    const CfVariable *rhs;
    const CfVariable *result = add_variable(st, &boolType, andor==AND ? "$and" : "$or");
    CfInstruction *ins;

    CfBlock *lhstrue = add_block(st);
    CfBlock *lhsfalse = add_block(st);
    CfBlock *done = add_block(st);

    // if lhs:
    add_jump(st, lhs, lhstrue, lhsfalse, lhstrue);

    switch(andor) {
    case AND:
        // result = rhs
        rhs = build_expression(st, rhsexpr, &boolType, errormsg, true);
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
        rhs = build_expression(st, rhsexpr, &boolType, errormsg, true);
        add_unary_op(st, rhsexpr->location, CF_VARCPY, rhs, result);
        break;
    }

    add_jump(st, NULL, done, done, done);
    return result;
}

static const CfVariable *build_address_of_expression(struct State *st, const AstExpression *address_of_what, bool is_assignment)
{
    const char *cant_take_address_of;

    switch(address_of_what->kind) {
    case AST_EXPR_GET_VARIABLE:
    {
        const CfVariable *var = find_variable(st, address_of_what->data.varname, &address_of_what->location);
        Type t = create_pointer_type(&var->type, (Location){0});
        const CfVariable *addr = add_variable(st, &t, "$address_of_var");
        free(t.data.valuetype);
        add_unary_op(st, address_of_what->location, CF_ADDRESS_OF_VARIABLE, var, addr);
        return addr;
    }
    case AST_EXPR_DEREFERENCE:
    {
        // &*foo --> just evaluate foo, but make sure it is a pointer
        const CfVariable *result = build_expression(st, &address_of_what->data.operands[0], NULL, NULL, true);
        check_dereferenced_pointer_type(address_of_what->location, &result->type);
        return result;
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
    case AST_EXPR_ASSIGN:
        cant_take_address_of = "an assignment";
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

static const char *nth(int n)
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
static const CfVariable *build_call(struct State *st, const AstCall *call, Location location)
{
    const Signature *sig = find_function(st, call->funcname);
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

    const CfVariable **args = calloc(call->nargs + 1, sizeof(args[0]));  // NOLINT
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

    const CfVariable *return_value;
    if (sig->returntype) {
        char debugname[100];
        snprintf(debugname, sizeof debugname, "$%s_ret", call->funcname);
        return_value = add_variable(st, sig->returntype, debugname);
    }else
        return_value = NULL;

    union CfInstructionData data;
    safe_strcpy(data.funcname, call->funcname);
    add_instruction(st, location, CF_CALL, &data, args, return_value);

    free(sigstr);
    free(args);
    return return_value;
}

static void build_body(struct State *st, const AstBody *body);

static void build_if_statement(struct State *st, const AstIfStatement *ifstmt)
{
    assert(ifstmt->n_if_and_elifs >= 1);

    CfBlock *done = add_block(st);
    for (int i = 0; i < ifstmt->n_if_and_elifs; i++) {
        const char *errmsg;
        if (i == 0)
            errmsg = "'if' condition must be a boolean, not FROM";
        else
            errmsg = "'elif' condition must be a boolean, not FROM";

        const CfVariable *cond = build_expression(
            st, &ifstmt->if_and_elifs[i].condition, &boolType, errmsg, true);
        CfBlock *then = add_block(st);
        CfBlock *otherwise = add_block(st);

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
    const AstExpression *init,
    const AstExpression *cond,
    const AstExpression *incr,
    const AstBody *body)
{
    assert(strlen(loopname) < 10);
    char errormsg[100];
    sprintf(errormsg, "'%s' condition must be a boolean, not FROM", loopname);

    CfBlock *condblock = add_block(st);  // evaluate condition and go to bodyblock or doneblock
    CfBlock *bodyblock = add_block(st);  // run loop body and go to incrblock
    CfBlock *incrblock = add_block(st);  // run incr and go to condblock
    CfBlock *doneblock = add_block(st);  // rest of the code goes here
    CfBlock *tmp;

    if (init)
        build_expression(st, init, NULL, NULL, false);

    // Evaluate condition. Jump to loop body or skip to after loop.
    add_jump(st, NULL, condblock, condblock, condblock);
    const CfVariable *condvar = build_expression(st, cond, &boolType, errormsg, true);
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
        const CfVariable *retvalue = build_expression(
            st, &stmt->data.expression, st->signature->returntype, msg, true);
        const CfVariable *retvariable = find_variable(st, "return", NULL);
        assert(retvariable);
        add_unary_op(st, stmt->location, CF_VARCPY, retvalue, retvariable);

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

    case AST_STMT_DECLARE_LOCAL_VAR:
        if (find_variable(st, stmt->data.vardecl.name, NULL))
            fail_with_error(stmt->location, "a variable named '%s' already exists", stmt->data.vardecl.name);

        Type type = build_type(st, &stmt->data.vardecl.type);
        CfVariable *v = add_variable(st, &type, stmt->data.vardecl.name);
        if (stmt->data.vardecl.initial_value) {
            const CfVariable *cfvar = build_expression(
                st, stmt->data.vardecl.initial_value, &type,
                "initial value for variable of type TO cannot be of type FROM",
                true);
            add_unary_op(st, stmt->location, CF_VARCPY, cfvar, v);
        }
        free_type(&type);
        break;

    case AST_STMT_EXPRESSION_STATEMENT:
        build_expression(st, &stmt->data.expression, NULL, NULL, false);
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
    st->signature = sig;
    st->cfg = calloc(1, sizeof *st->cfg);
    Append(&st->cfg->all_blocks, &st->cfg->start_block);
    Append(&st->cfg->all_blocks, &st->cfg->end_block);

    st->current_block = &st->cfg->start_block;

    for (int i = 0; i < sig->nargs; i++) {
        CfVariable *v = add_variable(st, &sig->argtypes[i], sig->argnames[i]);
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

static Signature build_signature(const struct State *st, const AstSignature *astsig, Location location)
{
    for (int i = 0; i < st->cfgfile->nfuncs; i++)
        if (!strcmp(st->cfgfile->signatures[i].funcname, astsig->funcname))
            fail_with_error(location, "a function named '%s' already exists", astsig->funcname);

    Signature result = { .nargs = astsig->nargs, .takes_varargs = astsig->takes_varargs };
    safe_strcpy(result.funcname, astsig->funcname);

    size_t size = sizeof(result.argnames[0]) * result.nargs;
    result.argnames = malloc(size);
    memcpy(result.argnames, astsig->argnames, size);

    result.argtypes = malloc(sizeof(result.argtypes[0]) * result.nargs);
    for (int i = 0; i < result.nargs; i++)
        result.argtypes[i] = build_type(st, &astsig->argtypes[i]);

    result.returntype = build_type_or_void(st, &astsig->returntype);
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

CfGraphFile build_control_flow_graphs(AstToplevelNode *ast)
{
    CfGraphFile result = { .filename = ast->location.filename };
    struct State st = { .cfgfile = &result };

    int n = 0;
    while (ast[n].kind!=AST_TOPLEVEL_END_OF_FILE) n++;
    result.graphs = malloc(sizeof(result.graphs[0]) * n);  // NOLINT
    result.signatures = malloc(sizeof(result.signatures[0]) * n);

    Signature sig;

    while (ast->kind != AST_TOPLEVEL_END_OF_FILE) {
        switch(ast->kind) {
        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        case AST_TOPLEVEL_DECLARE_FUNCTION:
            sig = build_signature(&st, &ast->data.decl_signature, ast->location);
            result.signatures[result.nfuncs] = sig;
            result.graphs[result.nfuncs] = NULL;
            result.nfuncs++;
            break;
        case AST_TOPLEVEL_DEFINE_FUNCTION:
            sig = build_signature(&st, &ast->data.funcdef.signature, ast->location);
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
