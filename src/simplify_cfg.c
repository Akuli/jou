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

static void simplify_cfg(struct CfGraph *cfg)
{
    void (*simplifiers[])(struct CfGraph *, bool *) = {
        remove_unreachable_blocks,
        mark_analyzable_variables,
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
