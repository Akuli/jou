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
    case CONSTANT_ENUM_MEMBER:
        printf("enum member %d of %s", c->data.enum_member.memberidx, c->data.enum_member.enumtype->name);
        break;
    case CONSTANT_BOOL:
        printf(c->data.boolean ? "True" : "False");
        break;
    case CONSTANT_FLOAT:
        printf("float %s", c->data.double_or_float_text);
        break;
    case CONSTANT_DOUBLE:
        printf("double %s", c->data.double_or_float_text);
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
    case TOKEN_SHORT:
        printf("shorter %hd\n", (short)token->data.short_value);
        break;
    case TOKEN_INT:
        printf("integer %d\n", (int)token->data.int_value);
        break;
    case TOKEN_LONG:
        printf("long %lld\n", (long long)token->data.long_value);
        break;
    case TOKEN_FLOAT:
        printf("float %s\n", token->data.name);
        break;
    case TOKEN_DOUBLE:
        printf("double %s\n", token->data.name);
        break;
    case TOKEN_CHAR:
        printf("byte ");
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
        printf("indent (+4 spaces)\n");
        break;
    case TOKEN_DEDENT:
        printf("dedent (-4 spaces)\n");
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

static void print_ast_type_without_line_number(const struct AstType *t)
{
    switch(t->kind) {
    case AST_TYPE_NAMED:
        printf("%s", t->data.name);
        break;
    case AST_TYPE_ARRAY:
        print_ast_type_without_line_number(t->data.array.membertype);
        // TODO: improve this?
        // The challenge is that expressions are currently printed on multiple lines,
        // and types on a single line.
        printf("[<size>]");
        break;
    case AST_TYPE_POINTER:
        print_ast_type_without_line_number(t->data.valuetype);
        printf("*");
        break;
    }
}

static void print_ast_type(const struct AstType *t)
{
    print_ast_type_without_line_number(t);
    printf(" [line %d]", t->location.lineno);
}

static void print_ast_function_signature(const AstSignature *sig)
{
    printf("%s(", sig->name);
    for (const AstNameTypeValue *ntv = sig->args.ptr; ntv < End(sig->args); ntv++) {
        if (ntv > sig->args.ptr) printf(", ");
        printf("%s: ", ntv->name);
        print_ast_type(&ntv->type);
        assert(!ntv->value);
    }
    if (sig->takes_varargs) {
        if (sig->args.len != 0)
            printf(", ");
        printf("...");
    }
    printf(") -> ");
    print_ast_type(&sig->returntype);
    printf("\n");
}

static void print_ast_call(const AstCall *call, struct TreePrinter tp, const AstExpression *self);

static void print_ast_expression(const AstExpression *expr, struct TreePrinter tp)
{
    printf("[line %d] ", expr->location.lineno);
    int n = 0;  // number of operands

    switch(expr->kind) {
    case AST_EXPR_FUNCTION_CALL:
        printf("call function \"%s\"\n", expr->data.call.calledname);
        print_ast_call(&expr->data.call, tp, NULL);
        break;
    case AST_EXPR_BRACE_INIT:
        printf("instantiate \"%s\"\n", expr->data.call.calledname);
        print_ast_call(&expr->data.call, tp, NULL);
        break;
    case AST_EXPR_ARRAY:
        printf("array\n");
        for (int i = 0; i < expr->data.array.count; i++)
            print_ast_expression(&expr->data.array.items[i], print_tree_prefix(tp, i==expr->data.array.count-1));
        break;
    case AST_EXPR_DEREF_AND_GET_FIELD:
        printf("dereference and ");
        __attribute__((fallthrough));
    case AST_EXPR_GET_FIELD:
        printf("get class field \"%s\"\n", expr->data.classfield.fieldname);
        print_ast_expression(expr->data.classfield.obj, print_tree_prefix(tp, true));
        break;
    case AST_EXPR_DEREF_AND_CALL_METHOD:
        printf("dereference and ");
        __attribute__((fallthrough));
    case AST_EXPR_CALL_METHOD:
        printf("call method \"%s\"\n", expr->data.methodcall.call.calledname);
        print_ast_call(&expr->data.methodcall.call, tp, expr->data.methodcall.obj);
        break;
    case AST_EXPR_GET_ENUM_MEMBER:
        printf("get member \"%s\" from enum \"%s\"\n",
            expr->data.enummember.membername, expr->data.enummember.enumname);
        break;
    case AST_EXPR_SIZEOF:
        printf("sizeof expression\n");
        print_ast_expression(expr->data.classfield.obj, print_tree_prefix(tp, true));
        break;
    case AST_EXPR_GET_VARIABLE:
        if (!strcmp(expr->data.varname, "self"))
            printf("self\n");
        else
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
    case AST_EXPR_NEG: puts("negate"); n=1; break;
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
    case AST_EXPR_MOD: puts("mod"); n=2; break;
    case AST_EXPR_AND: puts("and"); n=2; break;
    case AST_EXPR_OR: puts("or"); n=2; break;
    }

    for (int i = 0; i < n; i++)
        print_ast_expression(&expr->data.operands[i], print_tree_prefix(tp, i==n-1));
}

static void print_ast_call(const AstCall *call, struct TreePrinter tp, const AstExpression *self)
{
    if(self){
        struct TreePrinter self_printer = print_tree_prefix(tp, call->nargs == 0);
        printf("self: ");
        print_ast_expression(self, self_printer);
    }

    for (int i = 0; i < call->nargs; i++) {
        struct TreePrinter sub = print_tree_prefix(tp, i == call->nargs - 1);
        if (call->argnames)
            printf("field \"%s\": ", call->argnames[i]);
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
        case AST_STMT_ASSERT:
            printf("assert\n");
            print_ast_expression(&stmt->data.expression, print_tree_prefix(tp, true));
            break;
        case AST_STMT_RETURN:
            printf("return\n");
            if (stmt->data.returnvalue)
                print_ast_expression(stmt->data.returnvalue, print_tree_prefix(tp, true));
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
                printf("else body:\n");
                print_ast_body(&stmt->data.ifstatement.elsebody, sub);
            }
            break;
        case AST_STMT_WHILE:
            printf("while loop\n");
            sub = print_tree_prefix(tp, false);
            printf("condition: ");
            print_ast_expression(&stmt->data.whileloop.condition, sub);
            sub = print_tree_prefix(tp, true);
            printf("body:\n");
            print_ast_body(&stmt->data.whileloop.body, sub);
            break;
        case AST_STMT_FOR:
            printf("for loop\n");
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
        case AST_STMT_PASS:
            printf("pass\n");
            break;
        case AST_STMT_DECLARE_LOCAL_VAR:
            printf("declare local var %s: ", stmt->data.vardecl.name);
            print_ast_type(&stmt->data.vardecl.type);
            printf("\n");
            if (stmt->data.vardecl.value) {
                sub = print_tree_prefix(tp, true);
                printf("initial value: ");
                print_ast_expression(stmt->data.vardecl.value, sub);
            }
            break;
#define PrintAssignment \
    print_ast_expression(&stmt->data.assignment.target, print_tree_prefix(tp, false)); \
    print_ast_expression(&stmt->data.assignment.value, print_tree_prefix(tp, true));
        case AST_STMT_ASSIGN: puts("assign"); PrintAssignment; break;
        case AST_STMT_INPLACE_ADD: puts("in-place add"); PrintAssignment; break;
        case AST_STMT_INPLACE_SUB: puts("in-place sub"); PrintAssignment; break;
        case AST_STMT_INPLACE_MUL: puts("in-place mul"); PrintAssignment; break;
        case AST_STMT_INPLACE_DIV: puts("in-place div"); PrintAssignment; break;
        case AST_STMT_INPLACE_MOD: puts("in-place mod"); PrintAssignment; break;
#undef PrintAssignment
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

    for (const AstToplevelNode *t = topnodelist; t->kind != AST_TOPLEVEL_END_OF_FILE; t++) {
        printf("line %d: ", t->location.lineno);

        switch(t->kind) {
            case AST_TOPLEVEL_IMPORT:
                printf("Import \"%s\", which resolves to \"%s\".\n",
                    t->data.import.specified_path, t->data.import.resolved_path);
                break;
            case AST_TOPLEVEL_DECLARE_GLOBAL_VARIABLE:
                assert(!t->data.globalvar.value);
                printf("Declare a global variable %s: ", t->data.globalvar.name);
                print_ast_type(&t->data.globalvar.type);
                printf("\n");
                break;
            case AST_TOPLEVEL_DEFINE_GLOBAL_VARIABLE:
                assert(!t->data.globalvar.value);
                printf("Define a global variable %s: ", t->data.globalvar.name);
                print_ast_type(&t->data.globalvar.type);
                printf("\n");
                break;
            case AST_TOPLEVEL_FUNCTION:
                printf("%s a function: ", t->data.function.body.nstatements == 0 ? "Declare" : "Define");
                print_ast_function_signature(&t->data.function.signature);
                print_ast_body(&t->data.function.body, (struct TreePrinter){0});
                break;
            case AST_TOPLEVEL_DEFINE_CLASS:
                printf(
                    "Define a class \"%s\" with %d members:\n",
                    t->data.classdef.name, t->data.classdef.members.len);
                for (AstClassMember *m = t->data.classdef.members.ptr; m < End(t->data.classdef.members); m++){
                    switch(m->kind) {
                    case AST_CLASSMEMBER_FIELD:
                        printf("  field %s: ", m->data.field.name);
                        print_ast_type(&m->data.field.type);
                        printf("\n");
                        break;
                    case AST_CLASSMEMBER_UNION:
                        printf("  union:\n");
                        for (AstNameTypeValue *ntv = m->data.unionfields.ptr; ntv < End(m->data.unionfields); ntv++) {
                            printf("    %s: ", ntv->name);
                            print_ast_type(&ntv->type);
                            printf("\n");
                        }
                        break;
                    case AST_CLASSMEMBER_METHOD:
                        printf("  method ");
                        print_ast_function_signature(&m->data.method.signature);
                        print_ast_body(&m->data.method.body, (struct TreePrinter){.prefix="  "});
                        break;
                    }
                }
                break;
            case AST_TOPLEVEL_DEFINE_ENUM:
                printf("Define enum \"%s\" with %d members:\n",
                    t->data.enumdef.name, t->data.enumdef.nmembers);
                for (int i = 0; i < t->data.enumdef.nmembers; i++)
                    printf("  %s\n", t->data.enumdef.membernames[i]);
                break;
            case AST_TOPLEVEL_END_OF_FILE:
                assert(0);
        }
        printf("\n");
    }
}


static const char *varname(const LocalVariable *var)
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

static const char *very_short_number_type_description(const Type *t)
{
    switch(t->kind) {
        case TYPE_FLOATING_POINT: return "floating";
        case TYPE_SIGNED_INTEGER: return "signed";
        case TYPE_UNSIGNED_INTEGER: return "unsigned";
        default: assert(0);
    }
}

static void print_cf_instruction(const CfInstruction *ins)
{
    printf("    line %-4d  ", ins->location.lineno);

    if (ins->destvar)
        printf("%s = ", varname(ins->destvar));

    switch(ins->kind) {
    case CF_ADDRESS_OF_LOCAL_VAR:
        printf("address of %s (local variable)", varname(ins->operands[0]));
        break;
    case CF_ADDRESS_OF_GLOBAL_VAR:
        printf("address of %s (global variable)", ins->data.globalname);
        break;
    case CF_SIZEOF:
        printf("sizeof %s", ins->data.type->name);
        break;
    case CF_BOOL_NEGATE:
        printf("boolean negation of %s", varname(ins->operands[0]));
        break;
    case CF_CALL:
        if (get_self_class(&ins->data.signature))
            printf("call method %s.", get_self_class(&ins->data.signature)->name);
        else
            printf("call function ");
        printf("%s(", ins->data.signature.name);
        for (int i = 0; i < ins->noperands; i++) {
            if(i) printf(", ");
            printf("%s", varname(ins->operands[i]));
        }
        printf(")");
        break;
    case CF_NUM_CAST:
        printf(
            "number cast %s (%d-bit %s --> %d-bit %s)",
            varname(ins->operands[0]),
            ins->operands[0]->type->data.width_in_bits,
            very_short_number_type_description(ins->operands[0]->type),
            ins->destvar->type->data.width_in_bits,
            very_short_number_type_description(ins->destvar->type));
        break;
    case CF_ENUM_TO_INT32:
        printf("cast %s from enum to 32-bit signed int", varname(ins->operands[0]));
        break;
    case CF_INT32_TO_ENUM:
        printf("cast %s from 32-bit signed int to enum", varname(ins->operands[0]));
        break;
    case CF_CONSTANT:
        print_constant(&ins->data.constant);
        break;

    case CF_NUM_ADD:
    case CF_NUM_SUB:
    case CF_NUM_MUL:
    case CF_NUM_DIV:
    case CF_NUM_MOD:
    case CF_NUM_EQ:
    case CF_NUM_LT:
    case CF_PTR_EQ:
        switch(ins->kind){
            case CF_NUM_ADD: printf("num add "); break;
            case CF_NUM_SUB: printf("num sub "); break;
            case CF_NUM_MUL: printf("num mul "); break;
            case CF_NUM_DIV: printf("num div "); break;
            case CF_NUM_MOD: printf("num mod "); break;
            case CF_NUM_EQ: printf("num eq "); break;
            case CF_NUM_LT: printf("num lt "); break;
            case CF_PTR_EQ: printf("ptr eq "); break;
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
    case CF_PTR_CLASS_FIELD:
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

void print_control_flow_graph(const CfGraph *cfg)
{
    char *sigstr = signature_to_string(&cfg->signature, true);
    printf("Function %s\n", sigstr);
    free(sigstr);

    printf("  Variables:\n");
    for (LocalVariable **var = cfg->locals.ptr; var < End(cfg->locals); var++) {
        printf("    %-20s  %s\n", varname(*var), (*var)->type->name);
    }

    for (CfBlock **b = cfg->all_blocks.ptr; b < End(cfg->all_blocks); b++) {
        printf("  Block %d", (int)(b - cfg->all_blocks.ptr));
        if (*b == &cfg->start_block)
            printf(" (start block)");
        if (*b == &cfg->end_block) {
            assert((*b)->instructions.len == 0);
            printf(" is the end block.\n");
            continue;
        }
        printf(":\n");
        for (CfInstruction *ins = (*b)->instructions.ptr; ins < End((*b)->instructions); ins++)
            print_cf_instruction(ins);

        if (*b == &cfg->end_block) {
            assert((*b)->iftrue == NULL);
            assert((*b)->iffalse == NULL);
        } else if ((*b)->iftrue == NULL && (*b)->iffalse == NULL) {
            printf("    Execution stops here. We have called a noreturn function.\n");
        } else {
            int trueidx=-1, falseidx=-1;
            for (int i = 0; i < cfg->all_blocks.len; i++) {
                if (cfg->all_blocks.ptr[i]==(*b)->iftrue) trueidx=i;
                if (cfg->all_blocks.ptr[i]==(*b)->iffalse) falseidx=i;
            }
            assert(trueidx!=-1);
            assert(falseidx!=-1);
            if (trueidx==falseidx)
                printf("    Jump to block %d.\n", trueidx);
            else {
                assert((*b)->branchvar);
                printf("    If %s is True jump to block %d, otherwise block %d.\n",
                    varname((*b)->branchvar), trueidx, falseidx);
            }
        }
    }

    printf("\n");
}

void print_control_flow_graphs(const CfGraphFile *cfgfile)
{
    printf("===== Control Flow Graphs for file \"%s\" =====\n", cfgfile->filename);
    for (CfGraph **cfg = cfgfile->graphs.ptr; cfg < End(cfgfile->graphs); cfg++)
        print_control_flow_graph(*cfg);
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
