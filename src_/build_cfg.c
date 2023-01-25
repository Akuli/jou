#include "jou_compiler.h"


struct State {
    TypeContext *typectx;
    CfGraph *cfg;
    CfBlock *current_block;
    List(CfBlock *) breakstack;
    List(CfBlock *) continuestack;
};

<<<<<<< HEAD
static const Variable *find_variable(const struct State *st, const char *name)
=======
// If error_location is NULL, this will return NULL when variable is not found.
static CfVariable *find_variable(const struct State *st, const char *name, const Location *error_location)
>>>>>>> array
{
    for (Variable **var = st->typectx->variables.ptr; var < End(st->typectx->variables); var++)
        if (!strcmp((*var)->name, name))
            return *var;
    return NULL;
}

static Variable *add_variable(struct State *st, const Type *t)
{
    Variable *var = calloc(1, sizeof *var);
    var->id = st->typectx->variables.len;
    var->type = t;
    Append(&st->typectx->variables, var);
    return var;
}

static const ExpressionTypes *get_expr_types(const struct State *st, const AstExpression *expr)
{
    // TODO: a fancy binary search algorithm (need to add sorting)
    for (int i = 0; i < st->typectx->expr_types.len; i++)
        if (st->typectx->expr_types.ptr[i]->expr == expr)
            return st->typectx->expr_types.ptr[i];
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
        assert(branchvar->type == boolType);
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
    add_instruction((st), (loc), CF_CONSTANT, &(union CfInstructionData){ .constant=copy_constant((Constant[]){c}) }, NULL, (target))


<<<<<<< HEAD
static const Variable *build_bool_to_int_conversion(
    struct State *st, const Variable *boolvar, const Location location, const Type *t)
{
    assert(is_integer_type(t));
    Variable *result = add_variable(st, t);
=======
int evaluate_array_length(const AstExpression *expr)
{
    switch(expr->kind) {
    case AST_EXPR_CONSTANT:
        if (expr->data.constant.kind == CONSTANT_INTEGER
            && expr->data.constant.data.integer.is_signed
            && expr->data.constant.data.integer.width_in_bits == 32)
        {
            return (int)expr->data.constant.data.integer.value;
        }
        // fall through
    default:
        fail_with_error(expr->location, "cannot evaluate array length at compile time");
    }
}

// NULL return value means it is void
static Type *build_type_or_void(const struct State *st, const AstType *asttype)
{
    Type t, *tmp;

    switch(asttype->kind) {
    case AST_TYPE_NAMED:
        if (!strcmp(asttype->data.name, "int"))
            t = intType;
        else if (!strcmp(asttype->data.name, "byte"))
            t = byteType;
        else if (!strcmp(asttype->data.name, "bool"))
            t = boolType;
        else if (!strcmp(asttype->data.name, "void"))
            return NULL;
        else {
            bool found = false;
            for (struct Type *ptr = st->structs.ptr; ptr < End(st->structs); ptr++) {
                if (!strcmp(ptr->name, asttype->data.name)) {
                    t = copy_type(ptr);
                    found = true;
                    break;
                }
            }
            if(!found)
                fail_with_error(asttype->location, "there is no type named '%s'", asttype->data.name);
        }
        break;

    case AST_TYPE_POINTER:
        tmp = build_type_or_void(st, asttype->data.valuetype);
        if (tmp)
            t = create_pointer_type(tmp, asttype->location);
        else
            t = voidPtrType;
        break;

    case AST_TYPE_ARRAY:
        tmp = build_type_or_void(st, asttype->data.valuetype);
        if (!tmp)
            // TODO: test this error
            fail_with_error(asttype->location, "cannot make array of void");
        t = create_array_type(tmp, evaluate_array_length(asttype->data.array.len), asttype->location);
        break;
    }
>>>>>>> array

    CfBlock *set1 = add_block(st);
    CfBlock *set0 = add_block(st);
    CfBlock *done = add_block(st);

    add_jump(st, boolvar, set1, set0, set1);
    add_constant(st, location, int_constant(t, 1), result)->hide_unreachable_warning = true;
    add_jump(st, NULL, done, done, set0);
    add_constant(st, location, int_constant(t, 0), result)->hide_unreachable_warning = true;
    add_jump(st, NULL, done, done, done);

    return result;
}

static const Variable *build_cast(
    struct State *st, const Variable *obj, const Type *to, Location location)
{
    if (obj->type == to)
        return obj;

    if (is_pointer_type(obj->type) && is_pointer_type(to)) {
        const Variable *result = add_variable(st, to);
        add_unary_op(st, location, CF_PTR_CAST, obj, result);
        return result;
    }
    if (is_integer_type(obj->type) && is_integer_type(to)) {
        const Variable *result = add_variable(st, to);
        add_unary_op(st, location, CF_INT_CAST, obj, result);
        return result;
    }
    if (obj->type == boolType && is_integer_type(to))
        return build_bool_to_int_conversion(st, obj, location, to);
    assert(0);
}

static const Variable *build_binop(
    struct State *st,
    enum AstExpressionKind op,
    Location location,
    const Variable *lhs,
    const Variable *rhs,
    const Type *result_type)
{
    bool got_integers = is_integer_type(lhs->type) && is_integer_type(rhs->type);
    bool got_pointers = is_pointer_type(lhs->type) && is_pointer_type(rhs->type);
    bool is_signed = lhs->type->kind == TYPE_SIGNED_INTEGER || rhs->type->kind == TYPE_SIGNED_INTEGER;
    assert(got_integers || got_pointers);

    enum CfInstructionKind k;
    bool negate = false;
    bool swap = false;

    switch(op) {
        case AST_EXPR_ADD: k = CF_INT_ADD; break;
        case AST_EXPR_SUB: k = CF_INT_SUB; break;
        case AST_EXPR_MUL: k = CF_INT_MUL; break;
        case AST_EXPR_DIV: k = (is_signed ? CF_INT_SDIV : CF_INT_UDIV); break;
        case AST_EXPR_MOD: k = (is_signed ? CF_INT_SMOD : CF_INT_UMOD); break;
        case AST_EXPR_EQ: k = got_pointers?CF_PTR_EQ:CF_INT_EQ; break;
        case AST_EXPR_NE: k = got_pointers?CF_PTR_EQ:CF_INT_EQ; negate=true; break;
        case AST_EXPR_LT: k = CF_INT_LT; break;
        case AST_EXPR_GT: k = CF_INT_LT; swap=true; break;
        case AST_EXPR_LE: k = CF_INT_LT; negate=true; swap=true; break;
        case AST_EXPR_GE: k = CF_INT_LT; negate=true; break;
        default: assert(0);
    }

    const Variable *destvar = add_variable(st, result_type);
    add_binary_op(st, location, k, swap?rhs:lhs, swap?lhs:rhs, destvar);

    if (!negate)
        return destvar;

    const Variable *negated = add_variable(st, boolType);
    add_unary_op(st, location, CF_BOOL_NEGATE, destvar, negated);
    return negated;
}

static const Variable *build_struct_field_pointer(
    struct State *st, const Variable *structinstance, const char *fieldname, Location location)
{
    assert(structinstance->type->kind == TYPE_POINTER);
    assert(structinstance->type->data.valuetype->kind == TYPE_STRUCT);
    const Type *structtype = structinstance->type->data.valuetype;

    for (int i = 0; i < structtype->data.structfields.count; i++) {
        char name[100];
        safe_strcpy(name, structtype->data.structfields.names[i]);
<<<<<<< HEAD
        const Type *type = structtype->data.structfields.types[i];

        if (!strcmp(name, fieldname)) {
=======
        Type *type = &structtype->data.structfields.types[i];

        if (!strcmp(name, fieldname)) {
            const Type ptrtype = create_pointer_type(type, location);
            CfVariable* result = add_variable(st, &ptrtype, NULL);

>>>>>>> array
            union CfInstructionData dat;
            safe_strcpy(dat.fieldname, name);

            Variable* result = add_variable(st, get_pointer_type(type));
            add_instruction(st, location, CF_PTR_STRUCT_FIELD, &dat, (const Variable*[]){structinstance,NULL}, result);
            return result;
        }
    }

    assert(0);
}

static const Variable *build_struct_field(
    struct State *st, const Variable *structinstance, const char *fieldname, Location location)
{
    const Variable *ptr = add_variable(st, get_pointer_type(structinstance->type));
    add_unary_op(st, location, CF_ADDRESS_OF_VARIABLE, structinstance, ptr);
    const Variable *field_ptr = build_struct_field_pointer(st, ptr, fieldname, location);
    const Variable *field = add_variable(st, field_ptr->type->data.valuetype);
    add_unary_op(st, location, CF_PTR_LOAD, field_ptr, field);
    return field;
}

static const Variable *build_expression(struct State *st, const AstExpression *expr);
static const Variable *build_address_of_expression(struct State *st, const AstExpression *address_of_what);

enum PreOrPost { PRE, POST };

static const Variable *build_increment_or_decrement(
    struct State *st,
    Location location,
    const AstExpression *inner,
    enum PreOrPost pop,
    int diff)
{
    assert(diff==1 || diff==-1);  // 1=increment, -1=decrement

    const Variable *addr = build_address_of_expression(st, inner);
    assert(addr->type->kind == TYPE_POINTER);
    const Type *t = addr->type->data.valuetype;
    if (!is_integer_type(t) && !is_pointer_type(t))
        fail_with_error(location, "cannot %s a value of type %s", diff==1?"increment":"decrement", t->name);

    const Variable *old_value = add_variable(st, t);
    const Variable *new_value = add_variable(st, t);
    const Variable *diffvar = add_variable(st, is_integer_type(t) ? t : intType);

    add_constant(st, location, int_constant(diffvar->type, diff), diffvar);
    add_unary_op(st, location, CF_PTR_LOAD, addr, old_value);
    add_binary_op(st, location, is_integer_type(t)?CF_INT_ADD:CF_PTR_ADD_INT, old_value, diffvar, new_value);
    add_binary_op(st, location, CF_PTR_STORE, addr, new_value, NULL);

    switch(pop) {
        case PRE: return new_value;
        case POST: return old_value;
    }
    assert(0);
}

enum AndOr { AND, OR };

<<<<<<< HEAD
static const Variable *build_and_or(
=======
static const CfVariable *build_function_call(struct State *st, const AstCall *call, Location location);
static const CfVariable *build_struct_init(struct State *st, const AstCall *call, Location location);
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
    case AST_EXPR_FUNCTION_CALL:
        result = build_function_call(st, &expr->data.call, expr->location);
        if (result == NULL) {
            if (!needvalue)
                return NULL;
            // TODO: add a test for this
            fail_with_error(expr->location, "function '%s' does not return a value", expr->data.call.calledname);
        }
        break;
    case AST_EXPR_BRACE_INIT:
        result = build_struct_init(st, &expr->data.call, expr->location);
        break;
    case AST_EXPR_GET_FIELD:
    case AST_EXPR_DEREF_AND_GET_FIELD:
    case AST_EXPR_INDEXING:
        // To evaluate foo.bar or foo->bar, we first evaluate &foo.bar or &foo->bar.
        // We can't do this with all expressions: &(1 + 2) doesn't work, for example.
        {
            temp = build_address_of_expression(st, expr, false);
            assert(temp->type.kind == TYPE_POINTER);
            result = add_variable(st, temp->type.data.valuetype, NULL);
            add_unary_op(st, expr->location, CF_PTR_LOAD, temp, result);
        }
        break;
    case AST_EXPR_ADDRESS_OF:
        result = build_address_of_expression(st, &expr->data.operands[0], false);
        break;
    case AST_EXPR_GET_VARIABLE:
        result = find_variable(st, expr->data.varname, &expr->location);
        if (implicit_cast_to == NULL || same_type(implicit_cast_to, &result->type)) {
            // Must take a "snapshot" of this variable, as it may change soon.
            temp = result;
            result = add_variable(st, &temp->type, NULL);
            add_unary_op(st, expr->location, CF_VARCPY, temp, result);
        }
        break;
    case AST_EXPR_DEREFERENCE:
        temp = build_expression(st, &expr->data.operands[0], NULL, NULL, true);
        check_dereferenced_pointer_type(expr->location, &temp->type);
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
        temp = build_expression(st, &expr->data.operands[0], &boolType, "value after 'not' must be a boolean, not FROM", true);
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
    case AST_EXPR_AS:
        temp = build_expression(st, expr->data.as.obj, NULL, NULL, true);
        temptype = build_type(st, &expr->data.as.type);
        result = build_explicit_cast(st, temp, &temptype, expr->location);
        free_type(&temptype);
        break;
    }

    if (implicit_cast_to == NULL) {
        assert(!casterrormsg);
        return result;
    }
    assert(casterrormsg);
    return build_implicit_cast(st, result, implicit_cast_to, expr->location, casterrormsg);
}

// &ptr[index], aka ptr+index
static const CfVariable *build_ptr_add(struct State *st, const AstExpression *ptrexpr, const AstExpression *indexexpr)
{
    const CfVariable *ptr = build_expression(st, ptrexpr, NULL, NULL, true);

    if (ptr->type.kind == TYPE_ARRAY) {
        Type ptrtype = create_pointer_type(ptr->type.data.array.membertype, ptrexpr->location);
        const CfVariable *newptr = add_variable(st, &ptrtype, NULL);
        add_unary_op(st, ptrexpr->location, CF_ARRAY_TO_PTR, ptr, newptr);
        ptr = newptr;
    }

    if (ptr->type.kind != TYPE_POINTER)
        fail_with_error(ptrexpr->location, "value of type %s cannot be indexed", ptr->type.name);

    const CfVariable *index = build_expression(st, indexexpr, NULL, NULL, true);
    if (!is_integer_type(&index->type)) {
        fail_with_error(
            indexexpr->location,
            "the index inside [...] must be an integer, not %s",
            index->type.name);
    }

    const CfVariable *result = add_variable(st, &ptr->type, NULL);
    add_binary_op(st, ptrexpr->location, CF_PTR_ADD_INT, ptr, index, result);
    return result;
}

static const CfVariable *build_and_or(
>>>>>>> array
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
    const Variable *result = add_variable(st, boolType);
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

static const Variable *build_address_of_expression(struct State *st, const AstExpression *address_of_what)
{
    switch(address_of_what->kind) {
    case AST_EXPR_GET_VARIABLE:
    {
<<<<<<< HEAD
        const Variable *var = find_variable(st, address_of_what->data.varname);
        assert(var);
        const Variable *addr = add_variable(st, get_pointer_type(var->type));
=======
        CfVariable *var = find_variable(st, address_of_what->data.varname, &address_of_what->location);
        Type t = create_pointer_type(&var->type, (Location){0});
        const CfVariable *addr = add_variable(st, &t, NULL);
>>>>>>> array
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
        assert(obj->type->kind == TYPE_POINTER);
        assert(obj->type->data.valuetype->kind == TYPE_STRUCT);
        return build_struct_field_pointer(st, obj, address_of_what->data.field.fieldname, address_of_what->location);
    }
    case AST_EXPR_GET_FIELD:
    {
        // &obj.field aka &(obj.field), evaluate as &(&obj)->field
        const Variable *obj = build_address_of_expression(st, address_of_what->data.field.obj);
        assert(obj->type->kind == TYPE_POINTER);
        return build_struct_field_pointer(st, obj, address_of_what->data.field.fieldname, address_of_what->location);
    }
    case AST_EXPR_INDEXING:
<<<<<<< HEAD
    {
        // &ptr[index]
        const Variable *ptr = build_expression(st, &address_of_what->data.operands[0]);
        const Variable *index = build_expression(st, &address_of_what->data.operands[1]);
        assert(ptr->type->kind == TYPE_POINTER);
        assert(is_integer_type(index->type));

        const Variable *result = add_variable(st, ptr->type);
        add_binary_op(st, address_of_what->location, CF_PTR_ADD_INT, ptr, index, result);
        return result;
    }
=======
        return build_ptr_add(st, &address_of_what->data.operands[0], &address_of_what->data.operands[1]);
>>>>>>> array

    default:
        assert(0);
        break;
    }

    assert(0);
}

static const Variable *build_function_call(struct State *st, const AstExpression *expr)
{
    assert(expr->kind == AST_EXPR_FUNCTION_CALL);

    int nargs = expr->data.call.nargs;
    const Variable **args = calloc(nargs + 1, sizeof(args[0]));  // NOLINT
    for (int i = 0; i < nargs; i++)
        args[i] = build_expression(st, &expr->data.call.args[i]);

    const ExpressionTypes *types = get_expr_types(st, expr);
    const Variable *return_value;
    if (types)
        return_value = add_variable(st, types->type);
    else
        return_value = NULL;

    union CfInstructionData data;
    safe_strcpy(data.funcname, expr->data.call.calledname);
    add_instruction(st, expr->location, CF_CALL, &data, args, return_value);

    free(args);
    return return_value;
}

static const Variable *build_struct_init(struct State *st, const Type *type, const AstCall *call, Location location)
{
<<<<<<< HEAD
    const Variable *instance = add_variable(st, type);
    const Variable *instanceptr = add_variable(st, get_pointer_type(type));
=======
    struct AstType tmp = { .location = location, .kind = AST_TYPE_NAMED };
    safe_strcpy(tmp.data.name, call->calledname);
    Type t = build_type(st, &tmp);

    if (t.kind != TYPE_STRUCT) {
        // TODO: test this error. Currently it can never happen because
        // all non-struct types are created with keywords, and this
        // function is called only when there is a name token followed
        // by a '{'.
        fail_with_error(location, "type %s cannot be instantiated with the Foo{...} syntax", t.name);
    }

    const CfVariable *instance = add_variable(st, &t, NULL);
    Type p = create_pointer_type(&t, location);
    const CfVariable *instanceptr = add_variable(st, &p, NULL);
    free_type(&t);
>>>>>>> array

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

    switch(expr->kind) {
    case AST_EXPR_FUNCTION_CALL:
        result = build_function_call(st, expr);
        if (!result)
            return NULL;
        break;
    case AST_EXPR_BRACE_INIT:
        result = build_struct_init(st, types->type, &expr->data.call, expr->location);
        break;
    case AST_EXPR_GET_FIELD:
        temp = build_expression(st, expr->data.field.obj);
        result = build_struct_field(st, temp, expr->data.field.fieldname, expr->location);
        break;
    case AST_EXPR_DEREF_AND_GET_FIELD:
    case AST_EXPR_INDEXING:
        /*
        To evaluate foo->bar, we first evaluate &foo->bar and then dereference.
        We can similarly evaluate &foo[bar].

        This technique cannot be used with all expressions. For example, &(1+2)
        doesn't work, and &foo.bar doesn't work either whenever &foo doesn't work.
        But &foo->bar and &foo[bar] always work, because foo is already a pointer
        and we only add a memory offset to it.
        */
        temp = build_address_of_expression(st, expr);
        result = add_variable(st, types->type);
        add_unary_op(st, expr->location, CF_PTR_LOAD, temp, result);
        break;
    case AST_EXPR_ADDRESS_OF:
        result = build_address_of_expression(st, &expr->data.operands[0]);
        break;
    case AST_EXPR_GET_VARIABLE:
        result = find_variable(st, expr->data.varname);
        assert(result);
        if (types->type_after_cast == NULL || types->type == types->type_after_cast) {
            // Must take a "snapshot" of this variable, as it may change soon.
            temp = result;
            result = add_variable(st, temp->type);
            add_unary_op(st, expr->location, CF_VARCPY, temp, result);
        }
        break;
    case AST_EXPR_DEREFERENCE:
        temp = build_expression(st, &expr->data.operands[0]);
        result = add_variable(st, types->type);
        add_unary_op(st, expr->location, CF_PTR_LOAD, temp, result);
        break;
    case AST_EXPR_CONSTANT:
        result = add_variable(st, types->type);
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
        result = add_variable(st, boolType);
        add_unary_op(st, expr->location, CF_BOOL_NEGATE, temp, result);
        break;
    case AST_EXPR_NEG:
        temp = build_expression(st, &expr->data.operands[0]);
        const Variable *zero = add_variable(st, temp->type);
        result = add_variable(st, temp->type);
        add_constant(st, expr->location, int_constant(temp->type, 0), zero);
        add_binary_op(st, expr->location, CF_INT_SUB, zero, temp, result);
        break;
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
        {
            // Refactoring note: Make sure to evaluate lhs first. C doesn't guarantee evaluation
            // order of function arguments.
            const Variable *lhs = build_expression(st, &expr->data.operands[0]);
            const Variable *rhs = build_expression(st, &expr->data.operands[1]);
            result = build_binop(st, expr->kind, expr->location, lhs, rhs, types->type);
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
        result = build_cast(st, temp, types->type, expr->location);
        break;
    }

    assert(types);
    assert(result->type == types->type);
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
            if (targetexpr->kind == AST_EXPR_GET_VARIABLE) {
                // avoid pointers to help simplify_cfg
                const Variable *target = find_variable(st, targetexpr->data.varname);
                const Variable *value = build_expression(st, valueexpr);
                add_unary_op(st, stmt->location, CF_VARCPY, value, target);
            } else {
<<<<<<< HEAD
                const Variable *target = build_address_of_expression(st, targetexpr);
                const Variable *value = build_expression(st, valueexpr);
                assert(target->type->kind == TYPE_POINTER);
=======
                // Convert value to the type of an existing variable or other assignment target.
                char errmsg[500];
                switch(targetexpr->kind) {
                    case AST_EXPR_GET_VARIABLE:
                        strcpy(errmsg, "cannot assign a value of type FROM to variable of type TO");
                        break;
                    case AST_EXPR_DEREFERENCE:
                    case AST_EXPR_INDEXING:  // TODO: test this error
                        strcpy(errmsg, "cannot assign a value of type FROM into a pointer of type TO*");
                        break;
                    case AST_EXPR_GET_FIELD:
                    case AST_EXPR_DEREF_AND_GET_FIELD:
                        snprintf(
                            errmsg, sizeof errmsg,
                            "cannot assign a value of type FROM into field '%s' of type TO",
                            targetexpr->data.field.fieldname);
                        break;
                    default: assert(0);
                }

                // TODO: is this evaluation order good?
                const CfVariable *target = build_address_of_expression(st, targetexpr, true);
                assert(target->type.kind == TYPE_POINTER);
                const CfVariable *value = build_expression(
                    st, valueexpr, target->type.data.valuetype, errmsg, true);
>>>>>>> array
                add_binary_op(st, stmt->location, CF_PTR_STORE, target, value, NULL);
            }
            break;
        }

    case AST_STMT_RETURN_VALUE:
    {
        const Variable *retvalue = build_expression(st, &stmt->data.expression);
        const Variable *retvariable = find_variable(st, "return");
        assert(retvariable);
        add_unary_op(st, stmt->location, CF_VARCPY, retvalue, retvariable);
    }
    __attribute__((fallthrough));
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

static CfGraph *build_function(struct State *st, const AstBody *body)
{
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

    for (Variable **v = st->typectx->variables.ptr; v < End(st->typectx->variables); v++)
        Append(&st->cfg->variables, *v);

    reset_type_context(st->typectx);
    return st->cfg;
}

// TODO: passing a type context here doesn't really make sense.
// It would be better to pass only the public symbols that have been imported.
CfGraphFile build_control_flow_graphs(AstToplevelNode *ast, TypeContext *typectx)
{
    // imports are guaranteed to be in the beginning and we no longer need them
    while (ast->kind == AST_TOPLEVEL_IMPORT)
        ast++;

    CfGraphFile result = { .filename = ast->location.filename };
    struct State st = { .typectx = typectx };

    int n = 0;
    while (ast[n].kind!=AST_TOPLEVEL_END_OF_FILE) n++;
    result.graphs = malloc(sizeof(result.graphs[0]) * n);  // NOLINT
    result.signatures = malloc(sizeof(result.signatures[0]) * n);

    Signature sig;

    while (ast->kind != AST_TOPLEVEL_END_OF_FILE) {
        switch(ast->kind) {
        case AST_TOPLEVEL_IMPORT:
        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        case AST_TOPLEVEL_DECLARE_FUNCTION:
            sig = typecheck_function(typectx, ast->location, &ast->data.decl_signature, NULL);
            result.signatures[result.nfuncs] = sig;
            result.graphs[result.nfuncs] = NULL;
            result.nfuncs++;
            break;
        case AST_TOPLEVEL_DEFINE_FUNCTION:
            sig = typecheck_function(typectx, ast->location, &ast->data.funcdef.signature, &ast->data.funcdef.body);
            result.signatures[result.nfuncs] = sig;
            result.graphs[result.nfuncs] = build_function(&st, &ast->data.funcdef.body);
            result.nfuncs++;
            break;
        case AST_TOPLEVEL_DEFINE_STRUCT:
            typecheck_struct(typectx, &ast->data.structdef, ast->location);
            break;
        }
        ast++;
    }

    free(st.breakstack.ptr);
    free(st.continuestack.ptr);
    return result;
}
