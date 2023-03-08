// Implementation of the tokenize() function.

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "jou_compiler.h"
#include "util.h"


struct State {
    FILE *f;
    Location location;
    List(char) pushback;
    // Not dynamic, so that you can't make the compiler crash due to
    // too much recursion by feeding it lots of parentheses.
    Token parens[50];
    int nparens;
};

static char read_byte(struct State *st) {
    int c;
    if (st->pushback.len != 0) {
        c = Pop(&st->pushback);
        goto got_char;
    }

    c = fgetc(st->f);
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
        fail_with_error(st->location, "source file contains a zero byte");  // TODO: test this
    if (c == EOF) {
        assert(!ferror(st->f));
        return '\0';
    }

got_char:
    if (c == '\n')
        st->location.lineno++;
    return c;
}

// Can't use ungetc() because it is only guaranteed to work for one byte.
static void unread_byte(struct State *st, char c)
{
    if (c == '\0')
        return;
    assert(c!='\r');  // c should be from read_byte()
    Append(&st->pushback, c);
    if (c == '\n')
        st->location.lineno--;
}

static bool is_identifier_or_number_byte(char c)
{
    return ('A'<=c && c<='Z') || ('a'<=c && c<='z') || c=='_' || ('0'<=c && c<='9');
}

static void read_identifier_or_number(struct State *st, char firstbyte, char (*dest)[100])
{
    memset(*dest, 0, sizeof *dest);
    int destlen = 0;

    assert(is_identifier_or_number_byte(firstbyte));
    (*dest)[destlen++] = firstbyte;

    while(1) {
        char c = read_byte(st);
        if (is_identifier_or_number_byte(c)
            || ('0'<=firstbyte && firstbyte<='9' && c=='.')
            || ('0'<=firstbyte && firstbyte<='9' && (*dest)[destlen-1]=='e' && c=='-'))
        {
            if (destlen == sizeof *dest - 1)
                fail_with_error(st->location, "name is too long: %.20s...", *dest);
            (*dest)[destlen++] = c;
        } else {
            unread_byte(st, c);
            return;
        }
    }
}

static void consume_rest_of_line(struct State *st)
{
    while(1) {
        char c = read_byte(st);
        if (c == '\n') {
            unread_byte(st, '\n');
            return;
        }
        if (c == '\0')  // end of file
            return;
    }
}

// Assumes that the initial '\n' byte has been read already.
// Reads everything else that we need to read to get a newline token.
// Returns the resulting indentation level.
static int read_indentation_level_for_newline_token(struct State *st)
{
    int level = 0;
    while(1) {
        char c = read_byte(st);
        if (c == ' ')
            level++;
        else if (c == '\n')
            level = 0;
        else if (c == '#')
            consume_rest_of_line(st);
        else if (c == '\0') {
            // Ignore newline+spaces at end of file. Do not validate 4 spaces.
            // TODO: test case
            return 0;
        } else {
            unread_byte(st, c);
            return level;
        }
    }
}

static long long parse_integer(const char *str, Location location, int nbits)
{
    int base;
    const char *digits, *valid_digits;
    if (str[0] == '0' && str[1] == 'x') {
        // 0x123 = hexadecimal number
        base = 16;
        digits = &str[2];
        valid_digits = "0123456789ABCDEFabcdef";
    } else if (str[0] == '0' && str[1] == 'b') {
        // 0b1101 = binary number
        base = 2;
        digits = &str[2];
        valid_digits = "01";
    } else if (str[0] == '0' && str[1] == 'o') {
        // 0o777 = octal number
        base = 8;
        digits = &str[2];
        valid_digits = "01234567";
    } else if (str[0] == '0' && str[1] != '\0') {
        // wrong syntax like 0777
        fail_with_error(location, "unnecessary zero at start of number");
    } else {
        // default decimal umber
        base = 10;
        digits = &str[0];
        valid_digits = "0123456789";
    }

    if (!*digits || strspn(digits, valid_digits) != strlen(digits))
        fail_with_error(location, "invalid number or variable name \"%s\"", str);

    errno = 0;
    long long result = strtoll(digits, NULL, base);
    if (errno == ERANGE || result >> (nbits-1) != 0)
        fail_with_error(location, "value does not fit in a signed %d-bit integer", nbits);
    return result;
}

static bool has_multiple(const char *s, char c)
{
    return strchr(s, c) != strrchr(s, c);
}

static bool is_valid_double(const char *str)
{
    if (strspn(str, "0123456789.-e") < strlen(str))
        return false;
    if (has_multiple(str, '.') || has_multiple(str, '-') || has_multiple(str, 'e'))
        return false;

    const char *dot = strchr(str, '.');
    const char *minus = strchr(str, '-');
    const char *e = strchr(str, 'e');

    return (
        // 123.456
        !e && !minus && dot
    ) || (
        // 1e-4
        e && e[1] && (!dot || dot<e) && (!minus || (minus==e+1 && minus[1]))
    );
}

static bool is_valid_float(const char *str)
{
    int n = strlen(str);
    if (n==0 || (str[n-1]!='F' && str[n-1]!='f'))
        return false;

    char *tmp = strdup(str);
    tmp[n-1] = '\0';
    bool result = is_valid_double(tmp) || strspn(tmp, "0123456789") == strlen(tmp);
    free(tmp);
    return result;
}

static bool is_keyword(const char *s)
{
    const char *keywords[] = {
        // This keyword list is in 3 places. Please keep them in sync:
        //   - the Jou compiler written in C
        //   - self-hosted compiler
        //   - syntax documentation
        "from", "import",
        "def", "declare", "class", "enum", "global",
        "return", "if", "elif", "else", "while", "for", "break", "continue",
        "True", "False", "NULL", "self",
        "and", "or", "not", "as", "sizeof",
        "void", "bool", "byte", "int", "long", "size_t", "float", "double",
    };
    for (const char **kw = &keywords[0]; kw < &keywords[sizeof(keywords)/sizeof(keywords[0])]; kw++)
        if (!strcmp(*kw, s))
            return true;
    return false;
}

static char read_hex_escape_byte(struct State *st)
{
    char c1 = read_byte(st);
    char c2 = read_byte(st);
    if (!isxdigit(c1) || !isxdigit(c2))
        fail_with_error(st->location, "\\x must be followed by two hexadecimal digits (0-9, A-F) to specify a byte");

    char s[] = {c1, c2, '\0'};
    return strtol(s, NULL, 16);
}

// Assumes the initial quote has been read already
static char *read_string(struct State *st, char quote, int *len)
{
    assert(quote == '\'' || quote == '"');
    List(char) result = {0};

    char c, after_backslash;
    while( (c=read_byte(st)) != quote )
    {
        switch(c) {
        case '\n':
            st->location.lineno--;  // to get error at the correct line number
            goto missing_end_quote;
        case '\0':
            goto missing_end_quote;
        case '\\':
            // \n means newline, for example
            after_backslash = read_byte(st);
            switch(after_backslash) {
            case 'n':
                Append(&result, '\n');
                break;
            case 'r':
                Append(&result, '\r');
                break;
            case 't':
                Append(&result, '\t');
                break;
            case '\\':
            case '\'':
            case '"':
                if (after_backslash == '"' && quote == '\'')
                    fail_with_error(st->location, "double quotes shouldn't be escaped in byte literals");
                if (after_backslash == '\'' && quote == '"')
                    fail_with_error(st->location, "single quotes shouldn't be escaped in strings");
                Append(&result, after_backslash);
                break;
            case '0':
                if (quote == '"')
                    fail_with_error(st->location, "strings cannot contain zero bytes (\\0), because that is the special end marker byte");
                Append(&result, '\0');
                break;
            case 'x':
                {
                    char b = read_hex_escape_byte(st);
                    if (quote == '"' && b == 0)
                        fail_with_error(st->location, "strings cannot contain zero bytes (\\x00), because that is the special end marker byte");
                    Append(&result, b);
                }
                break;
            case '\n':
                // \ at end of line, string continues on next line
                if (quote == '\'') {
                    // TODO: tests
                    st->location.lineno--;  // to get error at the correct line number
                    goto missing_end_quote;
                }
                break;
            case '\0':
                goto missing_end_quote;
            default:
                if ((unsigned char)after_backslash < 0x80 && isprint(after_backslash))
                    fail_with_error(st->location, "unknown escape: '\\%c'", after_backslash);
                else
                    fail_with_error(st->location, "unknown '\\' escape");
            }
            break;
        default:
            Append(&result, c);
            break;
        }
    }

    if(len)
        *len = result.len;
    Append(&result, '\0');
    return result.ptr;

missing_end_quote:
    // TODO: tests
    if (quote == '"')
        fail_with_error(st->location, "missing \" to end the string");
    else
        fail_with_error(st->location, "missing ' to end the byte literal");
}

static char read_char_literal(struct State *st)
{
    int len;
    char *s = read_string(st, '\'', &len);
    if (len == 0)
        fail_with_error(st->location, "a byte literal cannot be empty, maybe use double quotes to instead make a string?");
    if (len >= 2)
        fail_with_error(st->location, "single quotes are for a single character, maybe use double quotes to instead make a string?");
    char result = s[0];
    free(s);
    return result;
}

static const char operatorChars[] = "=<>!.,()[]{};:+-*/&%";

static const char *read_operator(struct State *st)
{
    const char *operators[] = {
        // This list of operators is in 3 places. Please keep them in sync:
        //   - the Jou compiler written in C
        //   - self-hosted compiler
        //   - syntax documentation
        //
        // Longer operators are first, so that '==' does not tokenize as '=' '='
        "...", "===", "!==",
        "==", "!=", "->", "<=", ">=", "++", "--", "+=", "-=", "*=", "/=", "%=", "::",
        ".", ",", ":", ";", "=", "(", ")", "{", "}", "[", "]", "&", "%", "*", "/", "+", "-", "<", ">",
        NULL,
    };

    char operator[4] = {0};
    while(strlen(operator) < 3) {
        char c = read_byte(st);
        if (c=='\0')
            break;
        if(!strchr(operatorChars, c)) {
            unread_byte(st, c);
            break;
        }
        operator[strlen(operator)] = c;
    }

    for (const char *const *op = operators; *op; op++) {
        if (!strncmp(operator, *op, strlen(*op))) {
            // Unread the bytes we didn't use.
            for (int i = strlen(operator) - 1; i >= (int)strlen(*op); i--)
                unread_byte(st, operator[i]);
            // "===" and "!==" are here only to give a better error message to javascript people.
            if (!strcmp(*op, "===") || !strcmp(*op, "!=="))
                break;
            return *op;
        }
    }

    fail_with_error(st->location, "there is no '%s' operator", operator);
}


static void handle_parentheses(struct State *st, const struct Token *t)
{
    const char open2close[] = { ['[']=']', ['{']='}', ['(']=')' };
    const char close2open[] = { [']']='[', ['}']='{', [')']='(' };

    if (t->type == TOKEN_END_OF_FILE && st->nparens > 0) {
        Token opentoken = st->parens[0];
        char open = opentoken.data.operator[0];
        char close = open2close[(int)open];
        fail_with_error(opentoken.location, "'%c' without a matching '%c'", open, close);
    }

    if (t->type == TOKEN_OPERATOR && strstr("[{(", t->data.operator)) {
        if (st->nparens == sizeof(st->parens)/sizeof(st->parens[0]))
            fail_with_error(t->location, "too many nested parentheses");
        st->parens[st->nparens++] = *t;
    }

    if (t->type == TOKEN_OPERATOR && strstr("]})", t->data.operator)) {
        char close = t->data.operator[0];
        char open = close2open[(int)close];

        if (st->nparens == 0 || st->parens[--st->nparens].data.operator[0] != open)
            fail_with_error(t->location, "'%c' without a matching '%c'", close, open);
    }
}

static Token read_token(struct State *st)
{
    while(1) {
        Token t = { .location = st->location };
        char c = read_byte(st);

        switch(c) {
        case '#': consume_rest_of_line(st); continue;
        case ' ': continue;
        case '\n':
            if (st->nparens > 0)
                continue;  // Ignore newlines when inside parentheses.
            t.type = TOKEN_NEWLINE;
            t.data.indentation_level = read_indentation_level_for_newline_token(st);
            break;
        case '\0':
            t.type = TOKEN_END_OF_FILE;
            handle_parentheses(st, &t);
            break;
        case '\'': t.type = TOKEN_CHAR; t.data.char_value = read_char_literal(st); break;
        case '"': t.type = TOKEN_STRING; t.data.string_value = read_string(st, '"', NULL); break;
        default:
            if(is_identifier_or_number_byte(c)) {
                read_identifier_or_number(st, c, &t.data.name);
                if (is_keyword(t.data.name))
                    t.type = TOKEN_KEYWORD;
                else if ('0'<=t.data.name[0] && t.data.name[0]<='9') {
                    if (is_valid_double(t.data.name)) {
                        t.type = TOKEN_DOUBLE;
                    } else if (is_valid_float(t.data.name)) {
                        t.data.name[strlen(t.data.name)-1] = '\0';  // remove 'F' or 'f' suffix
                        t.type = TOKEN_FLOAT;
                    } else if (t.data.name[strlen(t.data.name)-1] == 'L') {
                        t.data.name[strlen(t.data.name)-1] = '\0';
                        t.type = TOKEN_LONG;
                        t.data.long_value = (int64_t)parse_integer(t.data.name, t.location, 64);
                    } else {
                        t.type = TOKEN_INT;
                        t.data.int_value = (int32_t)parse_integer(t.data.name, t.location, 32);
                    }
                } else {
                    t.type = TOKEN_NAME;
                }
            } else if (strchr(operatorChars, c)) {
                unread_byte(st, c);
                t.type = TOKEN_OPERATOR;
                strcpy(t.data.operator, read_operator(st));
                handle_parentheses(st, &t);
            } else {
                if ((unsigned char)c < 0x80 && isprint(c))
                    fail_with_error(st->location, "unexpected byte '%c' (%#02x)", c, (unsigned char)c);
                else
                    fail_with_error(st->location, "unexpected byte %#02x", (unsigned char)c);
            }
            break;
        }
        return t;
    }
}

static Token *tokenize_without_indent_dedent_tokens(FILE *f, const char *filename)
{
    assert(f);
    struct State st = { .location.filename=filename, .f = f };

    /*
    Add a fake newline to the beginning. It does a few things:
      * Less special-casing: blank lines in the beginning of the file can
        cause there to be a newline token anyway.
      * It is easier to detect an unexpected indentation in the beginning
        of the file, as it becomes just like any other indentation.
      * Line numbers start at 1.
    */
    Append(&st.pushback, '\n');

    List(Token) tokens = {0};
    while(tokens.len == 0 || tokens.ptr[tokens.len-1].type != TOKEN_END_OF_FILE)
        Append(&tokens, read_token(&st));

    free(st.pushback.ptr);
    return tokens.ptr;
}

// Add indent/dedent tokens after newline tokens that change the indentation level.
static Token *handle_indentations(const Token *temp_tokens)
{
    List(Token) tokens = {0};
    const Token *t = temp_tokens;
    int level = 0;

    do{
        if (t->type == TOKEN_END_OF_FILE) {
            while(level) {
                Append(&tokens, (Token){ .location=t->location, .type=TOKEN_DEDENT });
                level -= 4;
            }
        }

        Append(&tokens, *t);

        if (t->type == TOKEN_NEWLINE) {
            Location after_newline = t->location;
            after_newline.lineno++;

            if (t->data.indentation_level % 4 != 0)
                fail_with_error(after_newline, "indentation must be a multiple of 4 spaces");

            while (level < t->data.indentation_level) {
                Append(&tokens, (Token){ .location=after_newline, .type=TOKEN_INDENT });
                level += 4;
            }

            while (level > t->data.indentation_level) {
                Append(&tokens, (Token){ .location=after_newline, .type=TOKEN_DEDENT });
                level -= 4;
            }
        }
    } while (t++->type != TOKEN_END_OF_FILE);

    /*
    Delete the newline token in the beginning.

    If the file has indentations after it, they are now represented by separate
    indent tokens and parsing will fail. If the file doesn't have any blank/comment
    lines in the beginning, it has a newline token anyway to avoid special casing.
    */
    assert(tokens.ptr[0].type == TOKEN_NEWLINE);
    memmove(&tokens.ptr[0], &tokens.ptr[1], sizeof(tokens.ptr[0]) * (tokens.len - 1));

    return tokens.ptr;
}

Token *tokenize(FILE *f, const char *filename)
{
    Token *tokens1 = tokenize_without_indent_dedent_tokens(f, filename);
    Token *tokens2 = handle_indentations(tokens1);
    free(tokens1);
    return tokens2;
}
