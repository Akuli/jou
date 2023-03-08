#include "jou_compiler.h"
#include <stdnoreturn.h>

static const Type *find_type(const FileTypes *ft, const char *name)
{
    for (struct TypeAndUsedPtr *t = ft->types.ptr; t < End(ft->types); t++) {
        if (!strcmp(t->type->name, name)) {
            if (t->usedptr)
                *t->usedptr = true;
            return t->type;
        }
    }
    return NULL;
}

static const Signature *find_function(const FileTypes *ft, const char *name)
{
    for (struct SignatureAndUsedPtr *f = ft->functions.ptr; f < End(ft->functions); f++) {
        if (!strcmp(f->signature.name, name)) {
            if (f->usedptr)
                *f->usedptr = true;
            return &f->signature;
        }
    }
    return NULL;
}

static const Signature *find_method(const Type *selfclass, const char *name)
{
    if (selfclass->kind != TYPE_CLASS)
        return NULL;
    for (const Signature *m = selfclass->data.classdata.methods.ptr; m < End(selfclass->data.classdata.methods); m++)
        if (!strcmp(m->name, name))
            return m;
    return NULL;
}

static const Signature *find_function_or_method(const FileTypes *ft, const Type *selfclass, const char *name)
{
    if (selfclass)
        return find_method(selfclass, name);
    else
        return find_function(ft, name);
}

static const LocalVariable *find_local_var(const FileTypes *ft, const char *name)
{
    if (ft->current_fom_types)
        for (LocalVariable **var = ft->current_fom_types->locals.ptr; var < End(ft->current_fom_types->locals); var++)
            if (!strcmp((*var)->name, name))
                return *var;
    return NULL;
}

static const Type *find_any_var(const FileTypes *ft, const char *name)
{
    if (ft->current_fom_types)
        for (LocalVariable **var = ft->current_fom_types->locals.ptr; var < End(ft->current_fom_types->locals); var++)
            if (!strcmp((*var)->name, name))
                return (*var)->type;
    for (GlobalVariable **var = ft->globals.ptr; var < End(ft->globals); var++)
        if (!strcmp((*var)->name, name)) {
            if ((*var)->usedptr)
                *(*var)->usedptr = true;
            return (*var)->type;
        }
    return NULL;
}

ExportSymbol *typecheck_stage1_create_types(FileTypes *ft, const AstToplevelNode *ast)
{
    List(ExportSymbol) exports = {0};

    for (; ast->kind != AST_TOPLEVEL_END_OF_FILE; ast++) {
        Type *t;
        char name[100];

        switch(ast->kind) {
        case AST_TOPLEVEL_DEFINE_CLASS:
            safe_strcpy(name, ast->data.classdef.name);
            t = create_opaque_struct(name);
            break;
        case AST_TOPLEVEL_DEFINE_ENUM:
            safe_strcpy(name, ast->data.enumdef.name);
            t = create_enum(name, ast->data.enumdef.nmembers, ast->data.enumdef.membernames);
            break;
        default:
            continue;
        }

        if (find_type(ft, name))
            fail_with_error(ast->location, "a type named '%s' already exists", name);

        Append(&ft->types, (struct TypeAndUsedPtr){ .type=t, .usedptr=NULL });
        Append(&ft->owned_types, t);

        struct ExportSymbol es = { .kind = EXPSYM_TYPE, .data.type = t };
        safe_strcpy(es.name, name);
        Append(&exports, es);
    }

    Append(&exports, (ExportSymbol){0});
    return exports.ptr;
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
static const Type *type_or_void_from_ast(const FileTypes *ft, const AstType *asttype);

static const Type *type_from_ast(const FileTypes *ft, const AstType *asttype)
{
    const Type *t = type_or_void_from_ast(ft, asttype);
    if (!t)
        fail_with_error(asttype->location, "'void' cannot be used here because it is not a type");
    return t;
}

static const Type *type_or_void_from_ast(const FileTypes *ft, const AstType *asttype)
{
    const Type *tmp;

    switch(asttype->kind) {
    case AST_TYPE_NAMED:
        if (!strcmp(asttype->data.name, "int"))
            return intType;
        if (!strcmp(asttype->data.name, "long"))
            return longType;
        if (!strcmp(asttype->data.name, "size_t"))
            return sizeType;
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
        if ((tmp = find_type(ft, asttype->data.name)))
            return tmp;
        fail_with_error(asttype->location, "there is no type named '%s'", asttype->data.name);

    case AST_TYPE_POINTER:
        tmp = type_or_void_from_ast(ft, asttype->data.valuetype);
        if (tmp)
            return get_pointer_type(tmp);
        else
            return voidPtrType;

    case AST_TYPE_ARRAY:
        tmp = type_from_ast(ft, asttype->data.valuetype);
        int len = evaluate_array_length(asttype->data.array.len);
        if (len <= 0)
            fail_with_error(asttype->data.array.len->location, "array length must be positive");
        return get_array_type(tmp, len);
    }
}

static ExportSymbol handle_global_var(FileTypes *ft, const AstNameTypeValue *vardecl, bool defined_here)
{
    assert(ft->current_fom_types == NULL);  // find_any_var() only finds global vars
    if (find_any_var(ft, vardecl->name))
        fail_with_error(vardecl->name_location, "a global variable named '%s' already exists", vardecl->name);

    assert(!vardecl->value);
    GlobalVariable *g = calloc(1, sizeof *g);
    safe_strcpy(g->name, vardecl->name);
    g->type = type_from_ast(ft, &vardecl->type);
    g->defined_in_current_file = defined_here;
    Append(&ft->globals, g);

    ExportSymbol es = { .kind = EXPSYM_GLOBAL_VAR, .data.type = g->type };
    safe_strcpy(es.name, g->name);
    return es;
}

static Signature handle_signature(FileTypes *ft, const AstSignature *astsig, const Type *self_type)
{
    if (find_function_or_method(ft, self_type, astsig->name))
        fail_with_error(astsig->name_location, "a %s named '%s' already exists", self_type ? "method" : "function", astsig->name);

    Signature sig = { .nargs = astsig->args.len, .takes_varargs = astsig->takes_varargs };
    safe_strcpy(sig.name, astsig->name);

    size_t size = sizeof(sig.argnames[0]) * sig.nargs;
    sig.argnames = malloc(size);
    for (int i = 0; i < sig.nargs; i++)
        safe_strcpy(sig.argnames[i], astsig->args.ptr[i].name);

    sig.argtypes = malloc(sizeof(sig.argtypes[0]) * sig.nargs);  // NOLINT
    for (int i = 0; i < sig.nargs; i++) {
        if (!strcmp(sig.argnames[i], "self"))
            sig.argtypes[i] = get_pointer_type(self_type);
        else
            sig.argtypes[i] = type_from_ast(ft, &astsig->args.ptr[i].type);
    }

    sig.returntype = type_or_void_from_ast(ft, &astsig->returntype);
    // TODO: validate main() parameters
    // TODO: test main() taking parameters
    if (!self_type && !strcmp(sig.name, "main") && sig.returntype != intType) {
        fail_with_error(astsig->returntype.location, "the main() function must return int");
    }

    sig.returntype_location = astsig->returntype.location;

    if (!self_type)
        Append(&ft->functions, (struct SignatureAndUsedPtr){ .signature=copy_signature(&sig), .usedptr=NULL });

    return sig;
}

static const Type *handle_class_members_stage2(FileTypes *ft, const AstClassDef *classdef)
{
    // Previous type-checking stage created an opaque struct.
    Type *type = NULL;
    for (Type **s = ft->owned_types.ptr; s < End(ft->owned_types); s++) {
        if (!strcmp((*s)->name, classdef->name)) {
            type = *s;
            break;
        }
    }
    assert(type);
    assert(type->kind == TYPE_OPAQUE_CLASS);
    type->kind = TYPE_CLASS;

    memset(&type->data.classdata, 0, sizeof type->data.classdata);

    for (const AstNameTypeValue *classfield = classdef->fields.ptr; classfield < End(classdef->fields); classfield++) {
        struct ClassField f = {.type = type_from_ast(ft, &classfield->type)};
        safe_strcpy(f.name, classfield->name);
        Append(&type->data.classdata.fields, f);
    }

    for (const AstFunctionDef *m = classdef->methods.ptr; m < End(classdef->methods); m++) {
        // Don't handle the method body yet: that is a part of stage 3, not stage 2
        Signature sig = handle_signature(ft, &m->signature, type);
        Append(&type->data.classdata.methods, sig);
    }

    return type;
}

ExportSymbol *typecheck_stage2_signatures_globals_structbodies(FileTypes *ft, const AstToplevelNode *ast)
{
    List(ExportSymbol) exports = {0};

    for (; ast->kind != AST_TOPLEVEL_END_OF_FILE; ast++) {
        switch(ast->kind) {
        case AST_TOPLEVEL_DECLARE_GLOBAL_VARIABLE:
            Append(&exports, handle_global_var(ft, &ast->data.globalvar, false));
            break;
        case AST_TOPLEVEL_DEFINE_GLOBAL_VARIABLE:
            Append(&exports, handle_global_var(ft, &ast->data.globalvar, true));
            break;
        case AST_TOPLEVEL_DECLARE_FUNCTION:
        case AST_TOPLEVEL_DEFINE_FUNCTION:
            {
                Signature sig = handle_signature(ft, &ast->data.funcdef.signature, NULL);
                ExportSymbol es = { .kind = EXPSYM_FUNCTION, .data.funcsignature = sig };
                safe_strcpy(es.name, sig.name);
                Append(&exports, es);
            }
            break;
        case AST_TOPLEVEL_DEFINE_CLASS:
            handle_class_members_stage2(ft, &ast->data.classdef);
            break;
        case AST_TOPLEVEL_DEFINE_ENUM:
        case AST_TOPLEVEL_IMPORT:
            // Everything done in previous type-checking steps.
            break;
        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        }
    }

    Append(&exports, (ExportSymbol){0});
    return exports.ptr;
}

static LocalVariable *add_variable(FileTypes *ft, const Type *t, const char *name)
{
    LocalVariable *var = calloc(1, sizeof *var);
    var->id = ft->current_fom_types->locals.len;
    var->type = t;

    assert(name);
    assert(!find_local_var(ft, name));
    assert(strlen(name) < sizeof var->name);
    strcpy(var->name, name);

    Append(&ft->current_fom_types->locals, var);
    return var;
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

static bool can_cast_implicitly(const Type *from, const Type *to)
{
    return
        from == to
        || (
            // Cast to bigger integer types implicitly, unless it is signed-->unsigned.
            is_integer_type(from)
            && is_integer_type(to)
            && from->data.width_in_bits < to->data.width_in_bits
            && !(from->kind == TYPE_SIGNED_INTEGER && to->kind == TYPE_UNSIGNED_INTEGER)
        ) || (
            // Cast to bigger floating-point type.
            from == floatType && to == doubleType
        ) || (
            // Cast from any integer type to float/double.
            is_integer_type(from) && to->kind == TYPE_FLOATING_POINT
        ) || (
            // Cast implicitly between void pointer and any other pointer.
            (from->kind == TYPE_POINTER && to->kind == TYPE_VOID_POINTER)
            || (from->kind == TYPE_VOID_POINTER && to->kind == TYPE_POINTER)
        );
}

static void do_implicit_cast(
    ExpressionTypes *types, const Type *to, Location location, const char *errormsg_template)
{
    const Type *from = types->type;
    if (from == to)
        return;

    // Passing in NULL for errormsg_template can be used to "force" a cast to happen.
    if (errormsg_template != NULL && !can_cast_implicitly(from, to))
        fail_with_implicit_cast_error(location, errormsg_template, from, to);

    assert(!types->type_after_cast);
    types->type_after_cast = to;
}

static void check_explicit_cast(const Type *from, const Type *to, Location location)
{
    if (
        from != to  // TODO: should probably be error if it's the same type.
        && !(is_pointer_type(from) && is_pointer_type(to))
        && !(is_number_type(from) && is_number_type(to))
        && !(is_integer_type(from) && to->kind == TYPE_ENUM)
        && !(from->kind == TYPE_ENUM && is_integer_type(to))
        && !(from->kind == TYPE_BOOL && is_integer_type(to))
        // TODO: pointer-to-int, int-to-pointer
    )
    {
        // TODO: test this error
        fail_with_error(location, "cannot cast from type %s to %s", from->name, to->name);
    }
}

static ExpressionTypes *typecheck_expression(FileTypes *ft, const AstExpression *expr);

static ExpressionTypes *typecheck_expression_not_void(FileTypes *ft, const AstExpression *expr)
{
    ExpressionTypes *types = typecheck_expression(ft, expr);
    if (!types) {
        switch(expr->kind) {
        case AST_EXPR_FUNCTION_CALL:
            fail_with_error(
                expr->location, "function '%s' does not return a value", expr->data.call.calledname);
            break;
        case AST_EXPR_CALL_METHOD:
            fail_with_error(
                expr->location, "method '%s' does not return a value", expr->data.methodcall.call.calledname);
            break;
        default:
            assert(0);
        }
    }
    return types;
}

static void typecheck_expression_with_implicit_cast(
    FileTypes *ft,
    const AstExpression *expr,
    const Type *casttype,
    const char *errormsg_template)
{
    ExpressionTypes *types = typecheck_expression_not_void(ft, expr);
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
    bool got_enums = lhstypes->type->kind == TYPE_ENUM && rhstypes->type->kind == TYPE_ENUM;
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

    if(!(
        got_integers
        || got_numbers
        || ((got_enums || got_pointers) && (op == AST_EXPR_EQ || op == AST_EXPR_NE))
    ))
        fail_with_error(location, "wrong types: cannot %s %s and %s", do_what, lhstypes->type->name, rhstypes->type->name);

    const Type *cast_type = NULL;
    if (got_integers) {
        cast_type = get_integer_type(
            max(lhstypes->type->data.width_in_bits, rhstypes->type->data.width_in_bits),
            lhstypes->type->kind == TYPE_SIGNED_INTEGER || rhstypes->type->kind == TYPE_SIGNED_INTEGER
        );
    }
    if (got_numbers && !got_integers)
        cast_type = (lhstypes->type == doubleType || rhstypes->type == doubleType) ? doubleType : floatType;
    if (got_pointers)
        cast_type = voidPtrType;
    if (got_enums)
        cast_type = intType;
    assert(cast_type);

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
    case AST_EXPR_GET_ENUM_MEMBER: return "an enum member";
    case AST_EXPR_SIZEOF: return "a sizeof expression";
    case AST_EXPR_FUNCTION_CALL: return "a function call";
    case AST_EXPR_CALL_METHOD: return "a method call";
    case AST_EXPR_DEREF_AND_CALL_METHOD: return "a method call";
    case AST_EXPR_BRACE_INIT: return "a newly created instance";
    case AST_EXPR_ARRAY: return "an array";
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
        snprintf(result, sizeof result, "field '%s'", expr->data.classfield.fieldname);
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

static const Type *check_increment_or_decrement(FileTypes *ft, const AstExpression *expr)
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
    const Type *t = typecheck_expression_not_void(ft, &expr->data.operands[0])->type;
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
    FileTypes *ft, const AstExpression *ptrexpr, const AstExpression *indexexpr)
{
    const Type *ptrtype = typecheck_expression_not_void(ft, ptrexpr)->type;
    if (ptrtype->kind != TYPE_POINTER && ptrtype->kind != TYPE_ARRAY)
        fail_with_error(ptrexpr->location, "value of type %s cannot be indexed", ptrtype->name);
    if (ptrtype->kind == TYPE_ARRAY)
        ensure_can_take_address(ptrexpr, "cannot create a pointer into an array that comes from %s");

    const Type *indextype = typecheck_expression_not_void(ft, indexexpr)->type;
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
    FileTypes *ft, const AstExpression *lhsexpr, const AstExpression *rhsexpr, const char *and_or)
{
    assert(!strcmp(and_or, "and") || !strcmp(and_or, "or"));
    char errormsg[100];
    sprintf(errormsg, "'%s' only works with booleans, not FROM", and_or);

    typecheck_expression_with_implicit_cast(ft, lhsexpr, boolType, errormsg);
    typecheck_expression_with_implicit_cast(ft, rhsexpr, boolType, errormsg);
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
static const Type *typecheck_function_or_method_call(FileTypes *ft, const AstCall *call, const Type *self_type, Location location)
{
    const Signature *sig = find_function_or_method(ft, self_type, call->calledname);
    if (!sig) {
        if (!self_type)
            fail_with_error(location, "function '%s' not found", call->calledname);

        // If self type is a pointer to a struct that has the method, mention it in the error message
        if (self_type->kind == TYPE_POINTER && find_method(self_type->data.valuetype, call->calledname)) {
            fail_with_error(
                location,
                "the method '%s' is defined on class %s, not on the pointer type %s,"
                " so you need to dereference the pointer first (e.g. by using '->' instead of '.')",
                call->calledname, self_type->data.valuetype->name, self_type->name);
        }
        // If it is not a class, explain to the user that there are no methods
        if (self_type->kind != TYPE_CLASS) {
            fail_with_error(location, "type %s does not have any methods because it is not a class", self_type->name);
        }
        fail_with_error(location, "class %s does not have a method named '%s'",
            self_type->name, call->calledname);
    }

    char *sigstr = signature_to_string(sig, false);

    int n = call->nargs + !!self_type;
    if (n < sig->nargs || (n > sig->nargs && !sig->takes_varargs)) {
        fail_with_error(
            location,
            "%s %s takes %d argument%s, but it was called with %d argument%s",
            self_type ? "method" : "function",
            sigstr,
            sig->nargs,
            sig->nargs==1?"":"s",
            call->nargs,
            call->nargs==1?"":"s"
        );
    }

    int k = 0;
    for (int i = 0; i < sig->nargs; i++) {
        // This is a common error, so worth spending some effort to get a good error message.
        char msg[500];
        snprintf(msg, sizeof msg, "%s argument of %s %s should have type TO, not FROM", nth(i+1), self_type ? "method" : "function", sigstr);
        if (strcmp(sig->argnames[i], "self"))
            typecheck_expression_with_implicit_cast(ft, &call->args[k++], sig->argtypes[i], msg);
    }

    for (int i = k; i < call->nargs; i++) {
        // This code runs for varargs, e.g. the things to format in printf().
        ExpressionTypes *types = typecheck_expression_not_void(ft, &call->args[i]);

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

        if (types->type == floatType)
            do_implicit_cast(types, doubleType, (Location){0}, NULL);
    }

    free(sigstr);
    return sig->returntype;
}

static const Type *typecheck_class_field(
    const Type *classtype, const char *fieldname, Location location)
{
    assert(classtype->kind == TYPE_CLASS);

    for (struct ClassField *f = classtype->data.classdata.fields.ptr; f < End(classtype->data.classdata.fields); f++)
        if (!strcmp(f->name, fieldname))
            return f->type;

    fail_with_error(location, "class %s has no field named '%s'", classtype->name, fieldname);
}

static const Type *typecheck_struct_init(FileTypes *ft, const AstCall *call, Location location)
{
    struct AstType tmp = { .kind = AST_TYPE_NAMED, .location = location };
    safe_strcpy(tmp.data.name, call->calledname);
    const Type *t = type_from_ast(ft, &tmp);

    if (t->kind != TYPE_CLASS) {
        // TODO: test this error. Currently it can never happen because
        // all non-struct types are created with keywords, and this
        // function is called only when there is a name token followed
        // by a '{'.
        fail_with_error(location, "type %s cannot be instantiated with the Foo{...} syntax", t->name);
    }

    for (int i = 0; i < call->nargs; i++) {
        const Type *fieldtype = typecheck_class_field(t, call->argnames[i], call->args[i].location);
        char msg[1000];
        snprintf(msg, sizeof msg,
            "value for field '%s' of class %s must be of type TO, not FROM",
            call->argnames[i], call->calledname);
        typecheck_expression_with_implicit_cast(ft, &call->args[i], fieldtype, msg);
    }

    return t;
}

static const char *very_short_type_description(const Type *t)
{
    switch(t->kind) {
        case TYPE_CLASS:
        case TYPE_OPAQUE_CLASS:
            return "a class";
        case TYPE_ENUM:
            return "an enum";
        case TYPE_VOID_POINTER:
        case TYPE_POINTER:
            return "a pointer type";
        case TYPE_SIGNED_INTEGER:
        case TYPE_UNSIGNED_INTEGER:
        case TYPE_FLOATING_POINT:
            return "a number type";
        case TYPE_ARRAY:
            return "an array type";
        case TYPE_BOOL:
            return "the built-in boolean type";
    }
}

static bool enum_member_exists(const Type *t, const char *name)
{
    assert(t->kind == TYPE_ENUM);
    for (int i = 0; i < t->data.enummembers.count; i++)
        if (!strcmp(t->data.enummembers.names[i], name))
            return true;
    return false;
}

static const Type *cast_array_members_to_a_common_type(Location error_location, ExpressionTypes **exprtypes)
{
    // Avoid O(ntypes^2) code in a long array where all or almost all items have the same type.
    // This is at most O(ntypes*k) where k is the number of distinct types.
    List(const Type *) distinct = {0};
    for (ExpressionTypes **et = exprtypes; *et; et++) {
        bool found = false;
        for (const Type **t = distinct.ptr; t < End(distinct); t++) {
            if ((*et)->type == *t) {
                found = true;
                break;
            }
        }
        if (!found)
            Append(&distinct, (*et)->type);
    }

    List(const Type *) compatible_with_all = {0};
    for (const Type **t = distinct.ptr; t < End(distinct); t++) {
        bool t_compatible_with_all_others = true;
        for (const Type **t2 = distinct.ptr; t2 < End(distinct); t2++) {
            if (!can_cast_implicitly(*t2,*t)) {
                t_compatible_with_all_others = false;
                break;
            }
        }
        if (t_compatible_with_all_others)
            Append(&compatible_with_all, *t);
    }

    if (compatible_with_all.len != 1) {
        List(char) namestr = {0};
        for (const Type **t = distinct.ptr; t < End(distinct); t++) {
            AppendStr(&namestr, (*t)->name);
            AppendStr(&namestr, ", ");
        }
        fail_with_error(
            error_location, "array items have different types (%.*s)",
            namestr.len - 2, namestr.ptr);
    }
    const Type *elemtype = compatible_with_all.ptr[0];
    free(distinct.ptr);
    free(compatible_with_all.ptr);

    for (ExpressionTypes **et = exprtypes; *et; et++)
        do_implicit_cast(*et, elemtype, error_location, NULL);
    return elemtype;
}

static ExpressionTypes *typecheck_expression(FileTypes *ft, const AstExpression *expr)
{
    const Type *temptype;
    const Type *result;

    switch(expr->kind) {
    case AST_EXPR_GET_ENUM_MEMBER:
        result = find_type(ft, expr->data.enummember.enumname);
        if (!result)
            fail_with_error(
                expr->location, "there is no type named '%s'", expr->data.enummember.enumname);
        if (result->kind != TYPE_ENUM)
            fail_with_error(
                expr->location, "the '::' syntax is only for enums, but %s is %s",
                expr->data.enummember.enumname, very_short_type_description(result));
        if (!enum_member_exists(result, expr->data.enummember.membername))
            fail_with_error(expr->location, "enum %s has no member named '%s'",
                expr->data.enummember.enumname, expr->data.enummember.membername);
        break;
    case AST_EXPR_FUNCTION_CALL:
        result = typecheck_function_or_method_call(ft, &expr->data.call, NULL, expr->location);
        if (!result)
            return NULL;
        break;
    case AST_EXPR_SIZEOF:
        typecheck_expression_not_void(ft, &expr->data.operands[0]);
        result = sizeType;
        break;
    case AST_EXPR_BRACE_INIT:
        result = typecheck_struct_init(ft, &expr->data.call, expr->location);
        break;
    case AST_EXPR_ARRAY:
        {
            int n = expr->data.array.count;
            ExpressionTypes **exprtypes = calloc(sizeof(exprtypes[0]), n+1);  // NOLINT
            for (int i = 0; i < n; i++)
                exprtypes[i] = typecheck_expression_not_void(ft, &expr->data.array.items[i]);

            const Type *membertype = cast_array_members_to_a_common_type(expr->location, exprtypes);
            free(exprtypes);
            result = get_array_type(membertype, n);
        }
        break;
    case AST_EXPR_GET_FIELD:
        temptype = typecheck_expression_not_void(ft, expr->data.classfield.obj)->type;
        if (temptype->kind != TYPE_CLASS)
            fail_with_error(
                expr->location,
                "left side of the '.' operator must be a class, not %s",
                temptype->name);
        result = typecheck_class_field(temptype, expr->data.classfield.fieldname, expr->location);
        break;
    case AST_EXPR_DEREF_AND_GET_FIELD:
        temptype = typecheck_expression_not_void(ft, expr->data.classfield.obj)->type;
        if (temptype->kind != TYPE_POINTER || temptype->data.valuetype->kind != TYPE_CLASS)
            fail_with_error(
                expr->location,
                "left side of the '->' operator must be a pointer to a class, not %s",
                temptype->name);
        result = typecheck_class_field(temptype->data.valuetype, expr->data.classfield.fieldname, expr->location);
        break;
    case AST_EXPR_DEREF_AND_CALL_METHOD:
        temptype = typecheck_expression_not_void(ft, expr->data.classfield.obj)->type;
        if (temptype->kind != TYPE_POINTER)
            fail_with_error(
                expr->location,
                "left side of the '->' operator must be a pointer, not %s",
                temptype->name);
        result = typecheck_function_or_method_call(ft, &expr->data.methodcall.call, temptype->data.valuetype, expr->location);
        break;
    case AST_EXPR_CALL_METHOD:
        temptype = typecheck_expression_not_void(ft, expr->data.methodcall.obj)->type;
        result = typecheck_function_or_method_call(ft, &expr->data.methodcall.call, temptype, expr->location);
        if (!result)
            return NULL;
        break;
    case AST_EXPR_INDEXING:
        result = typecheck_indexing(ft, &expr->data.operands[0], &expr->data.operands[1]);
        break;
    case AST_EXPR_ADDRESS_OF:
        ensure_can_take_address(&expr->data.operands[0], "the '&' operator cannot be used with %s");
        temptype = typecheck_expression_not_void(ft, &expr->data.operands[0])->type;
        result = get_pointer_type(temptype);
        break;
    case AST_EXPR_GET_VARIABLE:
        result = find_any_var(ft, expr->data.varname);
        if (!result)
            fail_with_error(expr->location, "no variable named '%s'", expr->data.varname);
        break;
    case AST_EXPR_DEREFERENCE:
        temptype = typecheck_expression_not_void(ft, &expr->data.operands[0])->type;
        typecheck_dereferenced_pointer(expr->location, temptype);
        result = temptype->data.valuetype;
        break;
    case AST_EXPR_CONSTANT:
        result = type_of_constant(&expr->data.constant);
        break;
    case AST_EXPR_AND:
        typecheck_and_or(ft, &expr->data.operands[0], &expr->data.operands[1], "and");
        result = boolType;
        break;
    case AST_EXPR_OR:
        typecheck_and_or(ft, &expr->data.operands[0], &expr->data.operands[1], "or");
        result = boolType;
        break;
    case AST_EXPR_NOT:
        typecheck_expression_with_implicit_cast(
            ft, &expr->data.operands[0], boolType,
            "value after 'not' must be a boolean, not FROM");
        result = boolType;
        break;
    case AST_EXPR_NEG:
        result = typecheck_expression_not_void(ft, &expr->data.operands[0])->type;
        if (result->kind != TYPE_SIGNED_INTEGER && result->kind != TYPE_FLOATING_POINT)
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
            ExpressionTypes *lhstypes = typecheck_expression_not_void(ft, &expr->data.operands[0]);
            ExpressionTypes *rhstypes = typecheck_expression_not_void(ft, &expr->data.operands[1]);
            result = check_binop(expr->kind, expr->location, lhstypes, rhstypes);
            break;
        }
    case AST_EXPR_PRE_INCREMENT:
    case AST_EXPR_PRE_DECREMENT:
    case AST_EXPR_POST_INCREMENT:
    case AST_EXPR_POST_DECREMENT:
        result = check_increment_or_decrement(ft, expr);
        break;
    case AST_EXPR_AS:
        temptype = typecheck_expression_not_void(ft, expr->data.as.obj)->type;
        result = type_from_ast(ft, &expr->data.as.type);
        check_explicit_cast(temptype, result, expr->location);
        break;
    }

    ExpressionTypes *types = calloc(1, sizeof *types);
    types->expr = expr;
    types->type = result;
    Append(&ft->current_fom_types->expr_types, types);
    return types;
}

static void typecheck_statement(FileTypes *ft, const AstStatement *stmt);

static void typecheck_body(FileTypes *ft, const AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        typecheck_statement(ft, &body->statements[i]);
}

static void typecheck_if_statement(FileTypes *ft, const AstIfStatement *ifstmt)
{
    for (int i = 0; i < ifstmt->n_if_and_elifs; i++) {
        const char *errmsg;
        if (i == 0)
            errmsg = "'if' condition must be a boolean, not FROM";
        else
            errmsg = "'elif' condition must be a boolean, not FROM";

        typecheck_expression_with_implicit_cast(
            ft, &ifstmt->if_and_elifs[i].condition, boolType, errmsg);
        typecheck_body(ft, &ifstmt->if_and_elifs[i].body);
    }
    typecheck_body(ft, &ifstmt->elsebody);
}

static void typecheck_statement(FileTypes *ft, const AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_IF:
        typecheck_if_statement(ft, &stmt->data.ifstatement);
        break;

    case AST_STMT_WHILE:
        typecheck_expression_with_implicit_cast(
            ft, &stmt->data.whileloop.condition, boolType,
            "'while' condition must be a boolean, not FROM");
        typecheck_body(ft, &stmt->data.whileloop.body);
        break;

    case AST_STMT_FOR:
        typecheck_statement(ft, stmt->data.forloop.init);
        typecheck_expression_with_implicit_cast(
            ft, &stmt->data.forloop.cond, boolType,
            "'for' condition must be a boolean, not FROM");
        typecheck_body(ft, &stmt->data.forloop.body);
        typecheck_statement(ft, stmt->data.forloop.incr);
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
                && !find_any_var(ft, targetexpr->data.varname))
            {
                // Making a new variable. Use the type of the value being assigned.
                const ExpressionTypes *types = typecheck_expression_not_void(ft, valueexpr);
                add_variable(ft, types->type, targetexpr->data.varname);
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
                const ExpressionTypes *targettypes = typecheck_expression_not_void(ft, targetexpr);
                typecheck_expression_with_implicit_cast(ft, valueexpr, targettypes->type, errmsg);
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

        const ExpressionTypes *targettypes = typecheck_expression_not_void(ft, targetexpr);
        typecheck_expression_with_implicit_cast(ft, valueexpr, targettypes->type, errmsg);
        break;
    }

    case AST_STMT_RETURN_VALUE:
    {
        if(!ft->current_fom_types->signature.returntype){
            fail_with_error(
                stmt->location,
                "function '%s' cannot return a value because it was defined with '-> void'",
                ft->current_fom_types->signature.name);
        }

        char msg[200];
        snprintf(msg, sizeof msg,
            "attempting to return a value of type FROM from function '%s' defined with '-> TO'",
            ft->current_fom_types->signature.name);
        typecheck_expression_with_implicit_cast(
            ft, &stmt->data.expression, find_local_var(ft, "return")->type, msg);
        break;
    }

    case AST_STMT_RETURN_WITHOUT_VALUE:
        if (ft->current_fom_types->signature.returntype) {
            fail_with_error(
                stmt->location,
                "a return value is needed, because the return type of function '%s' is %s",
                ft->current_fom_types->signature.name,
                ft->current_fom_types->signature.returntype->name);
        }
        break;

    case AST_STMT_DECLARE_LOCAL_VAR:
        if (find_any_var(ft, stmt->data.vardecl.name))
            fail_with_error(stmt->location, "a variable named '%s' already exists", stmt->data.vardecl.name);

        const Type *type = type_from_ast(ft, &stmt->data.vardecl.type);
        add_variable(ft, type, stmt->data.vardecl.name);
        if (stmt->data.vardecl.value) {
            typecheck_expression_with_implicit_cast(
                ft, stmt->data.vardecl.value, type,
                "initial value for variable of type TO cannot be of type FROM");
        }
        break;

    case AST_STMT_EXPRESSION_STATEMENT:
        typecheck_expression(ft, &stmt->data.expression);
        break;
    }
}

static void typecheck_function_or_method_body(FileTypes *ft, const Signature *sig, const AstBody *body)
{
    assert(!ft->current_fom_types);
    Append(&ft->fomtypes, (struct FunctionOrMethodTypes){0});
    ft->current_fom_types = End(ft->fomtypes) - 1;
    ft->current_fom_types->signature = copy_signature(sig);

    for (int i = 0; i < sig->nargs; i++) {
        LocalVariable *v = add_variable(ft, sig->argtypes[i], sig->argnames[i]);
        v->is_argument = true;
    }
    if (sig->returntype)
        add_variable(ft, sig->returntype, "return");

    typecheck_body(ft, body);
    ft->current_fom_types = NULL;
}

void typecheck_stage3_function_and_method_bodies(FileTypes *ft, const AstToplevelNode *ast)
{
    for (; ast->kind != AST_TOPLEVEL_END_OF_FILE; ast++) {
        if (ast->kind == AST_TOPLEVEL_DEFINE_FUNCTION) {
            const Signature *sig = NULL;
            for (struct SignatureAndUsedPtr *f = ft->functions.ptr; f < End(ft->functions); f++) {
                if (!strcmp(f->signature.name, ast->data.funcdef.signature.name)) {
                    sig = &f->signature;
                    break;
                }
            }
            assert(sig);
            typecheck_function_or_method_body(ft, sig, &ast->data.funcdef.body);
        }

        if (ast->kind == AST_TOPLEVEL_DEFINE_CLASS) {
            Type *classtype = NULL;
            for (Type **t = ft->owned_types.ptr; t < End(ft->owned_types); t++) {
                if (!strcmp((*t)->name, ast->data.classdef.name)) {
                    classtype = *t;
                    break;
                }
            }
            assert(classtype);

            for (AstFunctionDef *m = ast->data.classdef.methods.ptr; m < End(ast->data.classdef.methods); m++) {
                Signature *sig = NULL;
                for (Signature *s = classtype->data.classdata.methods.ptr; s < End(classtype->data.classdata.methods); s++) {
                    if (!strcmp(s->name, m->signature.name)) {
                        sig = s;
                        break;
                    }
                }
                assert(sig);
                typecheck_function_or_method_body(ft, sig, &m->body);
            }
        }
    }
}
