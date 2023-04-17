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

static bool evaluate_condition(const AstExpression *condast, const char *if_or_elif)
{
    assert(!strcmp(if_or_elif, "if") || !strcmp(if_or_elif, "elif"));

    Constant cond;
    if (!evaluate_constant(condast, &cond)) {
        // TODO: test this error
        fail(condast->location, "cannot evaluate %s condition at compile time", if_or_elif);
    }
    if (cond.kind != CONSTANT_BOOL) {
        // TODO: test this error
        // TODO: mention the actual type
        fail(condast->location, "%s condition must be a boolean", if_or_elif);
    }
    return cond.data.boolean;
}

typedef List(AstStatement) StatementList;
static void handle_body(const AstBody *body, StatementList *result);

static void handle_if(const AstIfStatement *ifstmt, StatementList *result)
{
    bool a_condition_was_true = false;

    for (int i = 0; i < ifstmt->n_if_and_elifs; i++) {
        const AstExpression *condast = &ifstmt->if_and_elifs[i].condition;
        const AstBody *bodyast = &ifstmt->if_and_elifs[i].body;
        if (!a_condition_was_true && evaluate_condition(condast, i==0?"if":"elif")) {
            handle_body(bodyast, result);
            a_condition_was_true = true;
        } else {
            free_ast_body(bodyast);
        }
    }

    if (a_condition_was_true)
        free_ast_body(&ifstmt->elsebody);
    else
        handle_body(&ifstmt->elsebody, result);
}

static void handle_statement(const AstStatement *stmt, StatementList *result)
{
    switch(stmt->kind) {
    case AST_STMT_IF:
        handle_if(&stmt->data.ifstatement, result);
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

static void handle_body(const AstBody *body, StatementList *result)
{
    for (int i = 0; i < body->nstatements; i++)
        handle_statement(&body->statements[i], result);
}

void evaluate_compile_time_if_statements(AstFile *file)
{
    StatementList result = {0};
    handle_body(&file->body, &result);
    file->body.statements = result.ptr;
    file->body.nstatements = result.len;
}
