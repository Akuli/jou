#include "jou_compiler.h"

static const LocalVariable *find_local_var(const TypeContext *ctx, const char *name)
{
    for (LocalVariable **var = ctx->locals.ptr; var < End(ctx->locals); var++)
        if (!strcmp((*var)->name, name))
            return *var;
    return NULL;
}

static const Type *find_any_var(const TypeContext *ctx, const char *name)
{
    for (LocalVariable **var = ctx->locals.ptr; var < End(ctx->locals); var++)
        if (!strcmp((*var)->name, name))
            return (*var)->type;

    for (GlobalVariable **var = ctx->globals.ptr; var < End(ctx->globals); var++)
        if (!strcmp((*var)->name, name))
            return (*var)->type;

    for (const ExportSymbol **es = ctx->imports.ptr; es < End(ctx->imports); es++)
        if ((*es)->kind == EXPSYM_GLOBAL_VAR && !strcmp((*es)->name, name))
            return (*es)->data.type;

    return NULL;
}

static LocalVariable *add_variable(TypeContext *ctx, const Type *t, const char *name)
{
    LocalVariable *var = calloc(1, sizeof *var);
    var->id = ctx->locals.len;
    var->type = t;

    assert(name);
    assert(!find_local_var(ctx, name));
    assert(strlen(name) < sizeof var->name);
    strcpy(var->name, name);

    Append(&ctx->locals, var);
    return var;
}

static const Signature *find_function(const TypeContext *ctx, const char *name)
{
    for (Signature *sig = ctx->function_signatures.ptr; sig < End(ctx->function_signatures); sig++)
        if (!strcmp(sig->funcname, name))
            return sig;

    for (const ExportSymbol **es = ctx->imports.ptr; es < End(ctx->imports); es++)
        if ((*es)->kind == EXPSYM_FUNCTION && !strcmp((*es)->name, name))
            return &(*es)->data.funcsignature;

    return NULL;
}

static const Type *find_type(const TypeContext *ctx, const char *name)
{
    for (Type **t = ctx->structs.ptr; t < End(ctx->structs); t++)
        if (!strcmp((*t)->name, name))
            return *t;

    for (const ExportSymbol **es = ctx->imports.ptr; es < End(ctx->imports); es++)
        if ((*es)->kind == EXPSYM_TYPE && !strcmp((*es)->name, name))
            return (*es)->data.type;

    return NULL;
}

int evaluate_array_length(const AstExpression *expr)
{
    if (expr->kind == AST_EXPR_CONSTANT
        && expr->data.constant.kind == CONSTANT_INTEGER
        && expr->data.constant.data.integer.is_signed
        && expr->data.constant.data.integer.width_in_bits == 32)
    {
        return (int)expr->data.constant.data.integer.value;
    }

    fail_with_error(expr->location, "cannot evaluate array length at compile time");
}

// NULL return value means it is void
static const Type *type_or_void_from_ast(const TypeContext *ctx, const AstType *asttype);

static const Type *type_from_ast(const TypeContext *ctx, const AstType *asttype)
{
    const Type *t = type_or_void_from_ast(ctx, asttype);
    if (!t)
        fail_with_error(asttype->location, "'void' cannot be used here because it is not a type");
    return t;
}

static const Type *type_or_void_from_ast(const TypeContext *ctx, const AstType *asttype)
{
    const Type *tmp;

    switch(asttype->kind) {
    case AST_TYPE_NAMED:
        if (!strcmp(asttype->data.name, "int"))
            return intType;
        if (!strcmp(asttype->data.name, "long"))
            return longType;
        if (!strcmp(asttype->data.name, "byte"))
            return byteType;
        if (!strcmp(asttype->data.name, "bool"))
            return boolType;
        if (!strcmp(asttype->data.name, "float"))
            return floatType;
        if (!strcmp(asttype->data.name, "double"))
            return doubleType;
        if (!strcmp(asttype->data.name, "void"))
            return NULL;
        if ((tmp = find_type(ctx, asttype->data.name)))
            return tmp;
        fail_with_error(asttype->location, "there is no type named '%s'", asttype->data.name);

    case AST_TYPE_POINTER:
        tmp = type_or_void_from_ast(ctx, asttype->data.valuetype);
        if (tmp)
            return get_pointer_type(tmp);
        else
            return voidPtrType;

    case AST_TYPE_ARRAY:
        tmp = type_from_ast(ctx, asttype->data.valuetype);
        int len = evaluate_array_length(asttype->data.array.len);
        if (len <= 0)
            fail_with_error(asttype->data.array.len->location, "array length must be positive");
        return get_array_type(tmp, len);
    }
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

static void do_implicit_cast(
    ExpressionTypes *types, const Type *to, Location location, const char *errormsg_template)
{
    const Type *from = types->type;
    if (from == to)
        return;

    bool can_cast =
        errormsg_template == NULL   // This can be used to "force" a cast to happen.
        || (
            // Cast to bigger integer types implicitly, unless it is signed-->unsigned.
            is_integer_type(from)
            && is_integer_type(to)
            && from->data.width_in_bits < to->data.width_in_bits
            && !(from->kind == TYPE_SIGNED_INTEGER && to->kind == TYPE_UNSIGNED_INTEGER)
        ) || (
            is_integer_type(from) && to == floatType
        ) || (
            // Cast from any integer type to double.
            is_integer_type(from) && to == doubleType
        ) || (
            // Cast implicitly between void pointer and any other pointer.
            (from->kind == TYPE_POINTER && to->kind == TYPE_VOID_POINTER)
            || (from->kind == TYPE_VOID_POINTER && to->kind == TYPE_POINTER)
        );

    if (!can_cast)
        fail_with_implicit_cast_error(location, errormsg_template, from, to);

    assert(!types->type_after_cast);
    types->type_after_cast = to;
}

static void check_explicit_cast(const Type *from, const Type *to, Location location)
{
    if (
        from != to  // TODO: should probably be error if it's the same type.
        && !(is_pointer_type(from) && is_pointer_type(to))
        && !(is_integer_type(from) && is_integer_type(to))
        // TODO: pointer-to-int, int-to-pointer
    )
    {
        // TODO: test this error
        fail_with_error(location, "cannot cast from type %s to %s", from->name, to->name);
    }
}

static ExpressionTypes *typecheck_expression(TypeContext *ctx, const AstExpression *expr);

static ExpressionTypes *typecheck_expression_not_void(TypeContext *ctx, const AstExpression *expr)
{
    ExpressionTypes *types = typecheck_expression(ctx, expr);
    if (!types) {
        assert(expr->kind == AST_EXPR_FUNCTION_CALL);
        fail_with_error(
            expr->location, "function '%s' does not return a value", expr->data.call.calledname);
    }
    return types;
}

static void typecheck_expression_with_implicit_cast(
    TypeContext *ctx,
    const AstExpression *expr,
    const Type *casttype,
    const char *errormsg_template)
{
    ExpressionTypes *types = typecheck_expression_not_void(ctx, expr);
    do_implicit_cast(types, casttype, expr->location, errormsg_template);
}

static const Type *check_binop(
    enum AstExpressionKind op,
    Location location,
    ExpressionTypes *lhstypes,
    ExpressionTypes *rhstypes)
{
    const char *do_what;
    switch(op) {
    case AST_EXPR_ADD: do_what = "add"; break;
    case AST_EXPR_SUB: do_what = "subtract"; break;
    case AST_EXPR_MUL: do_what = "multiply"; break;
    case AST_EXPR_DIV: do_what = "divide"; break;
    case AST_EXPR_MOD: do_what = "take remainder with"; break;

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

    bool got_integers = is_integer_type(lhstypes->type) && is_integer_type(rhstypes->type);
    bool got_numbers = is_number_type(lhstypes->type) && is_number_type(rhstypes->type);
    bool got_pointers = (
        is_pointer_type(lhstypes->type)
        && is_pointer_type(rhstypes->type)
        && (
            // Ban comparisons like int* == byte*, unless one of the two types is void*
            lhstypes->type == rhstypes->type
            || lhstypes->type == voidPtrType
            || rhstypes->type == voidPtrType
        )
    );

    if (!got_integers && !got_numbers && !(got_pointers && (op == AST_EXPR_EQ || op == AST_EXPR_NE)))
        fail_with_error(location, "wrong types: cannot %s %s and %s", do_what, lhstypes->type->name, rhstypes->type->name);

    // TODO: is this a good idea?
    const Type *cast_type;
    if (got_integers) {
        cast_type = get_integer_type(
            max(lhstypes->type->data.width_in_bits, rhstypes->type->data.width_in_bits),
            lhstypes->type->kind == TYPE_SIGNED_INTEGER || rhstypes->type->kind == TYPE_SIGNED_INTEGER
        );
    }
    if (got_pointers) {
        cast_type = voidPtrType;
    }
    if (got_numbers && !got_integers) {
        cast_type = (lhstypes->type == doubleType || rhstypes->type == doubleType) ? doubleType : floatType;
    }

    do_implicit_cast(lhstypes, cast_type, (Location){0}, NULL);
    do_implicit_cast(rhstypes, cast_type, (Location){0}, NULL);

    switch(op) {
        case AST_EXPR_ADD:
        case AST_EXPR_SUB:
        case AST_EXPR_MUL:
        case AST_EXPR_DIV:
        case AST_EXPR_MOD:
            return cast_type;
        case AST_EXPR_EQ:
        case AST_EXPR_NE:
        case AST_EXPR_GT:
        case AST_EXPR_GE:
        case AST_EXPR_LT:
        case AST_EXPR_LE:
            return boolType;
        default:
            assert(0);
    }
}

// Intended for errors. Returned string can be overwritten in next call.
static const char *short_expression_description(const AstExpression *expr)
{
    static char result[200];

    switch(expr->kind) {
    // Imagine "cannot assign to" in front of these, e.g. "cannot assign to a constant"
    case AST_EXPR_CONSTANT: return "a constant";
    case AST_EXPR_SIZEOF: return "a sizeof expression";
    case AST_EXPR_FUNCTION_CALL: return "a function call";
    case AST_EXPR_BRACE_INIT: return "a newly created instance";
    case AST_EXPR_INDEXING: return "an indexed value";
    case AST_EXPR_AS: return "the result of a cast";
    case AST_EXPR_GET_VARIABLE: return "a variable";
    case AST_EXPR_DEREFERENCE: return "the value of a pointer";
    case AST_EXPR_AND: return "the result of 'and'";
    case AST_EXPR_OR: return "the result of 'or'";
    case AST_EXPR_NOT: return "the result of 'not'";

    case AST_EXPR_ADD:
    case AST_EXPR_SUB:
    case AST_EXPR_MUL:
    case AST_EXPR_DIV:
    case AST_EXPR_MOD:
    case AST_EXPR_NEG:
        return "the result of a calculation";

    case AST_EXPR_EQ:
    case AST_EXPR_NE:
    case AST_EXPR_GT:
    case AST_EXPR_GE:
    case AST_EXPR_LT:
    case AST_EXPR_LE:
        return "the result of a comparison";

    case AST_EXPR_PRE_INCREMENT:
    case AST_EXPR_POST_INCREMENT:
        return "the result of incrementing a value";

    case AST_EXPR_PRE_DECREMENT:
    case AST_EXPR_POST_DECREMENT:
        return "the result of decrementing a value";

    case AST_EXPR_ADDRESS_OF:
        snprintf(result, sizeof result, "address of %s", short_expression_description(&expr->data.operands[0]));
        break;

    case AST_EXPR_GET_FIELD:
    case AST_EXPR_DEREF_AND_GET_FIELD:
        snprintf(result, sizeof result, "field '%s'", expr->data.field.fieldname);
        break;
    }

    return result;
}

/*
The & operator can't go in front of most expressions.
You can't do &(1 + 2), for example.

The same rules apply to assignments: "foo = bar" is treated as setting the
value of the pointer &foo to bar.

errmsg_template can be e.g. "cannot take address of %s" or "cannot assign to %s"
*/
static void ensure_can_take_address(const AstExpression *expr, const char *errmsg_template)
{
    switch(expr->kind) {
    case AST_EXPR_GET_VARIABLE:
    case AST_EXPR_DEREFERENCE:
    case AST_EXPR_INDEXING:  // &foo[bar]
    case AST_EXPR_DEREF_AND_GET_FIELD:  // &foo->bar = foo + offset (it doesn't use &foo)
        break;
    case AST_EXPR_GET_FIELD:
        // &foo.bar = &foo + offset
        ensure_can_take_address(&expr->data.operands[0], errmsg_template);
        break;
    default:
        fail_with_error(expr->location, errmsg_template, short_expression_description(expr));
    }
}

static const Type *check_increment_or_decrement(TypeContext *ctx, const AstExpression *expr)
{
    const char *bad_type_fmt, *bad_expr_fmt;
    switch(expr->kind) {
    case AST_EXPR_PRE_INCREMENT:
    case AST_EXPR_POST_INCREMENT:
        bad_type_fmt = "cannot increment a value of type %s";
        bad_expr_fmt = "cannot increment %s";
        break;
    case AST_EXPR_PRE_DECREMENT:
    case AST_EXPR_POST_DECREMENT:
        bad_type_fmt = "cannot decrement a value of type %s";
        bad_expr_fmt = "cannot decrement %s";
        break;
    default:
        assert(0);
    }

    ensure_can_take_address(&expr->data.operands[0], bad_expr_fmt);
    const Type *t = typecheck_expression_not_void(ctx, &expr->data.operands[0])->type;
    if (!is_integer_type(t) && !is_pointer_type(t))
        fail_with_error(expr->location, bad_type_fmt, t->name);
    return t;
}

static void typecheck_dereferenced_pointer(Location location, const Type *t)
{
    // TODO: improved error message for dereferencing void*
    if (t->kind != TYPE_POINTER)
        fail_with_error(location, "the dereference operator '*' is only for pointers, not for %s", t->name);
}

// ptr[index]
static const Type *typecheck_indexing(
    TypeContext *ctx, const AstExpression *ptrexpr, const AstExpression *indexexpr)
{
    const Type *ptrtype = typecheck_expression_not_void(ctx, ptrexpr)->type;
    if (ptrtype->kind != TYPE_POINTER && ptrtype->kind != TYPE_ARRAY)
        fail_with_error(ptrexpr->location, "value of type %s cannot be indexed", ptrtype->name);
    if (ptrtype->kind == TYPE_ARRAY)
        ensure_can_take_address(ptrexpr, "cannot create a pointer into an array that comes from %s");

    const Type *indextype = typecheck_expression_not_void(ctx, indexexpr)->type;
    if (!is_integer_type(indextype)) {
        fail_with_error(
            indexexpr->location,
            "the index inside [...] must be an integer, not %s",
            indextype->name);
    }

    if (ptrtype->kind == TYPE_ARRAY)
        return ptrtype->data.array.membertype;
    else
        return ptrtype->data.valuetype;
}

static void typecheck_and_or(
    TypeContext *ctx, const AstExpression *lhsexpr, const AstExpression *rhsexpr, const char *and_or)
{
    assert(!strcmp(and_or, "and") || !strcmp(and_or, "or"));
    char errormsg[100];
    sprintf(errormsg, "'%s' only works with booleans, not FROM", and_or);

    typecheck_expression_with_implicit_cast(ctx, lhsexpr, boolType, errormsg);
    typecheck_expression_with_implicit_cast(ctx, rhsexpr, boolType, errormsg);
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

// returns NULL if the function doesn't return anything, otherwise non-owned pointer to non-owned type
static const Type *typecheck_function_call(TypeContext *ctx, const AstCall *call, Location location)
{
    const Signature *sig = find_function(ctx, call->calledname);
    if (!sig)
        fail_with_error(location, "function '%s' not found", call->calledname);
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

    for (int i = 0; i < sig->nargs; i++) {
        // This is a common error, so worth spending some effort to get a good error message.
        char msg[500];
        snprintf(msg, sizeof msg, "%s argument of function %s should have type TO, not FROM", nth(i+1), sigstr);
        typecheck_expression_with_implicit_cast(ctx, &call->args[i], sig->argtypes[i], msg);
    }
    for (int i = sig->nargs; i < call->nargs; i++) {
        // This code runs for varargs, e.g. the things to format in printf().
        ExpressionTypes *types = typecheck_expression_not_void(ctx, &call->args[i]);

        if (types->type->kind == TYPE_ARRAY) {
            fail_with_error(
                call->args[i].location,
                "arrays cannot be passed as varargs (try &array[0] instead of array)");
        }

        if ((is_integer_type(types->type) && types->type->data.width_in_bits < 32)
            || types->type == boolType)
        {
            // Add implicit cast to signed int, just like in C.
            do_implicit_cast(types, intType, (Location){0}, NULL);
        }
    }

    free(sigstr);
    return sig->returntype;
}

static const Type *typecheck_struct_field(
    const Type *structtype, const char *fieldname, Location location)
{
    assert(structtype->kind == TYPE_STRUCT);

    for (int i = 0; i < structtype->data.structfields.count; i++)
        if (!strcmp(structtype->data.structfields.names[i], fieldname))
            return structtype->data.structfields.types[i];

    fail_with_error(location, "struct %s has no field named '%s'", structtype->name, fieldname);
}

static const Type *typecheck_struct_init(TypeContext *ctx, const AstCall *call, Location location)
{
    struct AstType tmp = { .kind = AST_TYPE_NAMED, .location = location };
    safe_strcpy(tmp.data.name, call->calledname);
    const Type *t = type_from_ast(ctx, &tmp);

    if (t->kind != TYPE_STRUCT) {
        // TODO: test this error. Currently it can never happen because
        // all non-struct types are created with keywords, and this
        // function is called only when there is a name token followed
        // by a '{'.
        fail_with_error(location, "type %s cannot be instantiated with the Foo{...} syntax", t->name);
    }

    for (int i = 0; i < call->nargs; i++) {
        const Type *fieldtype = typecheck_struct_field(t, call->argnames[i], call->args[i].location);
        char msg[1000];
        snprintf(msg, sizeof msg,
            "value for field '%s' of struct %s must be of type TO, not FROM",
            call->argnames[i], call->calledname);
        typecheck_expression_with_implicit_cast(ctx, &call->args[i], fieldtype, msg);
    }

    return t;
}

static ExpressionTypes *typecheck_expression(TypeContext *ctx, const AstExpression *expr)
{
    const Type *temptype;
    const Type *result;

    switch(expr->kind) {
    case AST_EXPR_FUNCTION_CALL:
        {
            const Type *ret = typecheck_function_call(ctx, &expr->data.call, expr->location);
            if (!ret)
                return NULL;
            result = ret;
        }
        break;
    case AST_EXPR_SIZEOF:
        typecheck_expression_not_void(ctx, &expr->data.operands[0]);
        result = longType;
        break;
    case AST_EXPR_BRACE_INIT:
        result = typecheck_struct_init(ctx, &expr->data.call, expr->location);
        break;
    case AST_EXPR_GET_FIELD:
        temptype = typecheck_expression_not_void(ctx, expr->data.field.obj)->type;
        if (temptype->kind != TYPE_STRUCT)
            fail_with_error(
                expr->location,
                "left side of the '.' operator must be a struct, not %s",
                temptype->name);
        result = typecheck_struct_field(temptype, expr->data.field.fieldname, expr->location);
        break;
    case AST_EXPR_DEREF_AND_GET_FIELD:
        temptype = typecheck_expression_not_void(ctx, expr->data.field.obj)->type;
        if (temptype->kind != TYPE_POINTER || temptype->data.valuetype->kind != TYPE_STRUCT)
            fail_with_error(
                expr->location,
                "left side of the '->' operator must be a pointer to a struct, not %s",
                temptype->name);
        result = typecheck_struct_field(temptype->data.valuetype, expr->data.field.fieldname, expr->location);
        break;
    case AST_EXPR_INDEXING:
        result = typecheck_indexing(ctx, &expr->data.operands[0], &expr->data.operands[1]);
        break;
    case AST_EXPR_ADDRESS_OF:
        ensure_can_take_address(&expr->data.operands[0], "the '&' operator cannot be used with %s");
        temptype = typecheck_expression_not_void(ctx, &expr->data.operands[0])->type;
        result = get_pointer_type(temptype);
        break;
    case AST_EXPR_GET_VARIABLE:
        result = find_any_var(ctx, expr->data.varname);
        if (!result)
            fail_with_error(expr->location, "no variable named '%s'", expr->data.varname);
        break;
    case AST_EXPR_DEREFERENCE:
        temptype = typecheck_expression_not_void(ctx, &expr->data.operands[0])->type;
        typecheck_dereferenced_pointer(expr->location, temptype);
        result = temptype->data.valuetype;
        break;
    case AST_EXPR_CONSTANT:
        result = type_of_constant(&expr->data.constant);
        break;
    case AST_EXPR_AND:
        typecheck_and_or(ctx, &expr->data.operands[0], &expr->data.operands[1], "and");
        result = boolType;
        break;
    case AST_EXPR_OR:
        typecheck_and_or(ctx, &expr->data.operands[0], &expr->data.operands[1], "or");
        result = boolType;
        break;
    case AST_EXPR_NOT:
        typecheck_expression_with_implicit_cast(
            ctx, &expr->data.operands[0], boolType,
            "value after 'not' must be a boolean, not FROM");
        result = boolType;
        break;
    case AST_EXPR_NEG:
        result = typecheck_expression(ctx, &expr->data.operands[0])->type;
        if (result->kind != TYPE_SIGNED_INTEGER && !is_vaild_double(result))
            fail_with_error(
                expr->location,
                "value after '-' must be a float or double or a signed integer, not %s",
                result->name);
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
            ExpressionTypes *lhstypes = typecheck_expression_not_void(ctx, &expr->data.operands[0]);
            ExpressionTypes *rhstypes = typecheck_expression_not_void(ctx, &expr->data.operands[1]);
            result = check_binop(expr->kind, expr->location, lhstypes, rhstypes);
            break;
        }
    case AST_EXPR_PRE_INCREMENT:
    case AST_EXPR_PRE_DECREMENT:
    case AST_EXPR_POST_INCREMENT:
    case AST_EXPR_POST_DECREMENT:
        result = check_increment_or_decrement(ctx, expr);
        break;
    case AST_EXPR_AS:
        temptype = typecheck_expression_not_void(ctx, expr->data.as.obj)->type;
        result = type_from_ast(ctx, &expr->data.as.type);
        check_explicit_cast(temptype, result, expr->location);
        break;
    }

    ExpressionTypes *types = calloc(1, sizeof *types);
    types->expr = expr;
    types->type = result;
    Append(&ctx->expr_types, types);
    return types;
}

static void typecheck_statement(TypeContext *ctx, const AstStatement *stmt);

static void typecheck_body(TypeContext *ctx, const AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        typecheck_statement(ctx, &body->statements[i]);
}

static void typecheck_if_statement(TypeContext *ctx, const AstIfStatement *ifstmt)
{
    for (int i = 0; i < ifstmt->n_if_and_elifs; i++) {
        const char *errmsg;
        if (i == 0)
            errmsg = "'if' condition must be a boolean, not FROM";
        else
            errmsg = "'elif' condition must be a boolean, not FROM";

        typecheck_expression_with_implicit_cast(
            ctx, &ifstmt->if_and_elifs[i].condition, boolType, errmsg);
        typecheck_body(ctx, &ifstmt->if_and_elifs[i].body);
    }
    typecheck_body(ctx, &ifstmt->elsebody);
}

static void typecheck_statement(TypeContext *ctx, const AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_IF:
        typecheck_if_statement(ctx, &stmt->data.ifstatement);
        break;

    case AST_STMT_WHILE:
        typecheck_expression_with_implicit_cast(
            ctx, &stmt->data.whileloop.condition, boolType,
            "'while' condition must be a boolean, not FROM");
        typecheck_body(ctx, &stmt->data.whileloop.body);
        break;

    case AST_STMT_FOR:
        typecheck_statement(ctx, stmt->data.forloop.init);
        typecheck_expression_with_implicit_cast(
            ctx, &stmt->data.forloop.cond, boolType,
            "'for' condition must be a boolean, not FROM");
        typecheck_body(ctx, &stmt->data.forloop.body);
        typecheck_statement(ctx, stmt->data.forloop.incr);
        break;

    case AST_STMT_BREAK:
        break;

    case AST_STMT_CONTINUE:
        break;

    case AST_STMT_ASSIGN:
        {
            const AstExpression *targetexpr = &stmt->data.assignment.target;
            const AstExpression *valueexpr = &stmt->data.assignment.value;
            if (targetexpr->kind == AST_EXPR_GET_VARIABLE
                && !find_any_var(ctx, targetexpr->data.varname))
            {
                // Making a new variable. Use the type of the value being assigned.
                const ExpressionTypes *types = typecheck_expression(ctx, valueexpr);
                add_variable(ctx, types->type, targetexpr->data.varname);
            } else {
                // Convert value to the type of an existing variable or other assignment target.
                ensure_can_take_address(targetexpr, "cannot assign to %s");

                char errmsg[500];
                if (targetexpr->kind == AST_EXPR_DEREFERENCE) {
                    strcpy(errmsg, "cannot place a value of type FROM into a pointer of type TO*");
                } else {
                    snprintf(errmsg, sizeof errmsg,
                        "cannot assign a value of type FROM to %s of type TO",
                        short_expression_description(targetexpr));
                }
                const ExpressionTypes *targettypes = typecheck_expression(ctx, targetexpr);
                typecheck_expression_with_implicit_cast(ctx, valueexpr, targettypes->type, errmsg);
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
        const AstExpression *valueexpr = &stmt->data.assignment.value;

        // TODO: test this
        ensure_can_take_address(targetexpr, "cannot assign to %s");

        const char *opname;
        switch(stmt->kind) {
            case AST_STMT_INPLACE_ADD: opname = "addition"; break;
            case AST_STMT_INPLACE_SUB: opname = "subtraction"; break;
            case AST_STMT_INPLACE_MUL: opname = "multiplication"; break;
            case AST_STMT_INPLACE_DIV: opname = "division"; break;
            case AST_STMT_INPLACE_MOD: opname = "modulo"; break;
            default: assert(0);
        }

        // TODO: test this
        char errmsg[500];
        sprintf(errmsg, "%s produced a value of type FROM which cannot be assigned back to TO", opname);

        const ExpressionTypes *targettypes = typecheck_expression(ctx, targetexpr);
        typecheck_expression_with_implicit_cast(ctx, valueexpr, targettypes->type, errmsg);
        break;
    }

    case AST_STMT_RETURN_VALUE:
    {
        if(!ctx->current_function_signature->returntype){
            fail_with_error(
                stmt->location,
                "function '%s' cannot return a value because it was defined with '-> void'",
                ctx->current_function_signature->funcname);
        }

        char msg[200];
        snprintf(msg, sizeof msg,
            "attempting to return a value of type FROM from function '%s' defined with '-> TO'",
            ctx->current_function_signature->funcname);
        typecheck_expression_with_implicit_cast(
            ctx, &stmt->data.expression, find_local_var(ctx, "return")->type, msg);
        break;
    }

    case AST_STMT_RETURN_WITHOUT_VALUE:
        if (ctx->current_function_signature->returntype) {
            fail_with_error(
                stmt->location,
                "a return value is needed, because the return type of function '%s' is %s",
                ctx->current_function_signature->funcname,
                ctx->current_function_signature->returntype->name);
        }
        break;

    case AST_STMT_DECLARE_LOCAL_VAR:
        if (find_any_var(ctx, stmt->data.vardecl.name))
            fail_with_error(stmt->location, "a variable named '%s' already exists", stmt->data.vardecl.name);

        const Type *type = type_from_ast(ctx, &stmt->data.vardecl.type);
        if (stmt->data.vardecl.value) {
            typecheck_expression_with_implicit_cast(
                ctx, stmt->data.vardecl.value, type,
                "initial value for variable of type TO cannot be of type FROM");
        }
        add_variable(ctx, type, stmt->data.vardecl.name);
        break;

    case AST_STMT_EXPRESSION_STATEMENT:
        typecheck_expression(ctx, &stmt->data.expression);
        break;
    }
}

Signature typecheck_function(TypeContext *ctx, Location funcname_location, const AstSignature *astsig, const AstBody *body)
{
    if (find_function(ctx, astsig->funcname))
        fail_with_error(funcname_location, "a function named '%s' already exists", astsig->funcname);

    Signature sig = { .nargs = astsig->args.len, .takes_varargs = astsig->takes_varargs };
    safe_strcpy(sig.funcname, astsig->funcname);

    size_t size = sizeof(sig.argnames[0]) * sig.nargs;
    sig.argnames = malloc(size);
    for (int i = 0; i < sig.nargs; i++)
        safe_strcpy(sig.argnames[i], astsig->args.ptr[i].name);

    sig.argtypes = malloc(sizeof(sig.argtypes[0]) * sig.nargs);  // NOLINT
    for (int i = 0; i < sig.nargs; i++)
        sig.argtypes[i] = type_from_ast(ctx, &astsig->args.ptr[i].type);

    sig.returntype = type_or_void_from_ast(ctx, &astsig->returntype);
    // TODO: validate main() parameters
    // TODO: test main() taking parameters
    if (!strcmp(sig.funcname, "main") && sig.returntype != intType) {
        fail_with_error(astsig->returntype.location, "the main() function must return int");
    }

    sig.returntype_location = astsig->returntype.location;

    assert(ctx->current_function_signature == NULL);
    assert(ctx->expr_types.len == 0);
    assert(ctx->locals.len == 0);

    // Make signature of current function usable in function calls (recursion)
    Append(&ctx->function_signatures, sig);
    ctx->current_function_signature = &ctx->function_signatures.ptr[ctx->function_signatures.len - 1];

    if (body) {
        for (int i = 0; i < sig.nargs; i++) {
            LocalVariable *v = add_variable(ctx, sig.argtypes[i], sig.argnames[i]);
            v->is_argument = true;
        }
        if (sig.returntype)
            add_variable(ctx, sig.returntype, "return");

        typecheck_body(ctx, body);
    }

    ctx->current_function_signature = NULL;

    ExportSymbol es = { .kind = EXPSYM_FUNCTION, .data.funcsignature = copy_signature(&sig) };
    safe_strcpy(es.name, sig.funcname);
    Append(&ctx->exports, es);

    return copy_signature(&sig);
}

void typecheck_struct(TypeContext *ctx, const AstStructDef *structdef, Location location)
{
    if (find_type(ctx, structdef->name))
        fail_with_error(location, "a type named '%s' already exists", structdef->name);

    int n = structdef->fields.len;

    char (*fieldnames)[100] = malloc(n * sizeof(fieldnames[0]));
    for (int i = 0; i<n; i++)
        safe_strcpy(fieldnames[i], structdef->fields.ptr[i].name);

    const Type **fieldtypes = malloc(n * sizeof fieldtypes[0]);  // NOLINT
    for (int i = 0; i < n; i++)
        fieldtypes[i] = type_from_ast(ctx, &structdef->fields.ptr[i].type);

    Type *structtype = create_struct(structdef->name, n, fieldnames, fieldtypes);
    Append(&ctx->structs, structtype);

    ExportSymbol es = { .kind = EXPSYM_TYPE, .data.type = structtype };
    safe_strcpy(es.name, structdef->name);
    Append(&ctx->exports, es);
}

GlobalVariable *typecheck_global_var(TypeContext *ctx, const AstNameTypeValue *vardecl)
{
    assert(!vardecl->value);
    GlobalVariable *g = calloc(1, sizeof *g);
    safe_strcpy(g->name, vardecl->name);
    g->type = type_from_ast(ctx, &vardecl->type);
    Append(&ctx->globals, g);

    ExportSymbol es = { .kind = EXPSYM_GLOBAL_VAR, .data.type = g->type };
    safe_strcpy(es.name, g->name);
    Append(&ctx->exports, es);

    return g;
}

void reset_type_context(TypeContext *ctx)
{
    for (ExpressionTypes **et = ctx->expr_types.ptr; et < End(ctx->expr_types); et++)
        free(*et);
    ctx->expr_types.len = 0;
    ctx->locals.len = 0;
}
