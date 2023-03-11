#include "jou_compiler.h"

struct State {
    const FileTypes *filetypes;
    const FunctionOrMethodTypes *fomtypes;
    CfGraph *cfg;
    CfBlock *current_block;
    List(CfBlock *) breakstack;
    List(CfBlock *) continuestack;
};

static const LocalVariable *find_local_var(const struct State *st, const char *name)
{
    for (LocalVariable **var = st->cfg->locals.ptr; var < End(st->cfg->locals); var++)
        if (!strcmp((*var)->name, name))
            return *var;
    return NULL;
}

static LocalVariable *add_local_var(struct State *st, const Type *t)
{
    LocalVariable *var = calloc(1, sizeof *var);
    var->id = st->cfg->locals.len;
    var->type = t;
    Append(&st->cfg->locals, var);
    return var;
}

static const ExpressionTypes *get_expr_types(const struct State *st, const AstExpression *expr)
{
    // TODO: a fancy binary search algorithm (need to add sorting)
    assert(st->fomtypes);
    for (int i = 0; i < st->fomtypes->expr_types.len; i++)
        if (st->fomtypes->expr_types.ptr[i]->expr == expr)
            return st->fomtypes->expr_types.ptr[i];
    return NULL;
}

static CfBlock *add_block(const struct State *st)
{
    CfBlock *block = calloc(1, sizeof *block);
    Append(&st->cfg->all_blocks, block);
    return block;
}

static void add_jump(
    struct State *st,
    const LocalVariable *branchvar,
    CfBlock *iftrue,
    CfBlock *iffalse,
    CfBlock *new_current_block)
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
    const LocalVariable **operands, // NULL terminated, or NULL for empty
    const LocalVariable *destvar)
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
    add_instruction((st), (loc), (op), NULL, (const LocalVariable*[]){(arg),NULL}, (target))
#define add_binary_op(st, loc, op, lhs, rhs, target) \
    add_instruction((st), (loc), (op), NULL, (const LocalVariable*[]){(lhs),(rhs),NULL}, (target))
#define add_constant(st, loc, c, target) \
    add_instruction((st), (loc), CF_CONSTANT, &(union CfInstructionData){ .constant=copy_constant((Constant[]){c}) }, NULL, (target))


static const LocalVariable *build_bool_to_int_conversion(
    struct State *st, const LocalVariable *boolvar, const Location location, const Type *t)
{
    assert(is_integer_type(t));
    LocalVariable *result = add_local_var(st, t);

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

static const LocalVariable *build_cast(
    struct State *st, const LocalVariable *obj, const Type *to, Location location)
{
    if (obj->type == to)
        return obj;

    if (is_pointer_type(obj->type) && is_pointer_type(to)) {
        const LocalVariable *result = add_local_var(st, to);
        add_unary_op(st, location, CF_PTR_CAST, obj, result);
        return result;
    }

    if (is_number_type(obj->type) && is_number_type(to)) {
        const LocalVariable *result = add_local_var(st, to);
        add_unary_op(st, location, CF_NUM_CAST, obj, result);
        return result;
    }

    if (is_integer_type(obj->type) || to->kind == TYPE_ENUM) {
        const LocalVariable *i32var = add_local_var(st, intType);
        const LocalVariable *result = add_local_var(st, to);
        add_unary_op(st, location, CF_NUM_CAST, obj, i32var);
        add_unary_op(st, location, CF_INT32_TO_ENUM, i32var, result);
        return result;
    }

    if (obj->type->kind == TYPE_ENUM && is_integer_type(to)) {
        const LocalVariable *i32var = add_local_var(st, intType);
        const LocalVariable *result = add_local_var(st, to);
        add_unary_op(st, location, CF_ENUM_TO_INT32, obj, i32var);
        add_unary_op(st, location, CF_NUM_CAST, i32var, result);
        return result;
    }

    if (obj->type == boolType && is_integer_type(to))
        return build_bool_to_int_conversion(st, obj, location, to);

    assert(0);
}

static const LocalVariable *build_binop(
    struct State *st,
    enum AstExpressionKind op,
    Location location,
    const LocalVariable *lhs,
    const LocalVariable *rhs,
    const Type *result_type)
{
    bool got_numbers = is_number_type(lhs->type) && is_number_type(rhs->type);
    bool got_pointers = is_pointer_type(lhs->type) && is_pointer_type(rhs->type);
    assert(got_numbers || got_pointers);

    enum CfInstructionKind k;
    bool negate = false;
    bool swap = false;

    switch(op) {
        case AST_EXPR_ADD: k = CF_NUM_ADD; break;
        case AST_EXPR_SUB: k = CF_NUM_SUB; break;
        case AST_EXPR_MUL: k = CF_NUM_MUL; break;
        case AST_EXPR_DIV: k = CF_NUM_DIV; break;
        case AST_EXPR_MOD: k = CF_NUM_MOD; break;
        case AST_EXPR_EQ: k = got_pointers?CF_PTR_EQ:CF_NUM_EQ; break;
        case AST_EXPR_NE: k = got_pointers?CF_PTR_EQ:CF_NUM_EQ; negate=true; break;
        case AST_EXPR_LT: k = CF_NUM_LT; break;
        case AST_EXPR_GT: k = CF_NUM_LT; swap=true; break;
        case AST_EXPR_LE: k = CF_NUM_LT; negate=true; swap=true; break;
        case AST_EXPR_GE: k = CF_NUM_LT; negate=true; break;
        default: assert(0);
    }

    const LocalVariable *destvar = add_local_var(st, result_type);
    add_binary_op(st, location, k, swap?rhs:lhs, swap?lhs:rhs, destvar);

    if (!negate)
        return destvar;

    const LocalVariable *negated = add_local_var(st, boolType);
    add_unary_op(st, location, CF_BOOL_NEGATE, destvar, negated);
    return negated;
}

static const LocalVariable *build_class_field_pointer(
    struct State *st, const LocalVariable *instance, const char *fieldname, Location location)
{
    assert(instance->type->kind == TYPE_POINTER);
    assert(instance->type->data.valuetype->kind == TYPE_CLASS);
    const Type *classtype = instance->type->data.valuetype;

    for (struct ClassField *f = classtype->data.classdata.fields.ptr; f < End(classtype->data.classdata.fields); f++) {
        if (!strcmp(f->name, fieldname)) {
            union CfInstructionData dat;
            safe_strcpy(dat.fieldname, f->name);

            LocalVariable* result = add_local_var(st, get_pointer_type(f->type));
            add_instruction(st, location, CF_PTR_CLASS_FIELD, &dat, (const LocalVariable*[]){instance,NULL}, result);
            return result;
        }
    }

    assert(0);
}

static const LocalVariable *build_class_field(
    struct State *st, const LocalVariable *instance, const char *fieldname, Location location)
{
    const LocalVariable *ptr = add_local_var(st, get_pointer_type(instance->type));
    add_unary_op(st, location, CF_ADDRESS_OF_LOCAL_VAR, instance, ptr);
    const LocalVariable *field_ptr = build_class_field_pointer(st, ptr, fieldname, location);
    const LocalVariable *field = add_local_var(st, field_ptr->type->data.valuetype);
    add_unary_op(st, location, CF_PTR_LOAD, field_ptr, field);
    return field;
}

static const LocalVariable *build_expression(struct State *st, const AstExpression *expr);
static const LocalVariable *build_address_of_expression(struct State *st, const AstExpression *address_of_what);

enum PreOrPost { PRE, POST };

static const LocalVariable *build_increment_or_decrement(
    struct State *st,
    Location location,
    const AstExpression *inner,
    enum PreOrPost pop,
    int diff)
{
    assert(diff==1 || diff==-1);  // 1=increment, -1=decrement

    const LocalVariable *addr = build_address_of_expression(st, inner);
    assert(addr->type->kind == TYPE_POINTER);
    const Type *t = addr->type->data.valuetype;
    if (!is_integer_type(t) && !is_pointer_type(t))
        fail_with_error(location, "cannot %s a value of type %s", diff==1?"increment":"decrement", t->name);

    const LocalVariable *old_value = add_local_var(st, t);
    const LocalVariable *new_value = add_local_var(st, t);
    const LocalVariable *diffvar = add_local_var(st, is_integer_type(t) ? t : intType);

    add_constant(st, location, int_constant(diffvar->type, diff), diffvar);
    add_unary_op(st, location, CF_PTR_LOAD, addr, old_value);
    add_binary_op(st, location, is_number_type(t)?CF_NUM_ADD:CF_PTR_ADD_INT, old_value, diffvar, new_value);
    add_binary_op(st, location, CF_PTR_STORE, addr, new_value, NULL);

    switch(pop) {
        case PRE: return new_value;
        case POST: return old_value;
    }
    assert(0);
}

enum AndOr { AND, OR };

static const LocalVariable *build_and_or(
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
    const LocalVariable *lhs = build_expression(st, lhsexpr);
    const LocalVariable *rhs;
    const LocalVariable *result = add_local_var(st, boolType);
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

static const LocalVariable *build_address_of_expression(struct State *st, const AstExpression *address_of_what)
{
    switch(address_of_what->kind) {
    case AST_EXPR_GET_VARIABLE:
    {
        const Type *ptrtype = get_pointer_type(get_expr_types(st, address_of_what)->type);
        const LocalVariable *addr = add_local_var(st, ptrtype);

        const LocalVariable *local_var = find_local_var(st, address_of_what->data.varname);
        if (local_var) {
            add_unary_op(st, address_of_what->location, CF_ADDRESS_OF_LOCAL_VAR, local_var, addr);
        } else {
            // Global variable (possibly imported from another file)
            union CfInstructionData data;
            safe_strcpy(data.globalname, address_of_what->data.varname);
            add_instruction(st, address_of_what->location, CF_ADDRESS_OF_GLOBAL_VAR, &data, NULL, addr);
        }
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
        const LocalVariable *obj = build_expression(st, address_of_what->data.classfield.obj);
        assert(obj->type->kind == TYPE_POINTER);
        assert(obj->type->data.valuetype->kind == TYPE_CLASS);
        return build_class_field_pointer(st, obj, address_of_what->data.classfield.fieldname, address_of_what->location);
    }
    case AST_EXPR_GET_FIELD:
    {
        // &obj.field aka &(obj.field), evaluate as &(&obj)->field
        const LocalVariable *obj = build_address_of_expression(st, address_of_what->data.classfield.obj);
        assert(obj->type->kind == TYPE_POINTER);
        return build_class_field_pointer(st, obj, address_of_what->data.classfield.fieldname, address_of_what->location);
    }
    case AST_EXPR_INDEXING:
    {
        const LocalVariable *ptr, *arrptr;

        // &pointer[index] = pointer + offset
        // &array[index] = cast(&array) + offset
        const Type *indexed_type = get_expr_types(st, &address_of_what->data.operands[0])->type;
        switch(indexed_type->kind) {
        case TYPE_POINTER:
            ptr = build_expression(st, &address_of_what->data.operands[0]);
            break;

        case TYPE_ARRAY:
            {
                arrptr = build_address_of_expression(st, &address_of_what->data.operands[0]);
                ptr = add_local_var(st, get_pointer_type(indexed_type->data.array.membertype));
                add_unary_op(st, address_of_what->location, CF_PTR_CAST, arrptr, ptr);
            }
            break;

        default:
            assert(0);
        }

        const LocalVariable *index = build_expression(st, &address_of_what->data.operands[1]);
        assert(is_integer_type(index->type));

        assert(ptr->type->kind == TYPE_POINTER);
        const LocalVariable *result = add_local_var(st, ptr->type);
        add_binary_op(st, address_of_what->location, CF_PTR_ADD_INT, ptr, index, result);
        return result;
    }

    default:
        assert(0);
        break;
    }

    assert(0);
}

static const LocalVariable *build_function_or_method_call(struct State *st, const Location location, const AstCall *call, const LocalVariable *self, const Type *returntype)
{
    if(self) {
        assert(self->type->kind == TYPE_POINTER);
        assert(self->type->data.valuetype->kind == TYPE_CLASS);
    }

    const LocalVariable **args = calloc(call->nargs + 2, sizeof(args[0]));  // NOLINT
    int k = 0;
    if (self)
        args[k++] = self;
    for (int i = 0; i < call->nargs; i++)
        args[k++] = build_expression(st, &call->args[i]);

    const LocalVariable *return_value;
    if (returntype)
        return_value = add_local_var(st, returntype);
    else
        return_value = NULL;

    const Signature *sig = NULL;
    if(self) {
        assert(self->type->kind == TYPE_POINTER);
        const Type *selfclass = self->type->data.valuetype;
        assert(selfclass->kind == TYPE_CLASS);
        for (const Signature *s = selfclass->data.classdata.methods.ptr; s < End(selfclass->data.classdata.methods); s++) {
            assert(get_self_class(s) == selfclass);
            if (!strcmp(s->name, call->calledname)) {
                sig = s;
                break;
            }
        }
    } else {
        for (const struct SignatureAndUsedPtr *f = st->filetypes->functions.ptr; f < End(st->filetypes->functions); f++) {
            if (!strcmp(f->signature.name, call->calledname)) {
                sig = &f->signature;
                break;
            }
        }
    }
    assert(sig);

    union CfInstructionData data = { .signature = copy_signature(sig) };
    add_instruction(st, location, CF_CALL, &data, args, return_value);

    free(args);
    return return_value;
}

static const LocalVariable *build_struct_init(struct State *st, const Type *type, const AstCall *call, Location location)
{
    const LocalVariable *instance = add_local_var(st, type);
    const LocalVariable *instanceptr = add_local_var(st, get_pointer_type(type));

    add_unary_op(st, location, CF_ADDRESS_OF_LOCAL_VAR, instance, instanceptr);
    add_unary_op(st, location, CF_PTR_MEMSET_TO_ZERO, instanceptr, NULL);

    for (int i = 0; i < call->nargs; i++) {
        const LocalVariable *fieldptr = build_class_field_pointer(st, instanceptr, call->argnames[i], call->args[i].location);
        const LocalVariable *fieldval = build_expression(st, &call->args[i]);
        add_binary_op(st, location, CF_PTR_STORE, fieldptr, fieldval, NULL);
    }

    return instance;
}

static const LocalVariable *build_array(struct State *st, const Type *type, const AstExpression *items, Location location)
{
    assert(type->kind == TYPE_ARRAY);

    const LocalVariable *arr = add_local_var(st, type);
    const LocalVariable *arrptr = add_local_var(st, get_pointer_type(type));
    add_unary_op(st, location, CF_ADDRESS_OF_LOCAL_VAR, arr, arrptr);
    const LocalVariable *first_item_ptr = add_local_var(st, get_pointer_type(type->data.array.membertype));
    add_unary_op(st, location, CF_PTR_CAST, arrptr, first_item_ptr);

    for (int i = 0; i < type->data.array.len; i++) {
        const LocalVariable *value = build_expression(st, &items[i]);

        const LocalVariable *ivar = add_local_var(st, intType);
        add_constant(st, location, int_constant(intType, i), ivar);

        const LocalVariable *destptr = add_local_var(st, first_item_ptr->type);
        add_binary_op(st, location, CF_PTR_ADD_INT, first_item_ptr, ivar, destptr);
        add_binary_op(st, location, CF_PTR_STORE, destptr, value, NULL);
    }

    return arr;
}

static int find_enum_member(const Type *enumtype, const char *name)
{
    for (int i = 0; i < enumtype->data.enummembers.count; i++)
        if (!strcmp(enumtype->data.enummembers.names[i], name))
            return i;
    assert(0);
}

static const LocalVariable *build_expression(struct State *st, const AstExpression *expr)
{
    const ExpressionTypes *types = get_expr_types(st, expr);

    const LocalVariable *result, *temp;

    switch(expr->kind) {
    case AST_EXPR_DEREF_AND_CALL_METHOD:
        temp = build_expression(st, expr->data.methodcall.obj);
        assert(temp);
        result = build_function_or_method_call(st, expr->location, &expr->data.methodcall.call, temp, types ? types->type : NULL);
        if (!result)
            return NULL;
        break;
    case AST_EXPR_CALL_METHOD:
        temp = build_address_of_expression(st, expr->data.methodcall.obj);
        assert(temp);
        result = build_function_or_method_call(st, expr->location, &expr->data.methodcall.call, temp, types ? types->type : NULL);
        if (!result)
            return NULL;
        break;
    case AST_EXPR_FUNCTION_CALL:
        result = build_function_or_method_call(st, expr->location, &expr->data.call, NULL, types ? types->type : NULL);
        if (!result)
            return NULL;
        break;
    case AST_EXPR_BRACE_INIT:
        result = build_struct_init(st, types->type, &expr->data.call, expr->location);
        break;
    case AST_EXPR_ARRAY:
        assert(types->type->kind == TYPE_ARRAY);
        assert(types->type->data.array.len == expr->data.array.count);
        result = build_array(st, types->type, expr->data.array.items, expr->location);
        break;
    case AST_EXPR_GET_FIELD:
        temp = build_expression(st, expr->data.classfield.obj);
        result = build_class_field(st, temp, expr->data.classfield.fieldname, expr->location);
        break;
    case AST_EXPR_GET_ENUM_MEMBER:
        result = add_local_var(st, types->type);
        Constant c = { CONSTANT_ENUM_MEMBER, {
            .enum_member.enumtype = types->type,
            .enum_member.memberidx = find_enum_member(types->type, expr->data.enummember.membername),
        }};
        add_constant(st, expr->location, c, result);
        break;
    case AST_EXPR_GET_VARIABLE:
        if ((temp = find_local_var(st, expr->data.varname))) {
            if (types->type_after_cast == NULL || types->type == types->type_after_cast) {
                // Must take a "snapshot" of this variable, as it may change soon.
                result = add_local_var(st, temp->type);
                add_unary_op(st, expr->location, CF_VARCPY, temp, result);
            } else {
                result = temp;
            }
            break;
        }
        // For other than local variables we can evaluate as &*variable.
        // Would also work for locals, but it would confuse simplify_cfg.
        __attribute__((fallthrough));
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
        result = add_local_var(st, types->type);
        add_unary_op(st, expr->location, CF_PTR_LOAD, temp, result);
        break;
    case AST_EXPR_ADDRESS_OF:
        result = build_address_of_expression(st, &expr->data.operands[0]);
        break;
    case AST_EXPR_SIZEOF:
        result = add_local_var(st, sizeType);
        union CfInstructionData data = { .type = get_expr_types(st, &expr->data.operands[0])->type };
        add_instruction(st, expr->location, CF_SIZEOF, &data, NULL, result);
        break;
    case AST_EXPR_DEREFERENCE:
        temp = build_expression(st, &expr->data.operands[0]);
        result = add_local_var(st, types->type);
        add_unary_op(st, expr->location, CF_PTR_LOAD, temp, result);
        break;
    case AST_EXPR_CONSTANT:
        result = add_local_var(st, types->type);
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
        result = add_local_var(st, boolType);
        add_unary_op(st, expr->location, CF_BOOL_NEGATE, temp, result);
        break;
    case AST_EXPR_NEG:
        temp = build_expression(st, &expr->data.operands[0]);
        const LocalVariable *zero = add_local_var(st, temp->type);
        result = add_local_var(st, temp->type);
        if (temp->type == doubleType)
            add_constant(st, expr->location, ((Constant){ CONSTANT_DOUBLE, {.double_or_float_text="0"} }), zero);
        else if (temp->type == floatType)
            add_constant(st, expr->location, ((Constant){ CONSTANT_FLOAT, {.double_or_float_text="0"}}), zero);
        else
            add_constant(st, expr->location, int_constant(temp->type, 0), zero);
        add_binary_op(st, expr->location, CF_NUM_SUB, zero, temp, result);
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
            const LocalVariable *lhs = build_expression(st, &expr->data.operands[0]);
            const LocalVariable *rhs = build_expression(st, &expr->data.operands[1]);
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
        const LocalVariable *cond = build_expression(
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
    const LocalVariable *condvar = build_expression(st, cond);
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
            const LocalVariable *target;

            if (targetexpr->kind == AST_EXPR_GET_VARIABLE
                && (target = find_local_var(st, targetexpr->data.varname)))
            {
                // avoid pointers to help simplify_cfg
                const LocalVariable *value = build_expression(st, valueexpr);
                add_unary_op(st, stmt->location, CF_VARCPY, value, target);
            } else {
                // TODO: is this evaluation order good?
                target = build_address_of_expression(st, targetexpr);
                const LocalVariable *value = build_expression(st, valueexpr);
                assert(target->type->kind == TYPE_POINTER);
                add_binary_op(st, stmt->location, CF_PTR_STORE, target, value, NULL);
            }
            break;
        }

    case AST_STMT_INPLACE_ADD:
    case AST_STMT_INPLACE_SUB:
    case AST_STMT_INPLACE_MUL:
    case AST_STMT_INPLACE_DIV:
    case AST_STMT_INPLACE_MOD:
    {
        const AstExpression *targetexpr = &stmt->data.assignment.target;
        const AstExpression *rhsexpr = &stmt->data.assignment.value;

        const LocalVariable *targetptr = build_address_of_expression(st, targetexpr);
        const LocalVariable *rhs = build_expression(st, rhsexpr);
        assert(targetptr->type->kind == TYPE_POINTER);
        const LocalVariable *oldvalue = add_local_var(st, targetptr->type->data.valuetype);
        add_unary_op(st, stmt->location, CF_PTR_LOAD, targetptr, oldvalue);
        enum AstExpressionKind op;
        switch(stmt->kind){
            case AST_STMT_INPLACE_ADD: op=AST_EXPR_ADD; break;
            case AST_STMT_INPLACE_SUB: op=AST_EXPR_SUB; break;
            case AST_STMT_INPLACE_MUL: op=AST_EXPR_MUL; break;
            case AST_STMT_INPLACE_DIV: op=AST_EXPR_DIV; break;
            case AST_STMT_INPLACE_MOD: op=AST_EXPR_MOD; break;
            default: assert(0);
        }
        const LocalVariable *newvalue = build_binop(st, op, stmt->location, oldvalue, rhs, targetptr->type->data.valuetype);
        add_binary_op(st, stmt->location, CF_PTR_STORE, targetptr, newvalue, NULL);
        break;
    }

    case AST_STMT_RETURN_VALUE:
    {
        const LocalVariable *retvalue = build_expression(st, &stmt->data.expression);
        const LocalVariable *retvariable = find_local_var(st, "return");
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
        if (stmt->data.vardecl.value) {
            const LocalVariable *v = find_local_var(st, stmt->data.vardecl.name);
            assert(v);
            const LocalVariable *cfvar = build_expression(st, stmt->data.vardecl.value);
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

static CfGraph *build_function_or_method(struct State *st, const Type *selfclass, const char *name, const AstBody *body)
{
    assert(!st->fomtypes);
    assert(!st->cfg);

    for (const FunctionOrMethodTypes *f = st->filetypes->fomtypes.ptr; f < End(st->filetypes->fomtypes); f++) {
        if (!strcmp(f->signature.name, name) && get_self_class(&f->signature) == selfclass) {
            st->fomtypes = f;
            break;
        }
    }
    assert(st->fomtypes);

    st->cfg = calloc(1, sizeof *st->cfg);
    st->cfg->signature = copy_signature(&st->fomtypes->signature);
    for (LocalVariable **v = st->fomtypes->locals.ptr; v < End(st->fomtypes->locals); v++)
        Append(&st->cfg->locals, *v);
    Append(&st->cfg->all_blocks, &st->cfg->start_block);
    Append(&st->cfg->all_blocks, &st->cfg->end_block);
    st->current_block = &st->cfg->start_block;

    assert(st->breakstack.len == 0 && st->continuestack.len == 0);
    build_body(st, body);
    assert(st->breakstack.len == 0 && st->continuestack.len == 0);

    // Implicit return at the end of the function
    st->current_block->iftrue = &st->cfg->end_block;
    st->current_block->iffalse = &st->cfg->end_block;

    CfGraph *cfg = st->cfg;
    st->fomtypes = NULL;
    st->cfg = NULL;
    return cfg;
}

// TODO: passing a type context here doesn't really make sense.
// It would be better to pass only the public symbols that have been imported.
CfGraphFile build_control_flow_graphs(AstToplevelNode *ast, FileTypes *filetypes)
{
    CfGraphFile result = { .filename = ast->location.filename };
    struct State st = { .filetypes = filetypes };

    while (ast->kind != AST_TOPLEVEL_END_OF_FILE) {
        if(ast->kind == AST_TOPLEVEL_DEFINE_FUNCTION) {
            CfGraph *g = build_function_or_method(&st, NULL, ast->data.funcdef.signature.name, &ast->data.funcdef.body);
            Append(&result.graphs, g);
        }

        if (ast->kind == AST_TOPLEVEL_DEFINE_CLASS) {
            Type *classtype = NULL;
            for (Type **t = filetypes->owned_types.ptr; t < End(filetypes->owned_types); t++) {
                if (!strcmp((*t)->name, ast->data.classdef.name)) {
                    classtype = *t;
                    break;
                }
            }
            assert(classtype);

            for (AstFunctionDef *m = ast->data.classdef.methods.ptr; m < End(ast->data.classdef.methods); m++) {
                CfGraph *g = build_function_or_method(&st, classtype, m->signature.name, &m->body);
                Append(&result.graphs, g);
            }
        }
        ast++;
    }

    free(st.breakstack.ptr);
    free(st.continuestack.ptr);
    return result;
}
