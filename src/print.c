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

static void print_constant(const Constant *c)
{
    switch(c->kind) {
    case CONSTANT_BOOL:
        printf(c->data.boolean ? "True" : "False");
        break;
    case CONSTANT_INTEGER:
        printf(
            "%lld (%d-bit %s)",
            c->data.integer.value,
            c->data.integer.width_in_bits,
            c->data.integer.is_signed ? "signed" : "unsigned");
        break;
    case CONSTANT_NULL:
        printf("NULL");
        break;
    case CONSTANT_STRING:
        print_string(c->data.str);
        break;
    }
}

void print_token(const Token *token)
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

void print_tokens(const Token *tokens)
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

struct TreePrinter {
    char prefix[100];
};

// Returned sub-printer can be used to print lines that appear "inside" the given line.
struct TreePrinter print_tree_prefix(struct TreePrinter tp, bool last)
{
    struct TreePrinter sub;
    if (last) {
        printf("%s`--- ", tp.prefix);
        snprintf(sub.prefix, sizeof sub.prefix, "%s  ", tp.prefix);
    } else {
        printf("%s|--- ", tp.prefix);
        snprintf(sub.prefix, sizeof sub.prefix, "%s| ", tp.prefix);
    }
    return sub;
}

static void print_ast_type(const struct AstType *t)
{
    printf("%s", t->name);
    for (int i=0; i<t->npointers; i++)
        printf("*");
    printf(" [line %d]", t->location.lineno);
}

static void print_ast_function_signature(const AstSignature *sig)
{
    printf("%s(", sig->funcname);
    for (int i = 0; i < sig->nargs; i++) {
        if(i) printf(", ");
        printf("%s: ", sig->argnames[i]);
        print_ast_type(&sig->argtypes[i]);
    }
    printf(") -> ");
    print_ast_type(&sig->returntype);
    printf("\n");
}

static void print_ast_call(const AstCall *call, struct TreePrinter tp);

static void print_ast_expression(const AstExpression *expr, struct TreePrinter tp, bool last)
{
    struct TreePrinter sub = print_tree_prefix(tp, last);
    printf("[line %d] ", expr->location.lineno);

    switch(expr->kind) {
    case AST_EXPR_FUNCTION_CALL:
        printf("call function \"%s\"\n", expr->data.call.calledname);
        print_ast_call(&expr->data.call, sub);
        break;
    case AST_EXPR_BRACE_INIT:
        printf("brace init \"%s\"\n", expr->data.call.calledname);
        print_ast_call(&expr->data.call, sub);
        break;
    case AST_EXPR_GET_FIELD:
        printf("get field \"%s\"\n", expr->data.field.fieldname);
        print_ast_expression(expr->data.field.obj, sub, true);
        break;
    case AST_EXPR_DEREF_AND_GET_FIELD:
        printf("dereference and get field \"%s\"\n", expr->data.field.fieldname);
        print_ast_expression(expr->data.field.obj, sub, true);
        break;
    case AST_EXPR_INDEXING:
        printf("indexing\n");
        print_ast_expression(&expr->data.operands[0], sub, false);
        print_ast_expression(&expr->data.operands[1], sub, true);
        break;
    case AST_EXPR_ADDRESS_OF:
        printf("address of\n");
        print_ast_expression(&expr->data.operands[0], sub, true);
        break;
    case AST_EXPR_DEREFERENCE:
        printf("dereference\n");
        print_ast_expression(&expr->data.operands[0], sub, true);
        break;
    case AST_EXPR_EQ:
        printf("eq\n");
        print_ast_expression(&expr->data.operands[0], sub, false);
        print_ast_expression(&expr->data.operands[1], sub, true);
        break;
    case AST_EXPR_NE:
        printf("ne\n");
        print_ast_expression(&expr->data.operands[0], sub, false);
        print_ast_expression(&expr->data.operands[1], sub, true);
        break;
    case AST_EXPR_GT:
        printf("gt\n");
        print_ast_expression(&expr->data.operands[0], sub, false);
        print_ast_expression(&expr->data.operands[1], sub, true);
        break;
    case AST_EXPR_GE:
        printf("ge\n");
        print_ast_expression(&expr->data.operands[0], sub, false);
        print_ast_expression(&expr->data.operands[1], sub, true);
        break;
    case AST_EXPR_LT:
        printf("lt\n");
        print_ast_expression(&expr->data.operands[0], sub, false);
        print_ast_expression(&expr->data.operands[1], sub, true);
        break;
    case AST_EXPR_LE:
        printf("le\n");
        print_ast_expression(&expr->data.operands[0], sub, false);
        print_ast_expression(&expr->data.operands[1], sub, true);
        break;
    case AST_EXPR_ADD:
        printf("add\n");
        print_ast_expression(&expr->data.operands[0], sub, false);
        print_ast_expression(&expr->data.operands[1], sub, true);
        break;
    case AST_EXPR_SUB:
        printf("sub\n");
        print_ast_expression(&expr->data.operands[0], sub, false);
        print_ast_expression(&expr->data.operands[1], sub, true);
        break;
    case AST_EXPR_MUL:
        printf("mul\n");
        print_ast_expression(&expr->data.operands[0], sub, false);
        print_ast_expression(&expr->data.operands[1], sub, true);
        break;
    case AST_EXPR_DIV:
        printf("div\n");
        print_ast_expression(&expr->data.operands[0], sub, false);
        print_ast_expression(&expr->data.operands[1], sub, true);
        break;
    case AST_EXPR_AND:
        printf("and\n");
        print_ast_expression(&expr->data.operands[0], sub, false);
        print_ast_expression(&expr->data.operands[1], sub, true);
        break;
    case AST_EXPR_OR:
        printf("or\n");
        print_ast_expression(&expr->data.operands[0], sub, false);
        print_ast_expression(&expr->data.operands[1], sub, true);
        break;
    case AST_EXPR_NOT:
        printf("not\n");
        print_ast_expression(&expr->data.operands[0], sub, true);
        break;
    case AST_EXPR_PRE_INCREMENT: printf("pre-increment\n"); print_ast_expression(&expr->data.operands[0], sub, true); break;
    case AST_EXPR_PRE_DECREMENT: printf("pre-decrement\n"); print_ast_expression(&expr->data.operands[0], sub, true); break;
    case AST_EXPR_POST_INCREMENT: printf("post-increment\n"); print_ast_expression(&expr->data.operands[0], sub, true); break;
    case AST_EXPR_POST_DECREMENT: printf("post-decrement\n"); print_ast_expression(&expr->data.operands[0], sub, true); break;
    case AST_EXPR_GET_VARIABLE:
        printf("get variable \"%s\"\n", expr->data.varname);
        break;
    case AST_EXPR_CONSTANT:
        print_constant(&expr->data.constant);
        printf("\n");
        break;
    }
}

static void print_ast_call(const AstCall *call, struct TreePrinter tp)
{
    for (int i = 0; i < call->nargs; i++) {
        struct TreePrinter sub = print_tree_prefix(tp, i == call->nargs - 1);
        if (call->argnames)
            printf("argument \"%s\":\n", call->argnames[i]);
        else
            printf("argument %d:\n", i);
        print_ast_expression(&call->args[i], sub, true);
    }
}

static void print_ast_body(const AstBody *body, struct TreePrinter tp, bool last);

static void print_ast_statement(const AstStatement *stmt, struct TreePrinter tp, bool last)
{
    struct TreePrinter sub = print_tree_prefix(tp, last);
    printf("[line %d] ", stmt->location.lineno);

    struct TreePrinter sub2;

    switch(stmt->kind) {
        case AST_STMT_EXPRESSION_STATEMENT:
            printf("expression statement\n");
            print_ast_expression(&stmt->data.expression, sub, true);
            break;
        case AST_STMT_RETURN_VALUE:
            printf("return a value\n");
            print_ast_expression(&stmt->data.expression, sub, true);
            break;
        case AST_STMT_RETURN_WITHOUT_VALUE:
            printf("return without a value\n");
            break;
        case AST_STMT_IF:
            printf("if\n");
            for (int i = 0; i < stmt->data.ifstatement.n_if_and_elifs; i++) {
                sub2 = print_tree_prefix(sub, false);
                printf("condition:\n");
                print_ast_expression(&stmt->data.ifstatement.if_and_elifs[i].condition, sub2, true);

                bool lastrow = (
                    i == stmt->data.ifstatement.n_if_and_elifs-1
                    && stmt->data.ifstatement.elsebody.nstatements == 0);
                print_ast_body(&stmt->data.ifstatement.if_and_elifs[i].body, sub, lastrow);
            }
            if (stmt->data.ifstatement.elsebody.nstatements > 0) {
                sub2 = print_tree_prefix(sub, true);
                printf("else:\n");
                print_ast_body(&stmt->data.ifstatement.elsebody, sub2, true);
            }
            break;
        case AST_STMT_WHILE:
            printf("while\n");
            sub2 = print_tree_prefix(sub, true);
            printf("condition:\n");
            print_ast_expression(&stmt->data.whileloop.condition, sub2, true);
            print_ast_body(&stmt->data.whileloop.body, sub, true);
            break;
        case AST_STMT_FOR:
            printf("For loop\n");
            sub2 = print_tree_prefix(sub, false);
            printf("initializer:\n");
            print_ast_statement(stmt->data.forloop.init, sub2, true);
            sub2 = print_tree_prefix(sub, false);
            printf("condition:\n");
            print_ast_expression(&stmt->data.forloop.cond, sub2, true);
            sub2 = print_tree_prefix(sub, false);
            printf("incrementer (runs after body):\n");
            print_ast_statement(stmt->data.forloop.incr, sub2, true);
            print_ast_body(&stmt->data.forloop.body, sub, true);
            break;
        case AST_STMT_BREAK:
            printf("break\n");
            break;
        case AST_STMT_CONTINUE:
            printf("continue\n");
            break;
        case AST_STMT_DECLARE_LOCAL_VAR:
            printf("declare local var \"%s\", type ", stmt->data.vardecl.name);
            print_ast_type(&stmt->data.vardecl.type);
            printf("\n");
            if (stmt->data.vardecl.initial_value) {
                sub2 = print_tree_prefix(sub, true);
                printf("initial value:\n");
                print_ast_expression(stmt->data.vardecl.initial_value, sub2, true);
            } else {
                printf("\n");
            }
            break;
        case AST_STMT_ASSIGN:
            printf("assign\n");
            print_ast_expression(&stmt->data.assignment.target, sub, false);
            print_ast_expression(&stmt->data.assignment.value, sub, true);
            break;
    }
}

static void print_ast_body(const AstBody *body, struct TreePrinter tp, bool last)
{
    struct TreePrinter sub = print_tree_prefix(tp, last);
    printf("body:\n");
    for (int i = 0; i < body->nstatements; i++)
        print_ast_statement(&body->statements[i], sub, (i == body->nstatements - 1));
}

void print_ast(const AstToplevelNode *topnodelist)
{
    printf("===== AST for file \"%s\" =====\n", topnodelist->location.filename);

    do {
        printf("line %d: ", topnodelist->location.lineno);

        switch(topnodelist->kind) {
            case AST_TOPLEVEL_DECLARE_FUNCTION:
                printf("Declare a function: ");
                print_ast_function_signature(&topnodelist->data.decl_signature);
                break;
            case AST_TOPLEVEL_DEFINE_FUNCTION:
                printf("Define a function: ");
                print_ast_function_signature(&topnodelist->data.funcdef.signature);
                print_ast_body(&topnodelist->data.funcdef.body, (struct TreePrinter){0}, true);
                break;
            case AST_TOPLEVEL_DEFINE_STRUCT:
                printf("Define struct \"%s\" with %d fields:\n",
                    topnodelist->data.structdef.name, topnodelist->data.structdef.nfields);
                for (int i = 0; i < topnodelist->data.structdef.nfields; i++) {
                    printf("  %s: ", topnodelist->data.structdef.fieldnames[i]);
                    print_ast_type(&topnodelist->data.structdef.fieldtypes[i]);
                    printf("\n");
                }
                break;
            case AST_TOPLEVEL_END_OF_FILE:
                printf("End of file.\n");
                break;
        }
        printf("\n");

    } while (topnodelist++->kind != AST_TOPLEVEL_END_OF_FILE);
}


static const char *varname(const CfVariable *var)
{
    if (var->name[0])
        return var->name;

    // Cycle through enough space for a few variables, so that you
    // can call this several times inside the same printf()
    static char names[50 + sizeof var->name][5];
    static unsigned i = 0;
    char *s = names[i++];
    i %= sizeof(names) / sizeof(names[0]);

    sprintf(s, "$%d", var->id);
    return s;
}

static void print_cf_instruction(const CfInstruction *ins, int indent)
{
    printf("%*sline %-4d  ", indent, "", ins->location.lineno);

    if (ins->destvar)
        printf("%s = ", varname(ins->destvar));

    switch(ins->kind) {
    case CF_ADDRESS_OF_VARIABLE:
        printf("address of %s", varname(ins->operands[0]));
        break;
    case CF_BOOL_NEGATE:
        printf("boolean negation of %s", varname(ins->operands[0]));
        break;
    case CF_CALL:
        printf("call %s(", ins->data.funcname);
        for (int i = 0; i < ins->noperands; i++) {
            if(i) printf(", ");
            printf("%s", varname(ins->operands[i]));
        }
        printf(")");
        break;
    case CF_INT_SCAST_TO_BIGGER:
        printf("cast %s to %d-bit signed int",
            varname(ins->operands[0]), ins->destvar->type.data.width_in_bits);
        break;
    case CF_INT_UCAST_TO_BIGGER:
        printf("cast %s to %d-bit unsigned int",
            varname(ins->operands[0]), ins->destvar->type.data.width_in_bits);
        break;
    case CF_CONSTANT:
        print_constant(&ins->data.constant);
        break;

    case CF_INT_ADD:
    case CF_INT_SUB:
    case CF_INT_MUL:
    case CF_INT_SDIV:
    case CF_INT_UDIV:
    case CF_INT_EQ:
    case CF_INT_LT:
    case CF_PTR_EQ:
        switch(ins->kind){
            case CF_INT_ADD: printf("iadd "); break;
            case CF_INT_SUB: printf("isub "); break;
            case CF_INT_MUL: printf("imul "); break;
            case CF_INT_SDIV: printf("sdiv "); break;
            case CF_INT_UDIV: printf("udiv "); break;
            case CF_INT_EQ: printf("ieq "); break;
            case CF_INT_LT: printf("ilt "); break;
            case CF_PTR_EQ: printf("ptreq "); break;
            default: assert(0);
        }
        printf("%s, %s", varname(ins->operands[0]), varname(ins->operands[1]));
        break;
    case CF_PTR_LOAD:
        // Extra parentheses to make these stand out a bit.
        printf("*(%s)", varname(ins->operands[0]));
        break;
    case CF_PTR_STORE:
        printf("*(%s) = %s", varname(ins->operands[0]), varname(ins->operands[1]));
        break;
    case CF_PTR_ADD_INT:
        printf("ptr %s + integer %s", varname(ins->operands[0]), varname(ins->operands[1]));
        break;
    case CF_PTR_STRUCT_FIELD:
        printf("%s + offset of field \"%s\"", varname(ins->operands[0]), ins->data.fieldname);
        break;
    case CF_PTR_CAST:
        printf("pointer cast %s", varname(ins->operands[0]));
        break;
    case CF_PTR_MEMSET_TO_ZERO:
        printf("set value of pointer %s to zero bytes", varname(ins->operands[0]));
        break;
    case CF_VARCPY:
        printf("%s", varname(ins->operands[0]));
        break;
    }

    printf("\n");
}

static void print_control_flow_graph_with_indent(const CfGraph *cfg, int indent)
{
    if (!cfg) {
        printf("%*sControl Flow Graph = NULL\n", indent, "");
        return;
    }

    printf("%*sVariables:\n", indent, "");
    for (CfVariable **var = cfg->variables.ptr; var < End(cfg->variables); var++) {
        printf("%*s  %-20s  %s\n", indent, "", varname(*var), (*var)->type.name);
    }

    for (CfBlock **b = cfg->all_blocks.ptr; b < End(cfg->all_blocks); b++) {
        printf("%*sBlock %d", indent, "", (int)(b - cfg->all_blocks.ptr));
        if (*b == &cfg->start_block)
            printf(" (start block)");
        if (*b == &cfg->end_block) {
            assert((*b)->instructions.len == 0);
            printf(" is the end block.\n");
            continue;
        }
        printf(":\n");
        for (CfInstruction *ins = (*b)->instructions.ptr; ins < End((*b)->instructions); ins++)
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
                    indent, "", varname((*b)->branchvar), trueidx, falseidx);
            }
        }
    }
}

void print_control_flow_graph(const CfGraph *cfg)
{
    print_control_flow_graph_with_indent(cfg, 0);
}

void print_control_flow_graphs(const CfGraphFile *cfgfile)
{
    printf("===== Control Flow Graphs for file \"%s\" =====\n", cfgfile->filename);
    for (int i = 0; i < cfgfile->nfuncs; i++) {
        char *sigstr = signature_to_string(&cfgfile->signatures[i], true);
        printf("Function %s\n", sigstr);
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
