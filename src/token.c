#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "token.h"
#include "misc.h"

void print_token(const struct Token *token)
{
    switch(token->type) {
        #define f(x) case x: printf(#x); break
        f(TOKEN_INT);
        f(TOKEN_OPENPAREN);
        f(TOKEN_CLOSEPAREN);
        f(TOKEN_NAME);
        f(TOKEN_NEWLINE);
        f(TOKEN_END_OF_FILE);
        f(TOKEN_CIMPORT);
        f(TOKEN_COLON);
        f(TOKEN_INDENT);
        f(TOKEN_DEDENT);
        f(TOKEN_RETURN);
        #undef f
    }

    switch(token->type) {
    case TOKEN_INT:
        printf(" value=%d", token->data.int_value);
        break;
    case TOKEN_NAME:
        printf(" name=\"%s\"", token->data.name);
        break;
    case TOKEN_NEWLINE:
        printf(" indentation_level=%d", token->data.indentation_level);
        break;

    // These tokens don't have any associated data to be printed here.
    //
    // Listed explicitly instead of "default" so that you get a compiler error
    // coming from here after adding a new token type. That should remind you
    // to keep this function up to date.
    case TOKEN_CIMPORT:
    case TOKEN_OPENPAREN:
    case TOKEN_CLOSEPAREN:
    case TOKEN_COLON:
    case TOKEN_END_OF_FILE:
    case TOKEN_INDENT:
    case TOKEN_DEDENT:
    case TOKEN_RETURN:
        break;
    }

    printf("\n");
}

void print_tokens(const struct Token *tokens)
{
    printf("--- Tokens for file \"%s\" ---\n", tokens->location.filename);
    int lastlineno = -1;
    do {
        if (tokens->location.lineno != lastlineno) {
            printf("Line %d:\n", tokens->location.lineno);
            lastlineno = tokens->location.lineno;
        }
        printf("  ");
        print_token(tokens);
    } while (tokens++->type != TOKEN_END_OF_FILE);

    printf("\n");
}

void free_tokens(struct Token *tokenlist)
{
    free(tokenlist);
}
