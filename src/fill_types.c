#include <assert.h>
#include "jou_compiler.h"
#include "util.h"


struct State {
    List(struct AstFunctionSignature) functions;

    // func_ = information about the function containing the code that is checked
    const struct AstFunctionSignature *func_signature;
    List(struct AstLocalVariable) func_locals;
};

static const struct AstFunctionSignature *find_function(const struct State *st, const char *name)
{
    for (struct AstFunctionSignature *func = st->functions.ptr; func < End(st->functions); func++)
        if (!strcmp(func->funcname, name))
            return func;
    return NULL;
}

// Adding a variable makes pointers returned from previous calls bad when the list grows.
static const struct AstLocalVariable *add_local_variable(struct State *st, const char *name, const struct Type *type)
{
    struct AstLocalVariable v = { .type = *type };
    assert(strlen(name) < sizeof v.name);
    strcpy(v.name, name);
    Append(&st->func_locals, v);
    return End(st->func_locals) - 1;
}

static const struct AstLocalVariable *find_local_variable(const struct State *st, const char *name)
{
    for (struct AstLocalVariable *v = st->func_locals.ptr; v < End(st->func_locals); v++)
        if (!strcmp(v->name, name))
            return v;
    return NULL;
}


static void fill_types_expression(
    struct State *st,
    struct AstExpression *expr,
    const struct Type *implicit_cast_to,
    const char *casterrormsg,
    bool needvalue);

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

// Returns the return type of the function, NULL if the function does not return a value.
static const struct Type *fill_types_call(struct State *st, struct AstCall *call, struct Location location)
{
    const struct AstFunctionSignature *sig = find_function(st, call->funcname);
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

    for (int i = 0; i < sig->nargs; i++) {
        // This is a common error, so worth spending some effort to get a good error message.
        char msg[500];
        snprintf(msg, sizeof msg, "%s argument of function %s should have type TO, not FROM", nth(i+1), sigstr);
        fill_types_expression(st, &call->args[i], &sig->argtypes[i], msg, true);
    }
    for (int i = sig->nargs; i < call->nargs; i++) {
        // This code runs for varargs, e.g. the things to format in printf().
        fill_types_expression(st, &call->args[i], NULL, NULL, true);
    }

    free(sigstr);
    return sig->returntype;
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
noreturn void fail_with_implicit_cast_error(struct Location location, const char *template, const struct AstExpression *expr)
{
    List(char) msg = {0};
    while(*template){
        if (!strncmp(template, "FROM", 4)) {
            AppendStr(&msg, expr->type_before_implicit_cast.name);
            template += 4;
        } else if (!strncmp(template, "TO", 2)) {
            AppendStr(&msg, expr->type_after_implicit_cast.name);
            template += 2;
        } else {
            Append(&msg, template[0]);
            template++;
        }
    }
    fail_with_error(location, "%.*s", msg.len, msg.ptr);
}

// May set lhstype and rhstype to cast them. This simplifies codegen.
static struct Type get_type_for_binop(
    enum AstExpressionKind op,
    struct Location error_location,
    struct Type *lhstype,
    struct Type *rhstype)
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

    // TODO: is this a good idea?
    struct Type cast_type;
    if (is_integer_type(lhstype) && is_integer_type(rhstype)) {
        cast_type = create_integer_type(
            max(lhstype->data.width_in_bits, rhstype->data.width_in_bits),
            lhstype->kind == TYPE_SIGNED_INTEGER || rhstype->kind == TYPE_SIGNED_INTEGER
        );
    }

    switch(op) {
    case AST_EXPR_ADD:
    case AST_EXPR_SUB:
    case AST_EXPR_MUL:
    case AST_EXPR_DIV:
        if (!is_integer_type(lhstype) || !is_integer_type(rhstype))
            fail_with_error(error_location, "wrong types: cannot %s %s and %s", do_what, lhstype->name, rhstype->name);
        *lhstype = cast_type;
        *rhstype = cast_type;
        return cast_type;

    case AST_EXPR_EQ:
    case AST_EXPR_NE:
    case AST_EXPR_GT:
    case AST_EXPR_GE:
    case AST_EXPR_LT:
    case AST_EXPR_LE:
        if (!is_integer_type(lhstype) || !is_integer_type(rhstype))
            fail_with_error(error_location, "wrong types: cannot compare %s and %s for equality", lhstype->name, rhstype->name);
        *lhstype = cast_type;
        *rhstype = cast_type;
        return boolType;

    default:
        assert(0);
    }
}

static void fill_types_expression(
    struct State *st,
    struct AstExpression *expr,
    const struct Type *implicit_cast_to,  // can be NULL, there will be no implicit casting
    const char *casterrormsg,
    bool needvalue)  // Usually true. False means that calls to "-> void" functions are acceptable.
{
    assert((implicit_cast_to==NULL && casterrormsg==NULL)
        || (implicit_cast_to!=NULL && casterrormsg!=NULL));

    switch(expr->kind) {
        case AST_EXPR_GET_VARIABLE:
        {
            const struct AstLocalVariable *v = find_local_variable(st, expr->data.varname);
            if (!v)
                fail_with_error(expr->location, "no local variable named '%s'", expr->data.varname);
            expr->type_before_implicit_cast = v->type;
            break;
        }

        case AST_EXPR_ADDRESS_OF:
            fill_types_expression(st, &expr->data.operands[0], NULL, NULL, true);
            expr->type_before_implicit_cast = create_pointer_type(&expr->data.operands[0].type_after_implicit_cast, expr->location);
            break;

        case AST_EXPR_ASSIGN:
        {
            // TODO: i think "bytevar = intvar = 'x'" doesn't work because 'x' becomes int implicitly
            struct AstExpression *target = &expr->data.operands[0];
            struct AstExpression *value = &expr->data.operands[1];
            if (target->kind == AST_EXPR_GET_VARIABLE && !find_local_variable(st, target->data.varname))
            {
                // Making a new variable. Use the type of the value being assigned.
                fill_types_expression(st, value, NULL, NULL, true);
                const struct Type *t = &value->type_after_implicit_cast;
                add_local_variable(st, target->data.varname, t);
                target->type_before_implicit_cast = *t;
                target->type_after_implicit_cast = *t;
                fill_types_expression(st, target, NULL, NULL, true);
            } else {
                // Convert value to the type of an existing variable or other assignment target.
                fill_types_expression(st, target, NULL, NULL, true);
                const char *errmsg;
                switch(target->kind) {
                    case AST_EXPR_GET_VARIABLE: errmsg = "cannot assign a value of type FROM to variable of type TO"; break;
                    case AST_EXPR_DEREFERENCE: errmsg = "cannot assign a value of type FROM into a pointer of type TO*"; break;
                    default: assert(0);
                }
                fill_types_expression(st, value, &target->type_after_implicit_cast, errmsg, true);
            }
            expr->type_before_implicit_cast = value->type_after_implicit_cast;
            break;
        }

        case AST_EXPR_CALL:
        {
            const struct Type *t = fill_types_call(st, &expr->data.call, expr->location);
            if (!t) {
                if (!needvalue)
                    return;
                fail_with_error(expr->location, "function '%s' does not return a value", expr->data.call.funcname);
            }
            expr->type_before_implicit_cast = *t;
            break;
        }

        case AST_EXPR_DEREFERENCE:
        {
            fill_types_expression(st, &expr->data.operands[0], NULL, NULL, true);
            const struct Type ptrtype = expr->data.operands[0].type_before_implicit_cast;
            if (ptrtype.kind != TYPE_POINTER)
                fail_with_error(expr->location, "the dereference operator '*' is only for pointers, not for %s", ptrtype.name);
            expr->type_before_implicit_cast = *ptrtype.data.valuetype;
            break;
        }

        case AST_EXPR_TRUE:
        case AST_EXPR_FALSE:
            expr->type_before_implicit_cast = boolType;
            break;
        case AST_EXPR_INT_CONSTANT:
            expr->type_before_implicit_cast = intType;
            break;
        case AST_EXPR_CHAR_CONSTANT:
            expr->type_before_implicit_cast = byteType;
            break;
        case AST_EXPR_STRING_CONSTANT:
            expr->type_before_implicit_cast = stringType;
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
            fill_types_expression(st, &expr->data.operands[0], NULL, NULL, true);
            fill_types_expression(st, &expr->data.operands[1], NULL, NULL, true);
            expr->type_before_implicit_cast = get_type_for_binop(
                expr->kind,
                expr->location,
                &expr->data.operands[0].type_after_implicit_cast,
                &expr->data.operands[1].type_after_implicit_cast);
            break;
    }

    if (implicit_cast_to == NULL) {
        assert(!casterrormsg);
        expr->type_after_implicit_cast = expr->type_before_implicit_cast;
    } else {
        assert(casterrormsg);
        expr->type_after_implicit_cast = *implicit_cast_to;
        if (!can_cast_implicitly(&expr->type_before_implicit_cast, implicit_cast_to))
            fail_with_implicit_cast_error(expr->location, casterrormsg, expr);
    }
}

static void fill_types_body(struct State *st, const struct AstBody *body);

static void fill_types_statement(struct State *st, struct AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_IF:
        fill_types_expression(st, &stmt->data.ifstatement.condition, &boolType,
            "'if' condition must be a boolean, not FROM", true);
        fill_types_body(st, &stmt->data.ifstatement.body);
        break;

    case AST_STMT_EXPRESSION_STATEMENT:
        fill_types_expression(st, &stmt->data.expression, NULL, NULL, false);
        break;

    case AST_STMT_RETURN_VALUE:
        if (st->func_signature->returntype == NULL) {
            fail_with_error(
                stmt->location,
                "function '%s' cannot return a value because it was defined with '-> void'",
                st->func_signature->funcname);
        }

        char msg[200];
        snprintf(msg, sizeof msg,
            "attempting to return a value of type FROM from function '%s' defined with '-> TO'",
            st->func_signature->funcname);
        fill_types_expression(st, &stmt->data.expression, st->func_signature->returntype, msg, true);
        break;

    case AST_STMT_RETURN_WITHOUT_VALUE:
        if (st->func_signature->returntype != NULL) {
            fail_with_error(
                stmt->location,
                "a return value is needed, because the return type of function '%s' is %s",
                st->func_signature->funcname,
                st->func_signature->returntype->name);
        }
        break;
    }
}

static void fill_types_body(struct State *st, const struct AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        fill_types_statement(st, &body->statements[i]);
}

static void handle_signature(struct State *st, const struct AstFunctionSignature *sig)
{
    if (find_function(st, sig->funcname))
        fail_with_error(sig->location, "a function named '%s' already exists", sig->funcname);

    if (!strcmp(sig->funcname, "main") &&
        (sig->returntype == NULL || !same_type(sig->returntype, &intType)))
    {
        fail_with_error(sig->location, "the main() function must return int");
    }

    Append(&st->functions, *sig);
}

void fill_types(struct AstToplevelNode *ast)
{
    struct State st = {0};

    for (; ast->kind != AST_TOPLEVEL_END_OF_FILE; ast++) {
        switch(ast->kind) {
        case AST_TOPLEVEL_CDECL_FUNCTION:
            handle_signature(&st, &ast->data.decl_signature);
            break;

        case AST_TOPLEVEL_DEFINE_FUNCTION:
            handle_signature(&st, &ast->data.funcdef.signature);

            for (int i = 0; i < ast->data.funcdef.signature.nargs; i++) {
                const char *name = ast->data.funcdef.signature.argnames[i];
                const struct Type *type = &ast->data.funcdef.signature.argtypes[i];
                if (find_local_variable(&st, name)) {
                    fail_with_error(
                        ast->data.funcdef.signature.location,
                        "duplicate argument name: %s", name);
                }
                add_local_variable(&st, name, type);
            }

            st.func_signature = &ast->data.funcdef.signature;
            fill_types_body(&st, &ast->data.funcdef.body);
            st.func_signature = NULL;

            Append(&st.func_locals, (struct AstLocalVariable){0});
            ast->data.funcdef.locals = st.func_locals.ptr;
            memset(&st.func_locals, 0, sizeof st.func_locals);

            break;

        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        }
    }

    free(st.functions.ptr);
}
