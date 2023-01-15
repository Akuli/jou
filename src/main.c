#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jou_compiler.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

static const char usage_fmt[] = "Usage: %s [--help] [--verbose] [--no-jit] [-O0|-O1|-O2|-O3] FILENAME\n";
static const char long_help[] =
    "  --help           display this message\n"
    "  --verbose        display a lot of information about all compilation steps\n"
    "  --no-jit         compile code to file and run the file (can be faster)\n"
    "  -O0/-O1/-O2/-O3  set optimization level (1 = default, 3 = runs fastest)\n"
    ;

void parse_arguments(int argc, char **argv, CommandLineFlags *flags, const char **filename)
{
    if (argc < 2)
        goto usage;

    *filename = argv[argc-1];
    if ((*filename)[0] == '-')
        goto usage;

    *flags = (CommandLineFlags){ .verbose=false, .jit=true, .optlevel=1 };
    for (int i = 1; i < argc-1; i++) {
#define ArgMatches(s) (!strcmp(argv[i], (s)))
        if (ArgMatches("--help")) {
            printf(usage_fmt, argv[0]);
            printf("\n%s\n", long_help);
            exit(0);
        } else if (ArgMatches("--verbose"))
            flags->verbose = true;
        else if (ArgMatches("--no-jit"))
            flags->jit = false;
        else if (ArgMatches("-O0") || ArgMatches("-O1") || ArgMatches("-O2") || ArgMatches("-O3"))
            flags->optlevel = argv[i][2] - '0';
        else
            goto usage;
    }
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

    return run_program(module, &flags);
}
