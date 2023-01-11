// Implementation of the parse() function.

#include "jou_compiler.h"
#include "util.h"
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <string.h>

static noreturn void fail_with_parse_error(const Token *token, const char *what_was_expected_instead)
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
    struct AstType result = { .location = (*tokens)->location };

    if (!is_keyword(*tokens, "void")
        && !is_keyword(*tokens, "int")
        && !is_keyword(*tokens, "byte")
        && !is_keyword(*tokens, "bool")
        && (*tokens)->type != TOKEN_NAME)
    {
        fail_with_parse_error(*tokens, "a type");
    }
    safe_strcpy(result.name, (*tokens)->data.name);
    ++*tokens;

    while (is_operator(*tokens, "*")) {
        result.npointers++;
        ++*tokens;
    }

    return result;
}

// char[100] is wrapped in a struct so that you can do List(Name).
// You can't do List(char[100]) or similar because C doesn't allow assigning arrays.
struct Name { char name[100]; };
static_assert(sizeof(struct Name) == 100, "u have weird c compiler...");
typedef List(struct Name) NameList;
typedef List(AstType) TypeList;

static void parse_name_and_type(
    const Token **tokens, NameList *names, TypeList *types,
    const char *expected_what_for_name, // e.g. "an argument name" to get error message "expected an argument name, got blah"
    const char *duplicate_name_error_fmt)  // %s will be replaced by a name
{
    if((*tokens)->type != TOKEN_NAME)
        fail_with_parse_error(*tokens, expected_what_for_name);

    for (int i = 0; i < names->len; i++) {
        if (!strcmp(names->ptr[i].name, (*tokens)->data.name)) {
            fail_with_error((*tokens)->location, duplicate_name_error_fmt, (*tokens)->data.name);
        }
    }

    struct Name n;
    safe_strcpy(n.name, (*tokens)->data.name);
    Append(names, n);
    ++*tokens;

    if (!is_operator(*tokens, ":"))
        fail_with_parse_error(*tokens, "':' and a type after it (example: \"foo: int\")");
    ++*tokens;
    Append(types, parse_type(tokens));
}

static AstSignature parse_function_signature(const Token **tokens)
{
    AstSignature result = {0};

    if ((*tokens)->type != TOKEN_NAME)
        fail_with_parse_error(*tokens, "a function name");
    safe_strcpy(result.funcname, (*tokens)->data.name);
    ++*tokens;

    if (!is_operator(*tokens, "("))
        fail_with_parse_error(*tokens, "a '(' to denote the start of function arguments");
    ++*tokens;

    NameList argnames = {0};
    TypeList argtypes = {0};

    while (!is_operator(*tokens, ")")) {
        if (result.takes_varargs)
            fail_with_error((*tokens)->location, "if '...' is used, it must be the last parameter");

        if (is_operator(*tokens, "...")) {
            result.takes_varargs = true;
            ++*tokens;
        } else {
            parse_name_and_type(
                tokens, &argnames, &argtypes,
                "an argument name", "there are multiple arguments named '%s'");
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

    result.returntype = parse_type(tokens);
    return result;
}

static AstExpression parse_expression(const Token **tokens);

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
    } else if (is_operator(t, "=")) {
        assert(arity == 2);
        result.kind = AST_EXPR_ASSIGN;
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

static AstExpression parse_elementary_expression(const Token **tokens)
{
    AstExpression expr = { .location = (*tokens)->location };

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
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = (Constant){ CONSTANT_INTEGER, {.integer={
            .is_signed = true,
            .value = (*tokens)->data.int_value,
            .width_in_bits = 32,
        }}};
        ++*tokens;
        break;
    case TOKEN_CHAR:
        expr.kind = AST_EXPR_CONSTANT;
        expr.data.constant = (Constant){ CONSTANT_INTEGER, {.integer={
            .is_signed = false,
            .value = (*tokens)->data.char_value,
            .width_in_bits = 8,
        }}};
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

static AstExpression parse_expression_with_fields_and_indexing(const Token **tokens)
{
    AstExpression result = parse_elementary_expression(tokens);
    while (is_operator(*tokens, ".") || is_operator(*tokens, "->") || is_operator(*tokens, "["))
    {
        if (is_operator(*tokens, "[")) {
            add_to_binop(tokens, &result, parse_elementary_expression);  // eats [ token
            if (!is_operator(*tokens, "]"))
                fail_with_parse_error(*tokens, "a ']'");
            ++*tokens;
        } else {
            const Token *startop = (*tokens)++;
            AstExpression result2 = {
                .location = startop->location,
                .kind = (is_operator(startop, "->") ? AST_EXPR_DEREF_AND_GET_FIELD : AST_EXPR_GET_FIELD),
            };
            result2.data.field.obj = malloc(sizeof *result2.data.field.obj);
            *result2.data.field.obj = result;

            if ((*tokens)->type != TOKEN_NAME)
                fail_with_parse_error(*tokens, "a field name");
            safe_strcpy(result2.data.field.fieldname, (*tokens)->data.name);
            ++*tokens;

            result = result2;
        }
    }
    return result;
}

// Unary operators: foo++, foo--, ++foo, --foo, &foo, *foo
static AstExpression parse_expression_with_unary_operators(const Token **tokens)
{
    // sequneces of 0 or more unary operator tokens: start,start+1,...,end-1
    const Token *prefixstart = *tokens;
    while(is_operator(*tokens,"++")||is_operator(*tokens,"--")||is_operator(*tokens,"&")||is_operator(*tokens,"*")) ++*tokens;
    const Token *prefixend = *tokens;

    AstExpression result = parse_expression_with_fields_and_indexing(tokens);

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
    while (is_operator(*tokens, "*") || is_operator(*tokens, "/"))
        add_to_binop(tokens, &result, parse_expression_with_unary_operators);
    return result;
}

static AstExpression parse_expression_with_add(const Token **tokens)
{
    AstExpression result = parse_expression_with_mul_and_div(tokens);
    while (is_operator(*tokens, "+") || is_operator(*tokens, "-"))
        add_to_binop(tokens, &result, parse_expression_with_mul_and_div);
    return result;
}

static AstExpression parse_expression_with_comparisons(const Token **tokens)
{
    AstExpression result = parse_expression_with_add(tokens);
#define IsComparator(x) (is_operator((x),"<") || is_operator((x),">") || is_operator((x),"<=") || is_operator((x),">=") || is_operator((x),"==") || is_operator((x),"!="))
    if (IsComparator(*tokens))
        add_to_binop(tokens, &result, parse_expression_with_add);
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

static AstExpression parse_expression_with_assignments(const Token **tokens)
{
    // We can't use add_to_binop() because then x=y=z would parse as (x=y)=z, not x=(y=z).
    AstExpression target = parse_expression_with_and_or(tokens);
    if (!is_operator(*tokens, "="))
        return target;
    const Token *t = (*tokens)++;
    AstExpression value = parse_expression_with_assignments(tokens);  // this function
    return build_operator_expression(t, 2, (AstExpression[]){ target, value });
}

static AstExpression parse_expression(const Token **tokens)
{
    return parse_expression_with_assignments(tokens);
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
    case AST_EXPR_ASSIGN:
    case AST_EXPR_FUNCTION_CALL:
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

static AstStatement parse_statement(const Token **tokens)
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
        eat_newline(tokens);
    } else if (is_keyword(*tokens, "if")) {
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
        // TODO: improve error messages
        result.data.forloop.init = parse_expression(tokens);
        if (!is_operator(*tokens, ";"))
            fail_with_parse_error(*tokens, "a ';'");
        ++*tokens;
        result.data.forloop.cond = parse_expression(tokens);
        if (!is_operator(*tokens, ";"))
            fail_with_parse_error(*tokens, "a ';'");
        ++*tokens;
        result.data.forloop.incr = parse_expression(tokens);
        result.data.forloop.body = parse_body(tokens);
    } else if (is_keyword(*tokens, "break")) {
        ++*tokens;
        result.kind = AST_STMT_BREAK;
        eat_newline(tokens);
    } else if (is_keyword(*tokens, "continue")) {
        ++*tokens;
        result.kind = AST_STMT_CONTINUE;
        eat_newline(tokens);
    } else if ((*tokens)->type == TOKEN_NAME && is_operator(&(*tokens)[1], ":")) {
        // "foo: int" creates a variable "foo" of type "int"
        result.kind = AST_STMT_DECLARE_LOCAL_VAR;
        safe_strcpy(result.data.vardecl.name, (*tokens)->data.name);
        *tokens += 2;
        result.data.vardecl.type = parse_type(tokens);
        if (is_operator(*tokens, "=")) {
            ++*tokens;
            AstExpression *p = malloc(sizeof *p);
            *p = parse_expression(tokens);
            result.data.vardecl.initial_value = p;
        } else {
            result.data.vardecl.initial_value = NULL;
        }
        eat_newline(tokens);
    } else {
        result.kind = AST_STMT_EXPRESSION_STATEMENT;
        result.data.expression = parse_expression(tokens);
        validate_expression_statement(&result.data.expression);
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

static AstStructDef parse_structdef(const Token **tokens)
{
    AstStructDef result;
    if ((*tokens)->type != TOKEN_NAME)
        fail_with_parse_error(*tokens, "a name for the struct");
    safe_strcpy(result.name, (*tokens)->data.name);
    ++*tokens;

    NameList fieldnames = {0};
    TypeList fieldtypes = {0};

    parse_start_of_body(tokens);
    while ((*tokens)->type != TOKEN_DEDENT) {
        parse_name_and_type(
            tokens, &fieldnames, &fieldtypes,
            "a name for a struct field", "there are multiple fields named '%s'");
        eat_newline(tokens);
    }
    ++*tokens;

    result.fieldnames = (char(*)[100])fieldnames.ptr;
    result.fieldtypes = fieldtypes.ptr;
    assert(fieldnames.len == fieldtypes.len);
    result.nfields = fieldnames.len;
    return result;
}

static AstToplevelNode parse_toplevel_node(const Token **tokens)
{
    AstToplevelNode result = { .location = (*tokens)->location };

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
                fail_with_error((*tokens)->location, "functions with variadic arguments cannot be defined yet");
            }
            result.data.funcdef.body = parse_body(tokens);
            break;
        }
        if (is_keyword(*tokens, "declare")) {
            ++*tokens;
            result.kind = AST_TOPLEVEL_DECLARE_FUNCTION;
            result.data.decl_signature = parse_function_signature(tokens);
            eat_newline(tokens);
            break;
        }
        if (is_keyword(*tokens, "struct")) {
            ++*tokens;
            result.kind = AST_TOPLEVEL_DEFINE_STRUCT;
            result.data.structdef = parse_structdef(tokens);
            break;
        }
        // fall through

    default:
        fail_with_parse_error(*tokens, "a definition or declaration");
    }

    return result;
}   

AstToplevelNode *parse(const Token *tokens)
{
    List(AstToplevelNode) result = {0};
    do {
        Append(&result, parse_toplevel_node(&tokens));
    } while (result.ptr[result.len - 1].kind != AST_TOPLEVEL_END_OF_FILE);

    return result.ptr;
}
