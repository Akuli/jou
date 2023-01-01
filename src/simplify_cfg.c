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

static bool add_possibilities(struct BoolStatus *dest, const struct BoolStatus *src, int n)
{
    bool did_something = false;
    for (int i = 0; i < n; i++) {
        if (src[i].can_be_true && !dest[i].can_be_true) {
            dest[i].can_be_true = 1;
            did_something = true;
        }
        if (src[i].can_be_false && !dest[i].can_be_false) {
            dest[i].can_be_false = 1;
            did_something = true;
        }
    }
    return did_something;
}

static bool remove_impossibilities(struct BoolStatus *dest, const struct BoolStatus *src, int n)
{
    bool did_something = false;
    for (int i = 0; i < n; i++) {
        if (!src[i].can_be_true && dest[i].can_be_true) {
            dest[i].can_be_true = 0;
            did_something = true;
        }
        if (!src[i].can_be_false && dest[i].can_be_false) {
            dest[i].can_be_false = 0;
            did_something = true;
        }
    }
    return did_something;
}

static bool all_zero(const char *ptr, int n)
{
    for (int i = 0; i < n; i++)
        if (ptr[i])
            return false;
    return true;
}

/*
Figure out whether boolean variables are true or false.
return_value[block_index][variable_index] = status of variable at END of block
variable_index must be so that the variable has type bool.

Idea: Initially mark everything as impossible, so no variable can be true or false
anywhere, except arguments which can be anything. Loop through instructions of
start block, marking other possibilities. Repeat for blocks where execution jumps,
unless we got same result as last time, then jumped blocks are unaffected.
*/
static struct BoolStatus **determine_known_bool_values(const struct CfGraph *cfg)
{
    int nblocks = cfg->all_blocks.len;
    int nvars = cfg->variables.len;

    struct BoolStatus **result = malloc(sizeof(result[0]) * nblocks);  // NOLINT
    for (int i = 0; i < nblocks; i++)
        result[i] = calloc(nvars, sizeof(result[i][0]));

    char *blocks_to_visit = calloc(1, nblocks);
    blocks_to_visit[0] = true;  // visit initial block

    struct BoolStatus *tempstatus = malloc(nvars*sizeof(tempstatus[0]));

    while(!all_zero(blocks_to_visit, nblocks)){
        // Find a block to visit.
        int visiting = 0;
        while (!blocks_to_visit[visiting]) visiting++;
        printf("Visiting %d\n", visiting);
        blocks_to_visit[visiting] = false;
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
                || cfg->all_blocks.ptr[i]->iffalse == cfg->all_blocks.ptr[visiting])
            {
                // TODO: If we only get here from the true jump, or only from false
                // jump, we could assume that the variable used in the jump was true/false.
                add_possibilities(tempstatus, result[i], nvars);
            }
        }

        printf("Got initially:");
        for (int i = 0; i < nvars; i++)
            if (cfg->variables.ptr[i]->analyzable && cfg->variables.ptr[i]->type.kind == TYPE_BOOL)
                printf("    %s, true? %d, false? %d", cfg->variables.ptr[i]->name, tempstatus[i].can_be_true, tempstatus[i].can_be_false);
        printf("\n");

        // Figure out how each instruction affects booleans.
        for (const struct CfInstruction *ins = visitingblock->instructions.ptr; ins < End(visitingblock->instructions); ins++) {
            if (!ins->destvar || !ins->destvar->analyzable || ins->destvar->type.kind != TYPE_BOOL)
                continue;

            int destidx = find_var_index(cfg, ins->destvar);
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
                printf("** block %d: %s can be anything\n", visiting, ins->destvar->name);
                tempstatus[destidx] = (struct BoolStatus){ .can_be_true=1, .can_be_false=1 };
                break;
            }
        }

        // If some values of variables are possible, remember that from now on.
        bool result_affected = add_possibilities(result[visiting], tempstatus, nvars);

        printf("Ended up with:");
        for (int i = 0; i < nvars; i++)
            if (cfg->variables.ptr[i]->analyzable && cfg->variables.ptr[i]->type.kind == TYPE_BOOL)
                printf("    %s, true? %d, false? %d", cfg->variables.ptr[i]->name, result[visiting][i].can_be_true, result[visiting][i].can_be_false);
        printf("\n");

        if (result_affected && visitingblock != &cfg->end_block) {
            // Also need to update blocks where we jump from here.
            int i1 = find_block_index(cfg, visitingblock->iftrue);
            int i2 = find_block_index(cfg, visitingblock->iffalse);
            blocks_to_visit[i1] = true;
            blocks_to_visit[i2] = true;
            printf("  Will visit %d,%d\n", i1,i2);
        }
    }

    return result;
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
    struct BoolStatus **statuses = determine_known_bool_values(cfg);
    int nblocks = cfg->all_blocks.len;
    int nvars = cfg->variables.len;

    for (int blockidx = 0; blockidx < nblocks; blockidx++) {
        struct CfBlock *block = cfg->all_blocks.ptr[blockidx];
        if (block == &cfg->end_block || block->iftrue == block->iffalse)
            continue;

        struct BoolStatus s = statuses[blockidx][find_var_index(cfg, block->branchvar)];
        if (s.can_be_true && !s.can_be_false) {
            // Always jump to true case.
            block->iffalse = block->iftrue;
            *did_something = true;
        }
        if (!s.can_be_true && s.can_be_false) {
            // Always jump to false case.
            block->iftrue = block->iffalse;
            *did_something = true;
        }
        printf("\n");
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
    int nvars = cfg->variables.len;
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
    print_control_flow_graphs(cfgfile);
}
