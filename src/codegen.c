#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include "jou_compiler.h"
#include "util.h"

static LLVMTypeRef build_type(const Type *type)
{
    switch(type->kind) {
    case TYPE_ARRAY:
        return LLVMArrayType(build_type(type->data.array.membertype), type->data.array.len);
    case TYPE_POINTER:
        return LLVMPointerType(build_type(type->data.valuetype), 0);
    case TYPE_FLOATING_POINT:
        switch(type->data.width_in_bits) {
            case 32: return LLVMFloatType();
            case 64: return LLVMDoubleType();
            default: assert(0);
        }
    case TYPE_VOID_POINTER:
        // just use i8* as here https://stackoverflow.com/q/36724399
        return LLVMPointerType(LLVMInt8Type(), 0);
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
        return LLVMIntType(type->data.width_in_bits);
    case TYPE_BOOL:
        return LLVMInt1Type();
    case TYPE_OPAQUE_CLASS:
        assert(0);
    case TYPE_CLASS:
        {
            int n = type->data.classdata.fields.len;
            LLVMTypeRef *elems = malloc(sizeof(elems[0]) * n);  // NOLINT
            for (int i = 0; i < n; i++) {
                // Treat all pointers inside structs as if they were void*.
                // This allows structs to contain pointers to themselves.
                if (type->data.classdata.fields.ptr[i].type->kind == TYPE_POINTER)
                    elems[i] = build_type(voidPtrType);
                else
                    elems[i] = build_type(type->data.classdata.fields.ptr[i].type);
            }
            LLVMTypeRef result = LLVMStructType(elems, n, false);
            free(elems);
            return result;
        }
    case TYPE_ENUM:
        return LLVMInt32Type();
    }
    assert(0);
}

struct Variable {
    char name[100];
    LLVMValueRef ptr;
};

struct State {
    const FileTypes *filetypes;
    const FunctionOrMethodTypes *fomtypes;
    LLVMValueRef llvmfunc;
    List(LLVMBasicBlockRef) breakstack, continuestack;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    // All local variables are represented as pointers to stack space, even
    // if they are never reassigned. LLVM will optimize the mess.
    List(struct Variable) locals, globals;
};

static const ExpressionTypes *get_expr_types(const struct State *st, const AstExpression *expr)
{
    for (ExpressionTypes **et = st->fomtypes->expr_types.ptr; et < End(st->fomtypes->expr_types); et++)
        if ((*et)->expr == expr)
            return *et;
    return NULL;
}

const Type *get_type_after_cast(const struct State *st, const AstExpression *expr)
{
    // TODO: type vs type_after_cast is unclear. The "type" field should be named type_before_cast
    const ExpressionTypes *et = get_expr_types(st, expr);
    assert(et);
    assert(et->type);
    if (et->type_after_cast)
        return et->type_after_cast;
    return et->type;
}

static struct Variable *find_local_var(const struct State *st, const char *name)
{
    for (struct Variable *v = st->locals.ptr; v < End(st->locals); v++)
        if (!strcmp(v->name, name))
            return v;

    printf("Fatal Codegen Error 404: variable not found: %s\n", name);
    LLVMDumpModule(st->module);
    assert(0);
}

static LLVMValueRef find_var(const struct State *st, const char *name)
{
    LLVMValueRef varptr = LLVMGetNamedGlobal(st->module, name);
    if (varptr)
        return varptr;
    return find_local_var(st, name)->ptr;
}

static LLVMValueRef build_function_or_method_decl(const struct State *st, const Signature *sig)
{
    char fullname[200];
    if (get_self_class(sig))
        snprintf(fullname, sizeof fullname, "%s.%s", get_self_class(sig)->name, sig->name);
    else
        safe_strcpy(fullname, sig->name);

    // Make it so that this can be called many times without issue
    LLVMValueRef func = LLVMGetNamedFunction(st->module, fullname);
    if (func)
        return func;

    LLVMTypeRef *argtypes = malloc(sig->nargs * sizeof(argtypes[0]));  // NOLINT
    for (int i = 0; i < sig->nargs; i++)
        argtypes[i] = build_type(sig->argtypes[i]);

    LLVMTypeRef returntype;
    if (sig->returntype == NULL)
        returntype = LLVMVoidType();
    else
        returntype = build_type(sig->returntype);

    LLVMTypeRef functype = LLVMFunctionType(returntype, argtypes, sig->nargs, sig->takes_varargs);
    free(argtypes);

    func = LLVMAddFunction(st->module, fullname, functype);

    // Terrible hack: if declaring an OS function that doesn't exist on current platform,
    // make it a definition instead of a declaration so that there are no linker errors.
    // Ideally it would be possible to compile some parts of Jou code only for a specific platform.
#ifdef _WIN32
    const char *doesnt_exist[] = { "readlink", "mkdir" };
#else
    const char *doesnt_exist[] = { "GetModuleFileNameA", "_mkdir" };
#endif
    for (unsigned i = 0; i < sizeof doesnt_exist / sizeof doesnt_exist[0]; i++) {
        if (!strcmp(fullname, doesnt_exist[i])) {
            LLVMBasicBlockRef block = LLVMAppendBasicBlock(func, "my_block");
            LLVMBuilderRef b = LLVMCreateBuilder();
            LLVMPositionBuilderAtEnd(b, block);
            LLVMBuildUnreachable(b);
            LLVMDisposeBuilder(b);
            break;
        }
    }

    return func;
}

static LLVMValueRef build_expression(const struct State *st, const AstExpression *expr);
static void build_statement(struct State *st, const AstStatement *stmt);
static void build_body(struct State *st, const AstBody *body);

static LLVMValueRef build_signed_mod(LLVMBuilderRef builder, LLVMValueRef lhs, LLVMValueRef rhs)
{
    // Jou's % operator ensures that a%b has same sign as b:
    // jou_mod(a, b) = llvm_mod(llvm_mod(a, b) + b, b)
    LLVMValueRef llmod = LLVMBuildSRem(builder, lhs, rhs, "smod_tmp");
    LLVMValueRef sum = LLVMBuildAdd(builder, llmod, rhs, "smod_tmp");
    return LLVMBuildSRem(builder, sum, rhs, "smod");
}

static LLVMValueRef build_signed_div(LLVMBuilderRef builder, LLVMValueRef lhs, LLVMValueRef rhs)
{
    /*
    LLVM's provides two divisions. One truncates, the other is an "exact div"
    that requires there is no remainder. Jou uses floor division which is
    neither of the two, but is quite easy to implement:

        floordiv(a, b) = exact_div(a - jou_mod(a, b), b)
    */
    LLVMValueRef top = LLVMBuildSub(builder, lhs, build_signed_mod(builder, lhs, rhs), "sdiv_tmp");
    return LLVMBuildExactSDiv(builder, top, rhs, "sdiv");
}

static LLVMValueRef build_binop(
    const struct State *st,
    const enum AstExpressionKind op,
    LLVMValueRef lhs,
    const Type *lhstype,
    LLVMValueRef rhs,
    const Type *rhstype)
{
    bool got_numbers = is_number_type(lhstype) && is_number_type(rhstype);
    bool got_pointers = is_pointer_type(lhstype) && is_pointer_type(rhstype);
    assert(got_numbers || got_pointers);

    if (lhstype->kind == TYPE_FLOATING_POINT && rhstype->kind == TYPE_FLOATING_POINT) {
        switch(op) {
            case AST_EXPR_ADD: return LLVMBuildFAdd(st->builder, lhs, rhs, "add"); break;
            case AST_EXPR_SUB: return LLVMBuildFSub(st->builder, lhs, rhs, "sub"); break;
            case AST_EXPR_MUL: return LLVMBuildFMul(st->builder, lhs, rhs, "mul"); break;
            case AST_EXPR_DIV: return LLVMBuildFDiv(st->builder, lhs, rhs, "div"); break;
            case AST_EXPR_MOD: return LLVMBuildFRem(st->builder, lhs, rhs, "mod"); break;
            case AST_EXPR_EQ: return LLVMBuildFCmp(st->builder, LLVMRealOEQ, lhs, rhs, "eq"); break;
            case AST_EXPR_NE: return LLVMBuildFCmp(st->builder, LLVMRealONE, lhs, rhs, "ne"); break;
            case AST_EXPR_GT: return LLVMBuildFCmp(st->builder, LLVMRealOGT, lhs, rhs, "gt"); break;
            case AST_EXPR_GE: return LLVMBuildFCmp(st->builder, LLVMRealOGE, lhs, rhs, "ge"); break;
            case AST_EXPR_LT: return LLVMBuildFCmp(st->builder, LLVMRealOLT, lhs, rhs, "lt"); break;
            case AST_EXPR_LE: return LLVMBuildFCmp(st->builder, LLVMRealOLE, lhs, rhs, "le"); break;
            default: assert(0);
        }
    } else if (is_integer_type(lhstype) && is_integer_type(rhstype)) {
        bool is_signed = lhstype->kind == TYPE_SIGNED_INTEGER && rhstype->kind == TYPE_SIGNED_INTEGER;
        switch(op) {
            case AST_EXPR_ADD: return LLVMBuildAdd(st->builder, lhs, rhs, "add"); break;
            case AST_EXPR_SUB: return LLVMBuildSub(st->builder, lhs, rhs, "sub"); break;
            case AST_EXPR_MUL: return LLVMBuildMul(st->builder, lhs, rhs, "mul"); break;
            case AST_EXPR_DIV: return is_signed ? build_signed_div(st->builder, lhs, rhs) : LLVMBuildUDiv(st->builder, lhs, rhs, "div"); break;
            case AST_EXPR_MOD: return is_signed ? build_signed_mod(st->builder, lhs, rhs) : LLVMBuildURem(st->builder, lhs, rhs, "mod"); break;
            case AST_EXPR_EQ: return LLVMBuildICmp(st->builder, LLVMIntEQ, lhs, rhs, "eq"); break;
            case AST_EXPR_NE: return LLVMBuildICmp(st->builder, LLVMIntNE, lhs, rhs, "ne"); break;
            case AST_EXPR_GT: return LLVMBuildICmp(st->builder, is_signed ? LLVMIntSGT : LLVMIntUGT, lhs, rhs, "gt"); break;
            case AST_EXPR_GE: return LLVMBuildICmp(st->builder, is_signed ? LLVMIntSGE : LLVMIntUGE, lhs, rhs, "ge"); break;
            case AST_EXPR_LT: return LLVMBuildICmp(st->builder, is_signed ? LLVMIntSLT : LLVMIntULT, lhs, rhs, "lt"); break;
            case AST_EXPR_LE: return LLVMBuildICmp(st->builder, is_signed ? LLVMIntSLE : LLVMIntULE, lhs, rhs, "le"); break;
            default: printf("%d\n", op); assert(0);
        }
    } else {
        assert(0);
    }
}

static LLVMValueRef build_cast(const struct State *st, LLVMValueRef obj, const Type *from, const Type *to)
{
    // bool --> int
    if (from->kind == TYPE_BOOL && is_integer_type(to))
        return LLVMBuildZExt(st->builder, obj, build_type(to), "cast");

    // pointer --> pointer
    if (is_pointer_type(from) && is_pointer_type(to))
        return LLVMBuildBitCast(st->builder, obj, build_type(to), "cast");

    // integer --> integer
    if (is_integer_type(from) && is_integer_type(to)) {
        if (from->data.width_in_bits < to->data.width_in_bits) {
            if (from->kind == TYPE_SIGNED_INTEGER) {
                // example: signed 8-bit 0xFF --> 16-bit 0xFFFF
                return LLVMBuildSExt(st->builder, obj, build_type(to), "cast");
            } else {
                // example: unsigned 8-bit 0xFF --> 16-bit 0x00FF
                return LLVMBuildZExt(st->builder, obj, build_type(to), "cast");
            }
        }
        if (from->data.width_in_bits > to->data.width_in_bits)
            return LLVMBuildTrunc(st->builder, obj, build_type(to), "cast");
        // same size, LLVM doesn't distinguish signed and unsigned integer types
        return obj;
    }

    // integer --> double / float
    if (is_integer_type(from) && to->kind == TYPE_FLOATING_POINT) {
        if (from->kind == TYPE_SIGNED_INTEGER)
            return LLVMBuildSIToFP(st->builder, obj, build_type(to), "cast");
        else
            return LLVMBuildUIToFP(st->builder, obj, build_type(to), "cast");
    }

    // double / float --> integer
    if (from->kind == TYPE_FLOATING_POINT && to->kind == TYPE_SIGNED_INTEGER)
        return LLVMBuildFPToSI(st->builder, obj, build_type(to), "cast");
    if (from->kind == TYPE_FLOATING_POINT && to->kind == TYPE_UNSIGNED_INTEGER)
        return LLVMBuildFPToUI(st->builder, obj, build_type(to), "cast");

    // floats of different sizes
    if (from->kind == TYPE_FLOATING_POINT && to->kind == TYPE_FLOATING_POINT)
        return LLVMBuildFPCast(st->builder, obj, build_type(to), "cast");

    printf("Don't know how cast %s --> %s\n", from->name, to->name);
    assert(0);
}

static LLVMValueRef build_class_field_pointer(
    const struct State *st, const Type *classtype, LLVMValueRef instanceptr, const char *fieldname)
{
    int i=0;
    for (struct ClassField *f = classtype->data.classdata.fields.ptr; f < End(classtype->data.classdata.fields); f++,i++) {
        if (!strcmp(f->name, fieldname)) {
            LLVMValueRef fieldptr = LLVMBuildStructGEP2(st->builder, build_type(classtype), instanceptr, i, f->name);
            if (f->type->kind == TYPE_POINTER) {
                // We lied to LLVM that the struct member is i8*, so that we can do self-referencing types
                fieldptr = LLVMBuildBitCast(st->builder, fieldptr, LLVMPointerType(build_type(f->type),0), "struct_member_i8_hack");
            }
            return fieldptr;
        }
    }

    assert(0);
}

static LLVMValueRef build_address_of_expression(const struct State *st, const AstExpression *expr)
{
    switch(expr->kind) {
    case AST_EXPR_GET_VARIABLE:
        return find_var(st, expr->data.varname);
    case AST_EXPR_INDEXING:
        {
            LLVMValueRef ptr = build_expression(st, &expr->data.operands[0]);
            LLVMValueRef idx = build_expression(st, &expr->data.operands[1]);
            if (get_type_after_cast(st, &expr->data.operands[1])->kind == TYPE_UNSIGNED_INTEGER) {
                // https://github.com/Akuli/jou/issues/48
                // Apparently the default is to interpret indexes as signed.
                idx = LLVMBuildZExt(st->builder, idx, LLVMInt64Type(), "indexcast");
            }
            return LLVMBuildGEP(st->builder, ptr, &idx, 1, "indexed");
        }
    case AST_EXPR_DEREF_AND_GET_FIELD:
        // &foo->bar
        {
            const Type *t = get_type_after_cast(st, expr->data.classfield.obj);
            assert(t->kind == TYPE_POINTER);
            assert(t->data.valuetype->kind == TYPE_CLASS);
            LLVMValueRef objptr = build_expression(st, expr->data.classfield.obj);
            return build_class_field_pointer(st, t->data.valuetype, objptr, expr->data.classfield.fieldname);
        }
    default:
        printf("%d\n", expr->kind);
        assert(0);
    }
}

static LLVMValueRef build_call(const struct State *st, LLVMValueRef self, const Type *selftype, const AstCall *call)
{
    assert((self && selftype) || (!self && !selftype));

    const Signature *sig = NULL;
    if(self) {
        assert(selftype->kind == TYPE_POINTER);
        const Type *selfclass = selftype->data.valuetype;
        assert(selfclass->kind == TYPE_CLASS);
        for (const Signature *s = selfclass->data.classdata.methods.ptr; s < End(selfclass->data.classdata.methods); s++) {
            assert(get_self_class(s) == selfclass);
            if (!strcmp(s->name, call->calledname)) {
                sig = s;
                break;
            }
        }
    } else {
        for (const struct SignatureAndUsedPtr *f = st->filetypes->functions.ptr; f < End(st->filetypes->functions); f++) {
            if (!strcmp(f->signature.name, call->calledname)) {
                sig = &f->signature;
                break;
            }
        }
    }
    assert(sig);
    LLVMValueRef function = build_function_or_method_decl(st, sig);

    LLVMValueRef *args = malloc(call->nargs * sizeof args[0]);  // NOLINT
    for (int i = 0; i < call->nargs; i++)
        args[i] = build_expression(st, &call->args[i]);

    assert(function);
    assert(LLVMGetTypeKind(LLVMTypeOf(function)) == LLVMPointerTypeKind);
    LLVMTypeRef function_type = LLVMGetElementType(LLVMTypeOf(function));
    assert(LLVMGetTypeKind(function_type) == LLVMFunctionTypeKind);

    char debug_name[100] = {0};
    if (sig->returntype)
        snprintf(debug_name, sizeof debug_name, "%s_return_value", sig->name);

    LLVMValueRef result = LLVMBuildCall2(st->builder, function_type, function, args, call->nargs, debug_name);
    free(args);
    return sig->returntype ? result : NULL;
}

static LLVMValueRef make_a_string_constant(const struct State *st, const char *s)
{
    LLVMValueRef array = LLVMConstString(s, strlen(s), false);
    LLVMValueRef global_var = LLVMAddGlobal(st->module, LLVMTypeOf(array), "string_literal");
    LLVMSetLinkage(global_var, LLVMPrivateLinkage);  // This makes it a static global variable
    LLVMSetInitializer(global_var, array);

    LLVMTypeRef string_type = LLVMPointerType(LLVMInt8Type(), 0);
    return LLVMBuildBitCast(st->builder, global_var, string_type, "string_ptr");
}

static LLVMValueRef build_constant(const struct State *st, const Constant *c)
{
    switch(c->kind) {
    case CONSTANT_BOOL:
        return LLVMConstInt(LLVMInt1Type(), c->data.boolean, false);
    case CONSTANT_INTEGER:
        return LLVMConstInt(build_type(type_of_constant(c)), c->data.integer.value, c->data.integer.is_signed);
    case CONSTANT_FLOAT:
    case CONSTANT_DOUBLE:
        return LLVMConstRealOfString(build_type(type_of_constant(c)), c->data.double_or_float_text);
    case CONSTANT_NULL:
        return LLVMConstNull(build_type(voidPtrType));
    case CONSTANT_STRING:
        return make_a_string_constant(st, c->data.str);
    case CONSTANT_ENUM_MEMBER:
        return LLVMConstInt(LLVMInt32Type(), c->data.enum_member.memberidx, false);
    }
    assert(0);
}

enum PrePost { PRE, POST };

static LLVMValueRef build_incr_decr(const struct State *st, const AstExpression *expr, enum PrePost pp, int diff)
{
    assert(diff==-1 || diff==1);
    LLVMValueRef ptr = build_address_of_expression(st, expr);
    LLVMValueRef oldval = LLVMBuildLoad(st->builder, ptr, "oldval");
    LLVMValueRef newval;

    switch(get_type_after_cast(st, expr)->kind) {
    case TYPE_POINTER:
        newval = LLVMBuildGEP(st->builder, oldval, (LLVMValueRef[]){LLVMConstInt(LLVMInt64Type(), diff, true)}, 1, "newval");
        break;
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
        newval = LLVMBuildAdd(st->builder, oldval, LLVMConstInt(LLVMTypeOf(oldval), diff, true), "newval");
        break;
    default:
        assert(0);
    }
    LLVMBuildStore(st->builder, newval, ptr);

    switch(pp) {
        case PRE: return newval;
        case POST: return oldval;
    }
    assert(0);
}

enum AndOr { AND, OR };

static LLVMValueRef build_and_or(const struct State *st, const AstExpression *lhs, const AstExpression *rhs, enum AndOr andor)
{
    LLVMBasicBlockRef lhsstartblock = LLVMAppendBasicBlock(st->llvmfunc, "andor_lhs");
    LLVMBasicBlockRef rhsstartblock = LLVMAppendBasicBlock(st->llvmfunc, "andor_rhs");
    LLVMBasicBlockRef done = LLVMAppendBasicBlock(st->llvmfunc, "andor_done");
    LLVMBuildBr(st->builder, lhsstartblock);

    LLVMPositionBuilderAtEnd(st->builder, lhsstartblock);
    LLVMValueRef lhsvalue = build_expression(st, lhs);
    LLVMBasicBlockRef lhsendblock = LLVMGetInsertBlock(st->builder);
    switch(andor) {
        case AND: LLVMBuildCondBr(st->builder, lhsvalue, rhsstartblock, done); break;
        case OR: LLVMBuildCondBr(st->builder, lhsvalue, done, rhsstartblock); break;
    }

    LLVMPositionBuilderAtEnd(st->builder, rhsstartblock);
    LLVMValueRef rhsvalue = build_expression(st, rhs);
    LLVMBasicBlockRef rhsendblock = LLVMGetInsertBlock(st->builder);
    LLVMBuildBr(st->builder, done);

    LLVMPositionBuilderAtEnd(st->builder, done);
    LLVMValueRef phi = LLVMBuildPhi(st->builder, LLVMInt1Type(), "and");
    LLVMAddIncoming(phi, &lhsvalue, &lhsendblock, 1);
    LLVMAddIncoming(phi, &rhsvalue, &rhsendblock, 1);
    return phi;
}

static LLVMValueRef build_instantiation(const struct State *st, const Type *type, const AstCall *call)
{
    LLVMValueRef instanceptr = LLVMBuildAlloca(st->builder, build_type(type), "instanceptr");
    LLVMBuildMemSet(st->builder, instanceptr, LLVMConstInt(LLVMInt8Type(), 0, false), LLVMSizeOf(build_type(type)), 0);

    for (int i = 0; i < call->nargs; i++) {
        LLVMValueRef arg = build_expression(st, &call->args[i]);
        LLVMValueRef fieldptr = build_class_field_pointer(st, type, instanceptr, call->argnames[i]);
        LLVMBuildStore(st->builder, arg, fieldptr);
    }

    return LLVMBuildLoad2(st->builder, build_type(type), instanceptr, "instance");
}

static LLVMValueRef build_expression(const struct State *st, const AstExpression *expr)
{
    LLVMValueRef result, temp;

    switch(expr->kind) {
    case AST_EXPR_FUNCTION_CALL:
        result = build_call(st, NULL, NULL, &expr->data.call);
        break;
    case AST_EXPR_CONSTANT:
        result = build_constant(st, &expr->data.constant);
        break;
    case AST_EXPR_ADD:
    case AST_EXPR_SUB:
    case AST_EXPR_MUL:
    case AST_EXPR_DIV:
    case AST_EXPR_MOD:
    case AST_EXPR_EQ:
    case AST_EXPR_NE:
    case AST_EXPR_GT:
    case AST_EXPR_GE:
    case AST_EXPR_LT:
    case AST_EXPR_LE:
        {
            // Refactoring note: Make sure to evaluate lhs first. C doesn't guarantee evaluation
            // order of function arguments.
            LLVMValueRef lhs = build_expression(st, &expr->data.operands[0]);
            LLVMValueRef rhs = build_expression(st, &expr->data.operands[1]);
            const Type *lhstype = get_type_after_cast(st, &expr->data.operands[0]);
            const Type *rhstype = get_type_after_cast(st, &expr->data.operands[1]);
            result = build_binop(st, expr->kind, lhs, lhstype, rhs, rhstype);
            break;
        }
    case AST_EXPR_NEG:
        temp = build_expression(st, &expr->data.operands[0]);
        switch(get_type_after_cast(st, &expr->data.operands[0])->kind) {
        case TYPE_FLOATING_POINT:
            result = LLVMBuildFNeg(st->builder, temp, "neg");
            break;
        case TYPE_SIGNED_INTEGER:
            result = LLVMBuildNeg(st->builder, temp, "neg");
            break;
        default:
            assert(0);
        }
        break;
    case AST_EXPR_GET_VARIABLE:
        result = LLVMBuildLoad(st->builder, build_address_of_expression(st, expr), "val");
        break;
    case AST_EXPR_PRE_INCREMENT: result = build_incr_decr(st, &expr->data.operands[0], PRE, +1); break;
    case AST_EXPR_PRE_DECREMENT: result = build_incr_decr(st, &expr->data.operands[0], PRE, -1); break;
    case AST_EXPR_POST_INCREMENT: result = build_incr_decr(st, &expr->data.operands[0], POST, +1); break;
    case AST_EXPR_POST_DECREMENT: result = build_incr_decr(st, &expr->data.operands[0], POST, -1); break;
    case AST_EXPR_ADDRESS_OF:
        result = build_address_of_expression(st, &expr->data.operands[0]);
        break;
    case AST_EXPR_AND:
        result = build_and_or(st, &expr->data.operands[0], &expr->data.operands[1], AND);
        break;
    case AST_EXPR_OR:
        result = build_and_or(st, &expr->data.operands[0], &expr->data.operands[1], OR);
        break;
    case AST_EXPR_NOT:
        result = LLVMBuildNot(st->builder, build_expression(st, &expr->data.operands[0]), "not");
        break;
    case AST_EXPR_AS:
        temp = build_expression(st, expr->data.as.obj);
        result = build_cast(st, temp, get_type_after_cast(st, expr->data.as.obj), get_expr_types(st, expr)->type);
        break;
    case AST_EXPR_DEREF_AND_GET_FIELD:
    case AST_EXPR_INDEXING:
        /*
        To evaluate foo->bar, we first evaluate &foo->bar and then dereference.
        We can similarly evaluate &foo[bar].

        This technique cannot be used with all expressions. For example, &(1+2)
        doesn't work, and &foo.bar doesn't work either whenever &foo doesn't work.
        But &foo->bar and &foo[bar] always work, because foo is already a pointer
        and we only add a memory offset to it.
        */
        temp = build_address_of_expression(st, expr);
        result = LLVMBuildLoad(st->builder, temp, "dereffed");
        break;
    case AST_EXPR_BRACE_INIT:
        result = build_instantiation(st, get_expr_types(st, expr)->type, &expr->data.call);
        break;
    case AST_EXPR_SIZEOF:
        result = LLVMSizeOf(build_type(get_type_after_cast(st, &expr->data.operands[0])));
        break;
    case AST_EXPR_ARRAY:
    case AST_EXPR_CALL_METHOD:
    case AST_EXPR_DEREFERENCE:
    case AST_EXPR_DEREF_AND_CALL_METHOD:
    case AST_EXPR_GET_ENUM_MEMBER:
    case AST_EXPR_GET_FIELD:
        printf("%d\n", expr->kind);
        assert(0);
    }

    if (!result) {
        assert(expr->kind == AST_EXPR_FUNCTION_CALL);
        return NULL;
    }

    const ExpressionTypes *types = get_expr_types(st, expr);
    assert(types);
    if (types->type_after_cast)
        return build_cast(st, result, types->type, types->type_after_cast);
    else
        return result;
}

// for init; cond; incr:
//     ...body...
//
// While loop is basically a special case of for loop, so it uses this too.
static void build_loop(
    struct State *st,
    const AstStatement *init,
    const AstExpression *cond,
    const AstStatement *incr,
    const AstBody *body)
{
    LLVMBasicBlockRef condblock = LLVMAppendBasicBlock(st->llvmfunc, "loop_cond");
    LLVMBasicBlockRef bodyblock = LLVMAppendBasicBlock(st->llvmfunc, "loop_body");
    LLVMBasicBlockRef incrblock = LLVMAppendBasicBlock(st->llvmfunc, "loop_incr");
    LLVMBasicBlockRef doneblock = LLVMAppendBasicBlock(st->llvmfunc, "loop_done");

    // Loop start: evaluate init and go to condition
    if (init)
        build_statement(st, init);
    LLVMBuildBr(st->builder, condblock);

    // Evaluate condition.
    LLVMPositionBuilderAtEnd(st->builder, condblock);
    LLVMBuildCondBr(st->builder, build_expression(st, cond), bodyblock, doneblock);

    // Run loop body: 'break' skips to after loop, 'continue' goes to incr.
    LLVMPositionBuilderAtEnd(st->builder, bodyblock);
    Append(&st->breakstack, doneblock);
    Append(&st->continuestack, incrblock);
    build_body(st, body);
    LLVMBasicBlockRef tmp;
    tmp = Pop(&st->breakstack); assert(tmp == doneblock);
    tmp = Pop(&st->continuestack); assert(tmp == incrblock);
    LLVMBuildBr(st->builder, incrblock);

    // Run incr and jump back to condition.
    LLVMPositionBuilderAtEnd(st->builder, incrblock);
    if (incr)
        build_statement(st, incr);
    LLVMBuildBr(st->builder, condblock);

    // Stuff after the loop goes to doneblock
    LLVMPositionBuilderAtEnd(st->builder, doneblock);
}

static void build_if_statement(struct State *st, const AstIfStatement *ifstmt)
{
    assert(ifstmt->n_if_and_elifs >= 1);

    LLVMBasicBlockRef done = LLVMAppendBasicBlock(st->llvmfunc, "if_done");
    for (int i = 0; i < ifstmt->n_if_and_elifs; i++) {
        LLVMBasicBlockRef then = LLVMAppendBasicBlock(st->llvmfunc, "then");
        LLVMBasicBlockRef otherwise = LLVMAppendBasicBlock(st->llvmfunc, "otherwise");

        LLVMValueRef cond = build_expression(st, &ifstmt->if_and_elifs[i].condition);
        LLVMBuildCondBr(st->builder, cond, then, otherwise);

        LLVMPositionBuilderAtEnd(st->builder, then);
        build_body(st, &ifstmt->if_and_elifs[i].body);
        LLVMBuildBr(st->builder, done);

        LLVMPositionBuilderAtEnd(st->builder, otherwise);
    }

    build_body(st, &ifstmt->elsebody);
    LLVMBuildBr(st->builder, done);
    LLVMPositionBuilderAtEnd(st->builder, done);
}

// Used in situations where we can have code after something, but it will never run.
// For example, there can be code after a "break" or "continue" statement in a loop.
// It goes into an unreachable dummy block.
static void position_builder_to_dummy_block(struct State *st)
{
    LLVMBasicBlockRef b = LLVMAppendBasicBlock(st->llvmfunc, "unreachable");
    LLVMPositionBuilderAtEnd(st->builder, b);
}

static void build_statement(struct State *st, const AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_ASSIGN:
        {
            LLVMValueRef targetptr = build_address_of_expression(st, &stmt->data.assignment.target);
            LLVMValueRef value = build_expression(st, &stmt->data.assignment.value);
            LLVMBuildStore(st->builder, value, targetptr);
            break;
        }
    case AST_STMT_BREAK:
        assert(st->breakstack.len > 0);
        LLVMBuildBr(st->builder, End(st->breakstack)[-1]);
        position_builder_to_dummy_block(st);
        break;
    case AST_STMT_CONTINUE:
        assert(st->breakstack.len > 0);
        LLVMBuildBr(st->builder, End(st->breakstack)[-1]);
        position_builder_to_dummy_block(st);
        break;
    case AST_STMT_DECLARE_LOCAL_VAR:
        if (stmt->data.vardecl.value) {
            LLVMValueRef value = build_expression(st, stmt->data.vardecl.value);
            LLVMBuildStore(st->builder, value, find_local_var(st, stmt->data.vardecl.name)->ptr);
        }
        break;
    case AST_STMT_EXPRESSION_STATEMENT:
        build_expression(st, &stmt->data.expression);
        break;
    case AST_STMT_FOR:
        build_loop(
            st,
            stmt->data.forloop.init, &stmt->data.forloop.cond, stmt->data.forloop.incr,
            &stmt->data.forloop.body);
        break;
    case AST_STMT_WHILE:
        build_loop(st, NULL, &stmt->data.whileloop.condition, NULL, &stmt->data.whileloop.body);
        break;
    case AST_STMT_INPLACE_ADD:
    case AST_STMT_INPLACE_SUB:
    case AST_STMT_INPLACE_MUL:
    case AST_STMT_INPLACE_DIV:
    case AST_STMT_INPLACE_MOD:
    {
        const AstExpression *targetexpr = &stmt->data.assignment.target;
        const AstExpression *rhsexpr = &stmt->data.assignment.value;
        const Type *targettype = get_type_after_cast(st, targetexpr);
        const Type *rhstype = get_type_after_cast(st, rhsexpr);

        LLVMValueRef targetptr = build_address_of_expression(st, targetexpr);
        LLVMValueRef rhs = build_expression(st, rhsexpr);
        LLVMValueRef oldvalue = LLVMBuildLoad(st->builder, targetptr, "old");
        enum AstExpressionKind op;
        switch(stmt->kind){
            case AST_STMT_INPLACE_ADD: op=AST_EXPR_ADD; break;
            case AST_STMT_INPLACE_SUB: op=AST_EXPR_SUB; break;
            case AST_STMT_INPLACE_MUL: op=AST_EXPR_MUL; break;
            case AST_STMT_INPLACE_DIV: op=AST_EXPR_DIV; break;
            case AST_STMT_INPLACE_MOD: op=AST_EXPR_MOD; break;
            default: assert(0);
        }
        LLVMValueRef newvalue = build_binop(st, op, oldvalue, targettype, rhs, rhstype);
        LLVMBuildStore(st->builder, newvalue, targetptr);
        break;
    }
    case AST_STMT_IF:
        build_if_statement(st, &stmt->data.ifstatement);
        break;
    case AST_STMT_RETURN_VALUE:
        LLVMBuildRet(st->builder, build_expression(st, &stmt->data.expression));
        position_builder_to_dummy_block(st);
        break;
    case AST_STMT_RETURN_WITHOUT_VALUE:
        LLVMBuildRetVoid(st->builder);
        position_builder_to_dummy_block(st);
        break;
    }
}

static void build_body(struct State *st, const AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        build_statement(st, &body->statements[i]);
}

#ifdef _WIN32
static void build_call_to_the_special_startup_function(const struct State *st)
{
    LLVMTypeRef functype = LLVMFunctionType(LLVMVoidType(), NULL, 0, false);
    LLVMValueRef func = LLVMAddFunction(st->module, "_jou_windows_startup", functype);
    LLVMBuildCall2(st->builder, functype, func, NULL, 0, "");
}
#endif

static void build_function_or_method_def(struct State *st, const Type *selfclass, const char *name, const AstBody *body)
{
    assert(!st->fomtypes);
    for (const FunctionOrMethodTypes *f = st->filetypes->fomtypes.ptr; f < End(st->filetypes->fomtypes); f++) {
        if (!strcmp(f->signature.name, name) && get_self_class(&f->signature) == selfclass) {
            st->fomtypes = f;
            break;
        }
    }
    assert(st->fomtypes);

    const Signature *sig = &st->fomtypes->signature;
    st->llvmfunc = build_function_or_method_decl(st, sig);

    LLVMBasicBlockRef startblock = LLVMAppendBasicBlock(st->llvmfunc, "start");
    LLVMPositionBuilderAtEnd(st->builder, startblock);

#ifdef _WIN32
    if (!get_self_class(sig) && !strcmp(sig->name, "main"))
        build_call_to_the_special_startup_function(st);
#endif

    // Create local variables.
    assert(st->locals.len == 0);
    for (LocalVariable **var = st->fomtypes->locals.ptr; var < End(st->fomtypes->locals); var++) {
        LLVMValueRef ptr = LLVMBuildAlloca(st->builder, build_type((*var)->type), (*var)->name);
        struct Variable v = { .ptr = ptr };
        safe_strcpy(v.name, (*var)->name);
        Append(&st->locals, v);
    }

    // Store arguments to first n local variables.
    for (int i = 0; i < sig->nargs; i++)
        LLVMBuildStore(st->builder, LLVMGetParam(st->llvmfunc, i), st->locals.ptr[i].ptr);

    build_body(st, body);
    if (sig->returntype)
        LLVMBuildUnreachable(st->builder);
    else
        LLVMBuildRetVoid(st->builder);

    free(st->locals.ptr);
    memset(&st->locals, 0, sizeof st->locals);
    st->fomtypes = NULL;
}

LLVMModuleRef codegen(AstToplevelNode *ast, FileTypes *ft)
{
    struct State st = {
        .filetypes = ft,
        .module = LLVMModuleCreateWithName(ast[0].location.filename),
        .builder = LLVMCreateBuilder(),
    };

    LLVMSetTarget(st.module, get_target()->triple);
    LLVMSetDataLayout(st.module, get_target()->data_layout);

    for (GlobalVariable **v = ft->globals.ptr; v < End(ft->globals); v++) {
        LLVMTypeRef t = build_type((*v)->type);
        LLVMValueRef globalptr = LLVMAddGlobal(st.module, t, (*v)->name);
        if ((*v)->defined_in_current_file)
            LLVMSetInitializer(globalptr, LLVMGetUndef(t));
    }

    while (ast->kind != AST_TOPLEVEL_END_OF_FILE) {
        if(ast->kind == AST_TOPLEVEL_DEFINE_FUNCTION)
            build_function_or_method_def(&st, NULL, ast->data.funcdef.signature.name, &ast->data.funcdef.body);

        if (ast->kind == AST_TOPLEVEL_DEFINE_CLASS) {
            Type *classtype = NULL;
            for (Type **t = ft->owned_types.ptr; t < End(ft->owned_types); t++) {
                if (!strcmp((*t)->name, ast->data.classdef.name)) {
                    classtype = *t;
                    break;
                }
            }
            assert(classtype);

            for (AstFunctionDef *m = ast->data.classdef.methods.ptr; m < End(ast->data.classdef.methods); m++)
                build_function_or_method_def(&st, classtype, m->signature.name, &m->body);
        }
        ast++;
    }

    LLVMDisposeBuilder(st.builder);
    return st.module;
}
