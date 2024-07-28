// Implementation of the parse() function.

#include "jou_compiler.h"
#include "util.h"
#include <libgen.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const Token *tokens;
    const char *stdlib_path;
    bool is_parsing_method_body;
} ParserState;

static AstExpression parse_expression(ParserState *ps);

static noreturn void fail_with_parse_error(const Token *token, const char *what_was_expected_instead)
{
    char got[200];
    switch(token->type) {
        case TOKEN_SHORT: strcpy(got, "a short"); break;
        case TOKEN_INT: strcpy(got, "an integer"); break;
        case TOKEN_LONG: strcpy(got, "a long integer"); break;
        case TOKEN_FLOAT: strcpy(got, "a float constant"); break;
        case TOKEN_DOUBLE: strcpy(got, "a double constant"); break;
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
    fail(token->location, "expected %s, got %s", what_was_expected_instead, got);
}

static bool is_keyword(const Token *t, const char *kw)
{
    return t->type == TOKEN_KEYWORD && !strcmp(t->data.name, kw);
}

static bool is_operator(const Token *t, const char *op)
{
    return t->type == TOKEN_OPERATOR && !strcmp(t->data.operator, op);
}

static AstType parse_type(ParserState *ps)
{
    AstType result = { .kind = AST_TYPE_NAMED, .location = ps->tokens->location };

    if (ps->tokens->type != TOKEN_NAME
        && !is_keyword(ps->tokens, "None")
        && !is_keyword(ps->tokens, "void")
        && !is_keyword(ps->tokens, "noreturn")
        && !is_keyword(ps->tokens, "short")
        && !is_keyword(ps->tokens, "int")
        && !is_keyword(ps->tokens, "long")
        && !is_keyword(ps->tokens, "byte")
        && !is_keyword(ps->tokens, "float")
        && !is_keyword(ps->tokens, "double")
        && !is_keyword(ps->tokens, "bool"))
    {
        fail_with_parse_error(ps->tokens, "a type");
    }
    safe_strcpy(result.data.name, ps->tokens->data.name);
    ps->tokens++;

    while(is_operator(ps->tokens, "*") || is_operator(ps->tokens, "[")) {
        AstType *p = malloc(sizeof(*p));
        *p = result;

        if (is_operator(ps->tokens, "*")) {
            result = (AstType){
                .location = ps->tokens++->location,
                .kind = AST_TYPE_POINTER,
                .data.valuetype = p,
            };
        } else {
            Location location = ps->tokens++->location;

            AstExpression *len = malloc(sizeof(*len));
            *len = parse_expression(ps);

            if (!is_operator(ps->tokens, "]"))
                fail_with_parse_error(ps->tokens, "a ']' to end the array size");
            ps->tokens++;

            result = (AstType){
                .location = location,
                .kind = AST_TYPE_ARRAY,
                .data.array = {.membertype=p, .len=len},
            };
        }
    }

    return result;
}

// name: type = value
// The value is optional, and will be NULL if missing.
static AstNameTypeValue parse_name_type_value(ParserState *ps, const char *expected_what_for_name)
{
    AstNameTypeValue result;

    if (ps->tokens->type != TOKEN_NAME) {
        assert(expected_what_for_name);
        fail_with_parse_error(ps->tokens, expected_what_for_name);
    }
    safe_strcpy(result.name, ps->tokens->data.name);
    result.name_location = ps->tokens->location;
    ps->tokens++;

    if (!is_operator(ps->tokens, ":"))
        fail_with_parse_error(ps->tokens, "':' and a type after it (example: \"foo: int\")");
    ps->tokens++;
    result.type = parse_type(ps);

    if (is_operator(ps->tokens, "=")) {
        ps->tokens++;
        AstExpression *p = malloc(sizeof *p);
        *p = parse_expression(ps);
        result.value = p;
    } else {
        result.value = NULL;
    }

    return result;
}

static AstSignature parse_function_signature(ParserState *ps, bool accept_self)
{
    AstSignature result = {0};
    bool used_self = false;
    if (ps->tokens->type != TOKEN_NAME)
        fail_with_parse_error(ps->tokens, "a function name");
    result.name_location = ps->tokens->location;
    safe_strcpy(result.name, ps->tokens->data.name);
    ps->tokens++;

    if (!is_operator(ps->tokens, "("))
        fail_with_parse_error(ps->tokens, "a '(' to denote the start of function arguments");
    ps->tokens++;

    while (!is_operator(ps->tokens, ")")) {
        if (result.takes_varargs)
            fail(ps->tokens->location, "if '...' is used, it must be the last parameter");

        if (is_operator(ps->tokens, "...")) {
            result.takes_varargs = true;
            ps->tokens++;
        } else if (is_keyword(ps->tokens, "self")) {
            if (!accept_self)
                fail(ps->tokens->location, "'self' cannot be used here");
            AstNameTypeValue self_arg = { .name="self", .type.kind = AST_TYPE_NAMED, .type.data.name = "", .name_location=ps->tokens++->location };
            if (is_operator(ps->tokens, ":")) {
                ps->tokens++;
                self_arg.type = parse_type(ps);
            }
            Append(&result.args, self_arg);
            used_self = true;
        } else {
            AstNameTypeValue arg = parse_name_type_value(ps, "an argument name");

            if (arg.value)
                fail(arg.value->location, "arguments cannot have default values");

            for (const AstNameTypeValue *prevarg = result.args.ptr; prevarg < End(result.args); prevarg++)
                if (!strcmp(prevarg->name, arg.name))
                    fail(arg.name_location, "there are multiple arguments named '%s'", prevarg->name);
            Append(&result.args, arg);
        }

        if (is_operator(ps->tokens, ","))
            ps->tokens++;
        else
            break;
    }
    if (!is_operator(ps->tokens, ")"))
        fail_with_parse_error(ps->tokens, "a ')'");
    ps->tokens++;

    if (!is_operator(ps->tokens, "->")) {
        // Special case for common typo:   def foo():
        if (is_operator(ps->tokens, ":")) {
            fail(
                ps->tokens->location,
                "return type must be specified with '->',"
                " or with '-> None' if the function doesn't return anything"
            );
        }
        fail_with_parse_error(ps->tokens, "a '->'");
    }
    ps->tokens++;

    if (!used_self && accept_self) {
        fail(
            ps->tokens->location,
            "missing self, should be 'def %s(self, ...)'",
            result.name
        );
    }

    result.returntype = parse_type(ps);
    return result;
}

static AstCall parse_call(ParserState *ps, char openparen, char closeparen, bool args_are_named)
{
    AstCall result = {0};

    assert(ps->tokens->type == TOKEN_NAME);  // must be checked when calling this function
    safe_strcpy(result.calledname, ps->tokens->data.name);
    ps->tokens++;

    if (!is_operator(ps->tokens, (char[]){openparen,'\0'})) {
        char msg[100];
        sprintf(msg, "a '%c' to denote the start of arguments", openparen);
        fail_with_parse_error(ps->tokens, msg);
    }
    ps->tokens++;

    List(AstExpression) args = {0};

    // char[100] is wrapped in a struct so we can make a list of it.
    // You can't do List(char[100]) or similar because C doesn't allow assigning arrays.
    struct Name { char name[100]; };
    static_assert(sizeof(struct Name) == 100, "u have weird c compiler...");
    List(struct Name) argnames = {0};

    while (!is_operator(ps->tokens, (char[]){closeparen,'\0'})) {
        if (args_are_named) {
            // This code is only for instantiating classes, because there are no named function arguments.

            if (ps->tokens->type != TOKEN_NAME)
                fail_with_parse_error(ps->tokens,"a field name");

            for (struct Name *oldname = argnames.ptr; oldname < End(argnames); oldname++) {
                if (!strcmp(oldname->name, ps->tokens->data.name)) {
                    fail(
                        ps->tokens->location, "multiple values were given for field '%s'", oldname->name);
                }
            }

            struct Name n;
            safe_strcpy(n.name,ps->tokens->data.name);
            Append(&argnames,n);
            ps->tokens++;

            if (!is_operator(ps->tokens, "=")) {
                char msg[300];
                snprintf(msg, sizeof msg, "'=' followed by a value for field '%s'", n.name);
                fail_with_parse_error(ps->tokens, msg);
            }
            ps->tokens++;
        }

        Append(&args, parse_expression(ps));
        if (is_operator(ps->tokens, ","))
            ps->tokens++;
        else
            break;
    }

    result.args = args.ptr;
    result.argnames = (char(*)[100])argnames.ptr;  // can be NULL
    result.nargs = args.len;

    if (!is_operator(ps->tokens, (char[]){closeparen,'\0'})) {
        char msg[100];
        sprintf(msg, "a '%c'", closeparen);
        fail_with_parse_error(ps->tokens, "a ')'");
    }
    ps->tokens++;

    return result;
}

// arity = number of operands, e.g. 2 for a binary operator such as "+"
static AstExpression build_operator_expression(const Token *t, int arity, const AstExpression *operands)
{
    assert(arity==1 || arity==2);
    size_t nbytes = arity * sizeof operands[0];
    AstExpression *ptr = malloc(nbytes);
    memcpy(ptr, operands, nbytes);

    AstExpression result = { .location = t->location, .data.operands = ptr };

    if (is_operator(t, "&")) {
        assert(arity == 1);
        result.kind = AST_EXPR_ADDRESS_OF;
    } else if (is_operator(t, "[")) {
        assert(arity == 2);
        result.kind = AST_EXPR_INDEXING;
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
        result.kind = arity==2 ? AST_EXPR_SUB : AST_EXPR_NEG;
    } else if (is_operator(t, "*")) {
        result.kind = arity==2 ? AST_EXPR_MUL : AST_EXPR_DEREFERENCE;
    } else if (is_operator(t, "/")) {
        assert(arity == 2);
        result.kind = AST_EXPR_DIV;
    } else if (is_operator(t, "%")) {
        assert(arity == 2);
        result.kind = AST_EXPR_MOD;
    } else if (is_keyword(t, "and")) {
        assert(arity == 2);
        result.kind = AST_EXPR_AND;
    } else if (is_keyword(t, "or")) {
        assert(arity == 2);
        result.kind = AST_EXPR_OR;
    } else if (is_keyword(t, "not")) {
        assert(arity == 1);
        result.kind = AST_EXPR_NOT;
    } else {
        assert(0);
    }

    return result;
}

// If tokens is e.g. [1, '+', 2], this will be used to parse the ['+', 2] part.
// Callback function cb defines how to parse the expression following the operator token.
static void add_to_binop(ParserState *ps, AstExpression *result, AstExpression (*cb)(ParserState*))
{
    const Token *t = ps->tokens++;
    AstExpression rhs = cb(ps);
    *result = build_operator_expression(t, 2, (AstExpression[]){*result, rhs});
}

static AstExpression parse_array(ParserState *ps)
{
    const Token *openbracket = ps->tokens;
    assert(is_operator(openbracket, "["));
    ps->tokens++;

    if (is_operator(ps->tokens, "]"))
        fail(ps->tokens->location, "arrays cannot be empty");

    List(AstExpression) items = {0};
    do {
        Append(&items, parse_expression(ps));
    } while (is_operator(ps->tokens, ",") && !is_operator(++ps->tokens, "]"));

    if (!is_operator(ps->tokens, "]"))
        fail_with_parse_error(ps->tokens, "a ']' to end the array");
    ps->tokens++;

    return (AstExpression){
        .kind=AST_EXPR_ARRAY,
        .location = openbracket->location,
        .data.array = {.count=items.len, .items=items.ptr},
    };
}

static AstExpression parse_elementary_expression(ParserState *ps)
{
    AstExpression expr = { .location = ps->tokens->location };

    switch(ps->tokens->type) {
    case TOKEN_OPERATOR:
        if (is_operator(ps->tokens, "[")) {
            expr = parse_array(ps);
        } else if (is_operator(ps->tokens, "(")) {
            ps->tokens++;
            expr = parse_expression(ps);
            if (!is_operator(ps->tokens, ")"))
                fail_with_parse_error(ps->tokens, "a ')'");
            ps->tokens++;
        } else {
            goto not_an_expression;
        }
        break;
    case TOKEN_SHORT:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = int_constant(shortType, ps->tokens->data.short_value);
        ps->tokens++;
        break;
    case TOKEN_INT:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = int_constant(intType, ps->tokens->data.int_value);
        ps->tokens++;
        break;
    case TOKEN_LONG:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = int_constant(longType, ps->tokens->data.long_value);
        ps->tokens++;
        break;
    case TOKEN_CHAR:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = int_constant(byteType, ps->tokens->data.char_value);
        ps->tokens++;
        break;
    case TOKEN_FLOAT:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = (Constant){ .kind=CONSTANT_FLOAT };
        safe_strcpy(expr.data.constant.data.double_or_float_text, ps->tokens->data.name);
        ps->tokens++;
        break;
    case TOKEN_DOUBLE:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = (Constant){ .kind=CONSTANT_DOUBLE };
        safe_strcpy(expr.data.constant.data.double_or_float_text, ps->tokens->data.name);
        ps->tokens++;
        break;
    case TOKEN_STRING:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = (Constant){ CONSTANT_STRING, {.str=strdup(ps->tokens->data.string_value)} };
        ps->tokens++;
        break;
    case TOKEN_NAME:
        if (is_operator(&ps->tokens[1], "(")) {
            expr.kind = AST_EXPR_FUNCTION_CALL;
            expr.data.call = parse_call(ps, '(', ')', false);
        } else if (is_operator(&ps->tokens[1], "{")) {
            expr.kind = AST_EXPR_BRACE_INIT;
            expr.data.call = parse_call(ps, '{', '}', true);
        } else if (is_operator(&ps->tokens[1], "::") && ps->tokens[2].type == TOKEN_NAME) {
            expr.kind = AST_EXPR_GET_ENUM_MEMBER;
            safe_strcpy(expr.data.enummember.enumname, ps->tokens[0].data.name);
            safe_strcpy(expr.data.enummember.membername, ps->tokens[2].data.name);
            ps->tokens += 3;
        } else {
            expr.kind = AST_EXPR_GET_VARIABLE;
            safe_strcpy(expr.data.varname, ps->tokens->data.name);
            ps->tokens++;
        }
        break;
    case TOKEN_KEYWORD:
        if (is_keyword(ps->tokens, "True") || is_keyword(ps->tokens, "False")) {
            expr.kind = AST_EXPR_CONSTANT;
            expr.data.constant = (Constant){ CONSTANT_BOOL, {.boolean=is_keyword(ps->tokens, "True")} };
            ps->tokens++;
        } else if (is_keyword(ps->tokens, "NULL")) {
            expr.kind = AST_EXPR_CONSTANT;
            expr.data.constant = (Constant){ CONSTANT_NULL, {{0}} };
            ps->tokens++;
        } else if (is_keyword(ps->tokens, "self")) {
            if (!ps->is_parsing_method_body)
                fail(ps->tokens->location, "'self' cannot be used here");
            expr.kind = AST_EXPR_GET_VARIABLE;
            strcpy(expr.data.varname, "self");
            ps->tokens++;
        } else if (is_keyword(ps->tokens, "None")) {
            fail(ps->tokens[0].location, "None is not a value in Jou, use e.g. -1 for numbers or NULL for pointers");
        } else {
            goto not_an_expression;
        }
        break;
    default:
        goto not_an_expression;
    }
    return expr;

not_an_expression:
    fail_with_parse_error(ps->tokens, "an expression");
}

static AstExpression parse_expression_with_fields_and_methods_and_indexing(ParserState *ps)
{
    AstExpression result = parse_elementary_expression(ps);
    while (is_operator(ps->tokens, ".") || is_operator(ps->tokens, "->") || is_operator(ps->tokens, "["))
    {
        if (is_operator(ps->tokens, "[")) {
            add_to_binop(ps, &result, parse_expression);  // eats [ token
            if (!is_operator(ps->tokens, "]"))
                fail_with_parse_error(ps->tokens, "a ']'");
            ps->tokens++;
        } else {
            AstExpression *obj = malloc(sizeof *obj);
            *obj = result;
            memset(&result, 0, sizeof result);

            const Token *startop = ps->tokens++;
            if (ps->tokens->type != TOKEN_NAME)
                fail_with_parse_error(ps->tokens, "a field or method name");
            result.location = ps->tokens->location;

            bool is_deref = is_operator(startop, "->");
            bool is_call = is_operator(&ps->tokens[1], "(");
            if (is_deref && is_call) result.kind = AST_EXPR_DEREF_AND_CALL_METHOD;
            if (is_deref && !is_call) result.kind = AST_EXPR_DEREF_AND_GET_FIELD;
            if (!is_deref && is_call) result.kind = AST_EXPR_CALL_METHOD;
            if (!is_deref && !is_call) result.kind = AST_EXPR_GET_FIELD;

            if (is_call) {
                result.data.methodcall.obj = obj;
                result.data.methodcall.call = parse_call(ps, '(', ')', false);
            } else {
                result.data.classfield.obj = obj;
                safe_strcpy(result.data.classfield.fieldname, ps->tokens->data.name);
                ps->tokens++;
            }
        }
    }
    return result;
}

// Unary operators: foo++, foo--, ++foo, --foo, &foo, *foo, sizeof foo
static AstExpression parse_expression_with_unary_operators(ParserState *ps)
{
    // sequneces of 0 or more unary operator tokens: start,start+1,...,end-1
    const Token *prefixstart = ps->tokens;
    while(is_operator(ps->tokens,"++")||is_operator(ps->tokens,"--")||is_operator(ps->tokens,"&")||is_operator(ps->tokens,"*")||is_keyword(ps->tokens,"sizeof")) ps->tokens++;
    const Token *prefixend = ps->tokens;

    AstExpression result = parse_expression_with_fields_and_methods_and_indexing(ps);

    const Token *suffixstart = ps->tokens;
    while(is_operator(ps->tokens,"++")||is_operator(ps->tokens,"--")) ps->tokens++;
    const Token *suffixend = ps->tokens;

    while (prefixstart<prefixend || suffixstart<suffixend) {
        // ++ and -- "bind tighter", so *foo++ is equivalent to *(foo++)
        // It is implemented by always consuming ++/-- prefixes and suffixes when they exist.
        Location loc;
        enum AstExpressionKind k;
        if (prefixstart<prefixend && is_operator(prefixend-1, "++")) {
            k = AST_EXPR_PRE_INCREMENT;
            loc = (--prefixend)->location;
        } else if (prefixstart<prefixend && is_operator(prefixend-1, "--")) {
            k = AST_EXPR_PRE_DECREMENT;
            loc = (--prefixend)->location;
        } else if (suffixstart<suffixend && is_operator(suffixstart, "++")) {
            k = AST_EXPR_POST_INCREMENT;
            loc = suffixstart++->location;
        } else if (suffixstart<suffixend && is_operator(suffixstart, "--")) {
            k = AST_EXPR_POST_DECREMENT;
            loc = suffixstart++->location;
        } else {
            assert(prefixstart<prefixend && suffixstart==suffixend);
            if (is_operator(prefixend-1, "*"))
                k = AST_EXPR_DEREFERENCE;
            else if (is_operator(prefixend-1, "&"))
                k = AST_EXPR_ADDRESS_OF;
            else if (is_keyword(prefixend-1, "sizeof"))
                k = AST_EXPR_SIZEOF;
            else
                assert(0);
            loc = (--prefixend)->location;
        }

        AstExpression *p = malloc(sizeof(*p));
        *p = result;
        result = (AstExpression){ .location=loc, .kind=k, .data.operands=p };
    }

    return result;
}

static AstExpression parse_expression_with_mul_and_div(ParserState *ps)
{
    AstExpression result = parse_expression_with_unary_operators(ps);
    while (is_operator(ps->tokens, "*") || is_operator(ps->tokens, "/") || is_operator(ps->tokens, "%"))
        add_to_binop(ps, &result, parse_expression_with_unary_operators);
    return result;
}

static AstExpression parse_expression_with_add(ParserState *ps)
{
    const Token *minus = is_operator(ps->tokens, "-") ? ps->tokens++ : NULL;
    AstExpression result = parse_expression_with_mul_and_div(ps);
    if (minus)
        result = build_operator_expression(minus, 1, &result);

    while (is_operator(ps->tokens, "+") || is_operator(ps->tokens, "-"))
        add_to_binop(ps, &result, parse_expression_with_mul_and_div);
    return result;
}

// "as" operator has somewhat low precedence, so that "1+2 as float" works as expected
static AstExpression parse_expression_with_as(ParserState *ps)
{
    AstExpression result = parse_expression_with_add(ps);
    while (is_keyword(ps->tokens, "as")) {
        AstExpression *p = malloc(sizeof(*p));
        *p = result;
        Location as_location = ps->tokens++->location;
        AstType t = parse_type(ps);
        result = (AstExpression){ .location=as_location, .kind=AST_EXPR_AS, .data.as = { .obj=p, .type=t } };
    }
    return result;
}

static AstExpression parse_expression_with_comparisons(ParserState *ps)
{
    AstExpression result = parse_expression_with_as(ps);
#define IsComparator(x) (is_operator((x),"<") || is_operator((x),">") || is_operator((x),"<=") || is_operator((x),">=") || is_operator((x),"==") || is_operator((x),"!="))
    if (IsComparator(ps->tokens))
        add_to_binop(ps, &result, parse_expression_with_as);
    if (IsComparator(ps->tokens))
        fail(ps->tokens->location, "comparisons cannot be chained");
#undef IsComparator
    return result;
}

static AstExpression parse_expression_with_not(ParserState *ps)
{
    const Token *nottoken = NULL;
    if (is_keyword(ps->tokens, "not")) {
        nottoken = ps->tokens;
        ps->tokens++;
    }
    if (is_keyword(ps->tokens, "not"))
        fail(ps->tokens->location, "'not' cannot be repeated");

    AstExpression result = parse_expression_with_comparisons(ps);
    if (nottoken)
        result = build_operator_expression(nottoken, 1, &result);
    return result;
}

static AstExpression parse_expression_with_and_or(ParserState *ps)
{
    AstExpression result = parse_expression_with_not(ps);
    bool got_and = false, got_or = false;

    while (is_keyword(ps->tokens, "and") || is_keyword(ps->tokens, "or")) {
        got_and = got_and || is_keyword(ps->tokens, "and");
        got_or = got_or || is_keyword(ps->tokens, "or");
        if (got_and && got_or)
            fail(ps->tokens->location, "'and' cannot be chained with 'or', you need more parentheses");

        add_to_binop(ps, &result, parse_expression_with_not);
    }

    return result;
}

static AstExpression parse_expression(ParserState *ps)
{
    return parse_expression_with_and_or(ps);
}

static void eat_newline(ParserState *ps)
{
    if (ps->tokens->type != TOKEN_NEWLINE)
        fail_with_parse_error(ps->tokens, "end of line");
    ps->tokens++;
}

static void validate_expression_statement(const AstExpression *expr)
{
    switch(expr->kind) {
    case AST_EXPR_FUNCTION_CALL:
    case AST_EXPR_CALL_METHOD:
    case AST_EXPR_DEREF_AND_CALL_METHOD:
    case AST_EXPR_PRE_INCREMENT:
    case AST_EXPR_PRE_DECREMENT:
    case AST_EXPR_POST_INCREMENT:
    case AST_EXPR_POST_DECREMENT:
        break;
    default:
        fail(expr->location, "not a valid statement");
        break;
    }
}

static AstBody parse_body(ParserState *ps);

static AstIfStatement parse_if_statement(ParserState *ps)
{
    List(AstConditionAndBody) if_elifs = {0};

    assert(is_keyword(ps->tokens, "if"));
    do {
        ps->tokens++;
        AstExpression cond = parse_expression(ps);
        AstBody body = parse_body(ps);
        Append(&if_elifs, (AstConditionAndBody){cond,body});
    } while (is_keyword(ps->tokens, "elif"));

    AstBody elsebody = {0};
    if (is_keyword(ps->tokens, "else")) {
        ps->tokens++;
        elsebody = parse_body(ps);
    }

    return (AstIfStatement){
        .if_and_elifs = if_elifs.ptr,
        .n_if_and_elifs = if_elifs.len,
        .elsebody = elsebody,
    };
}

// reverse code golfing: https://xkcd.com/1960/
static enum AstStatementKind determine_the_kind_of_a_statement_that_starts_with_an_expression(
    const Token *this_token_is_after_that_initial_expression)
{
    if (is_operator(this_token_is_after_that_initial_expression, "="))
        return AST_STMT_ASSIGN;
    if (is_operator(this_token_is_after_that_initial_expression, "+="))
        return AST_STMT_INPLACE_ADD;
    if (is_operator(this_token_is_after_that_initial_expression, "-="))
        return AST_STMT_INPLACE_SUB;
    if (is_operator(this_token_is_after_that_initial_expression, "*="))
        return AST_STMT_INPLACE_MUL;
    if (is_operator(this_token_is_after_that_initial_expression, "/="))
        return AST_STMT_INPLACE_DIV;
    if (is_operator(this_token_is_after_that_initial_expression, "%="))
        return AST_STMT_INPLACE_MOD;
    return AST_STMT_EXPRESSION_STATEMENT;
}

static char *read_assertion_from_file(Location error_location, const Token *start, const Token *end)
{
    FILE *f = fopen(error_location.filename, "rb");
    if (!f)
        goto error;

    List(char) result = {0};

    long ostart, oend;  // offsets within file to include
    for (const Token *t = start; t < end; t++) {
        assert(t->start_offset < t->end_offset);

        if (t == start || t->location.lineno != t[-1].location.lineno) {
            // First token of a new line
            ostart = t->start_offset;
            oend = t->end_offset;
        } else {
            // Include more tokens from the line of code so that this token is added too.
            // We cannot include the entire line because it might contain comments.
            assert(oend <= t->start_offset);
            oend = t->end_offset;
        }

        if (t == end-1 || t[0].location.lineno != t[1].location.lineno) {
            // Last token of a line. Read code from file.
            if (fseek(f, ostart, SEEK_SET) < 0)
                goto error;
            if (result.len > 0)
                Append(&result, '\n');
            for (long i = ostart; i < oend; i++) {
                int c = fgetc(f);
                if (c == EOF || c == '\r' || c == '\n')
                    goto error;
                Append(&result, (char)c);
            }
        }
    }

    /*
    Join lines with spaces, but do not put spaces just after '(' or before ')'.
    This makes multiline asserts nicer, so "assert (\n    foo and bar\n)"
    shows "foo and bar" as the assert condition.
    */
    Append(&result, '\0');
    char *p;
    while ((p = strstr(result.ptr, "\n"))) {
        if ((p > result.ptr && p[-1] == '(') || (p[1] == ')')) {
            memmove(p, p+1, strlen(p));  // delete newline character at p
        } else {
            *p = ' ';  // join lines with a space
        }
    }

    fclose(f);
    return result.ptr;

error:
    fail(error_location, "internal error: cannot read assertion text from file");
}

// does not eat a trailing newline
static AstStatement parse_oneline_statement(ParserState *ps)
{
    AstStatement result = { .location = ps->tokens->location };
    if (is_keyword(ps->tokens, "return")) {
        ps->tokens++;
        result.kind = AST_STMT_RETURN;
        if (ps->tokens->type != TOKEN_NEWLINE) {
            result.data.returnvalue = malloc(sizeof *result.data.returnvalue);
            *result.data.returnvalue = parse_expression(ps);
        }
    } else if (is_keyword(ps->tokens, "assert")) {
        ps->tokens++;
        result.kind = AST_STMT_ASSERT;
        const Token *condstart = ps->tokens;
        result.data.assertion.condition = parse_expression(ps);
        result.data.assertion.condition_str = read_assertion_from_file(result.location, condstart, ps->tokens);
    } else if (is_keyword(ps->tokens, "pass")) {
        ps->tokens++;
        result.kind = AST_STMT_PASS;
    } else if (is_keyword(ps->tokens, "break")) {
        ps->tokens++;
        result.kind = AST_STMT_BREAK;
    } else if (is_keyword(ps->tokens, "continue")) {
        ps->tokens++;
        result.kind = AST_STMT_CONTINUE;
    } else if (ps->tokens->type == TOKEN_NAME && is_operator(&ps->tokens[1], ":")) {
        // "foo: int" creates a variable "foo" of type "int"
        result.kind = AST_STMT_DECLARE_LOCAL_VAR;
        result.data.vardecl = parse_name_type_value(ps, NULL);
    } else {
        AstExpression expr = parse_expression(ps);
        result.kind = determine_the_kind_of_a_statement_that_starts_with_an_expression(ps->tokens);
        if (result.kind == AST_STMT_EXPRESSION_STATEMENT) {
            validate_expression_statement(&expr);
            result.data.expression = expr;
        } else {
            ps->tokens++;
            result.data.assignment = (AstAssignment){.target=expr, .value=parse_expression(ps)};
            if (is_operator(ps->tokens, "="))
                fail(ps->tokens->location, "only one variable can be assigned at a time");
        }
    }
    return result;
}

static void parse_start_of_body(ParserState *ps)
{
    if (!is_operator(ps->tokens, ":"))
        fail_with_parse_error(ps->tokens, "':' followed by a new line with more indentation");
    ps->tokens++;

    if (ps->tokens->type != TOKEN_NEWLINE)
        fail_with_parse_error(ps->tokens, "a new line with more indentation after ':'");
    ps->tokens++;

    if (ps->tokens->type != TOKEN_INDENT)
        fail_with_parse_error(ps->tokens, "more indentation after ':'");
    ps->tokens++;
}

static AstFunction parse_funcdef(ParserState *ps, bool is_method)
{
    assert(is_keyword(ps->tokens, "def"));
    ps->tokens++;

    struct AstFunction funcdef = {0};
    funcdef.signature = parse_function_signature(ps, is_method);
    if (!strcmp(funcdef.signature.name, "__init__") && is_method) {
        fail(ps->tokens->location, "Jou does not have a special __init__ method like Python");
    }
    if (funcdef.signature.takes_varargs) {
        // TODO: support "def foo(x: str, ...)" in some way
        fail(ps->tokens->location, "functions with variadic arguments cannot be defined yet");
    }

    assert(!ps->is_parsing_method_body);
    ps->is_parsing_method_body = is_method;
    funcdef.body = parse_body(ps);
    ps->is_parsing_method_body = false;

    return funcdef;
}

static void check_class_for_duplicate_names(const AstClassDef *classdef)
{
    List(struct MemberInfo { const char *kindstr; const char *name; Location location; }) infos = {0};
    for (AstClassMember *m = classdef->members.ptr; m < End(classdef->members); m++) {
        switch(m->kind) {
        case AST_CLASSMEMBER_FIELD:
            Append(&infos, (struct MemberInfo){"a field", m->data.field.name, m->data.field.name_location});
            break;
        case AST_CLASSMEMBER_UNION:
            for (AstNameTypeValue *ntv = m->data.unionfields.ptr; ntv < End(m->data.unionfields); ntv++)
                Append(&infos, (struct MemberInfo){"a union member", ntv->name, ntv->name_location});
            break;
        case AST_CLASSMEMBER_METHOD:
            Append(&infos, (struct MemberInfo){"a method", m->data.method.signature.name, m->data.method.signature.name_location});
            break;
        }
    }

    for (struct MemberInfo *p1 = infos.ptr; p1 < End(infos); p1++)
        for (struct MemberInfo *p2 = p1+1; p2 < End(infos); p2++)
            if (!strcmp(p1->name, p2->name))
                fail(p2->location, "class %s already has %s named '%s'", classdef->name, p1->kindstr, p1->name);

    free(infos.ptr);
}

static AstClassDef parse_classdef(ParserState *ps)
{
    AstClassDef result = {0};
    if (ps->tokens->type != TOKEN_NAME)
        fail_with_parse_error(ps->tokens, "a name for the class");
    safe_strcpy(result.name, ps->tokens->data.name);
    ps->tokens++;

    parse_start_of_body(ps);
    while (ps->tokens->type != TOKEN_DEDENT) {
        if (is_keyword(ps->tokens, "def")) {
            Append(&result.members, (AstClassMember){
                .kind = AST_CLASSMEMBER_METHOD,
                .data.method = parse_funcdef(ps, true),
            });
        } else if (is_keyword(ps->tokens, "union")) {
            Location union_keyword_location = ps->tokens->location;
            ps->tokens++;
            parse_start_of_body(ps);
            AstClassMember umember = { .kind = AST_CLASSMEMBER_UNION };
            while (ps->tokens->type != TOKEN_DEDENT) {
                AstNameTypeValue field = parse_name_type_value(ps, "a union member");
                if (field.value)
                    fail(field.value->location, "union members cannot have default values");
                Append(&umember.data.unionfields, field);
                eat_newline(ps);
            }
            ps->tokens++;
            if (umember.data.unionfields.len < 2) {
                fail(union_keyword_location, "unions must have at least 2 members");
            }
            Append(&result.members, umember);
        } else {
            AstNameTypeValue field = parse_name_type_value(ps, "a method, a field or a union");
            if (field.value)
                fail(field.value->location, "class fields cannot have default values");
            Append(&result.members, (AstClassMember){
                .kind = AST_CLASSMEMBER_FIELD,
                .data.field = field,
            });
            eat_newline(ps);
        }
    }
    ps->tokens++;

    check_class_for_duplicate_names(&result);
    return result;
}

static AstEnumDef parse_enumdef(ParserState *ps)
{
    AstEnumDef result = {0};
    if (ps->tokens->type != TOKEN_NAME)
        fail_with_parse_error(ps->tokens, "a name for the enum");
    safe_strcpy(result.name, ps->tokens->data.name);
    ps->tokens++;

    parse_start_of_body(ps);
    List(const char*) membernames = {0};

    while (ps->tokens->type != TOKEN_DEDENT) {
        if (ps->tokens->type != TOKEN_NAME)
            fail_with_parse_error(ps->tokens, "a name for an enum member");

        for (const char **old = membernames.ptr; old < End(membernames); old++)
            if (!strcmp(*old, ps->tokens->data.name))
                fail(ps->tokens->location, "the enum has two members named '%s'", ps->tokens->data.name);

        Append(&membernames, ps->tokens->data.name);
        ps->tokens++;
        eat_newline(ps);
    }

    result.nmembers = membernames.len;
    result.membernames = malloc(sizeof(result.membernames[0]) * result.nmembers);
    for (int i = 0; i < result.nmembers; i++)
        strcpy(result.membernames[i], membernames.ptr[i]);

    free(membernames.ptr);
    ps->tokens++;
    return result;
}

static AstStatement parse_statement(ParserState *ps)
{
    AstStatement result = { .location = ps->tokens->location };

    if (is_keyword(ps->tokens, "import")) {
        fail(ps->tokens->location, "imports must be in the beginning of the file");
    } else if (is_keyword(ps->tokens, "def")) {
        ps->tokens++;  // skip 'def' keyword
        result.kind = AST_STMT_FUNCTION;
        result.data.function.signature = parse_function_signature(ps, false);
        if (result.data.function.signature.takes_varargs) {
            // TODO: support "def foo(x: str, ...)" in some way
            fail(ps->tokens->location, "functions with variadic arguments cannot be defined yet");
        }
        result.data.function.body = parse_body(ps);
    } else if (is_keyword(ps->tokens, "declare")) {
        ps->tokens++;
        if (is_keyword(ps->tokens, "global")) {
            ps->tokens++;
            result.kind = AST_STMT_DECLARE_GLOBAL_VAR;
            result.data.vardecl = parse_name_type_value(ps, "a variable name");
            if (result.data.vardecl.value) {
                fail(
                    result.data.vardecl.value->location,
                    "a value cannot be given when declaring a global variable");
            }
        } else {
            result.kind = AST_STMT_FUNCTION;
            result.data.function.signature = parse_function_signature(ps, false);
        }
        eat_newline(ps);
    } else if (is_keyword(ps->tokens, "global")) {
        ps->tokens++;
        result.kind = AST_STMT_DEFINE_GLOBAL_VAR;
        result.data.vardecl = parse_name_type_value(ps, "a variable name");
        if (result.data.vardecl.value) {
            // TODO: make this work
            fail(
                result.data.vardecl.value->location,
                "specifying a value for a global variable is not supported yet");
        }
        eat_newline(ps);
    } else if (is_keyword(ps->tokens, "class")) {
        ps->tokens++;
        result.kind = AST_STMT_DEFINE_CLASS;
        result.data.classdef = parse_classdef(ps);
    } else if (is_keyword(ps->tokens, "enum")) {
        ps->tokens++;
        result.kind = AST_STMT_DEFINE_ENUM;
        result.data.enumdef = parse_enumdef(ps);
    } else if (is_keyword(ps->tokens, "if")) {
        result.kind = AST_STMT_IF;
        result.data.ifstatement = parse_if_statement(ps);
    } else if (is_keyword(ps->tokens, "while")) {
        ps->tokens++;
        result.kind = AST_STMT_WHILE;
        result.data.whileloop.condition = parse_expression(ps);
        result.data.whileloop.body = parse_body(ps);
    } else if (is_keyword(ps->tokens, "for")) {
        ps->tokens++;
        result.kind = AST_STMT_FOR;
        result.data.forloop.init = malloc(sizeof *result.data.forloop.init);
        result.data.forloop.incr = malloc(sizeof *result.data.forloop.incr);
        // TODO: improve error messages
        *result.data.forloop.init = parse_oneline_statement(ps);
        if (!is_operator(ps->tokens, ";"))
            fail_with_parse_error(ps->tokens, "a ';'");
        ps->tokens++;
        result.data.forloop.cond = parse_expression(ps);
        if (!is_operator(ps->tokens, ";"))
            fail_with_parse_error(ps->tokens, "a ';'");
        ps->tokens++;
        *result.data.forloop.incr = parse_oneline_statement(ps);
        result.data.forloop.body = parse_body(ps);
    } else {
        result = parse_oneline_statement(ps);
        eat_newline(ps);
    }
    return result;
}

static AstBody parse_body(ParserState *ps)
{
    parse_start_of_body(ps);

    List(AstStatement) result = {0};
    while (ps->tokens->type != TOKEN_DEDENT)
        Append(&result, parse_statement(ps));
    ps->tokens++;

    return (AstBody){ .statements=result.ptr, .nstatements=result.len };
}

static AstImport parse_import(ParserState *ps)
{
    const Token *import_keyword = ps->tokens++;
    assert(is_keyword(import_keyword, "import"));

    const Token *pathtoken = ps->tokens++;
    if (pathtoken->type != TOKEN_STRING)
        fail_with_parse_error(pathtoken, "a string to specify the file name");

    const char *part1, *part2;
    char *tmp = NULL;
    if (!strncmp(pathtoken->data.string_value, "stdlib/", 7)) {
        // Starts with stdlib --> import from where stdlib actually is
        part1 = ps->stdlib_path;
        part2 = pathtoken->data.string_value + 7;
    } else if (pathtoken->data.string_value[0] == '.') {
        // Relative to directory where the file is
        tmp = strdup(pathtoken->location.filename);
        part1 = dirname(tmp);
        part2 = pathtoken->data.string_value;
    } else {
        fail(
            pathtoken->location,
            "import path must start with 'stdlib/' (standard-library import) or a dot (relative import)");
    }

    // 1 for slash, 1 for \0, 1 for fun
    char *path = malloc(strlen(part1) + strlen(part2) + 3);
    sprintf(path, "%s/%s", part1, part2);
    free(tmp);

    simplify_path(path);
    return (struct AstImport){
        .location = import_keyword->location,
        .specified_path = strdup(pathtoken->data.string_value),
        .resolved_path = path,
    };
}

AstFile parse(const Token *tokens, const char *stdlib_path)
{
    AstFile result = { .path = tokens[0].location.filename };
    ParserState ps = { .tokens = tokens, .stdlib_path = stdlib_path };

    while (is_keyword(&ps.tokens[0], "import")) {
        Append(&result.imports, parse_import(&ps));
        eat_newline(&ps);
    }

    List(AstStatement) body = {0};
    while (ps.tokens->type != TOKEN_END_OF_FILE)
        Append(&body, parse_statement(&ps));
    result.body.statements = body.ptr;
    result.body.nstatements = body.len;

    return result;
}
