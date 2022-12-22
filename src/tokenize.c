// Implementation of the tokenize() function. See compile_steps.h.

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "token.h"
#include "misc.h"


struct State {
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
            case '0': c = 0; break;
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

void read_identifier(struct State *st, char firstbyte, char (*dest)[100])
{
    memset(*dest, 0, sizeof *dest);
    int destlen = 0;

    assert(is_identifier_first_byte(firstbyte));
    (*dest)[destlen++] = firstbyte;

    while(1) {
        char c = read_byte(st);
        if (!is_identifier_continuation(c)) {
            unread_byte(st, c);
            return;
        }

        if (destlen == sizeof *dest - 1)
            fail_with_error(st->location, "name \"%s\" is too long", *dest);
        (*dest)[destlen++] = c;
    }
}

// Assumes that the initial '\n' byte has been read already.
void read_indentation_as_newline_token(struct State *st, struct Token *t)
{
    t->type = TOKEN_NEWLINE;

    while(1) {
        char c = read_byte(st);
        if (c == ' ')
            t->data.indentation_level++;
        else if (c == '\n')
            t->data.indentation_level = 0;
        else if (c == '\0') {
            // Ignore newline+spaces at end of file. Do not validate 4 spaces.
            // TODO: test case
            t->type = TOKEN_END_OF_FILE;
            return;
        } else {
            unread_byte(st, c);
            break;
        }
    }
}

static struct Token read_token(struct State *st)
{
    struct Token t = { .location = st->location };

    while(1) {
        char c = read_byte(st);
        switch(c) {
            case ' ': continue;
            case '\n': read_indentation_as_newline_token(st, &t); break;
            case '\0': t.type = TOKEN_END_OF_FILE; break;
            case '(': t.type = TOKEN_OPENPAREN; break;
            case ')': t.type = TOKEN_CLOSEPAREN; break;
            case ':': t.type = TOKEN_COLON; break;
            case '\'': t.type = TOKEN_INT; t.data.int_value = read_char_literal(st); break;
            default:
                if(is_identifier_first_byte(c)) {
                    t.type = TOKEN_NAME;
                    read_identifier(st, c, &t.data.name);
                    if (!strcmp(t.data.name, "cimport"))
                        t.type = TOKEN_CIMPORT;
                } else {
                    fail_with_error(st->location, "unexpected byte '%c' (%#02x)", c, (int)c);
                }
                break;
        }
        return t;
    }
}

static struct Token *tokenize_without_indent_dedent_tokens(const char *filename)
{
    struct State st = { .location.filename = filename, .f = fopen(filename, "rb") };
    if (!st.f)
        fail_with_error(st.location, "cannot open file: %s", strerror(errno));

    List(struct Token) tokens = {0};
    while(tokens.len == 0 || tokens.ptr[tokens.len-1].type != TOKEN_END_OF_FILE)
        Append(&tokens, read_token(&st));
    fclose(st.f);

    return tokens.ptr;
}

struct Token *tokenize(const char *filename)
{
    struct Token *temp_tokens = tokenize_without_indent_dedent_tokens(filename);

    assert(temp_tokens[0].type == TOKEN_NEWLINE);
    if (temp_tokens[0].data.indentation_level != 0)
        fail_with_error(temp_tokens[0].location, "file cannot start with indentation");

    // Add indent/dedent tokens after newline tokens that change the indentation level.
    List(struct Token) tokens = {0};
    const struct Token *t = temp_tokens;
    int level = 0;

    do{
        if (t->type == TOKEN_END_OF_FILE) {
            while(level) {
                Append(&tokens, (struct Token){ .location=t->location, .type=TOKEN_DEDENT });
                level -= 4;
            }
        }

        Append(&tokens, *t);

        if (t->type == TOKEN_NEWLINE) {
            if (t->data.indentation_level % 4 != 0)
                fail_with_error(t->location, "indentation must be a multiple of 4 spaces");

            struct Location after_newline = t->location;
            after_newline.lineno++;

            while (level < t->data.indentation_level) {
                Append(&tokens, (struct Token){ .location=after_newline, .type=TOKEN_INDENT });
                level += 4;
            }

            while (level > t->data.indentation_level) {
                Append(&tokens, (struct Token){ .location=after_newline, .type=TOKEN_DEDENT });
                level -= 4;
            }
        }
    } while (t++->type != TOKEN_END_OF_FILE);

    free(temp_tokens);
    return tokens.ptr;
}
