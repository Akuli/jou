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

static void print_string(const char *s, int len)
{
    putchar('"');
    for (int i = 0; i<len || (len==-1 && s[i]); i++) {
        if (isprint(s[i]))
            putchar(s[i]);
        else if (s[i] == '\n')
            printf("\\n");
        else
            printf("\\x%02x", (unsigned char)s[i]);     // TODO: \x is not yet recognized by the tokenizer
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
        print_string(c->data.str, -1);
        break;
    }
}

void print_token(const Token *token)
{
    switch(token->type) {
    case TOKEN_SHORT:
        printf("short %hd\n", (short)token->data.short_value);
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
        print_string(token->data.string_value, -1);
        printf("\n");
        break;
    case TOKEN_NAME:
        printf("name \"%s\"\n", token->data.name);
        break;
    case TOKEN_KEYWORD:
        printf("keyword \"%s\"\n", token->data.name);
        break;
    case TOKEN_DECORATOR:
        printf("decorator '%s'\n", token->data.name);
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
        if (!strcmp(ntv->name, "self") && ntv->type.kind == AST_TYPE_NAMED && ntv->type.data.name[0] == '\0') {
            // self with implicitly given type
            printf("self");
        } else {
            printf("%s: ", ntv->name);
            print_ast_type(&ntv->type);
        }
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
        printf("sizeof\n");
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
            printf("assert \"%s\"\n", stmt->data.assertion.condition_str);
            print_ast_expression(&stmt->data.assertion.condition, print_tree_prefix(tp, true));
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
            if (stmt->data.forloop.init) {
                sub = print_tree_prefix(tp, false);
                printf("init: ");
                print_ast_statement(stmt->data.forloop.init, sub);
            }
            if (stmt->data.forloop.cond) {
                sub = print_tree_prefix(tp, false);
                printf("cond: ");
                print_ast_expression(stmt->data.forloop.cond, sub);
            }
            if (stmt->data.forloop.incr) {
                sub = print_tree_prefix(tp, false);
                printf("incr: ");
                print_ast_statement(stmt->data.forloop.incr, sub);
            }
            sub = print_tree_prefix(tp, true);
            printf("body:\n");
            print_ast_body(&stmt->data.forloop.body, sub);
            break;
        case AST_STMT_MATCH:
            printf("match (printing not implemented)\n");
            // TODO: implement printing match statement, if needed for debugging
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
        case AST_STMT_DECLARE_GLOBAL_VAR:
            assert(!stmt->data.vardecl.value);
            printf("declare global var %s: ", stmt->data.vardecl.name);
            print_ast_type(&stmt->data.vardecl.type);
            printf("\n");
            break;
        case AST_STMT_DEFINE_GLOBAL_VAR:
            assert(!stmt->data.vardecl.value);
            printf("define global var %s: ", stmt->data.vardecl.name);
            print_ast_type(&stmt->data.vardecl.type);
            printf("\n");
            break;
        case AST_STMT_FUNCTION_DECLARE:
            printf("declare a function: ");
            print_ast_function_signature(&stmt->data.function.signature);
            break;
        case AST_STMT_FUNCTION_DEF:
            printf("define a function: ");
            print_ast_function_signature(&stmt->data.function.signature);
            print_ast_body(&stmt->data.function.body, tp);
            break;
        case AST_STMT_DEFINE_CLASS:
            printf(
                "define a class \"%s\" with %d members\n",
                stmt->data.classdef.name, stmt->data.classdef.members.len);
            for (AstClassMember *m = stmt->data.classdef.members.ptr; m < End(stmt->data.classdef.members); m++){
                sub = print_tree_prefix(tp, m == End(stmt->data.classdef.members)-1);
                switch(m->kind) {
                case AST_CLASSMEMBER_FIELD:
                    printf("field %s: ", m->data.field.name);
                    print_ast_type(&m->data.field.type);
                    printf("\n");
                    break;
                case AST_CLASSMEMBER_UNION:
                    printf("union:\n");
                    for (AstNameTypeValue *ntv = m->data.unionfields.ptr; ntv < End(m->data.unionfields); ntv++) {
                        print_tree_prefix(sub, ntv == End(m->data.unionfields)-1);
                        printf("%s: ", ntv->name);
                        print_ast_type(&ntv->type);
                        printf("\n");
                    }
                    break;
                case AST_CLASSMEMBER_METHOD:
                    printf("method ");
                    print_ast_function_signature(&m->data.method.signature);
                    print_ast_body(&m->data.method.body, sub);
                    break;
                }
            }
            break;
        case AST_STMT_DEFINE_ENUM:
            printf("define enum \"%s\" with %d members\n",
                stmt->data.enumdef.name, stmt->data.enumdef.nmembers);
            for (int i = 0; i < stmt->data.enumdef.nmembers; i++) {
                print_tree_prefix(tp, i==stmt->data.enumdef.nmembers-1);
                puts(stmt->data.enumdef.membernames[i]);
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

void print_ast(const AstFile *ast)
{
    printf("===== AST for file \"%s\" =====\n", ast->path);

    for (const AstImport *imp = ast->imports.ptr; imp < End(ast->imports); imp++) {
        printf(
            "line %d: Import \"%s\", which resolves to \"%s\".\n",
            imp->location.lineno,
            imp->specified_path,
            imp->resolved_path
        );
    }

    for (int i = 0; i < ast->body.nstatements; i++)
        print_ast_statement(&ast->body.statements[i], (struct TreePrinter){0});
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
