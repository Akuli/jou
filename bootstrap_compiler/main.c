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
    "  <argv0> [-o OUTFILE] [-O0|-O1|-O2|-O3] [--verbose] [--linker-flags \"...\"] FILENAME\n"
    "  <argv0> --help       # This message\n"
    "\n"
    "Options:\n"
    "  -o OUTFILE       output an executable file, don't run the code\n"
    "  -O0/-O1/-O2/-O3  set optimization level (0 = no optimization, 1 = default, 3 = runs fastest)\n"
    "  -v / --verbose   display some progress information\n"
    "  -vv              display a lot of information about all compilation steps\n"
    "  --valgrind       use valgrind when running the code\n"
    "  --tokenize-only  display only the output of the tokenizer, don't do anything else\n"
    "  --parse-only     display only the AST (parse tree), don't do anything else\n"
    "  --linker-flags   appended to the linker command, so you can use external libraries\n"
    ;

struct CommandLineArgs command_line_args;

void parse_arguments(int argc, char **argv)
{
    memset(&command_line_args, 0, sizeof command_line_args);
    command_line_args.argv0 = argv[0];
    /* Set default optimize to O1, user sets optimize will overwrite the default flag*/
    command_line_args.optlevel = 1;

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

    int i = 1;
    while (i < argc) {
        if (!strcmp(argv[i], "--help")) {
            fprintf(stderr, "%s: \"%s\" cannot be used with other arguments", argv[0], argv[i]);
            goto wrong_usage;
        } else if (!strcmp(argv[i], "--verbose")) {
            command_line_args.verbosity++;
            i++;
        } else if (strncmp(argv[i], "-v", 2) == 0 && strspn(argv[i] + 1, "v") == strlen(argv[i])-1) {
            command_line_args.verbosity += strlen(argv[i]) - 1;
            i++;
        } else if (!strcmp(argv[i], "--valgrind")) {
            command_line_args.valgrind = true;
            i++;
        } else if (!strcmp(argv[i], "--tokenize-only")) {
            if (argc > 3) {
                fprintf(stderr, "%s: --tokenize-only cannot be used together with other flags", argv[0]);
                goto wrong_usage;
            }
            command_line_args.tokenize_only = true;
            i++;
        } else if (!strcmp(argv[i], "--parse-only")) {
            if (argc > 3) {
                fprintf(stderr, "%s: --parse-only cannot be used together with other flags", argv[0]);
                goto wrong_usage;
            }
            command_line_args.parse_only = true;
            i++;
        } else if (!strcmp(argv[i], "--linker-flags")) {
            if (command_line_args.linker_flags) {
                fprintf(stderr, "%s: --linker-flags cannot be given multiple times", argv[0]);
                goto wrong_usage;
            }
            if (argc-i < 2) {
                fprintf(stderr, "%s: there must be a string of flags after --linker-flags", argv[0]);
                goto wrong_usage;
            }
            command_line_args.linker_flags = argv[i+1];
            i += 2;
        } else if (strlen(argv[i]) == 3
                && !strncmp(argv[i], "-O", 2)
                && argv[i][2] >= '0'
                && argv[i][2] <= '3')
        {
            command_line_args.optlevel = argv[i][2] - '0';
            i++;
        } else if (!strcmp(argv[i], "-o")) {
            if (argc-i < 2) {
                fprintf(stderr, "%s: there must be a file name after -o", argv[0]);
                goto wrong_usage;
            }
            command_line_args.outfile = argv[i+1];
            if (strlen(command_line_args.outfile) > 4 && !strcmp(&command_line_args.outfile[strlen(command_line_args.outfile)-4], ".jou")) {
                fprintf(stderr, "%s: the filename after -o should be an executable, not a Jou file", argv[0]);
                goto wrong_usage;
            }
            i += 2;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unknown argument \"%s\"", argv[0], argv[i]);
            goto wrong_usage;
        } else if (command_line_args.infile) {
            fprintf(stderr, "%s: you can only pass one Jou file", argv[0]);
            goto wrong_usage;
        } else {
            command_line_args.infile = argv[i++];
        }
    }

    if (!command_line_args.infile) {
        fprintf(stderr, "%s: missing Jou file name", argv[0]);
        goto wrong_usage;
    }
    return;

wrong_usage:
    fprintf(stderr, " (try \"%s --help\")\n", argv[0]);
    exit(2);
}


struct FileState {
    char *path;
    AstFile ast;
    FileTypes types;
    LLVMModuleRef module;
    ExportSymbol *pending_exports;
};

struct ParseQueueItem {
    const char *filename;
    Location import_location;
};

struct CompileState {
    const char *stdlib_path;
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

static FILE *open_the_file(const char *path, const Location *import_location)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (import_location)
            fail(*import_location, "cannot import from \"%s\": %s", path, strerror(errno));
        else
            fail((Location){.filename=path}, "cannot open file: %s", strerror(errno));
    }
    return f;
}

static bool defines_main(const AstFile *ast)
{
    for (int i = 0; i < ast->body.nstatements; i++) {
        const AstStatement *s = &ast->body.statements[i];
        if (s->kind == AST_STMT_FUNCTION && !strcmp(s->data.function.signature.name, "main"))
            return true;
    }
    return false;
}

static void parse_file(struct CompileState *compst, const char *filename, const Location *import_location)
{
    if (find_file(compst, filename))
        return;  // already parsed this file

    struct FileState fs = { .path = strdup(filename) };

    if(command_line_args.verbosity >= 1)
        printf("Tokenizing %s\n", filename);
    FILE *f = open_the_file(fs.path, import_location);
    Token *tokens = tokenize(f, fs.path);
    fclose(f);

    if(command_line_args.verbosity >= 2)
        print_tokens(tokens);

    if(command_line_args.verbosity >= 1)
        printf("Parsing %s\n", filename);
    fs.ast = parse(tokens, compst->stdlib_path);
    free_tokens(tokens);

    if (command_line_args.verbosity >= 1)
        printf("Evaluating compile-time if statements in %s\n", filename);
    evaluate_compile_time_if_statements(&fs.ast.body);

    if(command_line_args.verbosity >= 2)
        print_ast(&fs.ast);

    // If it's not the file passed on command line, it shouldn't define main()
    if (strcmp(filename, command_line_args.infile) && defines_main(&fs.ast)) {
        /*
        Set error location to import, so user immediately knows which file
        imports something that defines main().
        */
        assert(import_location);
        fail(*import_location, "imported file should not have `main` function");
    }

    for (const AstImport *imp = fs.ast.imports.ptr; imp < End(fs.ast.imports); imp++) {
        Append(&compst->parse_queue, (struct ParseQueueItem){
            .filename = imp->resolved_path,
            .import_location = imp->location,
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

static char *compile_ast_to_object_file(struct FileState *fs)
{
    if (command_line_args.verbosity >= 1)
        printf("Building Control Flow Graphs: %s\n", fs->path);

    CfGraphFile cfgfile = build_control_flow_graphs(&fs->ast, &fs->types);
    for (const AstImport *imp = fs->ast.imports.ptr; imp < End(fs->ast.imports); imp++)
        if (!imp->used)
            show_warning(imp->location, "\"%s\" imported but not used", imp->specified_path);

    if(command_line_args.verbosity >= 2)
        print_control_flow_graphs(&cfgfile);

    if(command_line_args.verbosity >= 1)
        printf("Analyzing CFGs: %s\n", fs->path);
    simplify_control_flow_graphs(&cfgfile);
    if(command_line_args.verbosity >= 2)
        print_control_flow_graphs(&cfgfile);

    if (command_line_args.verbosity >= 1)
        printf("Building LLVM IR: %s\n", fs->path);

    LLVMModuleRef mod = codegen(&cfgfile, &fs->types);
    free_control_flow_graphs(&cfgfile);

    if (command_line_args.verbosity >= 2)
        print_llvm_ir(mod, false);

    /*
    If this fails, it is not just users writing dumb code, it is a bug in this compiler.
    This compiler should always fail with an error elsewhere, or generate valid LLVM IR.
    */
    LLVMVerifyModule(mod, LLVMAbortProcessAction, NULL);

    if (command_line_args.optlevel) {
        if (command_line_args.verbosity >= 1)
            printf("Optimizing %s (level %d)\n", fs->path, command_line_args.optlevel);
        optimize(mod, command_line_args.optlevel);
        if(command_line_args.verbosity >= 2)
            print_llvm_ir(mod, true);
    }

    char *objpath = compile_to_object_file(mod);
    LLVMDisposeModule(mod);
    return objpath;
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

static bool statement_conflicts_with_an_import(const AstStatement *stmt, const ExportSymbol *import)
{
    switch(stmt->kind) {
    case AST_STMT_FUNCTION:
        return import->kind == EXPSYM_FUNCTION && !strcmp(import->name, stmt->data.function.signature.name);
    case AST_STMT_DECLARE_GLOBAL_VAR:
    case AST_STMT_DEFINE_GLOBAL_VAR:
        return import->kind == EXPSYM_GLOBAL_VAR && !strcmp(import->name, stmt->data.vardecl.name);
    case AST_STMT_DEFINE_CLASS:
        return import->kind == EXPSYM_TYPE && !strcmp(import->name, stmt->data.classdef.name);
    case AST_STMT_DEFINE_ENUM:
        return import->kind == EXPSYM_TYPE && !strcmp(import->name, stmt->data.enumdef.name);
    default:
        assert(0);  // TODO
    }
}

static void add_imported_symbol(struct FileState *fs, const ExportSymbol *es, AstImport *imp)
{
    for (int i = 0; i < fs->ast.body.nstatements; i++) {
        if (statement_conflicts_with_an_import(&fs->ast.body.statements[i], es)) {
            const char *wat;
            switch(es->kind) {
                case EXPSYM_FUNCTION: wat = "function"; break;
                case EXPSYM_GLOBAL_VAR: wat = "global variable"; break;
                case EXPSYM_TYPE: wat = "type"; break;
            }
            fail(fs->ast.body.statements[i].location, "a %s named '%s' already exists", wat, es->name);
        }
    }

    struct GlobalVariable g;

    switch(es->kind) {
    case EXPSYM_FUNCTION:
        Append(&fs->types.functions, (struct SignatureAndUsedPtr){
            .signature = copy_signature(&es->data.funcsignature),
            .usedptr = &imp->used,
        });
        break;
    case EXPSYM_TYPE:
        Append(&fs->types.types, (struct TypeAndUsedPtr){
            .type=es->data.type,
            .usedptr=&imp->used,
        });
        break;
    case EXPSYM_GLOBAL_VAR:
        g = (GlobalVariable){
            .type = es->data.type,
            .usedptr = &imp->used,
        };

        assert(strlen(es->name) < sizeof g.name);
        strcpy(g.name, es->name);
        Append(&fs->types.globals, g);
        break;
    }
}

static void add_imported_symbols(struct CompileState *compst)
{
    for (struct FileState *to = compst->files.ptr; to < End(compst->files); to++) {
        List(struct FileState *) seen_before = {0};

        for (AstImport *imp = to->ast.imports.ptr; imp < End(to->ast.imports); imp++) {
            struct FileState *from = find_file(compst, imp->resolved_path);
            assert(from);
            if (from == to) {
                fail(imp->location, "the file itself cannot be imported");
            }

            for (int i = 0; i < seen_before.len; i++) {
                if (seen_before.ptr[i] == from) {
                    fail(imp->location, "file \"%s\" is imported twice", imp->specified_path);
                }
            }
            Append(&seen_before, from);

            for (struct ExportSymbol *es = from->pending_exports; es->name[0]; es++) {
                if (command_line_args.verbosity >= 2) {
                    const char *kindstr;
                    switch(es->kind) {
                        case EXPSYM_FUNCTION: kindstr="function"; break;
                        case EXPSYM_GLOBAL_VAR: kindstr="global var"; break;
                        case EXPSYM_TYPE: kindstr="type"; break;
                    }
                    printf("Adding imported %s %s: %s --> %s\n",
                        kindstr, es->name, from->path, to->path);
                }
                add_imported_symbol(to, es, imp);
            }
        }

        free(seen_before.ptr);
    }

    // Mark all exports as no longer pending.
    for (struct FileState *fs = compst->files.ptr; fs < End(compst->files); fs++) {
        for (struct ExportSymbol *es = fs->pending_exports; es->name[0]; es++)
            free_export_symbol(es);
        free(fs->pending_exports);
        fs->pending_exports = NULL;
    }
}

static void include_special_stdlib_file(struct CompileState *compst, const char *filename)
{
    char *path = malloc(strlen(compst->stdlib_path) + strlen(filename) + 123);
    sprintf(path, "%s/%s", compst->stdlib_path, filename);
    parse_file(compst, path, NULL);
    free(path);
}

int main(int argc, char **argv)
{
    init_target();
    init_types();
    char *stdlib = find_stdlib();
    parse_arguments(argc, argv);

    struct CompileState compst = { .stdlib_path = stdlib };
    if (command_line_args.verbosity >= 2) {
        printf("Target triple: %s\n", get_target()->triple);
        printf("Data layout: %s\n", get_target()->data_layout);
    }

    if (command_line_args.tokenize_only || command_line_args.parse_only) {
        FILE *f = open_the_file(command_line_args.infile, NULL);
        Token *tokens = tokenize(f, command_line_args.infile);
        fclose(f);
        if (command_line_args.tokenize_only) {
            print_tokens(tokens);
        } else {
            AstFile ast = parse(tokens, compst.stdlib_path);
            print_ast(&ast);
            free_ast(&ast);
        }
        free_tokens(tokens);
        return 0;
    }

    include_special_stdlib_file(&compst, "_assert_fail.jou");

#if defined(__NetBSD__)
    assert(sizeof(FILE) == 152);  // magic number in the startup file
#endif

#if defined(_WIN32) || defined(__APPLE__) || defined(__NetBSD__)
    include_special_stdlib_file(&compst, "_jou_startup.jou");
#endif

    parse_file(&compst, command_line_args.infile, NULL);
    parse_all_pending_files(&compst);

    if (command_line_args.verbosity >= 1)
        printf("Type-checking...\n");

    for (struct FileState *fs = compst.files.ptr; fs < End(compst.files); fs++) {
        if (command_line_args.verbosity >= 1)
            printf("  stage 1: %s\n", fs->path);
        fs->pending_exports = typecheck_stage1_create_types(&fs->types, &fs->ast);
    }
    add_imported_symbols(&compst);
    for (struct FileState *fs = compst.files.ptr; fs < End(compst.files); fs++) {
        if (command_line_args.verbosity >= 1)
            printf("  stage 2: %s\n", fs->path);
        fs->pending_exports = typecheck_stage2_populate_types(&fs->types, &fs->ast);
    }
    add_imported_symbols(&compst);
    for (struct FileState *fs = compst.files.ptr; fs < End(compst.files); fs++) {
        if (command_line_args.verbosity >= 1)
            printf("  stage 3: %s\n", fs->path);
        typecheck_stage3_function_and_method_bodies(&fs->types, &fs->ast);
    }

    char **objpaths = calloc(sizeof objpaths[0], compst.files.len + 1);
    for (struct FileState *fs = compst.files.ptr; fs < End(compst.files); fs++)
        objpaths[fs - compst.files.ptr] = compile_ast_to_object_file(fs);

    /*
    Check for missing main() as late as possible, so that other errors come first.
    This way Jou users can work on other functions before main() function is written.
    */
    struct FileState *mainfile = find_file(&compst, command_line_args.infile);
    assert(mainfile);
    if (!defines_main(&mainfile->ast))
        fail((Location){.filename=mainfile->path, .lineno=0}, "missing `main` function to execute the program");

    for (struct FileState *fs = compst.files.ptr; fs < End(compst.files); fs++) {
        free_ast(&fs->ast);
        free(fs->path);
        free_file_types(&fs->types);
    }
    free(compst.files.ptr);
    free(stdlib);

    char *exepath;
    if (command_line_args.outfile)
        exepath = strdup(command_line_args.outfile);
    else
        exepath = get_default_exe_path();

    run_linker((const char *const*)objpaths, exepath);
    for (int i = 0; objpaths[i]; i++)
        free(objpaths[i]);
    free(objpaths);

    int ret = 0;
    if (!command_line_args.outfile) {
        if(command_line_args.verbosity >= 1)
            printf("Run: %s\n", exepath);
        ret = run_exe(exepath, command_line_args.valgrind);
    }

    free(exepath);

    return ret;
}
