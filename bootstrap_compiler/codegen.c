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

static LLVMTypeRef codegen_type(const Type *type)
{
    switch(type->kind) {
    case TYPE_ARRAY:
        return LLVMArrayType(codegen_type(type->data.array.membertype), type->data.array.len);
    case TYPE_POINTER:
        return LLVMPointerType(codegen_type(type->data.valuetype), 0);
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

            LLVMTypeRef *flat_elems = malloc(sizeof(flat_elems[0]) * n);  // NOLINT
            for (int i = 0; i < n; i++) {
                // Treat all pointers inside structs as if they were void*.
                // This allows structs to contain pointers to themselves.
                if (type->data.classdata.fields.ptr[i].type->kind == TYPE_POINTER)
                    flat_elems[i] = codegen_type(voidPtrType);
                else
                    flat_elems[i] = codegen_type(type->data.classdata.fields.ptr[i].type);
            }

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

struct State {
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LocalVariable **cfvars, **cfvars_end;
    // All local variables are represented as pointers to stack space, even
    // if they are never reassigned. LLVM will optimize the mess.
    LLVMValueRef *llvm_locals;
};

static LLVMValueRef get_pointer_to_local_var(const struct State *st, const LocalVariable *cfvar)
{
    assert(cfvar);
    /*
    The loop below looks stupid, but I don't see a better alternative.

    I want CFG variables to be used as pointers, so that it's easy to refer to a
    variable's name and type, check if you have the same variable, etc. But I
    can't make a List of variables when building CFG, because existing variable
    pointers would become invalid as the list grows. The solution is to allocate
    each variable separately when building the CFG.

    Another idea I had was to count the number of variables needed beforehand,
    so I wouldn't need to ever resize the list of variables, but the CFG building
    is already complicated enough as is.
    */
    for (LocalVariable **v = st->cfvars; v < st->cfvars_end; v++)
        if (*v == cfvar)
            return st->llvm_locals[v - st->cfvars];
    assert(0);
}

static LLVMValueRef get_local_var(const struct State *st, const LocalVariable *cfvar)
{
    assert(cfvar);
    for (LocalVariable **v = st->cfvars; v < st->cfvars_end; v++) {
        if (*v == cfvar) {
            LLVMValueRef varptr = st->llvm_locals[v - st->cfvars];
            LLVMTypeRef vartype = codegen_type((*v)->type);
            return LLVMBuildLoad2(st->builder, vartype, varptr, cfvar->name);
        }
    }
    assert(0);
}

static void set_local_var(const struct State *st, const LocalVariable *cfvar, LLVMValueRef value)
{
    assert(cfvar);
    for (LocalVariable **v = st->cfvars; v < st->cfvars_end; v++) {
        if (*v == cfvar) {
            LLVMBuildStore(st->builder, value, st->llvm_locals[v - st->cfvars]);
            return;
        }
    }
    assert(0);
}

static LLVMTypeRef codegen_function_type(const Signature *sig)
{
    LLVMTypeRef *argtypes = malloc(sig->nargs * sizeof(argtypes[0]));  // NOLINT
    for (int i = 0; i < sig->nargs; i++)
        argtypes[i] = codegen_type(sig->argtypes[i]);

    LLVMTypeRef returntype;
    // TODO: tell llvm, if we know a function is noreturn ?
    if (sig->returntype == NULL)  // "-> noreturn" or "-> None"
        returntype = LLVMVoidType();
    else
        returntype = codegen_type(sig->returntype);

    return LLVMFunctionType(returntype, argtypes, sig->nargs, sig->takes_varargs);
}

static LLVMValueRef codegen_function_or_method_decl(const struct State *st, const Signature *sig)
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

    LLVMTypeRef functype = codegen_function_type(sig);
    return LLVMAddFunction(st->module, fullname, functype);
}

static LLVMValueRef codegen_call(const struct State *st, const Signature *sig, LLVMValueRef *args, int nargs)
{
    char debug_name[100] = {0};
    if (sig->returntype != NULL)
        snprintf(debug_name, sizeof debug_name, "%s_return_value", sig->name);

    LLVMValueRef function = codegen_function_or_method_decl(st, sig);
    LLVMTypeRef function_type = codegen_function_type(sig);
    return LLVMBuildCall2(st->builder, function_type, function, args, nargs, debug_name);
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

static LLVMValueRef codegen_constant(const struct State *st, const Constant *c)
{
    switch(c->kind) {
    case CONSTANT_BOOL:
        return LLVMConstInt(LLVMInt1Type(), c->data.boolean, false);
    case CONSTANT_INTEGER:
        return LLVMConstInt(codegen_type(type_of_constant(c)), c->data.integer.value, c->data.integer.is_signed);
    case CONSTANT_FLOAT:
    case CONSTANT_DOUBLE:
        return LLVMConstRealOfString(codegen_type(type_of_constant(c)), c->data.double_or_float_text);
    case CONSTANT_NULL:
        return LLVMConstNull(codegen_type(voidPtrType));
    case CONSTANT_STRING:
        return make_a_string_constant(st, c->data.str);
    case CONSTANT_ENUM_MEMBER:
        return LLVMConstInt(LLVMInt32Type(), c->data.enum_member.memberidx, false);
    }
    assert(0);
}

static LLVMValueRef codegen_special_constant(const char *name)
{
    int v = get_special_constant(name);
    assert(v != -1);
    return LLVMConstInt(LLVMInt1Type(), v, false);
}

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

static void codegen_arithmetic_instruction(const struct State *st, const CfInstruction *ins)
{
    LLVMValueRef lhs = get_local_var(st, ins->operands[0]);
    LLVMValueRef rhs = get_local_var(st, ins->operands[1]);

    assert(ins->operands[0]->type == ins->operands[1]->type);
    const Type *type = ins->operands[0]->type;

    switch (type->kind) {
        case TYPE_FLOATING_POINT: {
            switch (ins->kind) {
                case CF_NUM_ADD: set_local_var(st, ins->destvar, LLVMBuildFAdd(st->builder, lhs, rhs, "float_sum")); break;
                case CF_NUM_SUB: set_local_var(st, ins->destvar, LLVMBuildFSub(st->builder, lhs, rhs, "float_diff")); break;
                case CF_NUM_MUL: set_local_var(st, ins->destvar, LLVMBuildFMul(st->builder, lhs, rhs, "float_prod")); break;
                case CF_NUM_DIV: set_local_var(st, ins->destvar, LLVMBuildFDiv(st->builder, lhs, rhs, "float_quot")); break;
                case CF_NUM_MOD: set_local_var(st, ins->destvar, LLVMBuildFRem(st->builder, lhs, rhs, "float_mod")); break;
                default: assert(0);
            }
            break;
        }

        case TYPE_SIGNED_INTEGER: {
            switch (ins->kind) {
                case CF_NUM_ADD: set_local_var(st, ins->destvar, LLVMBuildAdd(st->builder, lhs, rhs, "int_sum")); break;
                case CF_NUM_SUB: set_local_var(st, ins->destvar, LLVMBuildSub(st->builder, lhs, rhs, "int_diff")); break;
                case CF_NUM_MUL: set_local_var(st, ins->destvar, LLVMBuildMul(st->builder, lhs, rhs, "int_prod")); break;
                case CF_NUM_DIV: set_local_var(st, ins->destvar, build_signed_div(st->builder, lhs, rhs)); break;
                case CF_NUM_MOD: set_local_var(st, ins->destvar, build_signed_mod(st->builder, lhs, rhs)); break;
                default: assert(0);
            }
            break;
        }

        case TYPE_UNSIGNED_INTEGER: {
            switch (ins->kind) {
                case CF_NUM_ADD: set_local_var(st, ins->destvar, LLVMBuildAdd(st->builder, lhs, rhs, "uint_sum")); break;
                case CF_NUM_SUB: set_local_var(st, ins->destvar, LLVMBuildSub(st->builder, lhs, rhs, "uint_diff")); break;
                case CF_NUM_MUL: set_local_var(st, ins->destvar, LLVMBuildMul(st->builder, lhs, rhs, "uint_prod")); break;
                case CF_NUM_DIV: set_local_var(st, ins->destvar, LLVMBuildUDiv(st->builder, lhs, rhs, "uint_quot")); break;
                case CF_NUM_MOD: set_local_var(st, ins->destvar, LLVMBuildURem(st->builder, lhs, rhs, "uint_mod")); break;
                default: assert(0);
            }
            break;
        }

        default:
            assert(0);
    }
}

static void codegen_instruction(const struct State *st, const CfInstruction *ins)
{
#define setdest(val) set_local_var(st, ins->destvar, (val))
#define get(var) get_local_var(st, (var))
#define getop(i) get(ins->operands[(i)])

    switch(ins->kind) {
        case CF_CALL:
            {
                LLVMValueRef *args = malloc(ins->noperands * sizeof(args[0]));  // NOLINT
                for (int i = 0; i < ins->noperands; i++)
                    args[i] = getop(i);
                LLVMValueRef return_value = codegen_call(st, &ins->data.signature, args, ins->noperands);
                if (ins->destvar)
                    setdest(return_value);
                free(args);
            }
            break;
        case CF_CONSTANT: setdest(codegen_constant(st, &ins->data.constant)); break;
        case CF_SPECIAL_CONSTANT: setdest(codegen_special_constant(ins->data.scname)); break;
        case CF_STRING_ARRAY: setdest(LLVMConstString(ins->data.strarray.str, ins->data.strarray.len, true)); break;
        case CF_SIZEOF: setdest(LLVMSizeOf(codegen_type(ins->data.type))); break;
        case CF_ADDRESS_OF_LOCAL_VAR: setdest(get_pointer_to_local_var(st, ins->operands[0])); break;
        case CF_ADDRESS_OF_GLOBAL_VAR: setdest(LLVMGetNamedGlobal(st->module, ins->data.globalname)); break;
        case CF_PTR_LOAD: setdest(LLVMBuildLoad2(st->builder, codegen_type(ins->operands[0]->type->data.valuetype), getop(0), "ptr_load")); break;
        case CF_PTR_STORE: LLVMBuildStore(st->builder, getop(1), getop(0)); break;
        case CF_PTR_TO_INT64: setdest(LLVMBuildPtrToInt(st->builder, getop(0), LLVMInt64Type(), "ptr_as_long")); break;
        case CF_INT64_TO_PTR: setdest(LLVMBuildIntToPtr(st->builder, getop(0), codegen_type(ins->destvar->type), "long_as_ptr")); break;
        case CF_PTR_CLASS_FIELD:
            {
                const Type *classtype = ins->operands[0]->type->data.valuetype;
                const struct ClassField *f = classtype->data.classdata.fields.ptr;
                while (strcmp(f->name, ins->data.fieldname))
                    f++;

                LLVMValueRef val = LLVMBuildStructGEP2(st->builder, codegen_type(classtype), getop(0), f->union_id, ins->data.fieldname);
                // This cast is needed in two cases:
                //  * All pointers are i8* in structs so we can do self-referencing classes.
                //  * This is how unions work.
                val = LLVMBuildBitCast(st->builder, val, LLVMPointerType(codegen_type(f->type),0), "struct_member_cast");
                setdest(val);
            }
            break;
        case CF_PTR_MEMSET_TO_ZERO:
            {
                LLVMValueRef size = LLVMSizeOf(codegen_type(ins->operands[0]->type->data.valuetype));
                LLVMBuildMemSet(st->builder, getop(0), LLVMConstInt(LLVMInt8Type(), 0, false), size, 0);
            }
            break;
        case CF_PTR_ADD_INT:
            setdest(LLVMBuildGEP2(st->builder, codegen_type(ins->operands[0]->type->data.valuetype), getop(0), (LLVMValueRef[]){getop(1)}, 1, "ptr_add_int"));
            break;
        case CF_NUM_CAST:
            {
                const Type *from = ins->operands[0]->type;
                const Type *to = ins->destvar->type;
                assert(is_number_type(from) && is_number_type(to));

                if (is_integer_type(from) && is_integer_type(to)) {
                    // Examples:
                    //  signed 8-bit 0xFF (-1) --> 16-bit 0xFFFF (-1 or max value)
                    //  unsigned 8-bit 0xFF (255) --> 16-bit 0x00FF (255)
                    setdest(LLVMBuildIntCast2(st->builder, getop(0), codegen_type(to), from->kind == TYPE_SIGNED_INTEGER, "int_cast"));
                } else if (is_integer_type(from) && to->kind == TYPE_FLOATING_POINT) {
                    // integer --> double / float
                    if (from->kind == TYPE_SIGNED_INTEGER)
                        setdest(LLVMBuildSIToFP(st->builder, getop(0), codegen_type(to), "cast"));
                    else
                        setdest(LLVMBuildUIToFP(st->builder, getop(0), codegen_type(to), "cast"));
                } else if (from->kind == TYPE_FLOATING_POINT && is_integer_type(to)) {
                    if (to->kind == TYPE_SIGNED_INTEGER)
                        setdest(LLVMBuildFPToSI(st->builder, getop(0), codegen_type(to), "cast"));
                    else
                        setdest(LLVMBuildFPToUI(st->builder, getop(0), codegen_type(to), "cast"));
                } else if (from->kind == TYPE_FLOATING_POINT && to->kind == TYPE_FLOATING_POINT) {
                    setdest(LLVMBuildFPCast(st->builder, getop(0), codegen_type(to), "cast"));
                } else {
                    assert(0);
                }
            }
            break;

        case CF_BOOL_NEGATE: setdest(LLVMBuildXor(st->builder, getop(0), LLVMConstInt(LLVMInt1Type(), 1, false), "bool_negate")); break;
        case CF_PTR_CAST: setdest(LLVMBuildBitCast(st->builder, getop(0), codegen_type(ins->destvar->type), "ptr_cast")); break;

        // various no-ops
        case CF_VARCPY:
        case CF_INT32_TO_ENUM:
        case CF_ENUM_TO_INT32:
            setdest(getop(0));
            break;

        case CF_NUM_ADD:
        case CF_NUM_SUB:
        case CF_NUM_MUL:
        case CF_NUM_DIV:
        case CF_NUM_MOD:
            codegen_arithmetic_instruction(st, ins);
            break;

        case CF_NUM_EQ:
            if (is_integer_type(ins->operands[0]->type))
                setdest(LLVMBuildICmp(st->builder, LLVMIntEQ, getop(0), getop(1), "num_eq"));
            else
                setdest(LLVMBuildFCmp(st->builder, LLVMRealOEQ, getop(0), getop(1), "num_eq"));
            break;
        case CF_NUM_LT:
            if (ins->operands[0]->type->kind == TYPE_UNSIGNED_INTEGER && ins->operands[1]->type->kind == TYPE_UNSIGNED_INTEGER)
                setdest(LLVMBuildICmp(st->builder, LLVMIntULT, getop(0), getop(1), "num_lt"));
            else if (is_integer_type(ins->operands[0]->type) && is_integer_type(ins->operands[1]->type))
                setdest(LLVMBuildICmp(st->builder, LLVMIntSLT, getop(0), getop(1), "num_lt"));
            else
                setdest(LLVMBuildFCmp(st->builder, LLVMRealOLT, getop(0), getop(1), "num_lt"));
            break;
    }

#undef setdest
#undef get
#undef getop
}

static int find_block(const CfGraph *cfg, const CfBlock *b)
{
    for (int i = 0; i < cfg->all_blocks.len; i++)
        if (cfg->all_blocks.ptr[i] == b)
            return i;
    assert(0);
}

#if defined(_WIN32) || defined(__APPLE__) || defined(__NetBSD__)
static void codegen_call_to_the_special_startup_function(const struct State *st)
{
    LLVMTypeRef functype = LLVMFunctionType(LLVMVoidType(), NULL, 0, false);
    LLVMValueRef func = LLVMAddFunction(st->module, "_jou_startup", functype);
    LLVMBuildCall2(st->builder, functype, func, NULL, 0, "");
}
#endif

static void codegen_function_or_method_def(struct State *st, const CfGraph *cfg)
{
    st->cfvars = cfg->locals.ptr;
    st->cfvars_end = End(cfg->locals);
    st->llvm_locals = malloc(sizeof(st->llvm_locals[0]) * cfg->locals.len); // NOLINT

    LLVMValueRef llvm_func = codegen_function_or_method_decl(st, &cfg->signature);

    LLVMBasicBlockRef *blocks = malloc(sizeof(blocks[0]) * cfg->all_blocks.len); // NOLINT
    for (int i = 0; i < cfg->all_blocks.len; i++) {
        char name[50];
        sprintf(name, "block%d", i);
        blocks[i] = LLVMAppendBasicBlock(llvm_func, name);
    }

    assert(cfg->all_blocks.ptr[0] == &cfg->start_block);
    LLVMPositionBuilderAtEnd(st->builder, blocks[0]);

#if defined(_WIN32) || defined(__APPLE__) || defined(__NetBSD__)
    if (!get_self_class(&cfg->signature) && !strcmp(cfg->signature.name, "main"))
        codegen_call_to_the_special_startup_function(st);
#endif

    // Allocate stack space for local variables at start of function.
    LLVMValueRef return_value = NULL;
    for (int i = 0; i < cfg->locals.len; i++) {
        LocalVariable *v = cfg->locals.ptr[i];
        st->llvm_locals[i] = LLVMBuildAlloca(st->builder, codegen_type(v->type), v->name);
        if (!strcmp(v->name, "return"))
            return_value = st->llvm_locals[i];
    }

    // Place arguments into the first n local variables.
    for (int i = 0; i < cfg->signature.nargs; i++)
        set_local_var(st, cfg->locals.ptr[i], LLVMGetParam(llvm_func, i));

    for (CfBlock **b = cfg->all_blocks.ptr; b <End(cfg->all_blocks); b++) {
        LLVMPositionBuilderAtEnd(st->builder, blocks[b - cfg->all_blocks.ptr]);

        for (CfInstruction *ins = (*b)->instructions.ptr; ins < End((*b)->instructions); ins++)
            codegen_instruction(st, ins);

        if (*b == &cfg->end_block) {
            assert((*b)->instructions.len == 0);
            // The "return" variable may have been deleted as unused.
            // In that case return_value is NULL but signature.returntype isn't.
            if (return_value)
                LLVMBuildRet(st->builder, LLVMBuildLoad2(st->builder, codegen_type(cfg->signature.returntype), return_value, "return_value"));
            else if (cfg->signature.returntype || cfg->signature.is_noreturn)
                LLVMBuildUnreachable(st->builder);
            else
                LLVMBuildRetVoid(st->builder);
        } else if ((*b)->iftrue && (*b)->iffalse) {
            if ((*b)->iftrue == (*b)->iffalse) {
                LLVMBuildBr(st->builder, blocks[find_block(cfg, (*b)->iftrue)]);
            } else {
                assert((*b)->branchvar);
                LLVMBuildCondBr(
                    st->builder,
                    get_local_var(st, (*b)->branchvar),
                    blocks[find_block(cfg, (*b)->iftrue)],
                    blocks[find_block(cfg, (*b)->iffalse)]);
            }
        } else if (!(*b)->iftrue && !(*b)->iffalse) {
            LLVMBuildUnreachable(st->builder);
        } else {
            assert(0);
        }
    }

    free(blocks);
    free(st->llvm_locals);
}

LLVMModuleRef codegen(const CfGraphFile *cfgfile, const FileTypes *ft)
{
    struct State st = {
        .module = LLVMModuleCreateWithName(cfgfile->filename),
        .builder = LLVMCreateBuilder(),
    };

    LLVMSetTarget(st.module, get_target()->triple);
    LLVMSetDataLayout(st.module, get_target()->data_layout);

    for (GlobalVariable *v = ft->globals.ptr; v < End(ft->globals); v++) {
        LLVMTypeRef t = codegen_type(v->type);
        LLVMValueRef globalptr = LLVMAddGlobal(st.module, t, v->name);
        if (v->defined_in_current_file)
            LLVMSetInitializer(globalptr, LLVMConstNull(t));
    }

    for (CfGraph **g = cfgfile->graphs.ptr; g < End(cfgfile->graphs); g++)
        codegen_function_or_method_def(&st, *g);

    LLVMDisposeBuilder(st.builder);
    return st.module;
}
