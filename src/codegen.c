#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include "jou_compiler.h"
#include "util.h"

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
            LLVMTypeRef *elems = malloc(sizeof(elems[0]) * n);  // NOLINT
            for (int i = 0; i < n; i++) {
                // Treat all pointers inside structs as if they were void*.
                // This allows structs to contain pointers to themselves.
                if (type->data.classdata.fields.ptr[i].type->kind == TYPE_POINTER)
                    elems[i] = codegen_type(voidPtrType);
                else
                    elems[i] = codegen_type(type->data.classdata.fields.ptr[i].type);
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
    LLVMValueRef varptr = get_pointer_to_local_var(st, cfvar);
    return LLVMBuildLoad(st->builder, varptr, cfvar->name);
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

    LLVMTypeRef *argtypes = malloc(sig->nargs * sizeof(argtypes[0]));  // NOLINT
    for (int i = 0; i < sig->nargs; i++)
        argtypes[i] = codegen_type(sig->argtypes[i]);

    LLVMTypeRef returntype;
    if (sig->returntype == NULL)
        returntype = LLVMVoidType();
    else
        returntype = codegen_type(sig->returntype);

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

static LLVMValueRef codegen_call(const struct State *st, const Signature *sig, LLVMValueRef *args, int nargs)
{
    LLVMValueRef function = codegen_function_or_method_decl(st, sig);
    assert(function);
    assert(LLVMGetTypeKind(LLVMTypeOf(function)) == LLVMPointerTypeKind);
    LLVMTypeRef function_type = LLVMGetElementType(LLVMTypeOf(function));
    assert(LLVMGetTypeKind(function_type) == LLVMFunctionTypeKind);

    char debug_name[100] = {0};
    if (LLVMGetTypeKind(LLVMGetReturnType(function_type)) != LLVMVoidTypeKind)
        snprintf(debug_name, sizeof debug_name, "%s_return_value", sig->name);

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

static LLVMValueRef build_signed_mod(LLVMBuilderRef builder, LLVMValueRef lhs, LLVMValueRef rhs, const char *ignored)
{
    (void)ignored;  // for compatibility with llvm functions

    // Jou's % operator ensures that a%b has same sign as b:
    // jou_mod(a, b) = llvm_mod(llvm_mod(a, b) + b, b)
    LLVMValueRef llmod = LLVMBuildSRem(builder, lhs, rhs, "smod_tmp");
    LLVMValueRef sum = LLVMBuildAdd(builder, llmod, rhs, "smod_tmp");
    return LLVMBuildSRem(builder, sum, rhs, "smod");
}

static LLVMValueRef build_signed_div(LLVMBuilderRef builder, LLVMValueRef lhs, LLVMValueRef rhs, const char *ignored)
{
    (void)ignored;  // for compatibility with llvm functions
    /*
    LLVM's provides two divisions. One truncates, the other is an "exact div"
    that requires there is no remainder. Jou uses floor division which is
    neither of the two, but is quite easy to implement:

        floordiv(a, b) = exact_div(a - jou_mod(a, b), b)
    */
    LLVMValueRef top = LLVMBuildSub(builder, lhs, build_signed_mod(builder, lhs, rhs, NULL), "sdiv_tmp");
    return LLVMBuildExactSDiv(builder, top, rhs, "sdiv");
}

static LLVMValueRef build_num_operation(
    LLVMBuilderRef builder,
    LLVMValueRef lhs,
    LLVMValueRef rhs,
    /*
    Many number operations are not the same for signed and unsigned integers.
    Signed division example with 8 bits: 255 / 2 = (-1) / 2 = 0
    Unsigned division example with 8 bits: 255 / 2 = 127
    */
    const Type *t,
    LLVMValueRef (*signedfn)(LLVMBuilderRef,LLVMValueRef,LLVMValueRef,const char*),
    LLVMValueRef (*unsignedfn)(LLVMBuilderRef,LLVMValueRef,LLVMValueRef,const char*),
    LLVMValueRef (*floatfn)(LLVMBuilderRef,LLVMValueRef,LLVMValueRef,const char*))
{
    switch(t->kind) {
        case TYPE_FLOATING_POINT: return floatfn(builder, lhs, rhs, "float_op");
        case TYPE_SIGNED_INTEGER: return signedfn(builder, lhs, rhs, "signed_op");
        case TYPE_UNSIGNED_INTEGER: return unsignedfn(builder, lhs, rhs, "unsigned_op");
        default: assert(0);
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
        case CF_SIZEOF: setdest(LLVMBuildTrunc(st->builder, LLVMSizeOf(codegen_type(ins->data.type)), LLVMIntType(sizeof(size_t)*8), "asd")); break;
        case CF_ADDRESS_OF_LOCAL_VAR: setdest(get_pointer_to_local_var(st, ins->operands[0])); break;
        case CF_ADDRESS_OF_GLOBAL_VAR: setdest(LLVMGetNamedGlobal(st->module, ins->data.globalname)); break;
        case CF_PTR_LOAD: setdest(LLVMBuildLoad(st->builder, getop(0), "ptr_load")); break;
        case CF_PTR_STORE: LLVMBuildStore(st->builder, getop(1), getop(0)); break;
        case CF_PTR_EQ:
            {
                LLVMValueRef lhsint = LLVMBuildPtrToInt(st->builder, getop(0), LLVMInt64Type(), "ptreq_lhs");
                LLVMValueRef rhsint = LLVMBuildPtrToInt(st->builder, getop(1), LLVMInt64Type(), "ptreq_rhs");
                setdest(LLVMBuildICmp(st->builder, LLVMIntEQ, lhsint, rhsint, "ptr_eq"));
            }
            break;
        case CF_PTR_CLASS_FIELD:
            {
                const Type *classtype = ins->operands[0]->type->data.valuetype;
                const struct ClassField *f = classtype->data.classdata.fields.ptr;
                int i = 0;
                while (strcmp(f->name, ins->data.fieldname)) {
                    f++;
                    i++;
                }

                LLVMValueRef val = LLVMBuildStructGEP2(st->builder, codegen_type(classtype), getop(0), i, ins->data.fieldname);
                if (f->type->kind == TYPE_POINTER) {
                    // We lied to LLVM that the struct member is i8*, so that we can do self-referencing types
                    val = LLVMBuildBitCast(st->builder, val, LLVMPointerType(codegen_type(f->type),0), "struct_member_i8_hack");
                }
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
            {
                LLVMValueRef index = getop(1);
                if (ins->operands[1]->type->kind == TYPE_UNSIGNED_INTEGER) {
                    // https://github.com/Akuli/jou/issues/48
                    // Apparently the default is to interpret indexes as signed.
                    index = LLVMBuildZExt(st->builder, index, LLVMInt64Type(), "ptr_add_int_implicit_cast");
                }
                setdest(LLVMBuildGEP(st->builder, getop(0), &index, 1, "ptr_add_int"));
            }
            break;
        case CF_NUM_CAST:
            {
                const Type *from = ins->operands[0]->type;
                const Type *to = ins->destvar->type;
                assert(is_number_type(from) && is_number_type(to));

                if (is_integer_type(from) && is_integer_type(to)) {
                    if (from->data.width_in_bits < to->data.width_in_bits) {
                        if (from->kind == TYPE_SIGNED_INTEGER) {
                            // example: signed 8-bit 0xFF --> 16-bit 0xFFFF
                            setdest(LLVMBuildSExt(st->builder, getop(0), codegen_type(to), "int_cast"));
                        } else {
                            // example: unsigned 8-bit 0xFF --> 16-bit 0x00FF
                            setdest(LLVMBuildZExt(st->builder, getop(0), codegen_type(to), "int_cast"));
                        }
                    } else if (from->data.width_in_bits > to->data.width_in_bits) {
                        setdest(LLVMBuildTrunc(st->builder, getop(0), codegen_type(to), "int_cast"));
                    } else {
                        // same size, LLVM doesn't distinguish signed and unsigned integer types
                        setdest(getop(0));
                    }
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

        case CF_NUM_ADD: setdest(build_num_operation(st->builder, getop(0), getop(1), ins->operands[0]->type, LLVMBuildAdd, LLVMBuildAdd, LLVMBuildFAdd)); break;
        case CF_NUM_SUB: setdest(build_num_operation(st->builder, getop(0), getop(1), ins->operands[0]->type, LLVMBuildSub, LLVMBuildSub, LLVMBuildFSub)); break;
        case CF_NUM_MUL: setdest(build_num_operation(st->builder, getop(0), getop(1), ins->operands[0]->type, LLVMBuildMul, LLVMBuildMul, LLVMBuildFMul)); break;
        case CF_NUM_DIV: setdest(build_num_operation(st->builder, getop(0), getop(1), ins->operands[0]->type, build_signed_div, LLVMBuildUDiv, LLVMBuildFDiv)); break;
        case CF_NUM_MOD: setdest(build_num_operation(st->builder, getop(0), getop(1), ins->operands[0]->type, build_signed_mod, LLVMBuildURem, LLVMBuildFRem)); break;

        case CF_NUM_EQ:
            if (is_integer_type(ins->operands[0]->type))
                setdest(LLVMBuildICmp(st->builder, LLVMIntEQ, getop(0), getop(1), "num_eq"));
            else
                setdest(LLVMBuildFCmp(st->builder, LLVMRealOEQ, getop(0), getop(1), "num_eq"));
            break;
        case CF_NUM_LT:
            if (is_integer_type(ins->operands[0]->type))
                // TODO: unsigned less than
                setdest(LLVMBuildICmp(st->builder, LLVMIntSLT, getop(0), getop(1), "num_lt"));
            else
                // TODO: signed less than
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

#ifdef _WIN32
static void codegen_call_to_the_special_startup_function(const struct State *st)
{
    LLVMTypeRef functype = LLVMFunctionType(LLVMVoidType(), NULL, 0, false);
    LLVMValueRef func = LLVMAddFunction(st->module, "_jou_windows_startup", functype);
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

#ifdef _WIN32
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
            if (return_value)
                LLVMBuildRet(st->builder, LLVMBuildLoad(st->builder, return_value, "return_value"));
            else if (cfg->signature.returntype)  // "return" variable was deleted as unused
                LLVMBuildUnreachable(st->builder);
            else
                LLVMBuildRetVoid(st->builder);
        } else {
            assert((*b)->iftrue && (*b)->iffalse);
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

    for (GlobalVariable **v = ft->globals.ptr; v < End(ft->globals); v++) {
        LLVMTypeRef t = codegen_type((*v)->type);
        LLVMValueRef globalptr = LLVMAddGlobal(st.module, t, (*v)->name);
        if ((*v)->defined_in_current_file)
            LLVMSetInitializer(globalptr, LLVMGetUndef(t));
    }

    for (CfGraph **g = cfgfile->graphs.ptr; g < End(cfgfile->graphs); g++)
        codegen_function_or_method_def(&st, *g);

    LLVMDisposeBuilder(st.builder);
    return st.module;
}
