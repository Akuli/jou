#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool starts_with(const char *s, const char *prefix) { return strncmp(s,prefix,strlen(prefix)) == 0; }

struct Location { const char *filename; int lineno; };

struct Token {
    enum { TOK_EOF,TOK_NEWLINE,TOK_KEYWORD,TOK_NAME,TOK_DECORATOR,TOK_STRING,TOK_BYTE,TOK_OP,TOK_DOUBLE,TOK_INT } kind;
    char *code;
    struct Location location;
};

struct Token *tokenize(const char *path)
{
    static char buf[100000];
    memset(buf, 0, sizeof(buf));
    FILE *f = fopen(path,"rb"); assert(f);
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    static struct Token tokens[10000];
    assert(tokens != NULL);
    int ntokens=0, lineno=1;

    char *s = buf;
    while(*s){
        assert(ntokens + 100 < sizeof(tokens)/sizeof(tokens[0]));
        #define AddToken(K,N) do{ tokens[ntokens++] = (struct Token){(K), strndup(s,(N)), { path, lineno }}; s += (N); } while(0)
        if (*s == '\n') {
            lineno++;
            int n = 1;
            while(s[n] == ' ') n++;
            AddToken(TOK_NEWLINE, n);
        } else if (strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_", *s)) {
            int span = (int) strspn(s, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_");
            #define f(w) (strlen((w)) == span && starts_with(s, (w)))
            bool iskw = f("import")||f("link")||f("def")||f("declare")||f("class")||f("union")||f("enum")||f("global")||f("const")||f("return")||f("if")||f("elif")||f("else")||f("while")||f("for")||f("pass")||f("break")||f("continue")||f("True")||f("False")||f("None")||f("NULL")||f("void")||f("noreturn")||f("and")||f("or")||f("not")||f("self")||f("as")||f("sizeof")||f("assert")||f("bool")||f("byte")||f("int")||f("long")||f("float")||f("double")||f("match")||f("with")||f("case");
            #undef f
            AddToken(iskw ? TOK_KEYWORD : TOK_NAME, span);
        } else if (starts_with(s, "@public") || starts_with(s, "@inline")) {
            AddToken(TOK_DECORATOR, 7);
        } else if (*s == '"') {
            int n=1;
            while (s[n] != '"') {
                if (s[n++] == '\\') { n++; }
            }
            for (int i = 0; i < n; i++)
                lineno += s[i]=='\n';
            AddToken(TOK_DECORATOR, n+1);
        } else if (s[0] == '\'' && s[1] == '\\' && s[2] != '\0' && s[3] == '\'') {
            AddToken(TOK_BYTE, 4);
        } else if (s[0] == '\'' && s[1] != '\0' && s[2] == '\'') {
            AddToken(TOK_BYTE, 3);
        } else if (starts_with(s, "...")) {
            AddToken(TOK_OP, 3);
        #define f(w) starts_with(s,(w))
        } else if( f("==")||f("!=")||f("->")||f("<=")||f(">=")||f("++")||f("--")||f("+=")||f("-=")||f("*=")||f("/=")||f("%=")||f("&=")||f("|=")||f("^=") ){
        #undef f
            AddToken(TOK_OP, 2);
        } else if (strchr(".,:;=<>(){}[]+-*/%&|^", *s)) {
            AddToken(TOK_OP, 1);
        } else if (starts_with(s, "0b")) {
            AddToken(TOK_INT, 2 + strspn(&s[2], "01"));
        } else if (starts_with(s, "0o")) {
            AddToken(TOK_INT, 2 + strspn(&s[2], "01234567"));
        } else if (starts_with(s, "0x")) {
            AddToken(TOK_INT, 2 + strspn(&s[2], "0123456789ABCDEFabcdef"));
        } else if ('0' <= s[0] && s[0] <= '9') {
            bool integer = true;
            int n = (int)strspn(s, "0123456789");
            if (s[n] == '.') {
                integer = false;
                n++;
                n += (int)strspn(&s[n], "0123456789");
            }
            if (s[n] == 'e') {
                integer = false;
                if (s[++n] == '-') n++;
                n += (int) strspn(&s[n], "0123456789");
            }
            AddToken(integer ? TOK_INT : TOK_DOUBLE, n);
        } else if (*s == '#') {
            while(*s != '\n' && *s != '\0') s++;
        } else if (*s == ' ') {
            s++;
        } else {
            fprintf(stderr,"syntax error in file \"%s\", line %d\n%.10s...\n", path, lineno, s);
            exit(1);
        }
        #undef AddToken
    }
    return tokens;
}

int main(int argc, char **argv) {
    assert(argc >= 2);
    struct Token *t = tokenize(argv[1]);
    while (t->kind != TOK_EOF) {
        printf("line %d: (%d) \"%s\"\n", t->location.lineno, t->kind, t->code);
        t++;
    }
}
