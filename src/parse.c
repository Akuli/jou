// Implementation of the parse() function. See compile_steps.h.

#include "jou_compiler.h"
#include "util.h"
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <string.h>

static noreturn void fail_with_parse_error(const struct Token *token, const char *what_was_expected_instead)
{
    char got[200];
    switch(token->type) {
        case TOKEN_INT: strcpy(got, "an integer"); break;
        case TOKEN_CHAR: strcpy(got, "a character"); break;
        case TOKEN_STRING: strcpy(got, "a string"); break;
        case TOKEN_OPERATOR: snprintf(got, sizeof got, "'%s'", token->data.operator); break;
        case TOKEN_NAME: snprintf(got, sizeof got, "a variable name '%s'", token->data.name); break;
        case TOKEN_NEWLINE: strcpy(got, "end of line"); break;
        case TOKEN_END_OF_FILE: strcpy(got, "end of file"); break;
        case TOKEN_INDENT: strcpy(got, "more indentation"); break;
        case TOKEN_DEDENT: strcpy(got, "less indentation"); break;
        case TOKEN_KEYWORD: snprintf(got, sizeof got, "the '%s' keyword", token->data.name); break;
    }
    fail_with_error(token->location, "expected %s, got %s", what_was_expected_instead, got);
}

static bool is_keyword(const struct Token *t, const char *kw)
{
    return t->type == TOKEN_KEYWORD && !strcmp(t->data.name, kw);
}

static bool is_operator(const struct Token *t, const char *op)
{
    return t->type == TOKEN_OPERATOR && !strcmp(t->data.operator, op);
}

static struct Type parse_type(const struct Token **tokens)
{
    if ((*tokens)->type != TOKEN_NAME)
        fail_with_parse_error(*tokens, "a type");

    struct Type result;
    // TODO: these should probalby become keywords so u cant use them in varnames
    if (!strcmp((*tokens)->data.name, "int"))
        result = intType;
    else if (!strcmp((*tokens)->data.name, "byte"))
        result = byteType;
    else if (!strcmp((*tokens)->data.name, "bool"))
        result = boolType;
    else
        fail_with_error((*tokens)->location, "type '%s' not found", (*tokens)->data.name);
    ++*tokens;

    while (is_operator(*tokens, "*")) {
        result = create_pointer_type(&result, (*tokens)->location);
        ++*tokens;
    }

    return result;
}

static struct Signature parse_function_signature(const struct Token **tokens)
{
    struct Signature result = {.location=(*tokens)->location};

    if ((*tokens)->type != TOKEN_NAME)
        fail_with_parse_error(*tokens, "a function name");
    safe_strcpy(result.funcname, (*tokens)->data.name);
    ++*tokens;

    if (!is_operator(*tokens, "("))
        fail_with_parse_error(*tokens, "a '(' to denote the start of function arguments");
    ++*tokens;

    // Must be wrapped in a struct, because C doesn't allow assigning arrays (lol)
    struct Name { char name[100]; };
    static_assert(sizeof(struct Name) == 100, "your c compiler is stupid");
    List(struct Name) argnames = {0};
    List(struct Type) argtypes = {0};

    while (!is_operator(*tokens, ")")) {
        if (result.takes_varargs)
            fail_with_error((*tokens)->location, "if '...' is used, it must be the last parameter");

        if (is_operator(*tokens, "...")) {
            result.takes_varargs = true;
            ++*tokens;
        } else {
            if ((*tokens)->type != TOKEN_NAME)
                fail_with_parse_error(*tokens, "an argument name");

            for (const struct Name *n = argnames.ptr; n < End(argnames); n++)
                if (!strcmp(n->name, (*tokens)->data.name))
                    fail_with_error((*tokens)->location, "there are multiple arguments named '%s'", n->name);

            struct Name n;
            safe_strcpy(n.name, (*tokens)->data.name);
            Append(&argnames, n);
            ++*tokens;

            if (!is_operator(*tokens, ":"))
                fail_with_parse_error(*tokens, "':' and a type after the argument name (example: \"foo: int\")");
            ++*tokens;

            Append(&argtypes, parse_type(tokens));
        }

        if (is_operator(*tokens, ","))
            ++*tokens;
        else
            break;
    }

    if (!is_operator(*tokens, ")"))
        fail_with_parse_error(*tokens, "a ')'");
    ++*tokens;

    result.argnames = (char(*)[100])argnames.ptr;  // sometimes c syntax surprises me
    result.argtypes = argtypes.ptr;
    assert(argnames.len == argtypes.len);
    result.nargs = argnames.len;

    if (!is_operator(*tokens, "->")) {
        // Special case for common typo:   def foo():
        if (is_operator(*tokens, ":")) {
            fail_with_error(
                (*tokens)->location,
                "return type must be specified with '->',"
                " or with '-> void' if the function doesn't return anything"
            );
        }
        fail_with_parse_error(*tokens, "a '->'");
    }
    ++*tokens;

    if (is_keyword(*tokens, "void")) {
        result.returntype = NULL;
        ++*tokens;
    } else {
        result.returntype = malloc(sizeof(*result.returntype));
        *result.returntype = parse_type(tokens);
    }

    return result;
}

static struct AstExpression parse_expression(const struct Token **tokens);

static struct AstCall parse_call(const struct Token **tokens)
{
    struct AstCall result;

    if ((*tokens)->type != TOKEN_NAME)
        fail_with_parse_error(*tokens, "a function name");
    safe_strcpy(result.funcname, (*tokens)->data.name);
    ++*tokens;

    if (!is_operator(*tokens, "("))
        fail_with_parse_error(*tokens, "a '(' to denote the start of function arguments");
    ++*tokens;

    List(struct AstExpression) args = {0};

    while (!is_operator(*tokens, ")")) {
        Append(&args, parse_expression(tokens));
        if (is_operator(*tokens, ","))
            ++*tokens;
        else
            break;
    }

    result.args = args.ptr;
    result.nargs = args.len;

    if (!is_operator(*tokens, ")"))
        fail_with_parse_error(*tokens, "a ')'");
    ++*tokens;

    return result;
}

static struct AstExpression parse_elementary_expression(const struct Token **tokens)
{
    struct AstExpression expr = { .location = (*tokens)->location };

    switch((*tokens)->type) {
    case TOKEN_OPERATOR:
        if (!is_operator(*tokens, "("))
            goto not_an_expression;
        ++*tokens;
        expr = parse_expression(tokens);
        if (!is_operator(*tokens, ")"))
            fail_with_parse_error(*tokens, "a ')'");
        ++*tokens;
        break;
    case TOKEN_INT:
        expr.kind = AST_EXPR_INT_CONSTANT;
        expr.data.int_value = (*tokens)->data.int_value;
        ++*tokens;
        break;
    case TOKEN_CHAR:
        expr.kind = AST_EXPR_CHAR_CONSTANT;
        expr.data.int_value = (*tokens)->data.char_value;
        ++*tokens;
        break;
    case TOKEN_STRING:
        expr.kind = AST_EXPR_STRING_CONSTANT;
        expr.data.string_value = strdup((*tokens)->data.string_value);
        ++*tokens;
        break;
    case TOKEN_NAME:
        if (is_operator(&(*tokens)[1], "(")) {
            expr.kind = AST_EXPR_CALL;
            expr.data.call = parse_call(tokens);
        } else {
            expr.kind = AST_EXPR_GET_VARIABLE;
            safe_strcpy(expr.data.varname, (*tokens)->data.name);
            ++*tokens;
        }
        break;
    case TOKEN_KEYWORD:
        if (!strcmp((*tokens)->data.name, "True")) {
            expr.kind = AST_EXPR_TRUE;
            ++*tokens;
            break;
        } else if (!strcmp((*tokens)->data.name, "False")) {
            expr.kind = AST_EXPR_FALSE;
            ++*tokens;
        } else {
            goto not_an_expression;
        }
        break;
    default:
        goto not_an_expression;
    }
    return expr;

not_an_expression:
    fail_with_parse_error(*tokens, "an expression");
}

// The & operator can go only in front of a few kinds of expressions.
// You can't do &(1 + 2), for example.
// The same rules apply to assignments.
static void validate_address_of_operand(const struct AstExpression *expr, bool assignment)
{
    char what_is_it[100];
    switch(expr->kind) {
    case AST_EXPR_GET_VARIABLE:
    case AST_EXPR_DEREFERENCE:
        return;  // ok
    case AST_EXPR_INT_CONSTANT:
    case AST_EXPR_CHAR_CONSTANT:
    case AST_EXPR_STRING_CONSTANT:
    case AST_EXPR_TRUE:
    case AST_EXPR_FALSE:
        strcpy(what_is_it, "a constant");
        break;
    case AST_EXPR_ASSIGN:
        strcpy(what_is_it, "an assignment");
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
        strcpy(what_is_it, "a newly calculated value");
        break;
    }

    if (assignment)
        fail_with_error(expr->location, "cannot assign to %s", what_is_it);
    else
        fail_with_error(expr->location, "the address-of operator '&' cannot be used with %s", what_is_it);
}

// arity = number of operands, e.g. 2 for a binary operator such as "+"
static struct AstExpression build_operator_expression(const struct Token *t, int arity, const struct AstExpression *operands)
{
    assert(arity==1 || arity==2);
    size_t nbytes = arity * sizeof operands[0];
    struct AstExpression *ptr = malloc(nbytes);
    memcpy(ptr, operands, nbytes);

    struct AstExpression result = { .location = t->location, .data.operands = ptr };

    if (is_operator(t, "&")) {
        assert(arity == 1);
        result.kind = AST_EXPR_ADDRESS_OF;
        validate_address_of_operand(&operands[0], false);
    } else if (is_operator(t, "=")) {
        assert(arity == 2);
        result.kind = AST_EXPR_ASSIGN;
        validate_address_of_operand(&operands[0], true);
    } else if (is_operator(t, "==")) {
        assert(arity == 2);
        result.kind = AST_EXPR_EQ;
    } else if (is_operator(t, "!=")) {
        assert(arity == 2);
        result.kind = AST_EXPR_NE;
    } else if (is_operator(t, ">")) {
        assert(arity == 2);
        result.kind = AST_EXPR_GT;
    } else if (is_operator(t, ">=")) {
        assert(arity == 2);
        result.kind = AST_EXPR_GE;
    } else if (is_operator(t, "<")) {
        assert(arity == 2);
        result.kind = AST_EXPR_LT;
    } else if (is_operator(t, "<=")) {
        assert(arity == 2);
        result.kind = AST_EXPR_LE;
    } else if (is_operator(t, "+")) {
        assert(arity == 2);
        result.kind = AST_EXPR_ADD;
    } else if (is_operator(t, "-")) {
        assert(arity == 2);
        result.kind = AST_EXPR_SUB;
    } else if (is_operator(t, "*")) {
        result.kind = arity==2 ? AST_EXPR_MUL : AST_EXPR_DEREFERENCE;
    } else if (is_operator(t, "/")) {
        assert(arity == 2);
        result.kind = AST_EXPR_DIV;
    } else {
        assert(0);
    }

    return result;
}

static struct AstExpression parse_expression_with_addressof_and_dereference(const struct Token **tokens)
{
    // sequnece of 0 or more star tokens: start,start+1,...,end-1
    const struct Token *start = *tokens;
    const struct Token *end = *tokens;
    while (is_operator(end, "*") || is_operator(end, "&")) end++;

    *tokens = end;
    struct AstExpression result = parse_elementary_expression(tokens);
    for (const struct Token *t = end-1; t >= start; t--)
        result = build_operator_expression(t, 1, &result);
    return result;
}

// If tokens is e.g. [1, '+', 2], this will be used to parse the ['+', 2] part.
// Callback function cb defines how to parse the expression following the operator token.
static void add_to_binop(
    const struct Token **tokens, struct AstExpression *result,
    struct AstExpression (*cb)(const struct Token**))
{
    const struct Token *t = (*tokens)++;
    struct AstExpression rhs = cb(tokens);
    *result = build_operator_expression(t, 2, (struct AstExpression[]){*result, rhs});
}

static struct AstExpression parse_expression_with_mul_and_div(const struct Token **tokens)
{
    struct AstExpression result = parse_expression_with_addressof_and_dereference(tokens);
    while (is_operator(*tokens, "*") || is_operator(*tokens, "/"))
        add_to_binop(tokens, &result, parse_expression_with_addressof_and_dereference);
    return result;
}

static struct AstExpression parse_expression_with_add(const struct Token **tokens)
{
    struct AstExpression result = parse_expression_with_mul_and_div(tokens);
    while (is_operator(*tokens, "+") || is_operator(*tokens, "-"))
        add_to_binop(tokens, &result, parse_expression_with_mul_and_div);
    return result;
}

static struct AstExpression parse_expression_with_comparisons(const struct Token **tokens)
{
    struct AstExpression result = parse_expression_with_add(tokens);
#define IsComparator(x) (is_operator((x),"<") || is_operator((x),">") || is_operator((x),"<=") || is_operator((x),">=") || is_operator((x),"==") || is_operator((x),"!="))
    if (IsComparator(*tokens))
        add_to_binop(tokens, &result, parse_expression_with_add);
    if (IsComparator(*tokens))
        fail_with_error((*tokens)->location, "comparisons cannot be chained");
#undef IsComparator
    return result;
}

static struct AstExpression parse_expression_with_assignments(const struct Token **tokens)
{
    // We can't use add_to_binop() because then x=y=z would parse as (x=y)=z, not x=(y=z).
    struct AstExpression target = parse_expression_with_comparisons(tokens);
    if (!is_operator(*tokens, "="))
        return target;
    validate_address_of_operand(&target, true);
    const struct Token *t = (*tokens)++;
    struct AstExpression value = parse_expression_with_assignments(tokens);  // this function
    return build_operator_expression(t, 2, (struct AstExpression[]){ target, value });
}

static struct AstExpression parse_expression(const struct Token **tokens)
{
    return parse_expression_with_assignments(tokens);
}

static void eat_newline(const struct Token **tokens)
{
    if ((*tokens)->type != TOKEN_NEWLINE)
        fail_with_parse_error(*tokens, "end of line");
    ++*tokens;
}

static void validate_expression_statement(const struct AstExpression *expr)
{
    switch(expr->kind) {
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
    case AST_EXPR_GET_VARIABLE:
    case AST_EXPR_ADDRESS_OF:
    case AST_EXPR_DEREFERENCE:
    case AST_EXPR_CHAR_CONSTANT:
    case AST_EXPR_INT_CONSTANT:
    case AST_EXPR_STRING_CONSTANT:
    case AST_EXPR_TRUE:
    case AST_EXPR_FALSE:
        fail_with_error(expr->location, "not a valid statement");
        break;
    case AST_EXPR_ASSIGN:
    case AST_EXPR_CALL:
        break;
    }
}

static struct AstBody parse_body(const struct Token **tokens);

static struct AstStatement parse_statement(const struct Token **tokens)
{
    struct AstStatement result = { .location = (*tokens)->location };
    if (is_keyword(*tokens, "return")) {
        ++*tokens;
        if ((*tokens)->type == TOKEN_NEWLINE) {
            result.kind = AST_STMT_RETURN_WITHOUT_VALUE;
        } else {
            result.kind = AST_STMT_RETURN_VALUE;
            result.data.expression = parse_expression(tokens);
        }
        eat_newline(tokens);
    } else if (is_keyword(*tokens, "if")) {
        ++*tokens;
        result.kind = AST_STMT_IF;
        result.data.ifstatement.condition = parse_expression(tokens);
        result.data.ifstatement.body = parse_body(tokens);
    } else if (is_keyword(*tokens, "while")) {
        ++*tokens;
        result.kind = AST_STMT_WHILE;
        result.data.whileloop.condition = parse_expression(tokens);
        result.data.whileloop.body = parse_body(tokens);
    } else if (is_keyword(*tokens, "break")) {
        ++*tokens;
        result.kind = AST_STMT_BREAK;
        eat_newline(tokens);
    } else if (is_keyword(*tokens, "continue")) {
        ++*tokens;
        result.kind = AST_STMT_CONTINUE;
        eat_newline(tokens);
    } else {
        result.kind = AST_STMT_EXPRESSION_STATEMENT;
        result.data.expression = parse_expression(tokens);
        validate_expression_statement(&result.data.expression);
        eat_newline(tokens);
    }
    return result;
}

static struct AstBody parse_body(const struct Token **tokens)
{
    if (!is_operator(*tokens, ":"))
        fail_with_parse_error(*tokens, "':' followed by a new line with more indentation");
    ++*tokens;

    if ((*tokens)->type != TOKEN_NEWLINE)
        fail_with_parse_error(*tokens, "a new line with more indentation after ':'");
    ++*tokens;

    if ((*tokens)->type != TOKEN_INDENT)
        fail_with_parse_error(*tokens, "more indentation after ':'");
    ++*tokens;

    List(struct AstStatement) result = {0};
    while ((*tokens)->type != TOKEN_DEDENT)
        Append(&result, parse_statement(tokens));
    ++*tokens;

    return (struct AstBody){ .statements=result.ptr, .nstatements=result.len };
}

static struct AstToplevelNode parse_toplevel_node(const struct Token **tokens)
{
    struct AstToplevelNode result = { .location = (*tokens)->location };

    switch((*tokens)->type) {
    case TOKEN_END_OF_FILE:
        result.kind = AST_TOPLEVEL_END_OF_FILE;
        break;

    case TOKEN_KEYWORD:
        if (is_keyword(*tokens, "def")) {
            ++*tokens;  // skip 'def' keyword
            result.kind = AST_TOPLEVEL_DEFINE_FUNCTION;
            result.data.funcdef.signature = parse_function_signature(tokens);
            if (result.data.funcdef.signature.takes_varargs) {
                // TODO: support "def foo(x: str, ...)" in some way
                fail_with_error(
                    result.data.funcdef.signature.location,
                    "functions with variadic arguments cannot be defined yet");
            }
            result.data.funcdef.body = parse_body(tokens);
            break;
        }
        if (is_keyword(*tokens, "cdecl")) {
            ++*tokens;
            result.kind = AST_TOPLEVEL_CDECL_FUNCTION;
            result.data.decl_signature = parse_function_signature(tokens);
            eat_newline(tokens);
            break;
        }
        // fall through

    default:
        fail_with_parse_error(*tokens, "a C function declaration or a function definition");
    }

    return result;
}   

struct AstToplevelNode *parse(const struct Token *tokens)
{
    List(struct AstToplevelNode) result = {0};
    do {
        Append(&result, parse_toplevel_node(&tokens));
    } while (result.ptr[result.len - 1].kind != AST_TOPLEVEL_END_OF_FILE);

    return result.ptr;
}
