#include "jou_compiler.h"

static int find_block_index(const struct CfGraph *cfg, const struct CfBlock *b)
{
    for (int i = 0; i < cfg->all_blocks.len; i++)
        if (cfg->all_blocks.ptr[i] == b)
            return i;
    assert(0);
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
    }

    free(reachable);
    free(todo.ptr);
}

static void simplify_cfg(struct CfGraph *cfg)
{
    remove_unreachable_blocks(cfg);
}

void simplify_control_flow_graphs(const struct CfGraphFile *cfgfile)
{
    for (int i = 0; i < cfgfile->nfuncs; i++) {
        if (cfgfile->graphs[i])
            simplify_cfg(cfgfile->graphs[i]);
    }
}
