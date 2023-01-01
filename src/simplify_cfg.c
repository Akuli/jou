#include "jou_compiler.h"
#include <limits.h>

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

enum BoolStatus {
    KNOWN_TO_BE_TRUE,
    KNOWN_TO_BE_FALSE,
    /*
    The remaining statuses have different meanings:
    - CAN_CHANGE_UNPREDICTABLY: The address of the variable (&foo) has been used, so
      from now on, the variable's value can change in lines of code that don't seem
      to have anything to do with the variable. For example, a function that doesn't
      take the variable as an argument could still change the variable, if the
      pointer &foo was stored elsewhere earlier.
    - COULD_BE_TRUE_OR_FALSE: The value of the variable is not known.
    - UNSET: A temporary status that add_possibilities() always replaces with a
      different status.
    */
    CAN_CHANGE_UNPREDICTABLY,
    COULD_BE_TRUE_OR_FALSE,
    UNSET,
};

static bool add_possibilities(enum BoolStatus *dest, const enum BoolStatus *src, int n)
{
    bool did_something = false;
    for (int i = 0; i < n; i++) {
        enum BoolStatus newdest;

        assert(src[i] != UNSET);
        if (dest[i] == UNSET)
            newdest = src[i];
        else if (src[i] == CAN_CHANGE_UNPREDICTABLY || dest[i] == CAN_CHANGE_UNPREDICTABLY)
            newdest = CAN_CHANGE_UNPREDICTABLY;
        else if (src[i] == KNOWN_TO_BE_FALSE && dest[i] == KNOWN_TO_BE_FALSE)
            newdest = KNOWN_TO_BE_FALSE;
        else if (src[i] == KNOWN_TO_BE_TRUE && dest[i] == KNOWN_TO_BE_TRUE)
            newdest = KNOWN_TO_BE_TRUE;
        else
            newdest = COULD_BE_TRUE_OR_FALSE;

        if (dest[i] != newdest) {
            dest[i] = newdest;
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

Idea: Initially mark everything as possible, so all variables can be true or false.
Loop through instructions of start block, filtering out impossible options: for
example, if a variable is set to True, then it can be True and cannot be False.
Repeat for blocks where execution jumps from the current block, unless we got same
result as last time, then we know that we don't have to reanalyze blocks where
execution jumps from the current block.
*/
static enum BoolStatus **determine_known_bool_values(const struct CfGraph *cfg)
{
    int nblocks = cfg->all_blocks.len;
    int nvars = cfg->variables.len;

    enum BoolStatus **result = malloc(sizeof(result[0]) * nblocks);  // NOLINT
    for (int i = 0; i < nblocks; i++)
        result[i] = calloc(nvars, sizeof(result[i][0]));

    char *blocks_to_visit = calloc(1, nblocks);
    blocks_to_visit[0] = true;  // visit initial block

    enum BoolStatus *tempstatus = malloc(nvars*sizeof(tempstatus[0]));

    while(!all_zero(blocks_to_visit, nblocks)){
        // Find a block to visit.
        int visiting = 0;
        while (!blocks_to_visit[visiting]) visiting++;
        printf("Visit block %d\n", visiting);
        blocks_to_visit[visiting] = false;
        const struct CfBlock *visitingblock = cfg->all_blocks.ptr[visiting];

        // Determine initial values based on other blocks that jump here.
        for (int i = 0; i < nvars; i++) {
            if (visiting == 0) {
                // Start block: don't assume the value of any variable.
                tempstatus[i] = COULD_BE_TRUE_OR_FALSE;
            } else {
                // What is possible in other blocks is determined based on only how
                // they are jumped into.
                tempstatus[i] = UNSET;
            }
        }
        for (int i = 0; i < nblocks; i++) {
            if (cfg->all_blocks.ptr[i]->iftrue == visitingblock
                || cfg->all_blocks.ptr[i]->iffalse == visitingblock)
            {
                // TODO: If we only get here from the true jump, or only from false
                // jump, we could assume that the variable used in the jump was true/false.
                add_possibilities(tempstatus, result[i], nvars);
            }
        }

        // Figure out how each instruction affects booleans.
        for (const struct CfInstruction *ins = visitingblock->instructions.ptr; ins < End(visitingblock->instructions); ins++) {
            if (!ins->destvar || ins->destvar->type.kind != TYPE_BOOL)
                continue;

            int destidx = find_var_index(cfg, ins->destvar);
            switch(ins->kind) {
            case CF_VARCPY:
                switch(tempstatus[find_var_index(cfg, ins->operands[0])]) {
                case KNOWN_TO_BE_TRUE:
                    tempstatus[destidx] = KNOWN_TO_BE_TRUE;
                    break;
                case KNOWN_TO_BE_FALSE:
                    tempstatus[destidx] = KNOWN_TO_BE_FALSE;
                    break;
                case CAN_CHANGE_UNPREDICTABLY:  // Even an unpredictable variable yields true or false value.
                case COULD_BE_TRUE_OR_FALSE:
                    tempstatus[destidx] = COULD_BE_TRUE_OR_FALSE;
                    break;
                case UNSET:
                    assert(0);
                }
                break;
            case CF_TRUE:
                tempstatus[destidx] = KNOWN_TO_BE_TRUE;
                break;
            case CF_FALSE:
                tempstatus[destidx] = KNOWN_TO_BE_FALSE;
                break;
            default:
                tempstatus[destidx] = COULD_BE_TRUE_OR_FALSE;
                break;
            }
        }

        // If some values of variables are possible, remember that from now on.
        bool result_affected = add_possibilities(result[visiting], tempstatus, nvars);

        if (result_affected && visitingblock != &cfg->end_block) {
            // Also need to update blocks where we jump from here.
            printf("  Will visit %d and %d\n", find_block_index(cfg, visitingblock->iftrue), find_block_index(cfg, visitingblock->iffalse));
            blocks_to_visit[find_block_index(cfg, visitingblock->iftrue)] = true;
            blocks_to_visit[find_block_index(cfg, visitingblock->iffalse)] = true;
        }
    }

    free(blocks_to_visit);
    free(tempstatus);
    return result;
}

#if 1
static void print_known_bool_values(const struct CfGraph *cfg, enum BoolStatus **statuses)
{
    int nblocks = cfg->all_blocks.len;
    int nvars = cfg->variables.len;

    const char *strs[] = {
        [KNOWN_TO_BE_TRUE] = "is known to be true",
        [KNOWN_TO_BE_FALSE] = "is known to be false",
        [CAN_CHANGE_UNPREDICTABLY] = "can change unpredictably",
        [COULD_BE_TRUE_OR_FALSE] = "could be true or false",
        [UNSET] = "is in the temporary UNSET status",
    };

    for (int blockidx = 0; blockidx < nblocks; blockidx++) {
        printf("block %d:\n", blockidx);
        for (int i = 0; i < nvars; i++)
            if (cfg->variables.ptr[i]->type.kind == TYPE_BOOL)
                printf("  \"%s\" %s\n", cfg->variables.ptr[i]->name, strs[statuses[blockidx][i]]);
        printf("\n");
    }
}
#endif

static void clean_jumps_where_condition_always_true_or_always_false(struct CfGraph *cfg)
{
    enum BoolStatus **statuses = determine_known_bool_values(cfg);

    for (int blockidx = 0; blockidx < cfg->all_blocks.len; blockidx++) {
        struct CfBlock *block = cfg->all_blocks.ptr[blockidx];
        if (block == &cfg->end_block || block->iftrue == block->iffalse)
            continue;

        switch(statuses[blockidx][find_var_index(cfg, block->branchvar)]) {
        case KNOWN_TO_BE_TRUE:
            // Always jump to true case.
            block->iffalse = block->iftrue;
            break;
        case KNOWN_TO_BE_FALSE:
            // Always jump to false case.
            block->iftrue = block->iffalse;
            break;
        case UNSET:
            assert(0);
        case CAN_CHANGE_UNPREDICTABLY:
        case COULD_BE_TRUE_OR_FALSE:
            break;
        }
    }

    for (int i = 0; i < cfg->all_blocks.len; i++)
        free(statuses[i]);
    free(statuses);
}

/*
Two blocks will end up in the same group, if there is an execution path from one block to another.
Return value: array of groups, each group is an array of CfBlock pointers.
All returned arrays are NULL terminated.

This is not very efficient code, but it's only used for unreachable blocks.
In a typical program, I don't expect to have many unreachable blocks.
*/
static struct CfBlock ***group_blocks(struct CfBlock **blocks, int nblocks)
{
    struct CfBlock ***groups = calloc(sizeof(groups[0]), nblocks+1);  // NOLINT

    // Initially there is a separate group for each block.
    int ngroups = nblocks;
    for (int i = 0; i < nblocks; i++) {
        groups[i] = calloc(sizeof(groups[i][0]), nblocks + 1); // NOLINT
        groups[i][0] = blocks[i];
    }

    // For each block, we need to check whether that block can jump outside its
    // group. When that does, merge the two groups together.
    for (int block1index = 0; block1index < nblocks; block1index++) {
        struct CfBlock *block1 = blocks[block1index];
        if (block1->iffalse==NULL && block1->iftrue==NULL)
            continue;  // the end block

        for (int m = 0; m < 2; m++) {
            struct CfBlock *block2 = m ? block1->iffalse : block1->iftrue;
            assert(block2);

            // Find groups of block1 and block2.
            struct CfBlock ***g1 = NULL, ***g2 = NULL;
            for (int i = 0; groups[i]; i++) {
                for (int k = 0; groups[i][k]; k++) {
                    if (groups[i][k] == block1) g1=&groups[i];
                    if (groups[i][k] == block2) g2=&groups[i];
                }
            }

            if (g1 && g2 && *g1!=*g2) {
                // Append all blocks from group 2 to group 1.
                struct CfBlock **dest = *g1, **src = *g2;
                while (*dest) dest++;
                while ((*dest++ = *src++)) ;

                // Delete group 2.
                free(*g2);
                *g2 = groups[--ngroups];
                groups[ngroups] = NULL;
            }
        }
    }

    return groups;
}

static void show_unreachable_warnings(struct CfBlock **unreachable_blocks, int n_unreachable_blocks)
{
    // Show a warning in the beginning of each group of blocks.
    // Can't show a warning for each block, that would be too noisy.
    struct CfBlock ***groups = group_blocks(unreachable_blocks, n_unreachable_blocks);

    for (int groupidx = 0; groups[groupidx]; groupidx++) {
        struct Location first_location = { .lineno = INT_MAX };
        for (int i = 0; groups[groupidx][i]; i++) {
            if (groups[groupidx][i]->instructions.len != 0) {
                struct Location loc = groups[groupidx][i]->instructions.ptr[0].location;
                if (loc.lineno < first_location.lineno)
                    first_location = loc;
            }
        }
        if (first_location.lineno != INT_MAX)
            show_warning(first_location, "this code will never run");
    }

    for (int i = 0; groups[i]; i++)
        free(groups[i]);
    free(groups);
}

static void remove_given_blocks(struct CfGraph *cfg, struct CfBlock **blocks_to_remove, int n_blocks_to_remove)
{
    for (int i = cfg->all_blocks.len - 1; i >= 0; i--) {
        bool shouldgo = false;
        for (struct CfBlock **b = blocks_to_remove; b < &blocks_to_remove[n_blocks_to_remove]; b++) {
            if(*b==cfg->all_blocks.ptr[i]){
                shouldgo=true;
                break;
            }
        }
        if(shouldgo)
        {
            free_control_flow_graph_block(cfg, cfg->all_blocks.ptr[i]);
            cfg->all_blocks.ptr[i]=Pop(&cfg->all_blocks);
        }
    }
}

static void remove_unreachable_blocks(struct CfGraph *cfg)
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
    free(todo.ptr);

    List(struct CfBlock *) blocks_to_remove = {0};
    for (int i = 0; i < cfg->all_blocks.len; i++)
        if (!reachable[i] && cfg->all_blocks.ptr[i] != &cfg->end_block)
            Append(&blocks_to_remove, cfg->all_blocks.ptr[i]);
    free(reachable);

    show_unreachable_warnings(blocks_to_remove.ptr, blocks_to_remove.len);
    remove_given_blocks(cfg, blocks_to_remove.ptr, blocks_to_remove.len);
    free(blocks_to_remove.ptr);
}

static void remove_unused_variables(struct CfGraph *cfg)
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
        }
    }

    free(used);
}

static void simplify_cfg(struct CfGraph *cfg)
{
    clean_jumps_where_condition_always_true_or_always_false(cfg);
    remove_unreachable_blocks(cfg);
    remove_unused_variables(cfg);
}

void simplify_control_flow_graphs(const struct CfGraphFile *cfgfile)
{
    for (int i = 0; i < cfgfile->nfuncs; i++)
        if (cfgfile->graphs[i])
            simplify_cfg(cfgfile->graphs[i]);
}
