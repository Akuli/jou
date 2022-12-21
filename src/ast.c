#include "ast.h"
#include "compile_steps.h"

void print_ast(struct AstStatement *statements)
{
    printf("AST for file \"%s\":\n", statements->location.filename);

    do {
        printf("  Line %d: ", statements->location.lineno);
        switch(statements->kind) {
            #define f(x) case x: printf(#x); break
            f(AST_STMT_CIMPORT_FUNCTION);
            f(AST_STMT_CALL);
            f(AST_STMT_END_OF_FILE);
            #undef f
        }

        switch(statements->kind) {
        case AST_STMT_CIMPORT_FUNCTION:
            printf(" funcname=\"%s\"", statements->data.cimport.funcname);
            break;
        case AST_STMT_CALL:
            printf(" funcname=\"%s\" arg=%d",
                statements->data.call.funcname,
                statements->data.call.arg);
            break;
        case AST_STMT_END_OF_FILE:
            break;
        }

        printf("\n");

    } while (statements++->kind != AST_STMT_END_OF_FILE);

    printf("\n");
}

void free_ast(struct AstStatement *statements)
{
    free(statements);
}
