#include "ast.h"
#include "compile_steps.h"

static void print_function_signature(const struct AstFunctionSignature *sig, int indent)
{
    printf("%*sfunction signature (on line %d): int %s(", indent, "", sig->location.lineno, sig->funcname);
    for (int i = 0; i < sig->nargs; i++) {
        if(i) printf(", ");
        printf("int");
    }
    printf(")\n");
}

static void print_statement(const struct AstStatement *stmt, int indent)
{
    printf("%*s(line %d) ", indent, "", stmt->location.lineno);
    switch(stmt->kind) {
        #define f(x) case x: printf(#x); break
        f(AST_STMT_CALL);
        f(AST_STMT_RETURN);
        #undef f
    }

    switch(stmt->kind) {
        case AST_STMT_CALL:
            printf(" funcname=\"%s\" arg=%d\n", stmt->data.call.funcname, stmt->data.call.arg);
            break;
        case AST_STMT_RETURN:
            printf(" returnvalue=%d\n", stmt->data.returnvalue);
            break;
    }
}

static void print_body(const struct AstBody *body, int indent)
{
    printf("%*sbody:\n", indent, "");
    for (int i = 0; i < body->nstatements; i++)
        print_statement(&body->statements[i], indent+2);
}

void print_ast(const struct AstToplevelNode *topnodelist)
{
    printf("--- AST for file \"%s\" ---\n", topnodelist->location.filename);

    do {
        printf("line %d: ", topnodelist->location.lineno);

        switch(topnodelist->kind) {
            #define f(x) case x: printf(#x); break
            f(AST_TOPLEVEL_DEFINE_FUNCTION);
            f(AST_TOPLEVEL_CIMPORT_FUNCTION);
            f(AST_TOPLEVEL_END_OF_FILE);
            #undef f
        }
        printf("\n");

        switch(topnodelist->kind) {
            case AST_TOPLEVEL_CIMPORT_FUNCTION:
                print_function_signature(&topnodelist->data.cimport_signature, 2);
                break;
            case AST_TOPLEVEL_DEFINE_FUNCTION:
                print_function_signature(&topnodelist->data.funcdef.signature, 2);
                print_body(&topnodelist->data.funcdef.body, 2);
                break;
            case AST_TOPLEVEL_END_OF_FILE:
                break;
        }
        printf("\n");

    } while (topnodelist++->kind != AST_TOPLEVEL_END_OF_FILE);

    printf("\n");
}

static void free_body(const struct AstBody *body)
{
    free(body->statements);
}

void free_ast(struct AstToplevelNode *topnodelist)
{
    for (struct AstToplevelNode *t = topnodelist; t->kind != AST_TOPLEVEL_END_OF_FILE; t++) {
        switch(t->kind) {
        case AST_TOPLEVEL_CIMPORT_FUNCTION:
            break;
        case AST_TOPLEVEL_DEFINE_FUNCTION:
            free_body(&t->data.funcdef.body);
            break;
        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        }
    }
    free(topnodelist);
}
