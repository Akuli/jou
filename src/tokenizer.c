#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tokenizer.h"
#include "error.h"
#include "list.h"


struct State {
    // State for read_byte()
    FILE *f;
    struct Location location;
};

static char read_byte(struct State *st) {
    if (st->location.lineno == 0) {
        // Add a fake newline to beginning of the file. It has two purposes:
        //  * Indentations are simpler to spec: they always come after a newline.
        //  * Line numbers start at 1.
        st->location.lineno++;
        return '\n';
    }

    int c = fgetc(st->f);
    if (c == EOF && ferror(st->f))
        fail_with_error(st->location, "cannot read file: %s", strerror(errno));

    // For Windows: \r\n in source file is treated same as \n
    if (c == '\r') {
        c = fgetc(st->f);
        if (c == EOF && ferror(st->f))
            fail_with_error(st->location, "cannot read file: %s", strerror(errno));
        if (c != '\n')
            fail_with_error(st->location, "source file contains a CR byte ('\\r') that isn't a part of a CRLF line ending");
    }

    // Use the zero byte to represent end of file.
    if (c == '\0')
        fail_with_error(st->location, "source file contains a zero byte");
    if (c == EOF) {
        assert(!ferror(st->f));
        return '\0';
    }

    if (c == '\n')
        st->location.lineno++;

    return c;
}

// Do not attempt to unread multiple bytes, ungetc() is guaranteed to only work for one
static void unread_byte(struct State *st, char c)
{
    if (c == '\0')
        return;
    assert(c!='\r');  // c should be from read_byte()
    int ret = ungetc(c, st->f);
    assert(ret != EOF);  // should not fail if unreading just one byte

    if (c == '\n')
        st->location.lineno--;
}

static char read_char_literal(struct State *st)
{
    // The first ' has already been read.
    char c = read_byte(st);
    if (c == '\'')
        fail_with_error(st->location, "empty character literal: ''");

    if (c == '\\') {
        // '\n' means newline, for example
        char after_backslash = read_byte(st);
        switch(after_backslash) {
            case 'n': c = '\n'; break;
            case '\\': c = '\\'; break;
            case '\'': c = '\''; break;
            default:
                fail_with_error(st->location, "unknown character literal: '\\%c'", after_backslash);
        }
    }

    char endquote = read_byte(st);
    if (endquote  != '\'')
        fail_with_error(st->location,
            "a character literal (single quotes) can only contain one character, "
            "maybe you want to use a string literal (double quotes) instead?");

    return c;
}

bool is_identifier_first_byte(char c)
{
    return ('A'<=c && c<='Z') || ('a'<=c && c<='z') || c=='_';
}

bool is_identifier_continuation(char c)
{
    return is_identifier_first_byte(c) || ('0'<=c && c<='9');
}

static void *allocate_memory(struct State *st, void *old, size_t n)
{
    void *result = realloc(old, n);
    if(!result)
        fail_with_error(st->location, "not enough memory for tokenizing");
    return result;
}

char *read_identifier(struct State *st, char firstbyte)
{
    List(char) result = {0};

    assert(is_identifier_first_byte(firstbyte));
    Append(&result, firstbyte);

    while(1) {
        char c = read_byte(st);
        if (is_identifier_continuation(c))
            Append(&result, c);
        else {
            unread_byte(st, c);
            Append(&result, 0);
            return result.ptr;
        }
    }
}

static struct Token read_token(struct State *st)
{
    struct Token t = { .location = st->location };

    char c = read_byte(st);
    switch(c) {
        case '\n': t.type = TOKENTYPE_NEWLINE; break;
        case '\0': t.type = TOKENTYPE_END_OF_FILE; break;
        case '(': t.type = TOKENTYPE_OPENPAREN; break;
        case ')': t.type = TOKENTYPE_CLOSEPAREN; break;
        case '\'': t.type = TOKENTYPE_INT; t.data.value = read_char_literal(st); break;
        default:
            if(is_identifier_first_byte(c)) {
                t.type = TOKENTYPE_NAME;
                t.data.string = read_identifier(st, c);
            } else {
                fail_with_error(st->location, "unexpected byte '%c' (%#02x)", c, (int)c);
            }
            break;
    }

    return t;
}

struct Token *tokenize(const char *filename)
{
    struct State st = { .location.filename = filename, .f = fopen(filename, "rb") };
    if (!st.f)
        fail_with_error(st.location, "cannot open file: %s", strerror(errno));

    List(struct Token) tokens = {0};
    while(tokens.len == 0 || tokens.ptr[tokens.len-1].type != TOKENTYPE_END_OF_FILE)
        Append(&tokens, read_token(&st));

    fclose(st.f);
    return tokens.ptr;
}

void print_tokens(const struct Token *tokens)
{
    const char *tokentype_names[] = {
        #define f(x) [x] = #x,
        f(TOKENTYPE_INT)
        f(TOKENTYPE_OPENPAREN)
        f(TOKENTYPE_CLOSEPAREN)
        f(TOKENTYPE_NAME)
        f(TOKENTYPE_NEWLINE)
        f(TOKENTYPE_END_OF_FILE)
        #undef f
    };

    printf("Tokens for file \"%s\":\n", tokens->location.filename);
    int lastlineno = -1;
    do {
        if (tokens->location.lineno != lastlineno) {
            printf("  Line %d:\n", tokens->location.lineno);
            lastlineno = tokens->location.lineno;
        }

        printf("    %s", tokentype_names[tokens->type]);
        if (tokens->type == TOKENTYPE_INT)
            printf(" value=%d", tokens->data.value);
        if (tokens->type == TOKENTYPE_NAME)
            printf(" string=\"%s\"", tokens->data.string);
        printf("\n");
    } while (tokens++->type != TOKENTYPE_END_OF_FILE);

    printf("\n");
}

void free_tokens(struct Token *tokenlist)
{
    for (struct Token *t = tokenlist; t->type != TOKENTYPE_END_OF_FILE; t++)
        if (t->type == TOKENTYPE_NAME)
            free(t->data.string);
    free(tokenlist);
}
