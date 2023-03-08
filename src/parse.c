// Implementation of the parse() function.

#include "jou_compiler.h"
#include "util.h"
#include <libgen.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <string.h>

static AstExpression parse_expression(const Token **tokens);

static noreturn void fail_with_parse_error(const Token *token, const char *what_was_expected_instead)
{
    char got[200];
    switch(token->type) {
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
    fail_with_error(token->location, "expected %s, got %s", what_was_expected_instead, got);
}

static bool is_keyword(const Token *t, const char *kw)
{
    return t->type == TOKEN_KEYWORD && !strcmp(t->data.name, kw);
}

static bool is_operator(const Token *t, const char *op)
{
    return t->type == TOKEN_OPERATOR && !strcmp(t->data.operator, op);
}

static AstType parse_type(const Token **tokens)
{
    AstType result = { .kind = AST_TYPE_NAMED, .location = (*tokens)->location };

    if (!is_keyword(*tokens, "void")
        && !is_keyword(*tokens, "int")
        && !is_keyword(*tokens, "long")
        && !is_keyword(*tokens, "size_t")
        && !is_keyword(*tokens, "byte")
        && !is_keyword(*tokens, "float")
        && !is_keyword(*tokens, "double")
        && !is_keyword(*tokens, "bool")
        && (*tokens)->type != TOKEN_NAME)
    {
        fail_with_parse_error(*tokens, "a type");
    }
    safe_strcpy(result.data.name, (*tokens)->data.name);
    ++*tokens;

    while(is_operator(*tokens, "*") || is_operator(*tokens, "[")) {
        AstType *p = malloc(sizeof(*p));
        *p = result;

        if (is_operator(*tokens, "*")) {
            result = (AstType){
                .location = (*tokens)++->location,
                .kind = AST_TYPE_POINTER,
                .data.valuetype = p,
            };
        } else {
            Location location = (*tokens)++->location;

            AstExpression *len = malloc(sizeof(*len));
            *len = parse_expression(tokens);

            if (!is_operator(*tokens, "]"))
                fail_with_parse_error(*tokens, "a ']' to end the array size");
            ++*tokens;

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
static AstNameTypeValue parse_name_type_value(const Token **tokens, const char *expected_what_for_name)
{
    AstNameTypeValue result;

    if ((*tokens)->type != TOKEN_NAME) {
        assert(expected_what_for_name);
        fail_with_parse_error(*tokens, expected_what_for_name);
    }
    safe_strcpy(result.name, (*tokens)->data.name);
    result.name_location = (*tokens)->location;
    ++*tokens;

    if (!is_operator(*tokens, ":"))
        fail_with_parse_error(*tokens, "':' and a type after it (example: \"foo: int\")");
    ++*tokens;
    result.type = parse_type(tokens);

    if (is_operator(*tokens, "=")) {
        ++*tokens;
        AstExpression *p = malloc(sizeof *p);
        *p = parse_expression(tokens);
        result.value = p;
    } else {
        result.value = NULL;
    }

    return result;
}

static AstSignature parse_function_signature(const Token **tokens, bool accept_self)
{
    AstSignature result = {0};

    if ((*tokens)->type != TOKEN_NAME)
        fail_with_parse_error(*tokens, "a function name");
    result.name_location = (*tokens)->location;
    safe_strcpy(result.name, (*tokens)->data.name);
    ++*tokens;

    if (!is_operator(*tokens, "("))
        fail_with_parse_error(*tokens, "a '(' to denote the start of function arguments");
    ++*tokens;

    while (!is_operator(*tokens, ")")) {
        if (result.takes_varargs)
            fail_with_error((*tokens)->location, "if '...' is used, it must be the last parameter");

        if (is_operator(*tokens, "...")) {
            result.takes_varargs = true;
            ++*tokens;
        } else if (is_keyword(*tokens, "self")) {
            if (!accept_self)
                fail_with_error((*tokens)->location, "'self' cannot be used here");
            AstNameTypeValue self_arg = { .name="self", .name_location=(*tokens)++->location };
            Append(&result.args, self_arg);
        } else {
            AstNameTypeValue arg = parse_name_type_value(tokens, "an argument name");

            if (arg.value)
                fail_with_error(arg.value->location, "arguments cannot have default values");

            for (const AstNameTypeValue *prevarg = result.args.ptr; prevarg < End(result.args); prevarg++)
                if (!strcmp(prevarg->name, arg.name))
                    fail_with_error(arg.name_location, "there are multiple arguments named '%s'", prevarg->name);
            Append(&result.args, arg);
        }

        if (is_operator(*tokens, ","))
            ++*tokens;
        else
            break;
    }

    if (!is_operator(*tokens, ")"))
        fail_with_parse_error(*tokens, "a ')'");
    ++*tokens;

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

    result.returntype = parse_type(tokens);
    return result;
}

static AstCall parse_call(const Token **tokens, char openparen, char closeparen, bool args_are_named)
{
    AstCall result = {0};

    assert((*tokens)->type == TOKEN_NAME);  // must be checked when calling this function
    safe_strcpy(result.calledname, (*tokens)->data.name);
    ++*tokens;

    if (!is_operator(*tokens, (char[]){openparen,'\0'})) {
        char msg[100];
        sprintf(msg, "a '%c' to denote the start of arguments", openparen);
        fail_with_parse_error(*tokens, msg);
    }
    ++*tokens;

    List(AstExpression) args = {0};

    // char[100] is wrapped in a struct so we can make a list of it.
    // You can't do List(char[100]) or similar because C doesn't allow assigning arrays.
    struct Name { char name[100]; };
    static_assert(sizeof(struct Name) == 100, "u have weird c compiler...");
    List(struct Name) argnames = {0};

    while (!is_operator(*tokens, (char[]){closeparen,'\0'})) {
        if (args_are_named) {
            // This code is only for structs, because there are no named function arguments.

            if ((*tokens)->type != TOKEN_NAME)
                fail_with_parse_error((*tokens),"a field name");

            for (struct Name *oldname = argnames.ptr; oldname < End(argnames); oldname++) {
                if (!strcmp(oldname->name, (*tokens)->data.name)) {
                    fail_with_error(
                        (*tokens)->location, "there are two arguments named '%s'", oldname->name);
                }
            }

            struct Name n;
            safe_strcpy(n.name,(*tokens)->data.name);
            Append(&argnames,n);
            ++*tokens;

            if (!is_operator(*tokens, "=")) {
                char msg[300];
                snprintf(msg, sizeof msg, "'=' followed by a value for field '%s'", n.name);
                fail_with_parse_error(*tokens, msg);
            }
            ++*tokens;
        }

        Append(&args, parse_expression(tokens));
        if (is_operator(*tokens, ","))
            ++*tokens;
        else
            break;
    }

    result.args = args.ptr;
    result.argnames = (char(*)[100])argnames.ptr;  // can be NULL
    result.nargs = args.len;

    if (!is_operator(*tokens, (char[]){closeparen,'\0'})) {
        char msg[100];
        sprintf(msg, "a '%c'", closeparen);
        fail_with_parse_error(*tokens, "a ')'");
    }
    ++*tokens;

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
static void add_to_binop(const Token **tokens, AstExpression *result, AstExpression (*cb)(const Token**))
{
    const Token *t = (*tokens)++;
    AstExpression rhs = cb(tokens);
    *result = build_operator_expression(t, 2, (AstExpression[]){*result, rhs});
}

static AstExpression parse_array(const Token **tokens)
{
    const Token *openbracket = *tokens;
    assert(is_operator(openbracket, "["));
    ++*tokens;

    List(AstExpression) items = {0};
    do {
        Append(&items, parse_expression(tokens));
    } while (is_operator(*tokens, ",") && !is_operator(++*tokens, "]"));

    if (!is_operator(*tokens, "]"))
        fail_with_parse_error(*tokens, "a ']' to end the array");
    ++*tokens;

    return (AstExpression){
        .kind=AST_EXPR_ARRAY,
        .location = openbracket->location,
        .data.array = {.count=items.len, .items=items.ptr},
    };
}

static AstExpression parse_elementary_expression(const Token **tokens)
{
    AstExpression expr = { .location = (*tokens)->location };

    switch((*tokens)->type) {
    case TOKEN_OPERATOR:
        if (is_operator(*tokens, "[")) {
            expr = parse_array(tokens);
        } else if (is_operator(*tokens, "(")) {
            ++*tokens;
            expr = parse_expression(tokens);
            if (!is_operator(*tokens, ")"))
                fail_with_parse_error(*tokens, "a ')'");
            ++*tokens;
        } else {
            goto not_an_expression;
        }
        break;
    case TOKEN_INT:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = int_constant(intType, (*tokens)->data.int_value);
        ++*tokens;
        break;
    case TOKEN_LONG:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = int_constant(longType, (*tokens)->data.long_value);
        ++*tokens;
        break;
    case TOKEN_CHAR:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = int_constant(byteType, (*tokens)->data.char_value);
        ++*tokens;
        break;
    case TOKEN_FLOAT:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = (Constant){ .kind=CONSTANT_FLOAT };
        safe_strcpy(expr.data.constant.data.double_or_float_text, (*tokens)->data.name);
        ++*tokens;
        break;
    case TOKEN_DOUBLE:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = (Constant){ .kind=CONSTANT_DOUBLE };
        safe_strcpy(expr.data.constant.data.double_or_float_text, (*tokens)->data.name);
        ++*tokens;
        break;
    case TOKEN_STRING:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = (Constant){ CONSTANT_STRING, {.str=strdup((*tokens)->data.string_value)} };
        ++*tokens;
        break;
    case TOKEN_NAME:
        if (is_operator(&(*tokens)[1], "(")) {
            expr.kind = AST_EXPR_FUNCTION_CALL;
            expr.data.call = parse_call(tokens, '(', ')', false);
        } else if (is_operator(&(*tokens)[1], "{")) {
            expr.kind = AST_EXPR_BRACE_INIT;
            expr.data.call = parse_call(tokens, '{', '}', true);
        } else if (is_operator(&(*tokens)[1], "::") && (*tokens)[2].type == TOKEN_NAME) {
            expr.kind = AST_EXPR_GET_ENUM_MEMBER;
            safe_strcpy(expr.data.enummember.enumname, (*tokens)[0].data.name);
            safe_strcpy(expr.data.enummember.membername, (*tokens)[2].data.name);
            *tokens += 3;
        } else {
            expr.kind = AST_EXPR_GET_VARIABLE;
            safe_strcpy(expr.data.varname, (*tokens)->data.name);
            ++*tokens;
        }
        break;
    case TOKEN_KEYWORD:
        if (is_keyword(*tokens, "True") || is_keyword(*tokens, "False")) {
            expr.kind = AST_EXPR_CONSTANT;
            expr.data.constant = (Constant){ CONSTANT_BOOL, {.boolean=is_keyword(*tokens, "True")} };
            ++*tokens;
        } else if (is_keyword(*tokens, "NULL")) {
            expr.kind = AST_EXPR_CONSTANT;
            expr.data.constant = (Constant){ CONSTANT_NULL, {{0}} };
            ++*tokens;
        } else if (is_keyword(*tokens, "self")) {
            expr.kind = AST_EXPR_GET_VARIABLE;
            strcpy(expr.data.varname, "self");
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

static AstExpression parse_expression_with_fields_and_methods_and_indexing(const Token **tokens)
{
    AstExpression result = parse_elementary_expression(tokens);
    while (is_operator(*tokens, ".") || is_operator(*tokens, "->") || is_operator(*tokens, "["))
    {
        if (is_operator(*tokens, "[")) {
            add_to_binop(tokens, &result, parse_expression);  // eats [ token
            if (!is_operator(*tokens, "]"))
                fail_with_parse_error(*tokens, "a ']'");
            ++*tokens;
        } else {
            AstExpression *obj = malloc(sizeof *obj);
            *obj = result;
            memset(&result, 0, sizeof result);

            const Token *startop = (*tokens)++;
            if ((*tokens)->type != TOKEN_NAME)
                fail_with_parse_error(*tokens, "a field or method name");
            result.location = (*tokens)->location;

            bool is_deref = is_operator(startop, "->");
            bool is_call = is_operator(&(*tokens)[1], "(");
            if (is_deref && is_call) result.kind = AST_EXPR_DEREF_AND_CALL_METHOD;
            if (is_deref && !is_call) result.kind = AST_EXPR_DEREF_AND_GET_FIELD;
            if (!is_deref && is_call) result.kind = AST_EXPR_CALL_METHOD;
            if (!is_deref && !is_call) result.kind = AST_EXPR_GET_FIELD;

            if (is_call) {
                result.data.methodcall.obj = obj;
                result.data.methodcall.call = parse_call(tokens, '(', ')', false);
            } else {
                result.data.classfield.obj = obj;
                safe_strcpy(result.data.classfield.fieldname, (*tokens)->data.name);
                ++*tokens;
            }
        }
    }
    return result;
}

// Unary operators: foo++, foo--, ++foo, --foo, &foo, *foo, sizeof foo
static AstExpression parse_expression_with_unary_operators(const Token **tokens)
{
    // sequneces of 0 or more unary operator tokens: start,start+1,...,end-1
    const Token *prefixstart = *tokens;
    while(is_operator(*tokens,"++")||is_operator(*tokens,"--")||is_operator(*tokens,"&")||is_operator(*tokens,"*")||is_keyword(*tokens,"sizeof")) ++*tokens;
    const Token *prefixend = *tokens;

    AstExpression result = parse_expression_with_fields_and_methods_and_indexing(tokens);

    const Token *suffixstart = *tokens;
    while(is_operator(*tokens,"++")||is_operator(*tokens,"--")) ++*tokens;
    const Token *suffixend = *tokens;

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

static AstExpression parse_expression_with_mul_and_div(const Token **tokens)
{
    AstExpression result = parse_expression_with_unary_operators(tokens);
    while (is_operator(*tokens, "*") || is_operator(*tokens, "/") || is_operator(*tokens, "%"))
        add_to_binop(tokens, &result, parse_expression_with_unary_operators);
    return result;
}

static AstExpression parse_expression_with_add(const Token **tokens)
{
    const Token *minus = is_operator(*tokens, "-") ? (*tokens)++ : NULL;
    AstExpression result = parse_expression_with_mul_and_div(tokens);
    if (minus)
        result = build_operator_expression(minus, 1, &result);

    while (is_operator(*tokens, "+") || is_operator(*tokens, "-"))
        add_to_binop(tokens, &result, parse_expression_with_mul_and_div);
    return result;
}

// "as" operator has somewhat low precedence, so that "1+2 as float" works as expected
static AstExpression parse_expression_with_as(const Token **tokens)
{
    AstExpression result = parse_expression_with_add(tokens);
    while (is_keyword(*tokens, "as")) {
        AstExpression *p = malloc(sizeof(*p));
        *p = result;
        Location as_location = (*tokens)++->location;
        AstType t = parse_type(tokens);
        result = (AstExpression){ .location=as_location, .kind=AST_EXPR_AS, .data.as = { .obj=p, .type=t } };
    }
    return result;
}

static AstExpression parse_expression_with_comparisons(const Token **tokens)
{
    AstExpression result = parse_expression_with_as(tokens);
#define IsComparator(x) (is_operator((x),"<") || is_operator((x),">") || is_operator((x),"<=") || is_operator((x),">=") || is_operator((x),"==") || is_operator((x),"!="))
    if (IsComparator(*tokens))
        add_to_binop(tokens, &result, parse_expression_with_as);
    if (IsComparator(*tokens))
        fail_with_error((*tokens)->location, "comparisons cannot be chained");
#undef IsComparator
    return result;
}

static AstExpression parse_expression_with_not(const Token **tokens)
{
    const Token *nottoken = NULL;
    if (is_keyword(*tokens, "not")) {
        nottoken = *tokens;
        ++*tokens;
    }
    if (is_keyword(*tokens, "not"))
        fail_with_error((*tokens)->location, "'not' cannot be repeated");

    AstExpression result = parse_expression_with_comparisons(tokens);
    if (nottoken)
        result = build_operator_expression(nottoken, 1, &result);
    return result;
}

static AstExpression parse_expression_with_and_or(const Token **tokens)
{
    AstExpression result = parse_expression_with_not(tokens);
    bool got_and = false, got_or = false;

    while (is_keyword(*tokens, "and") || is_keyword(*tokens, "or")) {
        got_and = got_and || is_keyword(*tokens, "and");
        got_or = got_or || is_keyword(*tokens, "or");
        if (got_and && got_or)
            fail_with_error((*tokens)->location, "'and' cannot be chained with 'or', you need more parentheses");

        add_to_binop(tokens, &result, parse_expression_with_not);
    }

    return result;
}

static AstExpression parse_expression(const Token **tokens)
{
    return parse_expression_with_and_or(tokens);
}

static void eat_newline(const Token **tokens)
{
    if ((*tokens)->type != TOKEN_NEWLINE)
        fail_with_parse_error(*tokens, "end of line");
    ++*tokens;
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
        fail_with_error(expr->location, "not a valid statement");
        break;
    }
}

static AstBody parse_body(const Token **tokens);

static AstIfStatement parse_if_statement(const Token **tokens)
{
    List(AstConditionAndBody) if_elifs = {0};

    assert(is_keyword(*tokens, "if"));
    do {
        ++*tokens;
        AstExpression cond = parse_expression(tokens);
        AstBody body = parse_body(tokens);
        Append(&if_elifs, (AstConditionAndBody){cond,body});
    } while (is_keyword(*tokens, "elif"));

    AstBody elsebody = {0};
    if (is_keyword(*tokens, "else")) {
        ++*tokens;
        elsebody = parse_body(tokens);
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

// does not eat a trailing newline
static AstStatement parse_oneline_statement(const Token **tokens)
{
    AstStatement result = { .location = (*tokens)->location };
    if (is_keyword(*tokens, "return")) {
        ++*tokens;
        if ((*tokens)->type == TOKEN_NEWLINE) {
            result.kind = AST_STMT_RETURN_WITHOUT_VALUE;
        } else {
            result.kind = AST_STMT_RETURN_VALUE;
            result.data.expression = parse_expression(tokens);
        }
    } else if (is_keyword(*tokens, "break")) {
        ++*tokens;
        result.kind = AST_STMT_BREAK;
    } else if (is_keyword(*tokens, "continue")) {
        ++*tokens;
        result.kind = AST_STMT_CONTINUE;
    } else if ((*tokens)->type == TOKEN_NAME && is_operator(&(*tokens)[1], ":")) {
        // "foo: int" creates a variable "foo" of type "int"
        result.kind = AST_STMT_DECLARE_LOCAL_VAR;
        result.data.vardecl = parse_name_type_value(tokens, NULL);
    } else {
        AstExpression expr = parse_expression(tokens);
        result.kind = determine_the_kind_of_a_statement_that_starts_with_an_expression(*tokens);
        if (result.kind == AST_STMT_EXPRESSION_STATEMENT) {
            validate_expression_statement(&expr);
            result.data.expression = expr;
        } else {
            ++*tokens;
            result.data.assignment = (AstAssignment){.target=expr, .value=parse_expression(tokens)};
            if (is_operator(*tokens, "="))
                fail_with_error((*tokens)->location, "only one variable can be assigned at a time");
        }
    }
    return result;
}

static AstStatement parse_statement(const Token **tokens)
{
    AstStatement result = { .location = (*tokens)->location };
    if (is_keyword(*tokens, "if")) {
        result.kind = AST_STMT_IF;
        result.data.ifstatement = parse_if_statement(tokens);
    } else if (is_keyword(*tokens, "while")) {
        ++*tokens;
        result.kind = AST_STMT_WHILE;
        result.data.whileloop.condition = parse_expression(tokens);
        result.data.whileloop.body = parse_body(tokens);
    } else if (is_keyword(*tokens, "for")) {
        ++*tokens;
        result.kind = AST_STMT_FOR;
        result.data.forloop.init = malloc(sizeof *result.data.forloop.init);
        result.data.forloop.incr = malloc(sizeof *result.data.forloop.incr);
        // TODO: improve error messages
        *result.data.forloop.init = parse_oneline_statement(tokens);
        if (!is_operator(*tokens, ";"))
            fail_with_parse_error(*tokens, "a ';'");
        ++*tokens;
        result.data.forloop.cond = parse_expression(tokens);
        if (!is_operator(*tokens, ";"))
            fail_with_parse_error(*tokens, "a ';'");
        ++*tokens;
        *result.data.forloop.incr = parse_oneline_statement(tokens);
        result.data.forloop.body = parse_body(tokens);
    } else {
        result = parse_oneline_statement(tokens);
        eat_newline(tokens);
    }
    return result;
}

static void parse_start_of_body(const Token **tokens)
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
}

static AstBody parse_body(const Token **tokens)
{
    parse_start_of_body(tokens);

    List(AstStatement) result = {0};
    while ((*tokens)->type != TOKEN_DEDENT)
        Append(&result, parse_statement(tokens));
    ++*tokens;

    return (AstBody){ .statements=result.ptr, .nstatements=result.len };
}

static AstFunctionDef parse_funcdef(const Token **tokens, bool is_method)
{
    assert(is_keyword(*tokens, "def"));
    ++*tokens;

    struct AstFunctionDef funcdef = {0};
    funcdef.signature = parse_function_signature(tokens, is_method);
    if (funcdef.signature.takes_varargs) {
        // TODO: support "def foo(x: str, ...)" in some way
        fail_with_error((*tokens)->location, "functions with variadic arguments cannot be defined yet");
    }
    funcdef.body = parse_body(tokens);

    return funcdef;
}

static AstClassDef parse_classdef(const Token **tokens)
{
    AstClassDef result = {0};
    if ((*tokens)->type != TOKEN_NAME)
        fail_with_parse_error(*tokens, "a name for the class");
    safe_strcpy(result.name, (*tokens)->data.name);
    ++*tokens;

    parse_start_of_body(tokens);
    while ((*tokens)->type != TOKEN_DEDENT) {
        if (is_keyword(*tokens, "def")) {
            Append(&result.methods, parse_funcdef(tokens, true));
        } else {
            AstNameTypeValue field = parse_name_type_value(tokens, "a method or a class field");

            if (field.value)
                fail_with_error(field.value->location, "class fields cannot have default values");

            for (const AstNameTypeValue *prevfield = result.fields.ptr; prevfield < End(result.fields); prevfield++)
                if (!strcmp(prevfield->name, field.name))
                    fail_with_error(field.name_location, "there are multiple fields named '%s'", field.name);
            Append(&result.fields, field);
            eat_newline(tokens);
        }
    }

    ++*tokens;
    return result;
}

static AstEnumDef parse_enumdef(const Token **tokens)
{
    AstEnumDef result = {0};
    if ((*tokens)->type != TOKEN_NAME)
        fail_with_parse_error(*tokens, "a name for the enum");
    safe_strcpy(result.name, (*tokens)->data.name);
    ++*tokens;

    parse_start_of_body(tokens);
    List(const char*) membernames = {0};

    while ((*tokens)->type != TOKEN_DEDENT) {
        for (const char **old = membernames.ptr; old < End(membernames); old++)
            if (!strcmp(*old, (*tokens)->data.name))
                fail_with_error((*tokens)->location, "the enum has two members named '%s'", (*tokens)->data.name);

        Append(&membernames, (*tokens)->data.name);
        ++*tokens;
        eat_newline(tokens);
    }

    result.nmembers = membernames.len;
    result.membernames = malloc(sizeof(result.membernames[0]) * result.nmembers);
    for (int i = 0; i < result.nmembers; i++)
        strcpy(result.membernames[i], membernames.ptr[i]);

    free(membernames.ptr);
    ++*tokens;
    return result;
}

static char *get_actual_import_path(const Token *pathtoken, const char *stdlib_path)
{
    if (pathtoken->type != TOKEN_STRING)
        fail_with_parse_error(pathtoken, "a string to specify the file name");

    const char *part1, *part2;
    char *tmp = NULL;
    if (!strncmp(pathtoken->data.string_value, "stdlib/", 7)) {
        // Starts with stdlib --> import from where stdlib actually is
        part1 = stdlib_path;
        part2 = pathtoken->data.string_value + 7;
    } else if (pathtoken->data.string_value[0] == '.') {
        // Relative to directory where the file is
        tmp = strdup(pathtoken->location.filename);
        part1 = dirname(tmp);
        part2 = pathtoken->data.string_value;
    } else {
        fail_with_error(
            pathtoken->location,
            "import path must start with 'stdlib/' (standard-library import) or a dot (relative import)");
    }

    // 1 for slash, 1 for \0, 1 for fun
    char *path = malloc(strlen(part1) + strlen(part2) + 3);
    sprintf(path, "%s/%s", part1, part2);
    free(tmp);

    simplify_path(path);
    return path;
}

typedef List(AstToplevelNode) ToplevelNodeList;

static void parse_import(const Token **tokens, const char *stdlib_path, ToplevelNodeList *dest)
{
    // This simplifies the compiler: it's easy to loop through all imports of the file.
    if (dest->len > 0 && dest->ptr[dest->len - 1].kind != AST_TOPLEVEL_IMPORT)
        fail_with_error((*tokens)->location, "imports must be in the beginning of the file");

    assert(is_keyword(*tokens, "from"));
    ++*tokens;

    char *path = get_actual_import_path(*tokens, stdlib_path);
    ++*tokens;

    if (!is_keyword(*tokens, "import"))
        fail_with_parse_error(*tokens, "the 'import' keyword");
    ++*tokens;

    bool parens = is_operator(*tokens, "(");
    if(parens) ++*tokens;

    do {
        if ((*tokens)->type != TOKEN_NAME)
            fail_with_parse_error(*tokens, "the name of a symbol to import");

        struct AstImport imp = {0};
        imp.path = strdup(path);
        safe_strcpy(imp.symbolname, (*tokens)->data.name);

        Append(dest, (struct AstToplevelNode){
            .location = (*tokens)->location,
            .kind = AST_TOPLEVEL_IMPORT,
            .data.import = imp,
        });
        ++*tokens;

        if (is_operator(*tokens, ","))
            ++*tokens;
        else
            break;
    } while (!is_operator(*tokens, ")") && (*tokens)->type != TOKEN_NEWLINE);
    free(path);

    if (parens) {
        if (!is_operator(*tokens, ")"))
            fail_with_parse_error(*tokens, "a ')'");
        ++*tokens;
    }

    if ((*tokens)->type != TOKEN_NEWLINE)
        fail_with_parse_error(*tokens, "a comma or end of line");
    ++*tokens;
}

static AstToplevelNode parse_toplevel_node(const Token **tokens)
{
    AstToplevelNode result = { .location = (*tokens)->location };

    if ((*tokens)->type == TOKEN_END_OF_FILE) {
        result.kind = AST_TOPLEVEL_END_OF_FILE;
    } else if (is_keyword(*tokens, "def")) {
        ++*tokens;  // skip 'def' keyword
        result.kind = AST_TOPLEVEL_DEFINE_FUNCTION;
        result.data.funcdef.signature = parse_function_signature(tokens, false);
        if (result.data.funcdef.signature.takes_varargs) {
            // TODO: support "def foo(x: str, ...)" in some way
            fail_with_error((*tokens)->location, "functions with variadic arguments cannot be defined yet");
        }
        result.data.funcdef.body = parse_body(tokens);
    } else if (is_keyword(*tokens, "declare")) {
        ++*tokens;
        if (is_keyword(*tokens, "global")) {
            ++*tokens;
            result.kind = AST_TOPLEVEL_DECLARE_GLOBAL_VARIABLE;
            result.data.globalvar = parse_name_type_value(tokens, "a variable name");
            if (result.data.globalvar.value) {
                fail_with_error(
                    result.data.globalvar.value->location,
                    "a value cannot be given when declaring a global variable");
            }
        } else {
            result.kind = AST_TOPLEVEL_DECLARE_FUNCTION;
            result.data.funcdef.signature = parse_function_signature(tokens, false);
        }
        eat_newline(tokens);
    } else if (is_keyword(*tokens, "global")) {
        ++*tokens;
        result.kind = AST_TOPLEVEL_DEFINE_GLOBAL_VARIABLE;
        result.data.globalvar = parse_name_type_value(tokens, "a variable name");
        eat_newline(tokens);
    } else if (is_keyword(*tokens, "class")) {
        ++*tokens;
        result.kind = AST_TOPLEVEL_DEFINE_CLASS;
        result.data.classdef = parse_classdef(tokens);
    } else if (is_keyword(*tokens, "enum")) {
        ++*tokens;
        result.kind = AST_TOPLEVEL_DEFINE_ENUM;
        result.data.enumdef = parse_enumdef(tokens);
    } else {
        fail_with_parse_error(*tokens, "a definition or declaration");
    }

    return result;
}

AstToplevelNode *parse(const Token *tokens, const char *stdlib_path)
{
    ToplevelNodeList result = {0};
    do {
        // Imports are separate because one import statement can become multiple ast nodes.
        if (is_keyword(tokens, "from"))
            parse_import(&tokens, stdlib_path, &result);
        else
            Append(&result, parse_toplevel_node(&tokens));
    } while (result.ptr[result.len - 1].kind != AST_TOPLEVEL_END_OF_FILE);
    return result.ptr;
}
