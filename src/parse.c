// Implementation of the parse() function. See compile_steps.h.

#include "jou_compiler.h"
#include "util.h"
#include <stdnoreturn.h>
#include <stdio.h>
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

static struct AstFunctionSignature parse_function_signature(const struct Token **tokens)
{
    struct AstFunctionSignature result = {.location=(*tokens)->location};

    parse_type(tokens);  // return type

    if ((*tokens)->type != TOKEN_NAME)
        fail_with_parse_error(*tokens, "a function name");
    safe_strcpy(result.funcname, (*tokens)->data.name);
    ++*tokens;

    if ((*tokens)->type != TOKEN_OPENPAREN)
        fail_with_parse_error(*tokens, "a '(' to denote the start of function arguments");
    ++*tokens;

    while ((*tokens)->type != TOKEN_CLOSEPAREN) {
        result.nargs++;

        parse_type(tokens);  // type of argument

        // name of argument
        if ((*tokens)->type != TOKEN_NAME)
            fail_with_parse_error(*tokens, "an argument name");
        ++*tokens;

        // TODO: eat comma

        //if ((*tokens)->type == TOKEN_COMMA) {
        //    ...
        //} else {
            break;
        //}
    }

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

static void eat_newline(const struct Token **tokens)
{
    if ((*tokens)->type != TOKEN_NEWLINE)
        fail_with_parse_error(*tokens, "end of line");
    ++*tokens;
}

static struct AstStatement parse_statement(const struct Token **tokens)
{
    struct AstStatement result = { .location = (*tokens)->location };
    switch((*tokens)->type) {
        case TOKEN_NAME:
            result.kind = AST_STMT_CALL;
            result.data.call = parse_call(tokens);
            break;

        case TOKEN_RETURN:
            ++*tokens;
            result.kind = AST_STMT_RETURN;
            result.data.returnvalue = parse_expression(tokens);
            break;

        default:
            fail_with_parse_error(*tokens, "a statement");
    }

    eat_newline(tokens);
    return result;
}

static struct AstBody parse_body(const struct Token **tokens)
{
    if ((*tokens)->type != TOKEN_COLON)
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
        case TOKEN_CIMPORT:
            ++*tokens;
            result.kind = AST_TOPLEVEL_CIMPORT_FUNCTION;
            result.data.cimport_signature = parse_function_signature(tokens);
            eat_newline(tokens);
            break;

        case TOKEN_END_OF_FILE:
            result.kind = AST_TOPLEVEL_END_OF_FILE;
            break;

        default:
            result.kind = AST_TOPLEVEL_DEFINE_FUNCTION;
            result.data.funcdef.signature = parse_function_signature(tokens);
            result.data.funcdef.body = parse_body(tokens);
            break;
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
