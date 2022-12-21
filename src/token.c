#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "token.h"
#include "misc.h"


void print_tokens(const struct Token *tokens)
{
    printf("Tokens for file \"%s\":\n", tokens->location.filename);
    int lastlineno = -1;
    do {
        if (tokens->location.lineno != lastlineno) {
            printf("  Line %d:\n", tokens->location.lineno);
            lastlineno = tokens->location.lineno;
        }

        printf("    ");
        switch(tokens->type) {
            #define f(x) case x: printf(#x); break
            f(TOKEN_INT);
            f(TOKEN_OPENPAREN);
            f(TOKEN_CLOSEPAREN);
            f(TOKEN_NAME);
            f(TOKEN_NEWLINE);
            f(TOKEN_END_OF_FILE);
            f(TOKEN_CIMPORT);
            #undef f
        }

        switch(tokens->type) {
        case TOKEN_INT:
            printf(" value=%d", tokens->data.int_value);
            break;
        case TOKEN_NAME:
            printf(" name=\"%s\"", tokens->data.name);
            break;

        // These tokens don't have any associated data to be printed here.
        //
        // Listed explicitly instead of "default" so thatyou get a compiler warning
        // coming from here after adding a new token type, hopefully reminding you
        // to keep this function up to date.
        case TOKEN_CIMPORT:
        case TOKEN_OPENPAREN:
        case TOKEN_CLOSEPAREN:
        case TOKEN_END_OF_FILE:
        case TOKEN_NEWLINE:
            break;
        }

        printf("\n");

    } while (tokens++->type != TOKEN_END_OF_FILE);

    printf("\n");
}

void free_tokens(struct Token *tokenlist)
{
    free(tokenlist);
}
