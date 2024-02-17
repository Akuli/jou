#include "jou_compiler.h"


int get_special_constant(const char *name)
{
    if (!strcmp(name, "WINDOWS")) {
        #ifdef _WIN32
            return 1;
        #else
            return 0;
        #endif
    }
    if (!strcmp(name, "MACOS")) {
        #ifdef __APPLE__
            return 1;
        #else
            return 0;
        #endif
    }
    if (!strcmp(name, "NETBSD")) {
        #ifdef __NetBSD__
            return 1;
        #else
            return 0;
        #endif
    }
    return -1;
}


static bool evaluate_condition(const AstExpression *expr)
{
    if (expr->kind == AST_EXPR_GET_VARIABLE) {
        int v = get_special_constant(expr->data.varname);
        if (v != -1)
            return (bool)v;
    }

    fail(expr->location, "cannot evaluate condition at compile time");
}


// returns the statements to replace if statement with
static AstBody evaluate_compile_time_if_statement(AstIfStatement *if_stmt)
{
    AstBody *result = &if_stmt->elsebody;
    for (int i = 0; i < if_stmt->n_if_and_elifs; i++) {
        if (evaluate_condition(&if_stmt->if_and_elifs[i].condition)) {
            result = &if_stmt->if_and_elifs[i].body;
            break;
        }
    }

    AstBody ret = *result;
    *result = (AstBody){0};  // avoid double-free
    return ret;
}


// Replace body->statements[i] with zero or more statements from another body.
void replace(AstBody *body, int i, AstBody new)
{
    free_ast_statement(&body->statements[i]);

    size_t item_size = sizeof(body->statements[0]);
    body->statements = realloc(body->statements, (body->nstatements + new.nstatements) * item_size);
    memmove(&body->statements[i + new.nstatements], &body->statements[i+1], (body->nstatements - (i+1)) * item_size);
    memcpy(&body->statements[i], new.statements, new.nstatements * item_size);

    free(new.statements);
    body->nstatements--;
    body->nstatements += new.nstatements;
}


// This handles nested if statements.
void evaluate_compile_time_if_statements(AstBody *body)
{
    int i = 0;
    while (i < body->nstatements) {
        if (body->statements[i].kind == AST_STMT_IF)
            replace(body, i, evaluate_compile_time_if_statement(&body->statements[i].data.ifstatement));
        else
            i++;
    }
}
