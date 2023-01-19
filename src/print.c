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
        printf("integer %lld\n", token->data.int_value);
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
        printf("operator '%s'\n", token->data.operator);
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

// Returned sub-printer can be used to print lines that appear "inside"/"below" the given line.
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

static void print_ast_expression(const AstExpression *expr, struct TreePrinter tp)
{
    printf("[line %d] ", expr->location.lineno);
    int n = 0;  // number of operands

    switch(expr->kind) {
    case AST_EXPR_FUNCTION_CALL:
        printf("call function \"%s\"\n", expr->data.call.calledname);
        print_ast_call(&expr->data.call, tp);
        break;
    case AST_EXPR_BRACE_INIT:
        printf("brace init \"%s\"\n", expr->data.call.calledname);
        print_ast_call(&expr->data.call, tp);
        break;
    case AST_EXPR_DEREF_AND_GET_FIELD:
        printf("dereference and ");
        __attribute__((fallthrough));
    case AST_EXPR_GET_FIELD:
        printf("get field \"%s\"\n", expr->data.field.fieldname);
        print_ast_expression(expr->data.field.obj, print_tree_prefix(tp, true));
        break;
    case AST_EXPR_GET_VARIABLE:
        printf("get variable \"%s\"\n", expr->data.varname);
        break;
    case AST_EXPR_CONSTANT:
        print_constant(&expr->data.constant);
        printf("\n");
        break;
    case AST_EXPR_AS:
        printf("as ");
        print_ast_type(&expr->data.as.type);
        printf("\n");
        print_ast_expression(expr->data.as.obj, print_tree_prefix(tp, true));
        break;

    case AST_EXPR_ADDRESS_OF: puts("address of"); n=1; break;
    case AST_EXPR_DEREFERENCE: puts("dereference"); n=1; break;
    case AST_EXPR_NOT: puts("not"); n=1; break;
    case AST_EXPR_PRE_INCREMENT: puts("pre-increment"); n=1; break;
    case AST_EXPR_PRE_DECREMENT: puts("pre-decrement"); n=1; break;
    case AST_EXPR_POST_INCREMENT: puts("post-increment"); n=1; break;
    case AST_EXPR_POST_DECREMENT: puts("post-decrement"); n=1; break;

    case AST_EXPR_INDEXING: puts("indexing"); n=2; break;
    case AST_EXPR_EQ: puts("eq"); n=2; break;
    case AST_EXPR_NE: puts("ne"); n=2; break;
    case AST_EXPR_GT: puts("gt"); n=2; break;
    case AST_EXPR_GE: puts("ge"); n=2; break;
    case AST_EXPR_LT: puts("lt"); n=2; break;
    case AST_EXPR_LE: puts("le"); n=2; break;
    case AST_EXPR_ADD: puts("add"); n=2; break;
    case AST_EXPR_SUB: puts("sub"); n=2; break;
    case AST_EXPR_MUL: puts("mul"); n=2; break;
    case AST_EXPR_DIV: puts("div"); n=2; break;
    case AST_EXPR_AND: puts("and"); n=2; break;
    case AST_EXPR_OR: puts("or"); n=2; break;
    }

    for (int i = 0; i < n; i++)
        print_ast_expression(&expr->data.operands[i], print_tree_prefix(tp, i==n-1));
}

static void print_ast_call(const AstCall *call, struct TreePrinter tp)
{
    for (int i = 0; i < call->nargs; i++) {
        struct TreePrinter sub = print_tree_prefix(tp, i == call->nargs - 1);
        if (call->argnames)
            printf("argument \"%s\": ", call->argnames[i]);
        else
            printf("argument %d: ", i);
        print_ast_expression(&call->args[i], sub);
    }
}

static void print_ast_body(const AstBody *body, struct TreePrinter tp);

static void print_ast_statement(const AstStatement *stmt, struct TreePrinter tp)
{
    printf("[line %d] ", stmt->location.lineno);

    struct TreePrinter sub;

    switch(stmt->kind) {
        case AST_STMT_EXPRESSION_STATEMENT:
            printf("expression statement\n");
            print_ast_expression(&stmt->data.expression, print_tree_prefix(tp, true));
            break;
        case AST_STMT_RETURN_VALUE:
            printf("return a value\n");
            print_ast_expression(&stmt->data.expression, print_tree_prefix(tp, true));
            break;
        case AST_STMT_RETURN_WITHOUT_VALUE:
            printf("return without a value\n");
            break;
        case AST_STMT_IF:
            printf("if\n");
            for (int i = 0; i < stmt->data.ifstatement.n_if_and_elifs; i++) {
                sub = print_tree_prefix(tp, false);
                printf("condition: ");
                print_ast_expression(&stmt->data.ifstatement.if_and_elifs[i].condition, sub);

                bool is_last_row = (
                    i == stmt->data.ifstatement.n_if_and_elifs-1
                    && stmt->data.ifstatement.elsebody.nstatements == 0);
                sub = print_tree_prefix(tp, is_last_row);
                printf("body:\n");
                print_ast_body(&stmt->data.ifstatement.if_and_elifs[i].body, sub);
            }
            if (stmt->data.ifstatement.elsebody.nstatements > 0) {
                sub = print_tree_prefix(tp, true);
                printf("elsebody:\n");
                print_ast_body(&stmt->data.ifstatement.elsebody, sub);
            }
            break;
        case AST_STMT_WHILE:
            printf("while\n");
            sub = print_tree_prefix(tp, true);
            printf("condition: ");
            print_ast_expression(&stmt->data.whileloop.condition, sub);
            sub = print_tree_prefix(tp, true);
            printf("body:\n");
            print_ast_body(&stmt->data.whileloop.body, sub);
            break;
        case AST_STMT_FOR:
            printf("For loop\n");
            sub = print_tree_prefix(tp, false);
            printf("init: ");
            print_ast_statement(stmt->data.forloop.init, sub);
            sub = print_tree_prefix(tp, false);
            printf("cond: ");
            print_ast_expression(&stmt->data.forloop.cond, sub);
            sub = print_tree_prefix(tp, false);
            printf("incr: ");
            print_ast_statement(stmt->data.forloop.incr, sub);
            sub = print_tree_prefix(tp, true);
            printf("body:\n");
            print_ast_body(&stmt->data.forloop.body, sub);
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
                sub = print_tree_prefix(tp, true);
                printf("initial value:\n");
                print_ast_expression(stmt->data.vardecl.initial_value, print_tree_prefix(sub, true));
            } else {
                printf("\n");
            }
            break;
        case AST_STMT_ASSIGN:
            printf("assign\n");
            print_ast_expression(&stmt->data.assignment.target, print_tree_prefix(tp, false));
            print_ast_expression(&stmt->data.assignment.value, print_tree_prefix(tp, true));
            break;
    }
}

static void print_ast_body(const AstBody *body, struct TreePrinter tp)
{
    for (int i = 0; i < body->nstatements; i++)
        print_ast_statement(&body->statements[i], print_tree_prefix(tp, i == body->nstatements - 1));
}

void print_ast(const AstToplevelNode *topnodelist)
{
    printf("===== AST for file \"%s\" =====\n", topnodelist->location.filename);

    do {
        printf("line %d: ", topnodelist->location.lineno);

        switch(topnodelist->kind) {
            case AST_TOPLEVEL_IMPORT:
                printf("Import from \"%s\":", topnodelist->data.import.filename);
                for (int i = 0; i < topnodelist->data.import.nsymbols; i++)
                    printf(" %s", topnodelist->data.import.symbols[i]);
                printf("\n");
                break;
            case AST_TOPLEVEL_DECLARE_FUNCTION:
                printf("Declare a function: ");
                print_ast_function_signature(&topnodelist->data.decl_signature);
                break;
            case AST_TOPLEVEL_DEFINE_FUNCTION:
                printf("Define a function: ");
                print_ast_function_signature(&topnodelist->data.funcdef.signature);
                print_ast_body(&topnodelist->data.funcdef.body, (struct TreePrinter){0});
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


static const char *varname(const Variable *var)
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
    case CF_INT_CAST:
        assert(is_integer_type(ins->operands[0]->type));
        assert(is_integer_type(ins->destvar->type));
        printf("int cast %s (%d-bit %s --> %d-bit %s)", varname(ins->operands[0]),
            ins->operands[0]->type->data.width_in_bits,
            ins->operands[0]->type->kind==TYPE_SIGNED_INTEGER ? "signed" : "unsigned",
            ins->destvar->type->data.width_in_bits,
            ins->destvar->type->kind==TYPE_SIGNED_INTEGER ? "signed" : "unsigned");
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
    for (Variable **var = cfg->variables.ptr; var < End(cfg->variables); var++) {
        printf("%*s  %-20s  %s\n", indent, "", varname(*var), (*var)->type->name);
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


void print_llvm_ir(LLVMModuleRef module, bool is_optimized)
{
    size_t len;
    const char *filename = LLVMGetSourceFileName(module, &len);
    printf("===== %s LLVM IR for file \"%.*s\" =====\n",
        is_optimized ? "Optimized" : "Unoptimized", (int)len, filename);

    char *s = LLVMPrintModuleToString(module);
    puts(s);
    LLVMDisposeMessage(s);
}
