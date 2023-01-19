#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jou_compiler.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Linker.h>
#include <llvm-c/Transforms/PassManagerBuilder.h>


static void optimize(LLVMModuleRef module, int level)
{
    assert(1 <= level && level <= 3);

    LLVMPassManagerRef pm = LLVMCreatePassManager();

    /*
    The default settings should be fine for Jou because they work well for
    C and C++, and Jou is quite similar to C.
    */
    LLVMPassManagerBuilderRef pmbuilder = LLVMPassManagerBuilderCreate();
    LLVMPassManagerBuilderSetOptLevel(pmbuilder, level);
    LLVMPassManagerBuilderPopulateModulePassManager(pmbuilder, pm);
    LLVMPassManagerBuilderDispose(pmbuilder);

    LLVMRunPassManager(pm, module);
    LLVMDisposePassManager(pm);
}

static const char usage_fmt[] = "Usage: %s [--help] [--verbose] [-O0|-O1|-O2|-O3] FILENAME\n";
static const char long_help[] =
    "  --help           display this message\n"
    "  --verbose        display a lot of information about all compilation steps\n"
    "  -O0/-O1/-O2/-O3  set optimization level (0 = default, 3 = runs fastest)\n"
    ;

static void parse_arguments(int argc, char **argv, CommandLineFlags *flags, const char **filename)
{
    *flags = (CommandLineFlags){0};

    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "--help")) {
            printf(usage_fmt, argv[0]);
            printf("%s", long_help);
            exit(0);
        } else if (!strcmp(argv[i], "--verbose")) {
            flags->verbose = true;
            i++;
        } else if (strlen(argv[i]) == 3
                && !strncmp(argv[i], "-O", 2)
                && argv[i][2] >= '0'
                && argv[i][2] <= '3')
        {
            flags->optlevel = argv[i][2] - '0';
            i++;
        } else {
            goto usage;
        }
    }

    if (i != argc-1)
        goto usage;
    *filename = argv[i];
    return;

usage:
    fprintf(stderr, usage_fmt, argv[0]);
    exit(2);
}


struct FileState {
    char *filename;
    AstToplevelNode *ast;
    LLVMModuleRef module;
};

struct CompileState {
    CommandLineFlags flags;
    List(struct FileState) files;
    List(const char *) parse_queue;
    TypeContext typectx;
};

static void parse_file(struct CompileState *compst, const char *filename)
{
    for (struct FileState *fs = compst->files.ptr; fs < End(compst->files); fs++)
        if (!strcmp(fs->ast->location.filename, filename))
            return;  // already parsed this file

    struct FileState fs = { .filename = strdup(filename) };

    Token *tokens = tokenize(fs.filename);
    if(compst->flags.verbose)
        print_tokens(tokens);

    fs.ast = parse(tokens);
    free_tokens(tokens);
    if(compst->flags.verbose)
        print_ast(fs.ast);

    for (int i = 0; fs.ast[i].kind == AST_TOPLEVEL_IMPORT; i++)
        Append(&compst->parse_queue, fs.ast[i].data.import.filename);

    Append(&compst->files, fs);
}

static void parse_all_pending_files(struct CompileState *compst)
{
    while (compst->parse_queue.len > 0) {
        // TODO: is the order good? probably not, should pop from start?
        const char *s = Pop(&compst->parse_queue);
        parse_file(compst, s);
    }
}

static void compile_ast_to_llvm(struct CompileState *compst, struct FileState *fs)
{
    CfGraphFile cfgfile = build_control_flow_graphs(fs->ast, &compst->typectx);
    free_ast(fs->ast);
    fs->ast = NULL;

    if(compst->flags.verbose)
        print_control_flow_graphs(&cfgfile);

    simplify_control_flow_graphs(&cfgfile);
    if(compst->flags.verbose)
        print_control_flow_graphs(&cfgfile);

    fs->module = codegen(&cfgfile, &compst->typectx);
    free_control_flow_graphs(&cfgfile);

    if(compst->flags.verbose)
        print_llvm_ir(fs->module, false);

    /*
    If this fails, it is not just users writing dumb code, it is a bug in this compiler.
    This compiler should always fail with an error elsewhere, or generate valid LLVM IR.
    */
    LLVMVerifyModule(fs->module, LLVMAbortProcessAction, NULL);

    if (compst->flags.optlevel) {
        if (compst->flags.verbose)
            printf("\n*** Optimizing %s... (level %d)\n\n\n", fs->filename, compst->flags.optlevel);
        optimize(fs->module, compst->flags.optlevel);
        if(compst->flags.verbose)
            print_llvm_ir(fs->module, true);
    }
}

int main(int argc, char **argv)
{
    init_types();

    struct CompileState compst = {0};
    const char *filename;
    parse_arguments(argc, argv, &compst.flags, &filename);

    Append(&compst.parse_queue, filename);
    parse_all_pending_files(&compst);

    for (int i = compst.files.len - 1; i >= 0; i--) {
        struct FileState *fs = &compst.files.ptr[i];
        compile_ast_to_llvm(&compst, fs);
    }

    for (int i = 1; i < compst.files.len; i++) {
        if (compst.flags.verbose)
            printf("Link %s, %s\n", compst.files.ptr[0].filename, compst.files.ptr[i].filename);
        if (LLVMLinkModules2(compst.files.ptr[0].module, compst.files.ptr[i].module)) {
            fprintf(stderr, "error: LLVMLinkModules2() failed\n");
            return 1;
        }
    }

    //LLVMDumpModule(compst.files.ptr[0].module);

    return run_program(compst.files.ptr[0].module, &compst.flags);
}
