#include "jou_compiler.h"

bool evaluate_constant(const AstExpression *expr, Constant *dest)
{
    switch(expr->kind) {
    case AST_EXPR_GET_VARIABLE:
        if (!strcmp(expr->data.varname, "WINDOWS")) {
            #ifdef _WIN32
            *dest = bool_constant(true);
            #else
            *dest = bool_constant(false);
            #endif
            return true;
        }
        if (!strcmp(expr->data.varname, "MACOS")) {
            #ifdef __APPLE__
            *dest = bool_constant(true);
            #else
            *dest = bool_constant(false);
            #endif
            return true;
        }
        return false;
    case AST_EXPR_CONSTANT:
        *dest = copy_constant(&expr->data.constant);
        return true;
    default:
        return false;
    }
}

static int evaluate_condition(const AstExpression *condast)
{
    Constant cond;
    if (!evaluate_constant(condast, &cond)) {
        return -1;
    }
    if (cond.kind != CONSTANT_BOOL) {
        // TODO: test this error
        // TODO: mention the actual type
        fail(condast->location, "if condition must be a boolean");
    }
    return (int)cond.data.boolean;
}

typedef List(AstStatement) StatementList;

static void simplify_if_statement(AstIfStatement *ifstmt)
{
    AstConditionAndBody *pair = &ifstmt->if_and_elifs[0];
    AstConditionAndBody *end = &ifstmt->if_and_elifs[ifstmt->n_if_and_elifs];

    while (pair < end) {
        switch(evaluate_condition(&pair->condition)) {
        case 1:
            // Condition known to be true. Let's just go here unconditionally as if it was the "else" part.
            for (AstConditionAndBody *p = pair; p < end; p++) {
                free_ast_expression(&p->condition);
                if (p!=pair)
                    free_ast_body(&p->body);
            }
            free_ast_body(&ifstmt->elsebody);
            ifstmt->elsebody = pair->body;
            ifstmt->n_if_and_elifs = pair - ifstmt->if_and_elifs;
            return;
        case 0:
            // Condition known to be false. Let's delete the if+then pair.
            free_ast_expression(&pair->condition);
            free_ast_body(&pair->body);
            memmove(pair+1, pair, (sizeof *pair)*(end-(pair+1)));
            ifstmt->n_if_and_elifs--;
            end--;
            break;
        case -1:
            // Let's leave this pair untouched and continue to the next pair.
            pair++;
            break;
        default:
            assert(0);
        }
    }

    assert(end - ifstmt->if_and_elifs == ifstmt->n_if_and_elifs);
}

static void handle_toplevel_statement(AstStatement *stmt, StatementList *result)
{
    switch(stmt->kind) {
    case AST_STMT_IF:
        simplify_if_statement(&stmt->data.ifstatement);
        // Zero means we simplified the if statement so much that it is just a flat list of statements.
        if (stmt->data.ifstatement.n_if_and_elifs != 0) {
            // TODO: test this error
            fail(stmt->data.ifstatement.if_and_elifs[0].condition.location, "cannot evaluate if condition at compile time");
        }
        for (int i = 0; i < stmt->data.ifstatement.elsebody.nstatements; i++)
            handle_toplevel_statement(&stmt->data.ifstatement.elsebody.statements[i], result);
        break;

    case AST_STMT_FUNCTION:
    case AST_STMT_DECLARE_GLOBAL_VAR:
    case AST_STMT_DEFINE_GLOBAL_VAR:
    case AST_STMT_DEFINE_CLASS:
    case AST_STMT_DEFINE_ENUM:
        Append(result, *stmt);
        break;

    default:
        // TODO: test this
        // TODO: better error message?
        fail_with_error(stmt->location, "you must put this inside a function");
    }
}

void evaluate_compile_time_if_statements(AstFile *file)
{
    StatementList result = {0};
    for (int i = 0; i < file->body.nstatements; i++)
        handle_toplevel_statement(&file->body.statements[i], &result);
    file->body.statements = result.ptr;
    file->body.nstatements = result.len;
}
