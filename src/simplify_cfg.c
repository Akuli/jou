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

struct BoolStatus {
    bool can_be_true;
    bool can_be_false;
};

bool update(struct BoolStatus *dest, const struct BoolStatus *src, int n)
{
    bool changed = false;
    for (int i = 0; i < n; i++) {
        if (src[i].can_be_true && !dest[i].can_be_true) {
            dest[i].can_be_true = true;
            changed = true;
        }
        if (src[i].can_be_false && !dest[i].can_be_false) {
            dest[i].can_be_false = true;
            changed = true;
        }
    }
    return changed;
}

/*
Figure out whether boolean variables are true or false.
return_value[block_index][variable_index] = status of variable at END of block
variable_index must be so that the variable has type bool.
*/
static struct BoolStatus **determine_known_bool_values(const struct CfGraph *cfg)
{
    int nblocks = cfg->all_blocks.len;
    int nvars = cfg->all_blocks.len;

    struct BoolStatus **result = malloc(sizeof(result[0]) * nblocks);  // NOLINT
    for (int i = 0; i < nblocks; i++) {
        result[i] = malloc(nvars * sizeof(result[i][0]));
        for (int k = 0; k < nvars; k++)
            result[i][k] = (struct BoolStatus){ .can_be_true=1, .can_be_false=1 };
    }

    char *blocks_to_visit = calloc(1, nblocks);
    blocks_to_visit[0] = true;  // visit initial block
    int n_blocks_to_visit = 1;  // how many are true in blocks_to_visit

    struct BoolStatus *tempstatus = malloc(nvars*sizeof(tempstatus[0]));

    int visiting = 0;
    while(n_blocks_to_visit != 0){
        // Find a block to visit.
        while (!blocks_to_visit[visiting])
            visiting = (visiting+1) % nblocks;
        blocks_to_visit[visiting] = false;
        n_blocks_to_visit--;
        const struct CfBlock *visitingblock = cfg->all_blocks.ptr[visiting];

        // Determine initial values based on other blocks that jump here.
        for (int i = 0; i < nvars; i++) {
            if (visiting == 0 && cfg->variables.ptr[i]->is_argument) {
                // Start block: assume nothing about arguments.
                tempstatus[i].can_be_true = 1;
                tempstatus[i].can_be_false = 1;
            } else {
                // What is possible in other blocks is determined based on only how
                // they are jumped into.
                tempstatus[i].can_be_true = 0;
                tempstatus[i].can_be_false = 0;
            }
        }
        for (int i = 0; i < nblocks; i++) {
            if (cfg->all_blocks.ptr[i]->iftrue == cfg->all_blocks.ptr[visiting]
                && cfg->all_blocks.ptr[i]->iffalse == cfg->all_blocks.ptr[visiting])
            {
                // TODO: If we only get here from the true jump, or only from false
                // jump, we could assume that the variable used in the jump was true/false.
                update(tempstatus, result[visiting], nvars);
            }
        }

        // Figure out how each instruction affects booleans.
        for (const struct CfInstruction *ins = visitingblock->instructions.ptr; ins < End(visitingblock->instructions); ins++) {
            int destidx = find_var_index(cfg, ins->destvar);

            if (ins->destvar->type.kind != TYPE_BOOL || !ins->destvar->analyzable)
                continue;

            switch(ins->kind) {
            case CF_VARCPY:
                if (ins->operands[0]->analyzable)
                    tempstatus[destidx] = tempstatus[find_var_index(cfg, ins->operands[0])];
                else
                    tempstatus[destidx] = (struct BoolStatus){ .can_be_true=1, .can_be_false=1 };
                break;
            case CF_TRUE:
                tempstatus[destidx] = (struct BoolStatus){ .can_be_true=1, .can_be_false=0 };
                break;
            case CF_FALSE:
                tempstatus[destidx] = (struct BoolStatus){ .can_be_true=0, .can_be_false=1 };
                break;
            default:
                tempstatus[destidx] = (struct BoolStatus){ .can_be_true=1, .can_be_false=1 };
                break;
            }
        }

        // Determine final values of variables.
        bool result_affected = false;
        for (int i = 0; i < nvars; i++) {
            if (!tempstatus[i].can_be_true && result[visiting][i].can_be_true) {
                result[visiting][i].can_be_true = 0;
                result_affected = true;
            }
            if (!tempstatus[i].can_be_false && result[visiting][i].can_be_false) {
                result[visiting][i].can_be_false = 0;
                result_affected = true;
            }
        }

        if (result_affected && visitingblock != &cfg->end_block) {
            // Also need to update blocks where we jump from here.
            int i1 = find_block_index(cfg, visitingblock->iftrue);
            int i2 = find_block_index(cfg, visitingblock->iffalse);
            if (!blocks_to_visit[i1]) {
                blocks_to_visit[i1] = true;
                n_blocks_to_visit++;
            }
            if (!blocks_to_visit[i2]) {
                blocks_to_visit[i2] = true;
                n_blocks_to_visit++;
            }
        }
    }

    return result;
}

static int determine_bool_value(const struct CfVariable *var, const struct CfBlock *block, const struct CfInstruction *ins)
{
    assert(same_type(&var->type, &boolType));
    if (!var->analyzable || block->instructions.len == 0)
        return -1;

    while (--ins >= block->instructions.ptr) {
        if (ins->destvar == var) {
            switch(ins->kind) {
                case CF_VARCPY:
                    var = ins->operands[0];
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
            for (int i = 0; i < ins->noperands; i++)
                used[find_var_index(cfg, ins->operands[i])] = true;
        }
    }

    for (int i = cfg->variables.len - 1; i>=0; i--) {
        if (!used[i] && !cfg->variables.ptr[i]->is_argument) {
            free_type(&cfg->variables.ptr[i]->type);
            free(cfg->variables.ptr[i]);
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
                analyzable[find_var_index(cfg, ins->operands[0])] = false;

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

    struct BoolStatus **statuses = determine_known_bool_values(cfg);
    int nblocks = cfg->all_blocks.len;
    int nvars = cfg->all_blocks.len;
    for (int blockidx = 0; blockidx < nblocks; blockidx++) {
        printf("end of block %d:\n", blockidx);
        for (int varidx = 0; varidx < nvars; varidx++)
        {
            if (cfg->variables.ptr[varidx]->type.kind == TYPE_BOOL
                && cfg->variables.ptr[varidx]->analyzable)
            {
                printf("  %s --> can_be_true=%d can_be_false=%d\n",
                    cfg->variables.ptr[varidx]->name,
                    statuses[blockidx][varidx].can_be_true,
                    statuses[blockidx][varidx].can_be_false
                );
            }
        }
        printf("\n");
    }
}

void simplify_control_flow_graphs(const struct CfGraphFile *cfgfile)
{
    for (int i = 0; i < cfgfile->nfuncs; i++) {
        if (cfgfile->graphs[i]) {
            printf("function %s...\n", cfgfile->signatures[i].funcname);
            simplify_cfg(cfgfile->graphs[i]);
        }
    }
}
