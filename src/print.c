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
    case TOKEN_OPERATOR:
        printf("opereator '%s'\n", token->data.operator);
        break;
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

static void print_ast_function_signature(const struct Signature *sig, int indent)
{
    char *s = signature_to_string(sig, true);
    printf("%*sSignature on line %d: %s\n", indent, "", sig->location.lineno, s);
    free(s);
}

static void print_ast_call(const struct AstCall *call, int arg_indent);

static void print_ast_expression(const struct AstExpression *expr, int indent)
{
    printf("%*sExpression on line %d: ", indent, "", expr->location.lineno);

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
    case AST_EXPR_GT:
        printf("Check if greater.\n");
        print_ast_expression(&expr->data.operands[0], indent+2);
        print_ast_expression(&expr->data.operands[1], indent+2);
        break;
    case AST_EXPR_GE:
        printf("Check if greater or equal.\n");
        print_ast_expression(&expr->data.operands[0], indent+2);
        print_ast_expression(&expr->data.operands[1], indent+2);
        break;
    case AST_EXPR_LT:
        printf("Check if less.\n");
        print_ast_expression(&expr->data.operands[0], indent+2);
        print_ast_expression(&expr->data.operands[1], indent+2);
        break;
    case AST_EXPR_LE:
        printf("Check if less or equal.\n");
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
    case AST_EXPR_DIV:
        printf("Divide numbers.\n");
        print_ast_expression(&expr->data.operands[0], indent+2);
        print_ast_expression(&expr->data.operands[1], indent+2);
        break;
    case AST_EXPR_PRE_INCREMENT: printf("Pre-increment.\n"); print_ast_expression(&expr->data.operands[0], indent+2); break;
    case AST_EXPR_PRE_DECREMENT: printf("Pre-decrement.\n"); print_ast_expression(&expr->data.operands[0], indent+2); break;
    case AST_EXPR_POST_INCREMENT: printf("Post-increment.\n"); print_ast_expression(&expr->data.operands[0], indent+2); break;
    case AST_EXPR_POST_DECREMENT: printf("Post-decrement.\n"); print_ast_expression(&expr->data.operands[0], indent+2); break;
    case AST_EXPR_ASSIGN:
        printf("Set the value of a variable or pointer.\n");
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
        case AST_STMT_EXPRESSION_STATEMENT:
            printf("Evaluate an expression and discard the result.\n");
            print_ast_expression(&stmt->data.expression, indent+2);
            break;
        case AST_STMT_RETURN_VALUE:
            printf("Return a value:\n");
            print_ast_expression(&stmt->data.expression, indent+2);
            break;
        case AST_STMT_RETURN_WITHOUT_VALUE:
            printf("Return without a return value.\n");
            break;
        case AST_STMT_IF:
            printf("If statement\n");
            for (int i = 0; i < stmt->data.ifstatement.n_if_and_elifs; i++) {
                printf("%*s  Condition:\n", indent, "");
                print_ast_expression(&stmt->data.ifstatement.if_and_elifs[i].condition, indent+4);
                print_ast_body(&stmt->data.ifstatement.if_and_elifs[i].body, indent+2);
            }
            if (stmt->data.ifstatement.elsebody.nstatements > 0) {
                printf("%*s  Else:\n", indent, "");
                print_ast_body(&stmt->data.ifstatement.elsebody, indent+4);
            }
            break;
        case AST_STMT_WHILE:
            printf("While loop\n");
            printf("%*s  Condition:\n", indent, "");
            print_ast_expression(&stmt->data.whileloop.condition, indent+4);
            print_ast_body(&stmt->data.whileloop.body, indent+2);
            break;
        case AST_STMT_FOR:
            printf("For loop\n");
            printf("%*s  Initializer:\n", indent, "");
            print_ast_expression(&stmt->data.forloop.init, indent+4);
            printf("%*s  Condition:\n", indent, "");
            print_ast_expression(&stmt->data.forloop.cond, indent+4);
            printf("%*s  Incrementer (runs after body):\n", indent, "");
            print_ast_expression(&stmt->data.forloop.incr, indent+4);
            print_ast_body(&stmt->data.forloop.body, indent+2);
            break;
        case AST_STMT_BREAK:
            printf("Break loop\n");
            break;
        case AST_STMT_CONTINUE:
            printf("Continue loop\n");
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
            case AST_TOPLEVEL_DECLARE_FUNCTION:
                printf("Declare a C function.\n");
                print_ast_function_signature(&topnodelist->data.decl_signature, 2);
                break;
            case AST_TOPLEVEL_DEFINE_FUNCTION:
                printf("Define a function.\n");
                print_ast_function_signature(&topnodelist->data.funcdef.signature, 2);
                print_ast_body(&topnodelist->data.funcdef.body, 2);
                break;
            case AST_TOPLEVEL_END_OF_FILE:
                printf("End of file.\n");
                break;
        }
        printf("\n");

    } while (topnodelist++->kind != AST_TOPLEVEL_END_OF_FILE);
}


static void print_cf_instruction(const struct CfInstruction *ins, int indent)
{
    printf("%*sline %-4d  ", indent, "", ins->location.lineno);

    if (ins->destvar)
        printf("%s = ", ins->destvar->name);

    switch(ins->kind) {
    case CF_ADDRESS_OF_VARIABLE:
        printf("address of %s", ins->operands[0]->name);
        break;
    case CF_BOOL_NEGATE:
        printf("boolean negation of %s", ins->operands[0]->name);
        break;
    case CF_CALL:
        printf("call %s(", ins->data.funcname);
        for (int i = 0; i < ins->noperands; i++) {
            if(i) printf(", ");
            printf("%s", ins->operands[i]->name);
        }
        printf(")");
        break;
    case CF_CAST_TO_BIGGER_SIGNED_INT:
        printf("cast %s to %d-bit signed int",
            ins->operands[0]->name, ins->destvar->type.data.width_in_bits);
        break;
    case CF_CAST_TO_BIGGER_UNSIGNED_INT:
        printf("cast %s to %d-bit unsigned int",
            ins->operands[0]->name, ins->destvar->type.data.width_in_bits);
        break;
    case CF_INT_CONSTANT: printf("%lld", ins->data.int_value); break;
    case CF_STRING_CONSTANT: print_string(ins->data.string_value); break;
    case CF_TRUE: printf("True"); break;
    case CF_FALSE: printf("False"); break;

    case CF_INT_ADD:
    case CF_INT_SUB:
    case CF_INT_MUL:
    case CF_INT_SDIV:
    case CF_INT_UDIV:
    case CF_INT_EQ:
    case CF_INT_LT:
        switch(ins->kind){
            case CF_INT_ADD: printf("iadd "); break;
            case CF_INT_SUB: printf("isub "); break;
            case CF_INT_MUL: printf("imul "); break;
            case CF_INT_SDIV: printf("sdiv "); break;
            case CF_INT_UDIV: printf("udiv "); break;
            case CF_INT_EQ: printf("ieq "); break;
            case CF_INT_LT: printf("ilt "); break;
            default: assert(0);
        }
        printf("%s, %s", ins->operands[0]->name, ins->operands[1]->name);
        break;
    case CF_LOAD_FROM_POINTER:
        // Extra parentheses to make these stand out a bit.
        printf("*(%s)", ins->operands[0]->name);
        break;
    case CF_STORE_TO_POINTER:
        printf("*(%s) = %s", ins->operands[0]->name, ins->operands[1]->name);
        break;
    case CF_VARCPY:
        printf("%s", ins->operands[0]->name);
        break;
    }

    printf("\n");
}

static void print_control_flow_graph_with_indent(const struct CfGraph *cfg, int indent)
{
    if (!cfg) {
        printf("%*sControl Flow Graph = NULL\n", indent, "");
        return;
    }

    printf("%*sVariables:\n", indent, "");
    for (struct CfVariable **var = cfg->variables.ptr; var < End(cfg->variables); var++) {
        printf("%*s  %-20s  %s\n", indent, "", (*var)->name, (*var)->type.name);
    }

    for (struct CfBlock **b = cfg->all_blocks.ptr; b < End(cfg->all_blocks); b++) {
        printf("%*sBlock %d", indent, "", (int)(b - cfg->all_blocks.ptr));
        if (*b == &cfg->start_block)
            printf(" (start block)");
        if (*b == &cfg->end_block) {
            assert((*b)->instructions.len == 0);
            printf(" is the end block.\n");
            continue;
        }
        printf(":\n");
        for (struct CfInstruction *ins = (*b)->instructions.ptr; ins < End((*b)->instructions); ins++)
            print_cf_instruction(ins, indent+2);

        if (*b == &cfg->end_block) {
            assert((*b)->iftrue == NULL);
            assert((*b)->iffalse == NULL);
        } else {
            int trueidx=-1, falseidx=-1;
            for (int i = 0; i < cfg->all_blocks.len; i++) {
                if (cfg->all_blocks.ptr[i]==(*b)->iftrue) trueidx=i;
                if (cfg->all_blocks.ptr[i]==(*b)->iffalse) falseidx=i;
            }
            assert(trueidx!=-1);
            assert(falseidx!=-1);
            if (trueidx==falseidx)
                printf("%*s  Jump to block %d.\n", indent, "", trueidx);
            else {
                assert((*b)->branchvar);
                printf("%*s  If %s is True jump to block %d, otherwise block %d.\n",
                    indent, "", (*b)->branchvar->name, trueidx, falseidx);
            }
        }
    }
}

void print_control_flow_graph(const struct CfGraph *cfg)
{
    print_control_flow_graph_with_indent(cfg, 0);
}

void print_control_flow_graphs(const struct CfGraphFile *cfgfile)
{
    printf("===== Control Flow Graphs for file \"%s\" =====\n", cfgfile->filename);
    for (int i = 0; i < cfgfile->nfuncs; i++) {
        char *sigstr = signature_to_string(&cfgfile->signatures[i], true);
        printf("Function on line %d: %s\n", cfgfile->signatures[i].location.lineno, sigstr);
        free(sigstr);
        print_control_flow_graph_with_indent(cfgfile->graphs[i], 2);
        printf("\n");
    }
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
