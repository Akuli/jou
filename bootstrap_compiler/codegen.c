#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include "jou_compiler.h"
#include "util.h"

/*
LLVM doesn't have a built-in union type, and you're supposed to abuse other types for that:
https://mapping-high-level-constructs-to-llvm-ir.readthedocs.io/en/latest/basic-constructs/unions.html

My first idea was to use an array of bytes that is big enough to fit anything.
However, that might not be aligned properly.

Then I tried choosing the member type that has the biggest align, and making a large enough array of it.
Because the align is always a power of two, the memory will be suitably aligned for all member types.
But it didn't work for some reason I still don't understand.

Then I figured out how clang does it and did it the same way.
We make a struct that contains:
- the most aligned type as chosen before
- array of i8 as padding to make it the right size.
But for some reason that didn't work either.

As a "last resort" I just use an array of i64 large enough and hope it's aligned as needed.
*/
static LLVMTypeRef codegen_union_type(const LLVMTypeRef *types, int ntypes)
{
    // For some reason uncommenting this makes stuff compile almost 2x slower...
    //if (ntypes == 1)
    //    return types[0];

    unsigned long long sizeneeded = 0;
    for (int i = 0; i < ntypes; i++) {
        unsigned long long size1 = LLVMABISizeOfType(get_target()->target_data_ref, types[i]);
        unsigned long long size2 = LLVMStoreSizeOfType(get_target()->target_data_ref, types[i]);

        // If this assert fails, you need to figure out which of the size functions should be used.
        // I don't know what their difference is.
        // And if you need the alignment, there's 3 different functions for that...
        assert(size1 == size2);
        sizeneeded = max(sizeneeded, size1);
    }
    return LLVMArrayType(LLVMInt64Type(), (sizeneeded+7)/8);
}

static LLVMTypeRef type_to_llvm(const Type *type)
{
    switch(type->kind) {
    case TYPE_ARRAY:
        return LLVMArrayType(type_to_llvm(type->data.array.membertype), type->data.array.len);
    case TYPE_POINTER:
    case TYPE_VOID_POINTER:
        // Element type doesn't matter in new LLVM versions.
        return LLVMPointerType(LLVMInt8Type(), 0);
    case TYPE_FLOATING_POINT:
        switch(type->data.width_in_bits) {
            case 32: return LLVMFloatType();
            case 64: return LLVMDoubleType();
            default: assert(0);
        }
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

            LLVMTypeRef *flat_elems = malloc(sizeof(flat_elems[0]) * n);  // NOLINT
            for (int i = 0; i < n; i++)
                flat_elems[i] = type_to_llvm(type->data.classdata.fields.ptr[i].type);

            // Combine together fields of the same union.
            LLVMTypeRef *combined = malloc(sizeof(combined[0]) * n);  // NOLINT
            int combinedlen = 0;
            int start, end;
            for (start=0; start<n; start=end) {
                end = start+1;
                while (end < n && type->data.classdata.fields.ptr[start].union_id == type->data.classdata.fields.ptr[end].union_id)
                    end++;
                combined[combinedlen++] = codegen_union_type(&flat_elems[start], end-start);
            }

            LLVMTypeRef result = LLVMStructType(combined, combinedlen, false);
            free(flat_elems);
            free(combined);
            return result;
        }
    case TYPE_ENUM:
        return LLVMInt32Type();
    }
    assert(0);
}

struct LocalVar {
    char name[100];
    // All local variables are represented as pointers to stack space, even
    // if they are never reassigned. LLVM will optimize the mess.
    LLVMValueRef ptr;
};

struct State {
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    struct LocalVar locals[500];
    int nlocals;
    bool is_main_file;
    const FileTypes *filetypes;
    const FunctionOrMethodTypes *fomtypes;
    /*
    CfGraph *cfg;
    CfBlock *current_block;
    List(CfBlock *) breakstack;
    List(CfBlock *) continuestack;
    */
};

// Return value may become invalid when adding more local vars.
static const struct LocalVar *find_local_var(const struct State *st, const char *name)
{
    for (int i = 0; i < st->nlocals; i++) {
        if (!strcmp(st->locals[i].name, name))
            return &st->locals[i];
    }
    return NULL;
}

static struct LocalVar *add_local_var(struct State *st, const Type *t, const char *name)
{
    if (!name)
        name = "";
    if (name[0])
        assert(find_local_var(st, name) == NULL);

    assert(st->nlocals < (int)(sizeof(st->locals) / sizeof(st->locals[0])));
    struct LocalVar *v = &st->locals[st->nlocals++];

    assert(strlen(name) < sizeof(v->name));
    strcpy(v->name, name);
    v->ptr = LLVMBuildAlloca(st->builder, type_to_llvm(t), name);
    return v;
}

static LLVMValueRef build_expression(struct State *st, const AstExpression *expr);
static LLVMValueRef build_address_of_expression(struct State *st, const AstExpression *address_of_what);

enum PreOrPost { PRE, POST };

static const Type *type_of_expr(const AstExpression *expr)
{
    assert(expr->types.type);
    if (expr->types.implicit_cast_type)
        return expr->types.implicit_cast_type;
    else
        return expr->types.type;
}

static LLVMValueRef build_increment_or_decrement(
    struct State *st,
    const AstExpression *inner,
    enum PreOrPost pop,
    int diff)
{
    assert(diff==1 || diff==-1);  // 1=increment, -1=decrement

    const Type *t = type_of_expr(inner);
    LLVMValueRef ptr = build_address_of_expression(st, inner);
    LLVMValueRef old_value = LLVMBuildLoad2(st->builder, type_to_llvm(t), ptr, "old_value");
    LLVMValueRef new_value;

    switch(t->kind) {
        case TYPE_SIGNED_INTEGER:
        case TYPE_UNSIGNED_INTEGER:
            new_value = LLVMBuildAdd(st->builder, old_value, LLVMConstInt(type_to_llvm(t), diff, true), "new_value");
            break;
        case TYPE_FLOATING_POINT:
            if (diff == 1)
                new_value = LLVMBuildAdd(st->builder, old_value, LLVMConstRealOfString(type_to_llvm(t), "1"), "new_value");
            else
                new_value = LLVMBuildAdd(st->builder, old_value, LLVMConstRealOfString(type_to_llvm(t), "-1"), "new_value");
            break;
        case TYPE_POINTER:
            new_value = LLVMBuildGEP2(st->builder, type_to_llvm(t), old_value, (LLVMValueRef[]){LLVMConstInt(LLVMInt64Type(), diff, true)}, 1, "ptr_add_int");
            break;
        case TYPE_BOOL:
        case TYPE_VOID_POINTER:
        case TYPE_ARRAY:
        case TYPE_CLASS:
        case TYPE_OPAQUE_CLASS:
        case TYPE_ENUM:
            assert(false);
    }

    switch(pop) {
        case PRE: return new_value;
        case POST: return old_value;
    }
    assert(0);
}

enum AndOr { AND, OR };

static const LocalVariable *build_and_or(
    struct State *st, const AstExpression *lhsexpr, const AstExpression *rhsexpr, enum AndOr andor)
{
    /*
    Must be careful with side effects.

    and:
        # lhs returning False means we don't evaluate rhs
        if lhs:
            result = rhs
        else:
            result = False

    or:
        # lhs returning True means we don't evaluate rhs
        if lhs:
            result = True
        else:
            result = rhs
    */
    const LocalVariable *lhs = build_expression(st, lhsexpr);
    const LocalVariable *rhs;
    const LocalVariable *result = add_local_var(st, boolType);
    CfInstruction *ins;

    CfBlock *lhstrue = add_block(st);
    CfBlock *lhsfalse = add_block(st);
    CfBlock *done = add_block(st);

    // if lhs:
    add_jump(st, lhs, lhstrue, lhsfalse, lhstrue);

    switch(andor) {
    case AND:
        // result = rhs
        rhs = build_expression(st, rhsexpr);
        add_unary_op(st, rhsexpr->location, CF_VARCPY, rhs, result);
        break;
    case OR:
        // result = True
        ins = add_constant(st, lhsexpr->location, ((Constant){CONSTANT_BOOL, {.boolean=true}}), result);
        ins->hide_unreachable_warning = true;
        break;
    }

    // else:
    add_jump(st, NULL, done, done, lhsfalse);

    switch(andor) {
    case AND:
        // result = False
        ins = add_constant(st, lhsexpr->location, ((Constant){CONSTANT_BOOL, {.boolean=false}}), result);
        ins->hide_unreachable_warning = true;
        break;
    case OR:
        // result = rhs
        rhs = build_expression(st, rhsexpr);
        add_unary_op(st, rhsexpr->location, CF_VARCPY, rhs, result);
        break;
    }

    add_jump(st, NULL, done, done, done);
    return result;
}

static const LocalVariable *build_address_of_expression(struct State *st, const AstExpression *address_of_what)
{
    switch(address_of_what->kind) {
    case AST_EXPR_GET_VARIABLE:
    {
        const Type *ptrtype = get_pointer_type(address_of_what->types.type);
        const LocalVariable *addr = add_local_var(st, ptrtype);

        const LocalVariable *local_var = find_local_var(st, address_of_what->data.varname);
        if (local_var) {
            add_unary_op(st, address_of_what->location, CF_ADDRESS_OF_LOCAL_VAR, local_var, addr);
        } else {
            // Global variable (possibly imported from another file)
            union CfInstructionData data;
            safe_strcpy(data.globalname, address_of_what->data.varname);
            add_instruction(st, address_of_what->location, CF_ADDRESS_OF_GLOBAL_VAR, &data, NULL, addr);
        }
        return addr;
    }
    case AST_EXPR_DEREFERENCE:
    {
        // &*foo --> just evaluate foo
        return build_expression(st, &address_of_what->data.operands[0]);
    }
    case AST_EXPR_DEREF_AND_GET_FIELD:
    {
        // &obj->field aka &(obj->field)
        const LocalVariable *obj = build_expression(st, address_of_what->data.classfield.obj);
        assert(obj->type->kind == TYPE_POINTER);
        assert(obj->type->data.valuetype->kind == TYPE_CLASS);
        return build_class_field_pointer(st, obj, address_of_what->data.classfield.fieldname, address_of_what->location);
    }
    case AST_EXPR_GET_FIELD:
    {
        // &obj.field aka &(obj.field), evaluate as &(&obj)->field
        const LocalVariable *obj = build_address_of_expression(st, address_of_what->data.classfield.obj);
        assert(obj->type->kind == TYPE_POINTER);
        return build_class_field_pointer(st, obj, address_of_what->data.classfield.fieldname, address_of_what->location);
    }
    case AST_EXPR_INDEXING:
    {
        const LocalVariable *ptr = build_expression(st, &address_of_what->data.operands[0]);
        assert(ptr->type->kind == TYPE_POINTER);

        const LocalVariable *index = build_expression(st, &address_of_what->data.operands[1]);
        assert(is_integer_type(index->type));

        const LocalVariable *result = add_local_var(st, ptr->type);
        add_binary_op(st, address_of_what->location, CF_PTR_ADD_INT, ptr, index, result);
        return result;
    }

    default:
        assert(0);
        break;
    }

    assert(0);
}

static const LocalVariable *build_function_or_method_call(
    struct State *st,
    const Location location,
    const AstCall *call,
    const AstExpression *self,
    bool self_is_a_pointer)
{
    const Signature *sig = NULL;

    if(self) {
        const Type *selfclass = self->types.type;
        if (self_is_a_pointer) {
            assert(selfclass->kind == TYPE_POINTER);
            selfclass = selfclass->data.valuetype;
        }
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

    const LocalVariable **args = calloc(call->nargs + 2, sizeof(args[0]));  // NOLINT
    int k = 0;

    if (self) {
        if (is_pointer_type(sig->argtypes[0]) && !self_is_a_pointer) {
            args[k++] = build_address_of_expression(st, self);
        } else if (!is_pointer_type(sig->argtypes[0]) && self_is_a_pointer) {
            const LocalVariable *self_ptr = build_expression(st, self);
            assert(self_ptr->type->kind == TYPE_POINTER);

            // dereference the pointer
            const LocalVariable *val = add_local_var(st, self_ptr->type->data.valuetype);
            add_unary_op(st, self->location, CF_PTR_LOAD, self_ptr, val);
            args[k++] = val;
        } else {
            args[k++] = build_expression(st, self);
        }
    }

    for (int i = 0; i < call->nargs; i++)
        args[k++] = build_expression(st, &call->args[i]);

    const LocalVariable *return_value;
    if (sig->returntype)
        return_value = add_local_var(st, sig->returntype);
    else
        return_value = NULL;

    union CfInstructionData data = { .signature = copy_signature(sig) };
    add_instruction(st, location, CF_CALL, &data, args, return_value);

    if (sig->is_noreturn) {
        // Place the remaining code into an unreachable block, so you will get a warning if there is any
        add_jump(st, NULL, NULL, NULL, NULL);
    }

    free(args);
    return return_value;
}

static const LocalVariable *build_struct_init(struct State *st, const Type *type, const AstCall *call, Location location)
{
    const LocalVariable *instance = add_local_var(st, type);
    const LocalVariable *instanceptr = add_local_var(st, get_pointer_type(type));

    add_unary_op(st, location, CF_ADDRESS_OF_LOCAL_VAR, instance, instanceptr);
    add_unary_op(st, location, CF_PTR_MEMSET_TO_ZERO, instanceptr, NULL);

    for (int i = 0; i < call->nargs; i++) {
        const LocalVariable *fieldptr = build_class_field_pointer(st, instanceptr, call->argnames[i], call->args[i].location);
        const LocalVariable *fieldval = build_expression(st, &call->args[i]);
        add_binary_op(st, location, CF_PTR_STORE, fieldptr, fieldval, NULL);
    }

    return instance;
}

static const LocalVariable *build_array(struct State *st, const Type *type, const AstExpression *items, Location location)
{
    assert(type->kind == TYPE_ARRAY);

    const LocalVariable *arr = add_local_var(st, type);
    const LocalVariable *arrptr = add_local_var(st, get_pointer_type(type));
    add_unary_op(st, location, CF_ADDRESS_OF_LOCAL_VAR, arr, arrptr);
    const LocalVariable *first_item_ptr = add_local_var(st, get_pointer_type(type->data.array.membertype));
    add_unary_op(st, location, CF_PTR_CAST, arrptr, first_item_ptr);

    for (int i = 0; i < type->data.array.len; i++) {
        const LocalVariable *value = build_expression(st, &items[i]);

        const LocalVariable *ivar = add_local_var(st, intType);
        add_constant(st, location, int_constant(intType, i), ivar);

        const LocalVariable *destptr = add_local_var(st, first_item_ptr->type);
        add_binary_op(st, location, CF_PTR_ADD_INT, first_item_ptr, ivar, destptr);
        add_binary_op(st, location, CF_PTR_STORE, destptr, value, NULL);
    }

    return arr;
}

static int find_enum_member(const Type *enumtype, const char *name)
{
    for (int i = 0; i < enumtype->data.enummembers.count; i++)
        if (!strcmp(enumtype->data.enummembers.names[i], name))
            return i;
    assert(0);
}

static const LocalVariable *build_expression(struct State *st, const AstExpression *expr)
{
    const Type *t = expr->types.type;
    if (expr->types.implicit_array_to_pointer_cast) {
        const LocalVariable *arrptr = build_address_of_expression(st, expr);
        const LocalVariable *memberptr = add_local_var(st, expr->types.implicit_cast_type);
        add_unary_op(st, expr->location, CF_PTR_CAST, arrptr, memberptr);
        return memberptr;
    }

    if (expr->types.implicit_string_to_array_cast) {
        assert(expr->types.implicit_cast_type);
        assert(expr->types.implicit_cast_type->kind == TYPE_ARRAY);
        assert(expr->kind == AST_EXPR_CONSTANT);
        assert(expr->data.constant.kind == CONSTANT_STRING);

        char *padded = calloc(1, expr->types.implicit_cast_type->data.array.len);
        strcpy(padded, expr->data.constant.data.str);

        const LocalVariable *result = add_local_var(st, expr->types.implicit_cast_type);
        union CfInstructionData data = { .strarray = {
            .len = expr->types.implicit_cast_type->data.array.len,
            .str = padded,
        }};
        add_instruction(st, expr->location, CF_STRING_ARRAY, &data, NULL, result);
        return result;
    }

    const LocalVariable *result, *temp;

    switch(expr->kind) {
    case AST_EXPR_DEREF_AND_CALL_METHOD:
        result = build_function_or_method_call(st, expr->location, &expr->data.methodcall.call, expr->data.methodcall.obj, true);
        if (!result)
            return NULL;
        break;
    case AST_EXPR_CALL_METHOD:
        result = build_function_or_method_call(st, expr->location, &expr->data.methodcall.call, expr->data.methodcall.obj, false);
        if (!result)
            return NULL;
        break;
    case AST_EXPR_FUNCTION_CALL:
        result = build_function_or_method_call(st, expr->location, &expr->data.call, NULL, false);
        if (!result)
            return NULL;
        break;
    case AST_EXPR_BRACE_INIT:
        result = build_struct_init(st, t, &expr->data.call, expr->location);
        break;
    case AST_EXPR_ARRAY:
        assert(t->kind == TYPE_ARRAY);
        assert(t->data.array.len == expr->data.array.count);
        result = build_array(st, t, expr->data.array.items, expr->location);
        break;
    case AST_EXPR_GET_FIELD:
        temp = build_expression(st, expr->data.classfield.obj);
        result = build_class_field(st, temp, expr->data.classfield.fieldname, expr->location);
        break;
    case AST_EXPR_GET_ENUM_MEMBER:
        result = add_local_var(st, t);
        Constant c = { CONSTANT_ENUM_MEMBER, {
            .enum_member.enumtype = t,
            .enum_member.memberidx = find_enum_member(t, expr->data.enummember.membername),
        }};
        add_constant(st, expr->location, c, result);
        break;
    case AST_EXPR_GET_VARIABLE:
        if (get_special_constant(expr->data.varname) != -1) {
            result = add_local_var(st, boolType);
            union CfInstructionData data;
            safe_strcpy(data.scname, expr->data.varname);
            add_instruction(st, expr->location, CF_SPECIAL_CONSTANT, &data, NULL, result);
            break;
        }
        if ((temp = find_local_var(st, expr->data.varname))) {
            if (expr->types.implicit_cast_type == NULL || t == expr->types.implicit_cast_type) {
                // Must take a "snapshot" of this variable, as it may change soon.
                result = add_local_var(st, temp->type);
                add_unary_op(st, expr->location, CF_VARCPY, temp, result);
            } else {
                result = temp;
            }
            break;
        }
        // For other than local variables we can evaluate as &*variable.
        // Would also work for locals, but it would confuse simplify_cfg.
        __attribute__((fallthrough));
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
        result = add_local_var(st, t);
        add_unary_op(st, expr->location, CF_PTR_LOAD, temp, result);
        break;
    case AST_EXPR_ADDRESS_OF:
        result = build_address_of_expression(st, &expr->data.operands[0]);
        break;
    case AST_EXPR_SIZEOF:
        {
            result = add_local_var(st, longType);
            union CfInstructionData data = { .type = expr->data.operands[0].types.type };
            add_instruction(st, expr->location, CF_SIZEOF, &data, NULL, result);
        }
        break;
    case AST_EXPR_DEREFERENCE:
        temp = build_expression(st, &expr->data.operands[0]);
        result = add_local_var(st, t);
        add_unary_op(st, expr->location, CF_PTR_LOAD, temp, result);
        break;
    case AST_EXPR_CONSTANT:
        result = add_local_var(st, t);
        add_constant(st, expr->location, expr->data.constant, result);
        break;
    case AST_EXPR_AND:
        result = build_and_or(st, &expr->data.operands[0], &expr->data.operands[1], AND);
        break;
    case AST_EXPR_OR:
        result = build_and_or(st, &expr->data.operands[0], &expr->data.operands[1], OR);
        break;
    case AST_EXPR_NOT:
        temp = build_expression(st, &expr->data.operands[0]);
        result = add_local_var(st, boolType);
        add_unary_op(st, expr->location, CF_BOOL_NEGATE, temp, result);
        break;
    case AST_EXPR_NEG:
        temp = build_expression(st, &expr->data.operands[0]);
        const LocalVariable *zero = add_local_var(st, temp->type);
        result = add_local_var(st, temp->type);
        if (temp->type == doubleType)
            add_constant(st, expr->location, ((Constant){ CONSTANT_DOUBLE, {.double_or_float_text="0"} }), zero);
        else if (temp->type == floatType)
            add_constant(st, expr->location, ((Constant){ CONSTANT_FLOAT, {.double_or_float_text="0"}}), zero);
        else
            add_constant(st, expr->location, int_constant(temp->type, 0), zero);
        add_binary_op(st, expr->location, CF_NUM_SUB, zero, temp, result);
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
            const LocalVariable *lhs = build_expression(st, &expr->data.operands[0]);
            const LocalVariable *rhs = build_expression(st, &expr->data.operands[1]);
            result = build_binop(st, expr->kind, expr->location, lhs, rhs, t);
            break;
        }
    case AST_EXPR_PRE_INCREMENT:
    case AST_EXPR_PRE_DECREMENT:
    case AST_EXPR_POST_INCREMENT:
    case AST_EXPR_POST_DECREMENT:
        {
            enum PreOrPost pop;
            int diff;

            switch(expr->kind) {
                case AST_EXPR_PRE_INCREMENT: pop=PRE; diff=1; break;
                case AST_EXPR_PRE_DECREMENT: pop=PRE; diff=-1; break;
                case AST_EXPR_POST_INCREMENT: pop=POST; diff=1; break;
                case AST_EXPR_POST_DECREMENT: pop=POST; diff=-1; break;
                default: assert(0);
            }
            result = build_increment_or_decrement(st, expr->location, &expr->data.operands[0], pop, diff);
            break;
        }
    case AST_EXPR_AS:
        temp = build_expression(st, expr->data.as.obj);
        result = build_cast(st, temp, t, expr->location);
        break;
    }

    assert(result->type == t);
    if (expr->types.implicit_cast_type)
        return build_cast(st, result, expr->types.implicit_cast_type, expr->location);
    else
        return result;
}

static void build_body(struct State *st, const AstBody *body);

static void build_if_statement(struct State *st, const AstIfStatement *ifstmt)
{
    assert(ifstmt->n_if_and_elifs >= 1);

    CfBlock *done = add_block(st);
    for (int i = 0; i < ifstmt->n_if_and_elifs; i++) {
        const LocalVariable *cond = build_expression(
            st, &ifstmt->if_and_elifs[i].condition);
        CfBlock *then = add_block(st);
        CfBlock *otherwise = add_block(st);

        add_jump(st, cond, then, otherwise, then);
        build_body(st, &ifstmt->if_and_elifs[i].body);
        add_jump(st, NULL, done, done, otherwise);
    }

    build_body(st, &ifstmt->elsebody);
    add_jump(st, NULL, done, done, done);
}

static void build_assert(struct State *st, Location assert_location, const AstAssert *assertion)
{
    const LocalVariable *condvar = build_expression(st, &assertion->condition);

    // If the condition is true, we jump to a block where the rest of the code goes.
    // If the condition is false, we jump to a block that calls _jou_assert_fail().
    CfBlock *trueblock = add_block(st);
    CfBlock *falseblock = add_block(st);
    add_jump(st, condvar, trueblock, falseblock, falseblock);

    char (*argnames)[100] = malloc(3 * sizeof *argnames);
    strcpy(argnames[0], "assertion");
    strcpy(argnames[1], "path");
    strcpy(argnames[2], "lineno");

    const Type **argtypes = malloc(3 * sizeof(argtypes[0]));  // NOLINT
    argtypes[0] = get_pointer_type(byteType);
    argtypes[1] = get_pointer_type(byteType);
    argtypes[2] = intType;

    const LocalVariable *args[4];
    for (int i = 0; i < 3; i++)
        args[i] = add_local_var(st, argtypes[i]);
    args[3] = NULL;

    add_constant(st, assert_location, ((Constant){CONSTANT_STRING,{.str=assertion->condition_str}}), args[0]);
    char *tmp = strdup(assertion->condition.location.filename);
    add_constant(st, assert_location, ((Constant){CONSTANT_STRING,{.str=tmp}}), args[1]);
    free(tmp);
    add_constant(st, assert_location, int_constant(intType, assert_location.lineno), args[2]);

    union CfInstructionData data = { .signature = {
        .name = "_jou_assert_fail",
        .nargs = 3,
        .argtypes = argtypes,
        .argnames = argnames,
        .takes_varargs = false,
        .is_noreturn = true,
        .returntype_location = assert_location,
    } };
    add_instruction(st, assert_location, CF_CALL, &data, args, NULL);

    st->current_block = trueblock;
}

static void build_statement(struct State *st, const AstStatement *stmt);

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
    CfBlock *condblock = add_block(st);  // evaluate condition and go to bodyblock or doneblock
    CfBlock *bodyblock = add_block(st);  // run loop body and go to incrblock
    CfBlock *incrblock = add_block(st);  // run incr and go to condblock
    CfBlock *doneblock = add_block(st);  // rest of the code goes here
    CfBlock *tmp;

    if (init)
        build_statement(st, init);

    // Evaluate condition. Jump to loop body or skip to after loop.
    add_jump(st, NULL, condblock, condblock, condblock);
    const LocalVariable *condvar = build_expression(st, cond);
    add_jump(st, condvar, bodyblock, doneblock, bodyblock);

    // Run loop body: 'break' skips to after loop, 'continue' goes to incr.
    Append(&st->breakstack, doneblock);
    Append(&st->continuestack, incrblock);
    build_body(st, body);
    tmp = Pop(&st->breakstack); assert(tmp == doneblock);
    tmp = Pop(&st->continuestack); assert(tmp == incrblock);

    // Run incr and jump back to condition.
    add_jump(st, NULL, incrblock, incrblock, incrblock);
    if (incr)
        build_statement(st, incr);
    add_jump(st, NULL, condblock, condblock, doneblock);
}

static void build_match_statament(struct State *st, const AstMatchStatement *match_stmt)
{
    const LocalVariable *match_obj_enum = build_expression(st, &match_stmt->match_obj);
    LocalVariable *match_obj_int = add_local_var(st, intType);
    add_unary_op(st, match_stmt->match_obj.location, CF_ENUM_TO_INT32, match_obj_enum, match_obj_int);

    CfBlock *done = add_block(st);
    for (int i = 0; i < match_stmt->ncases; i++) {
    for (AstExpression *caseobj = match_stmt->cases[i].case_objs; caseobj < &match_stmt->cases[i].case_objs[match_stmt->cases[i].n_case_objs]; caseobj++) {
        const LocalVariable *case_obj_enum = build_expression(st, caseobj);
        LocalVariable *case_obj_int = add_local_var(st, intType);
        add_unary_op(st, caseobj->location, CF_ENUM_TO_INT32, case_obj_enum, case_obj_int);

        const LocalVariable *cond = build_binop(st, AST_EXPR_EQ, caseobj->location, match_obj_int, case_obj_int, boolType);
        CfBlock *then = add_block(st);
        CfBlock *otherwise = add_block(st);

        add_jump(st, cond, then, otherwise, then);
        build_body(st, &match_stmt->cases[i].body);
        add_jump(st, NULL, done, done, otherwise);
    }
    }

    build_body(st, &match_stmt->case_underscore);
    add_jump(st, NULL, done, done, done);
}

static void build_statement(struct State *st, const AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_IF:
        build_if_statement(st, &stmt->data.ifstatement);
        break;

    case AST_STMT_ASSERT:
        build_assert(st, stmt->location, &stmt->data.assertion);
        break;

    case AST_STMT_PASS:
        break;

    case AST_STMT_WHILE:
        build_loop(
            st, NULL, &stmt->data.whileloop.condition, NULL,
            &stmt->data.whileloop.body);
        break;

    case AST_STMT_FOR:
        build_loop(
            st, stmt->data.forloop.init, &stmt->data.forloop.cond, stmt->data.forloop.incr,
            &stmt->data.forloop.body);
        break;

    case AST_STMT_MATCH:
        build_match_statament(st, &stmt->data.match);
        break;

    case AST_STMT_BREAK:
        if (!st->breakstack.len)
            fail(stmt->location, "'break' can only be used inside a loop");
        add_jump(st, NULL, End(st->breakstack)[-1], End(st->breakstack)[-1], NULL);
        break;

    case AST_STMT_CONTINUE:
        if (!st->continuestack.len)
            fail(stmt->location, "'continue' can only be used inside a loop");
        add_jump(st, NULL, End(st->continuestack)[-1], End(st->continuestack)[-1], NULL);
        break;

    case AST_STMT_ASSIGN:
        {
            const AstExpression *targetexpr = &stmt->data.assignment.target;
            const AstExpression *valueexpr = &stmt->data.assignment.value;
            const LocalVariable *target;

            if (targetexpr->kind == AST_EXPR_GET_VARIABLE
                && (target = find_local_var(st, targetexpr->data.varname)))
            {
                // avoid pointers to help simplify_cfg
                const LocalVariable *value = build_expression(st, valueexpr);
                add_unary_op(st, stmt->location, CF_VARCPY, value, target);
            } else {
                // TODO: is this evaluation order good?
                target = build_address_of_expression(st, targetexpr);
                const LocalVariable *value = build_expression(st, valueexpr);
                assert(target->type->kind == TYPE_POINTER);
                add_binary_op(st, stmt->location, CF_PTR_STORE, target, value, NULL);
            }
            break;
        }

    case AST_STMT_INPLACE_ADD:
    case AST_STMT_INPLACE_SUB:
    case AST_STMT_INPLACE_MUL:
    case AST_STMT_INPLACE_DIV:
    case AST_STMT_INPLACE_MOD:
    {
        const AstExpression *targetexpr = &stmt->data.assignment.target;
        const AstExpression *rhsexpr = &stmt->data.assignment.value;

        const LocalVariable *targetptr = build_address_of_expression(st, targetexpr);
        const LocalVariable *rhs = build_expression(st, rhsexpr);
        assert(targetptr->type->kind == TYPE_POINTER);
        const LocalVariable *oldvalue = add_local_var(st, targetptr->type->data.valuetype);
        add_unary_op(st, stmt->location, CF_PTR_LOAD, targetptr, oldvalue);
        enum AstExpressionKind op;
        switch(stmt->kind){
            case AST_STMT_INPLACE_ADD: op=AST_EXPR_ADD; break;
            case AST_STMT_INPLACE_SUB: op=AST_EXPR_SUB; break;
            case AST_STMT_INPLACE_MUL: op=AST_EXPR_MUL; break;
            case AST_STMT_INPLACE_DIV: op=AST_EXPR_DIV; break;
            case AST_STMT_INPLACE_MOD: op=AST_EXPR_MOD; break;
            default: assert(0);
        }
        const LocalVariable *newvalue = build_binop(st, op, stmt->location, oldvalue, rhs, targetptr->type->data.valuetype);
        add_binary_op(st, stmt->location, CF_PTR_STORE, targetptr, newvalue, NULL);
        break;
    }

    case AST_STMT_RETURN:
        if (stmt->data.returnvalue) {
            const LocalVariable *retvalue = build_expression(st, stmt->data.returnvalue);
            const LocalVariable *retvariable = find_local_var(st, "return");
            assert(retvariable);
            add_unary_op(st, stmt->location, CF_VARCPY, retvalue, retvariable);
        }
        st->current_block->iftrue = &st->cfg->end_block;
        st->current_block->iffalse = &st->cfg->end_block;
        st->current_block = add_block(st);  // an unreachable block
        break;

    case AST_STMT_DECLARE_LOCAL_VAR:
        if (stmt->data.vardecl.value) {
            const LocalVariable *v = find_local_var(st, stmt->data.vardecl.name);
            assert(v);
            const LocalVariable *cfvar = build_expression(st, stmt->data.vardecl.value);
            add_unary_op(st, stmt->location, CF_VARCPY, cfvar, v);
        }
        break;

    case AST_STMT_EXPRESSION_STATEMENT:
        build_expression(st, &stmt->data.expression);
        break;

    case AST_STMT_FUNCTION_DECLARE:
    case AST_STMT_FUNCTION_DEF:
    case AST_STMT_DECLARE_GLOBAL_VAR:
    case AST_STMT_DEFINE_GLOBAL_VAR:
    case AST_STMT_DEFINE_CLASS:
    case AST_STMT_DEFINE_ENUM:
        assert(0);
    }
}

static void build_body(struct State *st, const AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        build_statement(st, &body->statements[i]);
}

static CfGraph *build_function_or_method(
    struct State *st,
    const Type *selfclass,
    const char *name,
    const AstBody *body,
    bool public)
{
    assert(!st->fomtypes);
    assert(!st->cfg);

    for (const FunctionOrMethodTypes *f = st->filetypes->fomtypes.ptr; f < End(st->filetypes->fomtypes); f++) {
        if (!strcmp(f->signature.name, name) && get_self_class(&f->signature) == selfclass) {
            st->fomtypes = f;
            break;
        }
    }
    assert(st->fomtypes);

    st->cfg = calloc(1, sizeof *st->cfg);
    st->cfg->public = public;
    st->cfg->signature = copy_signature(&st->fomtypes->signature);
    for (LocalVariable **v = st->fomtypes->locals.ptr; v < End(st->fomtypes->locals); v++)
        Append(&st->cfg->locals, *v);
    Append(&st->cfg->all_blocks, &st->cfg->start_block);
    Append(&st->cfg->all_blocks, &st->cfg->end_block);
    st->current_block = &st->cfg->start_block;

    assert(st->breakstack.len == 0 && st->continuestack.len == 0);
    build_body(st, body);
    assert(st->breakstack.len == 0 && st->continuestack.len == 0);

    // Implicit return at the end of the function
    st->current_block->iftrue = &st->cfg->end_block;
    st->current_block->iffalse = &st->cfg->end_block;

    CfGraph *cfg = st->cfg;
    st->fomtypes = NULL;
    st->cfg = NULL;
    return cfg;
}

LLVMModuleRef codegen(const AstFile *ast, const FileTypes *ft, bool is_main_file)
{
    CfGraphFile result = { .filename = ast->path };
    struct State st = {
        .filetypes = filetypes,
        .module = LLVMModuleCreateWithName(cfgfile->filename),
        .builder = LLVMCreateBuilder(),
        .is_main_file = is_main_file,
    };

    LLVMSetTarget(st.module, get_target()->triple);
    LLVMSetDataLayout(st.module, get_target()->data_layout);

    for (GlobalVariable *v = ft->globals.ptr; v < End(ft->globals); v++) {
        LLVMTypeRef t = type_to_llvm(v->type);
        LLVMValueRef globalptr = LLVMAddGlobal(st.module, t, v->name);
        if (v->defined_in_current_file)
            LLVMSetInitializer(globalptr, LLVMConstNull(t));
    }

    for (int i = 0; i < ast->body.nstatements; i++) {
        const AstStatement *stmt = &ast->body.statements[i];
        if(stmt->kind == AST_STMT_FUNCTION_DEF) {
            CfGraph *g = build_function_or_method(&st, NULL, stmt->data.function.signature.name, &stmt->data.function.body, stmt->data.function.public);
            Append(&result.graphs, g);
        }

        if (stmt->kind == AST_STMT_DEFINE_CLASS) {
            Type *classtype = NULL;
            for (Type **t = filetypes->owned_types.ptr; t < End(filetypes->owned_types); t++) {
                if (!strcmp((*t)->name, stmt->data.classdef.name)) {
                    classtype = *t;
                    break;
                }
            }
            assert(classtype);

            for (AstClassMember *m = stmt->data.classdef.members.ptr; m < End(stmt->data.classdef.members); m++) {
                if (m->kind == AST_CLASSMEMBER_METHOD) {
                    CfGraph *g = build_function_or_method(&st, classtype, m->data.method.signature.name, &m->data.method.body, true);
                    Append(&result.graphs, g);
                }
            }
        }
    }

    free(st.breakstack.ptr);
    free(st.continuestack.ptr);
    return result;
}
