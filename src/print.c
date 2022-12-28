#include <ctype.h>
#include <stdio.h>
#include "jou_compiler.h"
#include "util.h"

static void print_byte(char b)
{
    printf("%#02x", b);
    if (isprint(b))
        printf(" '%c'", b);
}

static void print_string(const char *s)
{
    putchar('"');
    for (int i = 0; s[i]; i++) {
        if (isprint(s[i]))
            putchar(s[i]);
        else if (s[i] == '\n')
            printf("\\n");
        else
            printf("\\x%02x", s[i]);     // TODO: \x is not yet recognized by the tokenizer
    }
    putchar('"');
}

void print_token(const struct Token *token)
{
    switch(token->type) {
    case TOKEN_INT:
        printf("integer %d\n", token->data.int_value);
        break;
    case TOKEN_CHAR:
        printf("character ");
        print_byte(token->data.char_value);
        printf("\n");
        break;
    case TOKEN_STRING:
        printf("string ");
        print_string(token->data.string_value);
        printf("\n");
        break;
    case TOKEN_NAME:
        printf("name \"%s\"\n", token->data.name);
        break;
    case TOKEN_KEYWORD:
        printf("keyword \"%s\"\n", token->data.name);
        break;
    case TOKEN_NEWLINE:
        printf("newline token (next line has %d spaces of indentation)\n", token->data.indentation_level);
        break;
    case TOKEN_END_OF_FILE:
        printf("end of file\n");
        break;
    case TOKEN_INDENT:
        printf("more indentation (+4 spaces)\n");
        break;
    case TOKEN_DEDENT:
        printf("less indentation (-4 spaces)\n");
        break;

    // These are listed explicitly here instead of "default" so that you get a
    // compiler error coming from here after adding a new token type. That should
    // remind you to keep this function up to date.
    case TOKEN_OPENPAREN: printf("'('\n"); break;
    case TOKEN_CLOSEPAREN: printf("')'\n"); break;
    case TOKEN_AMP: printf("'&'\n"); break;
    case TOKEN_ARROW: printf("'->'\n"); break;
    case TOKEN_COLON: printf("':'\n"); break;
    case TOKEN_COMMA: printf("','\n"); break;
    case TOKEN_EQUAL_SIGN: printf("'='\n"); break;
    case TOKEN_PLUS: printf("'+'\n"); break;
    case TOKEN_MINUS: printf("'-'\n"); break;
    case TOKEN_STAR: printf("'*'\n"); break;
    case TOKEN_DOT: printf("'.'\n"); break;
    case TOKEN_DOTDOTDOT: printf("'...'\n"); break;
    case TOKEN_EQ: printf("'=='\n"); break;
    case TOKEN_NE: printf("'!='\n"); break;
    }
}

void print_tokens(const struct Token *tokens)
{
    printf("===== Tokens for file \"%s\" =====\n", tokens->location.filename);
    int lastlineno = -1;
    do {
        if (tokens->location.lineno != lastlineno) {
            printf("\nLine %d:\n", tokens->location.lineno);
            lastlineno = tokens->location.lineno;
        }
        printf("  ");
        print_token(tokens);
    } while (tokens++->type != TOKEN_END_OF_FILE);

    printf("\n");
}

char *signature_to_string(const struct AstFunctionSignature *sig, bool include_return_type)
{
    List(char) result = {0};
    AppendStr(&result, sig->funcname);
    Append(&result, '(');

    for (int i = 0; i < sig->nargs; i++) {
        if(i)
            AppendStr(&result, ", ");
        AppendStr(&result, sig->argnames[i]);
        AppendStr(&result, ": ");
        AppendStr(&result, sig->argtypes[i].name);
    }
    if (sig->takes_varargs) {
        if (sig->nargs)
            AppendStr(&result, ", ");
        AppendStr(&result, "...");
    }
    Append(&result, ')');
    if (include_return_type) {
        AppendStr(&result, " -> ");
        AppendStr(&result, sig->returntype ? sig->returntype->name : "void");
    }
    Append(&result, '\0');
    return result.ptr;
}

static void print_ast_function_signature(const struct AstFunctionSignature *sig, int indent)
{
    char *s = signature_to_string(sig, true);
    printf("%*sSignature on line %d: %s\n", indent, "", sig->location.lineno, s);
    free(s);
}

static void print_ast_call(const struct AstCall *call, int arg_indent);

static void print_ast_expression(const struct AstExpression *expr, int indent)
{
    printf("%*sExpression of type %s", indent, "", expr->type_before_implicit_cast.name);
    if (strcmp(expr->type_before_implicit_cast.name, expr->type_after_implicit_cast.name))
        printf(" with implicit cast to %s", expr->type_after_implicit_cast.name);
    printf(" on line %d: ", expr->location.lineno);

    switch(expr->kind) {
    case AST_EXPR_CALL:
        print_ast_call(&expr->data.call, indent+2);
        break;
    case AST_EXPR_ADDRESS_OF:
        printf("Get the address of an object as a pointer.\n");
        print_ast_expression(&expr->data.operands[0], indent+2);
        break;
    case AST_EXPR_DEREFERENCE:
        printf("Dereference a pointer.\n");
        print_ast_expression(&expr->data.operands[0], indent+2);
        break;
    case AST_EXPR_EQ:
        printf("Check if equal.\n");
        print_ast_expression(&expr->data.operands[0], indent+2);
        print_ast_expression(&expr->data.operands[1], indent+2);
        break;
    case AST_EXPR_NE:
        printf("Check if not equal.\n");
        print_ast_expression(&expr->data.operands[0], indent+2);
        print_ast_expression(&expr->data.operands[1], indent+2);
        break;
    case AST_EXPR_ADD:
        printf("Add numbers.\n");
        print_ast_expression(&expr->data.operands[0], indent+2);
        print_ast_expression(&expr->data.operands[1], indent+2);
        break;
    case AST_EXPR_SUB:
        printf("Subtract numbers.\n");
        print_ast_expression(&expr->data.operands[0], indent+2);
        print_ast_expression(&expr->data.operands[1], indent+2);
        break;
    case AST_EXPR_MUL:
        printf("Multiply numbers.\n");
        print_ast_expression(&expr->data.operands[0], indent+2);
        print_ast_expression(&expr->data.operands[1], indent+2);
        break;
    case AST_EXPR_GET_VARIABLE:
        printf("Get the value of variable \"%s\".\n", expr->data.varname);
        break;
    case AST_EXPR_INT_CONSTANT:
        printf("int %d\n", expr->data.int_value);
        break;
    case AST_EXPR_CHAR_CONSTANT:
        printf("byte ");
        print_byte(expr->data.char_value);
        printf("\n");
        break;
    case AST_EXPR_STRING_CONSTANT:
        printf("string ");
        print_string(expr->data.string_value);
        printf("\n");
        break;
    case AST_EXPR_TRUE:
        printf("True\n");
        break;
    case AST_EXPR_FALSE:
        printf("False\n");
        break;
    }
}

static void print_ast_call(const struct AstCall *call, int arg_indent)
{
    printf("Call function %s with %d argument%s.\n", call->funcname, call->nargs, call->nargs==1?"":"s");
    for (int i = 0; i < call->nargs; i++) {
        printf("%*sArgument %d:\n", arg_indent, "", i);
        print_ast_expression(&call->args[i], arg_indent+2);
    }
}

static void print_ast_body(const struct AstBody *body, int indent);

static void print_ast_statement(const struct AstStatement *stmt, int indent)
{
    printf("%*sStatement on line %d: ", indent, "", stmt->location.lineno);

    switch(stmt->kind) {
        case AST_STMT_CALL:
            print_ast_call(&stmt->data.call, indent+2);
            break;
        case AST_STMT_SETVAR:
            printf("Set variable \"%s\" to:\n", stmt->data.setvar.varname);
            print_ast_expression(&stmt->data.setvar.value, indent+2);
            break;
        case AST_STMT_RETURN_VALUE:
            printf("Return a value:\n");
            print_ast_expression(&stmt->data.returnvalue, indent+2);
            break;
        case AST_STMT_RETURN_WITHOUT_VALUE:
            printf("Return without a return value.\n");
            break;
        case AST_STMT_IF:
            printf("If statement\n");
            printf("%*s  Condition:\n", indent*2, "");
            print_ast_expression(&stmt->data.ifstatement.condition, indent+4);
            print_ast_body(&stmt->data.ifstatement.body, indent+2);
            break;
    }
}

static void print_ast_body(const struct AstBody *body, int indent)
{
    printf("%*sBody:\n", indent, "");
    for (int i = 0; i < body->nstatements; i++)
        print_ast_statement(&body->statements[i], indent+2);
}

void print_ast(const struct AstToplevelNode *topnodelist)
{
    printf("===== AST for file \"%s\" =====\n", topnodelist->location.filename);

    do {
        printf("line %d: ", topnodelist->location.lineno);

        switch(topnodelist->kind) {
            case AST_TOPLEVEL_CDECL_FUNCTION:
                printf("Declare a C function.\n");
                print_ast_function_signature(&topnodelist->data.decl_signature, 2);
                break;
            case AST_TOPLEVEL_DEFINE_FUNCTION:
                printf("Define a function.\n");
                print_ast_function_signature(&topnodelist->data.funcdef.signature, 2);
                for (struct AstLocalVariable *var = topnodelist->data.funcdef.locals; var && var->name[0]; var++)
                    printf("  Type of local variable \"%s\" is %s.\n", var->name, var->type.name);
                print_ast_body(&topnodelist->data.funcdef.body, 2);
                break;
            case AST_TOPLEVEL_END_OF_FILE:
                printf("End of file.\n");
                break;
        }
        printf("\n");

    } while (topnodelist++->kind != AST_TOPLEVEL_END_OF_FILE);
}

void print_llvm_ir(LLVMModuleRef module)
{
    size_t len;
    const char *filename = LLVMGetSourceFileName(module, &len);
    printf("===== LLVM IR for file \"%.*s\" =====\n", (int)len, filename);

    char *s = LLVMPrintModuleToString(module);
    puts(s);
    LLVMDisposeMessage(s);
}
