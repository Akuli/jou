#include "jou_compiler.h"

static int find_block_index(const struct CfGraph *cfg, const struct CfBlock *b)
{
    for (int i = 0; i < cfg->all_blocks.len; i++)
        if (cfg->all_blocks.ptr[i] == b)
            return i;
    assert(0);
}

static int find_var_index(const struct CfGraph *cfg, const struct CfVariable *v)
{
    for (int i = 0; i < cfg->variables.len; i++)
        if (cfg->variables.ptr[i] == v)
            return i;
    assert(0);
}

/*
Figure out whether a boolean is true or false at the given instruction. Return values:
    1   surely true
    0   surely false
    -1  don't know

The instruction given as argument can point just beyond the last instruction in the block.

Currently this does not look at other blocks leading into this block.
*/
static int determine_bool_value(const struct CfVariable *var, const struct CfBlock *block, const struct CfInstruction *ins)
{
    assert(same_type(&var->type, &boolType));
    if (!var->analyzable || block->instructions.len == 0)
        return -1;

    while (--ins >= block->instructions.ptr) {
        if (ins->destvar == var) {
            switch(ins->kind) {
                case CF_VARCPY:
                    var = ins->data.operands[0];
                    if (!var->analyzable)
                        return -1;
                    continue;
                case CF_TRUE: return 1;
                case CF_FALSE: return 0;
                default: return -1;
            }
        }
    }

    return -1;
}

static void remove_unreachable_blocks(struct CfGraph *cfg, bool *did_something)
{
    bool *reachable = calloc(sizeof(reachable[0]), cfg->all_blocks.len);
    List(int) todo = {0};
    Append(&todo, 0);  // start block

    while (todo.len != 0) {
        int i = Pop(&todo);
        if (reachable[i])
            continue;
        reachable[i] = true;

        if (cfg->all_blocks.ptr[i] != &cfg->end_block) {
            Append(&todo, find_block_index(cfg, cfg->all_blocks.ptr[i]->iftrue));
            Append(&todo, find_block_index(cfg, cfg->all_blocks.ptr[i]->iffalse));
        }
    }

    for (int i = cfg->all_blocks.len - 1; i >= 0; i--) {
        if (reachable[i] || cfg->all_blocks.ptr[i] == &cfg->end_block)
            continue;

        // found unreachable block that can be removed
        if (cfg->all_blocks.ptr[i]->instructions.len > 0)
            show_warning(
                cfg->all_blocks.ptr[i]->instructions.ptr[0].location,
                "this code will never run");
        free_control_flow_graph_block(cfg, cfg->all_blocks.ptr[i]);
        cfg->all_blocks.ptr[i] = Pop(&cfg->all_blocks);
        *did_something = true;
    }

    free(reachable);
    free(todo.ptr);
}

static void remove_unused_variables(struct CfGraph *cfg, bool *did_something)
{
    char *used = calloc(1, cfg->variables.len);

    for (struct CfBlock **b = cfg->all_blocks.ptr; b < End(cfg->all_blocks); b++) {
        for (struct CfInstruction *ins = (*b)->instructions.ptr; ins < End((*b)->instructions); ins++) {
            if (ins->destvar)
                used[find_var_index(cfg, ins->destvar)] = true;

            switch(ins->kind) {
            case CF_TRUE:
            case CF_FALSE:
            case CF_CHAR_CONSTANT:
            case CF_INT_CONSTANT:
            case CF_STRING_CONSTANT:
                break;
            case CF_ADDRESS_OF_VARIABLE:
            case CF_BOOL_NEGATE:
            case CF_CAST_TO_BIGGER_SIGNED_INT:
            case CF_CAST_TO_BIGGER_UNSIGNED_INT:
            case CF_LOAD_FROM_POINTER:
            case CF_VARCPY:
                used[find_var_index(cfg, ins->data.operands[0])] = true;
                break;
            case CF_INT_ADD:
            case CF_INT_EQ:
            case CF_INT_LT:
            case CF_INT_MUL:
            case CF_INT_SDIV:
            case CF_INT_SUB:
            case CF_INT_UDIV:
            case CF_STORE_TO_POINTER:
                used[find_var_index(cfg, ins->data.operands[0])] = true;
                used[find_var_index(cfg, ins->data.operands[1])] = true;
                break;
            case CF_CALL:
                for (int i = 0; i < ins->data.call.nargs; i++)
                    used[find_var_index(cfg, ins->data.call.args[i])] = true;
                break;
            }
        }
    }

    for (int i = cfg->variables.len - 1; i>=0; i--) {
        if (!used[i] && !cfg->variables.ptr[i]->is_argument) {
            cfg->variables.ptr[i] = Pop(&cfg->variables);
            *did_something = true;
        }
    }

    free(used);
}

static void mark_analyzable_variables(struct CfGraph *cfg, bool *did_something)
{
    char *analyzable = malloc(cfg->variables.len);
    memset(analyzable, 1, cfg->variables.len);

    for (struct CfBlock **b = cfg->all_blocks.ptr; b < End(cfg->all_blocks); b++)
        for (struct CfInstruction *ins = (*b)->instructions.ptr; ins < End((*b)->instructions); ins++)
            if (ins->kind == CF_ADDRESS_OF_VARIABLE)
                analyzable[find_var_index(cfg, ins->data.operands[0])] = false;

    for (int i = 0; i < cfg->variables.len; i++) {
        // Variables cannot become non-analyzable: if it was analyzable before, it
        // is still analyzable now.
        if (cfg->variables.ptr[i]->analyzable)
            assert(analyzable[i]);

        if (analyzable[i] && !cfg->variables.ptr[i]->analyzable) {
            *did_something = true;
            cfg->variables.ptr[i]->analyzable = true;
        }
    }

    free(analyzable);
}

static void clean_branches_where_condition_always_true_or_always_false(struct CfGraph *cfg, bool *did_something)
{
    for (struct CfBlock **b = cfg->all_blocks.ptr; b < End(cfg->all_blocks); b++) {
        if (*b == &cfg->end_block || (*b)->iftrue == (*b)->iffalse)
            continue;

        switch(determine_bool_value((*b)->branchvar, *b, End((*b)->instructions))) {
        case 0:
            // Condition always false. Make it unconditionally go to the "false" block.
            (*b)->iftrue = (*b)->iffalse;
            *did_something = true;
            break;
        case 1:
            (*b)->iffalse = (*b)->iftrue;
            *did_something = true;
            break;
        case -1:
            break;
        default:
            assert(0);
        }
    }
}

static void simplify_cfg(struct CfGraph *cfg)
{
    void (*simplifiers[])(struct CfGraph *, bool *) = {
        remove_unused_variables,
        mark_analyzable_variables,
        clean_branches_where_condition_always_true_or_always_false,
        remove_unreachable_blocks,
    };
    int n = sizeof(simplifiers) / sizeof(simplifiers[0]);

    // Run simplifiers one after another until none of them does anything.
    int nothing_happened_count=0;
    for (int i = 0; nothing_happened_count < n; i=(i+1)%n) {
        bool did_something = false;
        simplifiers[i](cfg, &did_something);
        if (did_something)
            nothing_happened_count = 0;
        else
            nothing_happened_count++;
    }
}

void simplify_control_flow_graphs(const struct CfGraphFile *cfgfile)
{
    for (int i = 0; i < cfgfile->nfuncs; i++) {
        if (cfgfile->graphs[i])
            simplify_cfg(cfgfile->graphs[i]);
    }
}
