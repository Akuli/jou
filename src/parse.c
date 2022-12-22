// Implementation of the parse() function. See compile_steps.h.

#include "ast.h"
#include "token.h"
#include <string.h>

static noreturn void fail_with_parse_error(const struct Token *token, const char *what_was_expected_instead)
{
    char got[200];
    switch(token->type) {
        case TOKEN_INT: strcpy(got, "an integer"); break;
        case TOKEN_OPENPAREN: strcpy(got, "'('"); break;
        case TOKEN_CLOSEPAREN: strcpy(got, "')'"); break;
        case TOKEN_COLON: strcpy(got, "':'"); break;
        case TOKEN_NAME: snprintf(got, sizeof got, "a variable name '%s'", token->data.name); break;
        case TOKEN_NEWLINE: strcpy(got, "end of line"); break;
        case TOKEN_END_OF_FILE: strcpy(got, "end of file"); break;
        case TOKEN_INDENT: strcpy(got, "more indentation"); break;
        case TOKEN_DEDENT: strcpy(got, "less indentation"); break;
        case TOKEN_CIMPORT: strcpy(got, "the 'cimport' keyword"); break;
        case TOKEN_RETURN: strcpy(got, "the 'return' keyword"); break;
    }
    fail_with_error(token->location, "expected %s, got %s", what_was_expected_instead, got);
}

static void parse_type(const struct Token **tokens)
{
    // TODO: Do not assume that the type is "int"
    if ((*tokens)->type != TOKEN_NAME || strcmp((*tokens)->data.name, "int"))
        fail_with_parse_error(*tokens, "'int' (the only type that currently exists)");
    ++*tokens;
}

static int parse_expression(const struct Token **tokens)
{
    if ((*tokens)->type != TOKEN_INT)
        fail_with_parse_error(*tokens, "an integer (the only expression that currently exists)");
    return (*tokens)++->data.int_value;
}

static struct AstCImport parse_cimport(const struct Token **tokens)
{
    struct AstCImport result;

    if ((*tokens)->type != TOKEN_CIMPORT)
        fail_with_parse_error(*tokens, "the 'cimport' keyword");
    ++*tokens;

    parse_type(tokens);  // return type

    if ((*tokens)->type != TOKEN_NAME)
        fail_with_parse_error(*tokens, "a function name");
    safe_strcpy(result.funcname, (*tokens)->data.name);
    ++*tokens;

    if ((*tokens)->type != TOKEN_OPENPAREN)
        fail_with_parse_error(*tokens, "a '(' to denote the start of function arguments");
    ++*tokens;

    // We currently hard-code that there is one argument
    // type of argument
    parse_type(tokens);
    // name of argument
    if ((*tokens)->type != TOKEN_NAME)
        fail_with_parse_error(*tokens, "an argument name");
    ++*tokens;

    if ((*tokens)->type != TOKEN_CLOSEPAREN)
        fail_with_parse_error(*tokens, "a ')'");
    ++*tokens;

    return result;
}

static struct AstCall parse_call(const struct Token **tokens)
{
    struct AstCall result;

    if ((*tokens)->type != TOKEN_NAME)
        fail_with_parse_error(*tokens, "a function name");
    safe_strcpy(result.funcname, (*tokens)->data.name);
    ++*tokens;

    if ((*tokens)->type != TOKEN_OPENPAREN)
        fail_with_parse_error(*tokens, "a '(' to denote the start of function arguments");
    ++*tokens;

    result.arg = parse_expression(tokens);

    if ((*tokens)->type != TOKEN_CLOSEPAREN)
        fail_with_parse_error(*tokens, "a ')'");
    ++*tokens;

    return result;
}

static struct AstStatement parse_statement(const struct Token **tokens)
{
    struct AstStatement result = { .location = (*tokens)->location };
    switch((*tokens)->type) {
        case TOKEN_CIMPORT:
            result.kind = AST_STMT_CIMPORT_FUNCTION;
            result.data.cimport = parse_cimport(tokens);
            break;

        case TOKEN_NAME:
            result.kind = AST_STMT_CALL;
            result.data.call = parse_call(tokens);
            break;

        default:
            fail_with_parse_error(*tokens, "a statement");
    }

    if ((*tokens)->type != TOKEN_NEWLINE)
        fail_with_parse_error(*tokens, "end of line");
    ++*tokens;

    return result;
}

struct AstStatement *parse(const struct Token *tokens)
{
    // Skip initial newline token. It is always added by the tokenizer.
    assert((*tokens).type == TOKEN_NEWLINE);
    ++tokens;

    List(struct AstStatement) result = {0};
    while (tokens->type != TOKEN_END_OF_FILE)
        Append(&result, parse_statement(&tokens));

    Append(&result, (struct AstStatement){.location=tokens->location, .kind=AST_STMT_END_OF_FILE});
    return result.ptr;
}
