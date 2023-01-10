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
    case TYPE_POINTER:
        return LLVMPointerType(codegen_type(type->data.valuetype), 0);
    case TYPE_VOID_POINTER:
        // just use i8* as here https://stackoverflow.com/q/36724399
        return LLVMPointerType(LLVMInt8Type(), 0);
    case TYPE_SIGNED_INTEGER:
    case TYPE_UNSIGNED_INTEGER:
        return LLVMIntType(type->data.width_in_bits);
    case TYPE_BOOL:
        return LLVMInt1Type();
    case TYPE_STRUCT:
        {
            int n = type->data.structfields.count;
            LLVMTypeRef *elems = malloc(sizeof(elems[0]) * n);  // NOLINT
            for (int i = 0; i < n; i++)
                elems[i] = codegen_type(&type->data.structfields.types[i]);
            LLVMTypeRef result = LLVMStructType(elems, type->data.structfields.count, false);
            free(elems);
            return result;
        }
    }
    assert(0);
}

struct State {
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    CfVariable **cfvars, **cfvars_end;
    // All local variables are represented as pointers to stack space, even
    // if they are never reassigned. LLVM will optimize the mess.
    LLVMValueRef *llvm_locals;
};

const char *get_name_for_llvm(const CfVariable *cfvar)
{
    if (cfvar->name[0] == '$')
        return cfvar->name + 1;
    return cfvar->name;
}

static LLVMValueRef get_pointer_to_local_var(const struct State *st, const CfVariable *cfvar)
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
    for (CfVariable **v = st->cfvars; v < st->cfvars_end; v++)
        if (*v == cfvar)
            return st->llvm_locals[v - st->cfvars];
    assert(0);
}

static LLVMValueRef get_local_var(const struct State *st, const CfVariable *cfvar)
{
    LLVMValueRef varptr = get_pointer_to_local_var(st, cfvar);
    return LLVMBuildLoad(st->builder, varptr, get_name_for_llvm(cfvar));
}

static void set_local_var(const struct State *st, const CfVariable *cfvar, LLVMValueRef value)
{
    assert(cfvar);
    for (CfVariable **v = st->cfvars; v < st->cfvars_end; v++) {
        if (*v == cfvar) {
            LLVMBuildStore(st->builder, value, st->llvm_locals[v - st->cfvars]);
            return;
        }
    }
    assert(0);
}

static LLVMValueRef codegen_function_decl(const struct State *st, const Signature *sig)
{
    LLVMTypeRef *argtypes = malloc(sig->nargs * sizeof(argtypes[0]));  // NOLINT
    for (int i = 0; i < sig->nargs; i++)
        argtypes[i] = codegen_type(&sig->argtypes[i]);

    LLVMTypeRef returntype;
    if (sig->returntype == NULL)
        returntype = LLVMVoidType();
    else
        returntype = codegen_type(sig->returntype);

    LLVMTypeRef functype = LLVMFunctionType(returntype, argtypes, sig->nargs, sig->takes_varargs);
    free(argtypes);

    return LLVMAddFunction(st->module, sig->funcname, functype);
}

static LLVMValueRef codegen_call(const struct State *st, const char *funcname, LLVMValueRef *args, int nargs)
{
    LLVMValueRef function = LLVMGetNamedFunction(st->module, funcname);
    assert(function);
    assert(LLVMGetTypeKind(LLVMTypeOf(function)) == LLVMPointerTypeKind);
    LLVMTypeRef function_type = LLVMGetElementType(LLVMTypeOf(function));
    assert(LLVMGetTypeKind(function_type) == LLVMFunctionTypeKind);

    char debug_name[100] = {0};
    if (LLVMGetTypeKind(LLVMGetReturnType(function_type)) != LLVMVoidTypeKind)
        snprintf(debug_name, sizeof debug_name, "%s_return_value", funcname);

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
        return LLVMConstInt(codegen_type((Type[]){type_of_constant(c)}), c->data.integer.value, c->data.integer.is_signed);
    case CONSTANT_NULL:
        return LLVMConstNull(codegen_type(&voidPtrType));
    case CONSTANT_STRING:
        return make_a_string_constant(st, c->data.str);
    }
    assert(0);
}   

static void codegen_instruction(const struct State *st, const CfInstruction *ins)
{
#define setdest(val) set_local_var(st, ins->destvar, (val))
#define get(var) get_local_var(st, (var))
#define getop(i) get(ins->operands[(i)])

    const char *name = NULL;
    if (ins->destvar)
        name = get_name_for_llvm(ins->destvar);

    switch(ins->kind) {
        case CF_CALL:
            {
                LLVMValueRef *args = malloc(ins->noperands * sizeof(args[0]));  // NOLINT
                for (int i = 0; i < ins->noperands; i++)
                    args[i] = getop(i);
                LLVMValueRef return_value = codegen_call(st, ins->data.funcname, args, ins->noperands);
                if (ins->destvar)
                    setdest(return_value);
                free(args);
            }
            break;
        case CF_CONSTANT: setdest(codegen_constant(st, &ins->data.constant)); break;
        case CF_ADDRESS_OF_VARIABLE: setdest(get_pointer_to_local_var(st, ins->operands[0])); break;
        case CF_PTR_LOAD: setdest(LLVMBuildLoad(st->builder, getop(0), name)); break;
        case CF_PTR_STORE: LLVMBuildStore(st->builder, getop(1), getop(0)); break;
        case CF_PTR_EQ:
            {
                LLVMValueRef lhsint = LLVMBuildPtrToInt(st->builder, getop(0), LLVMInt64Type(), "ptreq_lhs");
                LLVMValueRef rhsint = LLVMBuildPtrToInt(st->builder, getop(1), LLVMInt64Type(), "ptreq_rhs");
                setdest(LLVMBuildICmp(st->builder, LLVMIntEQ, lhsint, rhsint, name));
            }
            break;
        case CF_PTR_STRUCT_FIELD:
            {
                const Type *structtype = ins->operands[0]->type.data.valuetype;
                int i = 0;
                while (strcmp(structtype->data.structfields.names[i], ins->data.fieldname))
                    i++;
                setdest(LLVMBuildStructGEP2(st->builder, codegen_type(structtype), getop(0), i, ins->data.fieldname));
            }
            break;
        case CF_BOOL_NEGATE: setdest(LLVMBuildXor(st->builder, getop(0), LLVMConstInt(LLVMInt1Type(), 1, false), name)); break;
        case CF_INT_SCAST_TO_BIGGER: setdest(LLVMBuildSExt(st->builder, getop(0), codegen_type(&ins->destvar->type), name)); break;
        case CF_INT_UCAST_TO_BIGGER: setdest(LLVMBuildZExt(st->builder, getop(0), codegen_type(&ins->destvar->type), name)); break;
        case CF_PTR_CAST: setdest(LLVMBuildBitCast(st->builder, getop(0), codegen_type(&ins->destvar->type), name)); break;
        case CF_INT_ADD: setdest(LLVMBuildAdd(st->builder, getop(0), getop(1), name)); break;
        case CF_INT_SUB: setdest(LLVMBuildSub(st->builder, getop(0), getop(1), name)); break;
        case CF_INT_MUL: setdest(LLVMBuildMul(st->builder, getop(0), getop(1), name)); break;
        case CF_INT_SDIV: setdest(LLVMBuildSDiv(st->builder, getop(0), getop(1), name)); break;
        case CF_INT_UDIV: setdest(LLVMBuildUDiv(st->builder, getop(0), getop(1), name)); break;
        case CF_INT_EQ: setdest(LLVMBuildICmp(st->builder, LLVMIntEQ, getop(0), getop(1), name)); break;
        // TODO: unsigned less-than
        case CF_INT_LT: setdest(LLVMBuildICmp(st->builder, LLVMIntSLT, getop(0), getop(1), name)); break;
        case CF_VARCPY: setdest(getop(0)); break;
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

static void codegen_function_def(struct State *st, const Signature *sig, const CfGraph *cfg)
{
    st->cfvars = cfg->variables.ptr;
    st->cfvars_end = End(cfg->variables);
    st->llvm_locals = malloc(sizeof(st->llvm_locals[0]) * cfg->variables.len); // NOLINT

    LLVMValueRef llvm_func = codegen_function_decl(st, sig);
    LLVMBasicBlockRef *blocks = malloc(sizeof(blocks[0]) * cfg->all_blocks.len); // NOLINT
    for (int i = 0; i < cfg->all_blocks.len; i++) {
        char name[50];
        sprintf(name, "block%d", i);
        blocks[i] = LLVMAppendBasicBlock(llvm_func, name);
    }

    assert(cfg->all_blocks.ptr[0] == &cfg->start_block);
    LLVMPositionBuilderAtEnd(st->builder, blocks[0]);

    // Allocate stack space for local variables at start of function.
    LLVMValueRef return_value = NULL;
    for (int i = 0; i < cfg->variables.len; i++) {
        CfVariable *v = cfg->variables.ptr[i];
        st->llvm_locals[i] = LLVMBuildAlloca(st->builder, codegen_type(&v->type), get_name_for_llvm(v));
        if (!strcmp(v->name, "return"))
            return_value = st->llvm_locals[i];
    }

    // Place arguments into the first n local variables.
    for (int i = 0; i < sig->nargs; i++)
        set_local_var(st, cfg->variables.ptr[i], LLVMGetParam(llvm_func, i));

    for (CfBlock **b = cfg->all_blocks.ptr; b <End(cfg->all_blocks); b++) {
        LLVMPositionBuilderAtEnd(st->builder, blocks[b - cfg->all_blocks.ptr]);

        for (CfInstruction *ins = (*b)->instructions.ptr; ins < End((*b)->instructions); ins++)
            codegen_instruction(st, ins);

        if (*b == &cfg->end_block) {
            assert((*b)->instructions.len == 0);
            if (return_value)
                LLVMBuildRet(st->builder, LLVMBuildLoad(st->builder, return_value, "return_value"));
            else if (sig->returntype)  // "return" variable was deleted as unused
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

LLVMModuleRef codegen(const CfGraphFile *cfgfile)
{
    struct State st = {
        .module = LLVMModuleCreateWithName(""),  // TODO: pass module name?
        .builder = LLVMCreateBuilder(),
    };
    LLVMSetSourceFileName(st.module, cfgfile->filename, strlen(cfgfile->filename));

    for (int i = 0; i < cfgfile->nfuncs; i++) {
        if (cfgfile->graphs[i])
            codegen_function_def(&st, &cfgfile->signatures[i], cfgfile->graphs[i]);
        else
            codegen_function_decl(&st, &cfgfile->signatures[i]);
    }

    LLVMDisposeBuilder(st.builder);
    return st.module;
}
