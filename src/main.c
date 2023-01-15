#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jou_compiler.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>


static char TempDir[50];

static void cleanup()
{
    char command[200];
    sprintf(command, "rm -rf '%s'", TempDir);
    system(command);
}

static void make_temp_dir()
{
    system("mkdir -p /tmp/jou");
    strcpy(TempDir, "/tmp/jou/XXXXXX");
    if (!mkdtemp(TempDir)){
        fprintf(stderr, "cannot create temporary directory: %s\n", strerror(errno));
        exit(1);
    }
    atexit(cleanup);
}

static const char *get_clang_path(void)
{
    // Makefile passes e.g. -DJOU_CLANG_PATH=/usr/lib/llvm-11/bin/clang
    // But retrieving the value is weird...
#define str(x) #x
#define str1(x) str(x)
    return str1(JOU_CLANG_PATH);
#undef str
#undef str1
}

static int run_main_with_clang(LLVMModuleRef module, const CommandLineFlags *flags)
{
    // TODO: this is a bit ridiculous...
    make_temp_dir();

    char filename[200];
    sprintf(filename, "%s/ir.bc", TempDir);

    char *errormsg;
    if (LLVMPrintModuleToFile(module, filename, &errormsg)) {
        fprintf(stderr, "writing LLVM IR to %s failed: %s\n", filename, errormsg);
        LLVMDisposeMessage(errormsg);
    }

    char command[2000];
    snprintf(command, sizeof command, "%s -O%d -Wno-override-module -o %s/exe %s/ir.bc && %s/exe",
        get_clang_path(), flags->optlevel, TempDir, TempDir, TempDir);
    if (flags->verbose)
        puts(command);
    return !!system(command);
}

static const char usage_fmt[] = "Usage: %s [--help] [--verbose] [-O0|-O1|-O2|-O3] FILENAME\n";
static const char long_help[] =
    "  --help           display this message\n"
    "  --verbose        display a lot of information about all compilation steps\n"
    "  -O0/-O1/-O2/-O3  set optimization level (1 = default, 3 = runs fastest)\n"
    ;

static const CommandLineFlags default_flags = {
    .verbose = false,
    .optlevel = 1,
};

void parse_arguments(int argc, char **argv, CommandLineFlags *flags, const char **filename)
{
    *flags = default_flags;

    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "--help")) {
            printf(usage_fmt, argv[0]);
            printf("\n%s\n", long_help);
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

int main(int argc, char **argv)
{
    init_types();

    CommandLineFlags flags;
    const char *filename;
    parse_arguments(argc, argv, &flags, &filename);

    Token *tokens = tokenize(filename);
    if(flags.verbose)
        print_tokens(tokens);

    AstToplevelNode *ast = parse(tokens);
    free_tokens(tokens);
    if(flags.verbose)
        print_ast(ast);

    CfGraphFile cfgfile = build_control_flow_graphs(ast);
    free_ast(ast);
    if(flags.verbose)
        print_control_flow_graphs(&cfgfile);

    simplify_control_flow_graphs(&cfgfile);
    if(flags.verbose)
        print_control_flow_graphs(&cfgfile);

    LLVMModuleRef module = codegen(&cfgfile);
    free_control_flow_graphs(&cfgfile);
    if(flags.verbose)
        print_llvm_ir(module);

    /*
    If this fails, it is not just users writing dumb code, it is a bug in this compiler.
    This compiler should always fail with an error elsewhere, or generate valid LLVM IR.
    */
    LLVMVerifyModule(module, LLVMAbortProcessAction, NULL);

    int ret = run_main_with_clang(module, &flags);
    LLVMDisposeModule(module);
    return ret;
}
