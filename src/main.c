#include <assert.h>
#include <libgen.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "jou_compiler.h"
#include "util.h"
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

static const char help_fmt[] =
    "Usage:\n"
    "  <argv0> [-o OUTFILE] [-O0|-O1|-O2|-O3] [--verbose] FILENAME\n"
    "  <argv0> --help       # This message\n"
    "  <argv0> --update     # Download and install the latest Jou\n"
    "\n"
    "Options:\n"
    "  -o OUTFILE       output an executable file, don't run the code\n"
    "  -O0/-O1/-O2/-O3  set optimization level (0 = default, 3 = runs fastest)\n"
    "  --verbose        display a lot of information about all compilation steps\n"
    ;

static void parse_arguments(int argc, char **argv, CommandLineFlags *flags, const char **filename)
{
    *flags = (CommandLineFlags){0};

    if (argc == 2 && !strcmp(argv[1], "--help")) {
        // Print help.
        const char *s = help_fmt;
        while (*s) {
            if (!strncmp(s, "<argv0>", 7)) {
                printf("%s", argv[0]);
                s += 7;
            } else {
                putchar(*s++);
            }
        }
        exit(0);
    }

    if (argc == 2 && !strcmp(argv[1], "--update")) {
        update_jou_compiler();
        exit(0);
    }

    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "--update")) {
            fprintf(stderr, "%s: \"%s\" cannot be used with other arguments", argv[0], argv[i]);
            goto wrong_usage;
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
        } else if (!strcmp(argv[i], "-o")) {
            if (argc-i < 2) {
                fprintf(stderr, "%s: there must be a file name after -o", argv[0]);
                goto wrong_usage;
            }
            flags->outfile = argv[i+1];
            if (strlen(flags->outfile) > 4 && !strcmp(&flags->outfile[strlen(flags->outfile)-4], ".jou")) {
                fprintf(stderr, "%s: the filename after -o should be an executable, not a Jou file", argv[0]);
                goto wrong_usage;
            }
            i += 2;
        } else {
            fprintf(stderr, "%s: unknown argument \"%s\"", argv[0], argv[i]);
            goto wrong_usage;
        }
    }

    if (i == argc) {
        fprintf(stderr, "%s: missing Jou file name", argv[0]);
        goto wrong_usage;
    }
    if (i < argc-1) {
        fprintf(stderr, "%s: you can only pass one Jou file", argv[0]);
        goto wrong_usage;
    }
    assert(i == argc-1);
    *filename = argv[i];
    return;

wrong_usage:
    fprintf(stderr, " (try \"%s --help\")\n", argv[0]);
    exit(2);
}


struct FileState {
    char *path;
    AstToplevelNode *ast;
    TypeContext typectx;
    LLVMModuleRef module;
    ExportSymbol *pending_exports;
};

struct ParseQueueItem {
    const char *filename;
    Location import_location;
};

struct CompileState {
    char *stdlib_path;
    CommandLineFlags flags;
    List(struct FileState) files;
    List(struct ParseQueueItem) parse_queue;
};

static struct FileState *find_file(const struct CompileState *compst, const char *path)
{
    for (struct FileState *fs = compst->files.ptr; fs < End(compst->files); fs++)
        if (!strcmp(fs->path, path))
            return fs;
    return NULL;
}

static void parse_file(struct CompileState *compst, const char *filename, const Location *import_location)
{
    if (find_file(compst, filename))
        return;  // already parsed this file

    struct FileState fs = { .path = strdup(filename) };

    FILE *f = fopen(fs.path, "rb");
    if (!f) {
        if (import_location)
            fail_with_error(*import_location, "cannot import from \"%s\": %s", filename, strerror(errno));
        else
            fail_with_error((Location){.filename=filename}, "cannot open file: %s", strerror(errno));
    }
    Token *tokens = tokenize(f, fs.path);
    fclose(f);
    if(compst->flags.verbose)
        print_tokens(tokens);

    fs.ast = parse(tokens, compst->stdlib_path);
    free_tokens(tokens);
    if(compst->flags.verbose)
        print_ast(fs.ast);

    for (AstToplevelNode *impnode = fs.ast; impnode->kind == AST_TOPLEVEL_IMPORT; impnode++) {
        Append(&compst->parse_queue, (struct ParseQueueItem){
            .filename = impnode->data.import.path,
            .import_location = impnode->location,
        });
    }

    Append(&compst->files, fs);
}

static void parse_all_pending_files(struct CompileState *compst)
{
    while (compst->parse_queue.len > 0) {
        struct ParseQueueItem it = Pop(&compst->parse_queue);
        parse_file(compst, it.filename, &it.import_location);
    }
    free(compst->parse_queue.ptr);
}

static void compile_ast_to_llvm(struct CompileState *compst, struct FileState *fs)
{
    if (compst->flags.verbose)
        printf("Build CFG: %s\n", fs->path);

    CfGraphFile cfgfile = build_control_flow_graphs(fs->ast, &fs->typectx);
    free_ast(fs->ast);
    fs->ast = NULL;

    if(compst->flags.verbose)
        print_control_flow_graphs(&cfgfile);

    simplify_control_flow_graphs(&cfgfile);
    if(compst->flags.verbose)
        print_control_flow_graphs(&cfgfile);

    if (compst->flags.verbose)
        printf("Build LLVM IR: %s\n", fs->path);

    fs->module = codegen(&cfgfile, &fs->typectx);
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
            printf("\n*** Optimizing %s... (level %d)\n\n\n", fs->path, compst->flags.optlevel);
        optimize(fs->module, compst->flags.optlevel);
        if(compst->flags.verbose)
            print_llvm_ir(fs->module, true);
    }
}

static char *find_stdlib()
{
    char *exe = find_current_executable();
#ifdef _WIN32
    simplify_path(exe);
#endif
    const char *exedir = dirname(exe);

    char *path = malloc(strlen(exedir) + 10);
    strcpy(path, exedir);
    strcat(path, "/stdlib");
    free(exe);

    char *iojou = malloc(strlen(path) + 10);
    sprintf(iojou, "%s/io.jou", path);
    struct stat st;
    if (stat(iojou, &st) != 0) {
        fprintf(stderr, "error: cannot find the Jou standard library in %s\n", path);
        exit(1);
    }
    free(iojou);

    return path;
}

static void add_imported_symbol(struct FileState *fs, const ExportSymbol *es)
{
    struct GlobalVariable *g;

    switch(es->kind) {
    case EXPSYM_FUNCTION:
        for (AstToplevelNode *ast = fs->ast; ast->kind != AST_TOPLEVEL_END_OF_FILE; ast++) {
            if ((ast->kind==AST_TOPLEVEL_DECLARE_FUNCTION || ast->kind==AST_TOPLEVEL_DEFINE_FUNCTION)
                && !strcmp(ast->data.funcdef.signature.funcname, es->name))
            {
                // A function with this name will be declared/defined in the file.
                // Emit an error at the declaration/definition, because it comes after the import in the file.
                // We must do this here, because the declaration/definition was already processed.
                fail_with_error(ast->location, "a function named '%s' already exists", es->name);
            }
        }
        Append(&fs->typectx.function_signatures, copy_signature(&es->data.funcsignature));
        break;
    case EXPSYM_GLOBAL_VAR:
        // TODO: ensure the symbol doesn't exist yet
        g = calloc(1, sizeof(*g));
        g->type = es->data.type;
        g->defined_outside_jou = true;  // TODO rename this field
        safe_strcpy(g->name, es->name);
        Append(&fs->typectx.globals, g);
        break;
    case EXPSYM_TYPE:
        // TODO: ensure the symbol doesn't exist yet
        Append(&fs->typectx.types, es->data.type);
        break;
    }
}

static void add_imported_symbols(struct CompileState *compst)
{
    // TODO: should it be possible for a file to import from itself?
    // Should fail with error?
    for (struct FileState *to = compst->files.ptr; to < End(compst->files); to++) {
        for (AstToplevelNode *ast = to->ast; ast->kind == AST_TOPLEVEL_IMPORT; ast++) {
            AstImport *imp = &ast->data.import;
            struct FileState *from = find_file(compst, imp->path);
            assert(from);

            for (struct ExportSymbol *es = from->pending_exports; es->name[0]; es++) {
                if (!strcmp(imp->symbolname, es->name)) {
                    if (compst->flags.verbose) {
                        const char *kindstr;
                        switch(es->kind) {
                            case EXPSYM_FUNCTION: kindstr="function"; break;
                            case EXPSYM_GLOBAL_VAR: kindstr="global var"; break;
                            case EXPSYM_TYPE: kindstr="type"; break;
                        }
                        printf("Adding imported %s %s: %s --> %s\n",
                            kindstr, es->name, from->path, to->path);
                    }
                    imp->found = true;
                    add_imported_symbol(to, es);
                }
            }
        }
    }

    // Mark all exports as no longer pending.
    for (struct FileState *fs = compst->files.ptr; fs < End(compst->files); fs++) {
        free(fs->pending_exports);
        fs->pending_exports = NULL;
    }
}

/*
Check whether each import statement in AST actually imported something.

This is trickier than you would expect, because multiple passes over
the AST look at the imports, and any of them could provide the symbol to import.
*/
static void check_for_404_imports(const struct CompileState *compst)
{
    for (struct FileState *fs = compst->files.ptr; fs < End(compst->files); fs++) {
        for (const AstToplevelNode *imp = fs->ast; imp->kind == AST_TOPLEVEL_IMPORT; imp++) {
            if (!imp->data.import.found) {
                fail_with_error(
                    imp->location, "file \"%s\" does not export a symbol named '%s'",
                    imp->data.import.path, imp->data.import.symbolname);
            }
        }
    }
}

int main(int argc, char **argv)
{
    init_types();

    struct CompileState compst = { .stdlib_path = find_stdlib() };
    const char *filename;
    parse_arguments(argc, argv, &compst.flags, &filename);

#ifdef _WIN32
    char *startup_path = malloc(strlen(compst.stdlib_path) + 50);
    sprintf(startup_path, "%s/_windows_startup.jou", compst.stdlib_path);
    parse_file(&compst, startup_path, NULL);
    free(startup_path);
#endif

    parse_file(&compst, filename, NULL);
    parse_all_pending_files(&compst);

    for (struct FileState *fs = compst.files.ptr; fs < End(compst.files); fs++) {
        if (compst.flags.verbose)
            printf("Typecheck step 1: %s\n", fs->path);
        fs->pending_exports = typecheck_step1_create_types(&fs->typectx, fs->ast);
    }
    add_imported_symbols(&compst);

    for (struct FileState *fs = compst.files.ptr; fs < End(compst.files); fs++) {
        if (compst.flags.verbose)
            printf("Typecheck step 2: %s\n", fs->path);
        fs->pending_exports = typecheck_step2_signatures_globals_structbodies(&fs->typectx, fs->ast);
    }
    add_imported_symbols(&compst);

    check_for_404_imports(&compst);

    if (compst.flags.verbose)
        printf("\n");

    for (struct FileState *fs = compst.files.ptr; fs < End(compst.files); fs++)
        compile_ast_to_llvm(&compst, fs);

    LLVMModuleRef main_module = LLVMModuleCreateWithName(filename);
    for (struct FileState *fs = compst.files.ptr; fs < End(compst.files); fs++) {
        // This "linking" doesn't mean creating an executable. It only combines LLVM modules together.
        if (compst.flags.verbose)
            printf("LLVMLinkModules2 %s\n", fs->path);
        if (LLVMLinkModules2(main_module, fs->module)) {
            fprintf(stderr, "error: LLVMLinkModules2() failed\n");
            return 1;
        }
        fs->module = NULL;  // consumed in linking
    }

    for (struct FileState *fs = compst.files.ptr; fs < End(compst.files); fs++) {
        free(fs->path);
        free_type_context(&fs->typectx);
    }
    free(compst.files.ptr);
    free(compst.stdlib_path);

    int ret = 0;
    if (compst.flags.outfile)
        compile_to_exe(main_module, compst.flags.outfile, &compst.flags);
    else
        ret = run_program(main_module, &compst.flags);

    LLVMDisposeModule(main_module);
    return ret;
}
