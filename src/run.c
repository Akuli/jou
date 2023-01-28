#ifdef _WIN32
    #include <windows.h>
    #define jou_mkdir(x) _mkdir((x), NULL)
#else
    #include <sys/stat.h>
    #define jou_mkdir(x) mkdir((x), 0777)  // this is what mkdir in bash does according to strace
#endif

#include "jou_compiler.h"
#include <libgen.h>
#include <errno.h>
#include <llvm-c/TargetMachine.h>

static void compile_to_object_file(LLVMModuleRef module, const char *path, const CommandLineFlags *flags)
{
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmParsers();
    LLVMInitializeAllAsmPrinters();

    char *triple = LLVMGetDefaultTargetTriple();
    assert(triple);
    if (flags->verbose)
        printf("Emitting object file \"%s\" for %s\n", path, triple);

    // The CPU name is the first component of the target triple.
    // But it's not quite that simple, achthually the typical cpu name is x86-64 instead of x86_64...
    char cpu[100];
    snprintf(cpu, sizeof cpu, "%.*s", (int)strcspn(triple, "-"), triple);
    if (!strcmp(cpu, "x86_64"))
        strcpy(cpu, "x86-64");

    LLVMTargetRef target = LLVMGetTargetFromName(cpu);
    assert(target);

    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target, triple, cpu, "", LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault);
    assert(machine);

    char *error = NULL;
    char *tmppath = strdup(path);
    if (LLVMTargetMachineEmitToFile(machine, module, tmppath, LLVMObjectFile, &error)) {
        assert(error);
        fprintf(stderr, "failed to emit object file \"%s\": %s\n", path, error);
        exit(1);
    }
    free(tmppath);
    assert(!error);

    LLVMDisposeTargetMachine(machine);
    LLVMDisposeMessage(triple);
}

static void link(const char *objpath, const char *exepath, const CommandLineFlags *flags)
{
#ifdef _WIN32
    #error TODO
#else
    char *command = malloc(strlen(JOU_CLANG_PATH) + strlen(objpath) + strlen(exepath) + 500);
    sprintf(command, "'%s' '%s' -o '%s'", JOU_CLANG_PATH, objpath, exepath);
#endif

    if (flags->verbose)
        puts(command);
    if (system(command))
        exit(1);
    free(command);
}

static char *get_filename_without_suffix(const LLVMModuleRef module)
{
    const char *filename = LLVMGetSourceFileName(module, (size_t[]){0});

    if (strrchr(filename, '/'))
        filename = strrchr(filename, '/') + 1;
#ifdef _WIN32
    if (strrchr(filename, '\\'))
        filename = strrchr(filename, '\\') + 1;
#endif

    int len = strlen(filename);
    if (len>4 && !strcmp(&filename[len-4], ".jou"))
        len -= 4;

    char *result = malloc(len+1);
    memcpy(result, filename, len);
    result[len] = '\0';
    
    return result;
}

char *get_path_to_file_in_jou_compiled(LLVMModuleRef module, const char *filename)
{
    char *sourcepath = strdup(LLVMGetSourceFileName(module, (size_t[]){0}));

    char *result = malloc(strlen(sourcepath) + strlen(filename) + 100);
    char *tmp = strdup(sourcepath);
    sprintf(result, "%s/jou_compiled", dirname(tmp));
    free(tmp);

    if (jou_mkdir(result) == -1 && errno != EEXIST) {
        fail_with_error(
            (Location){.filename=sourcepath}, "cannot create directory \"%s\": %s",
            result, strerror(errno));
    }
    free(sourcepath);

    strcat(result, "/");
    strcat(result, filename);
    return result;
}

void compile_to_exe(LLVMModuleRef module, const char *exepath, const CommandLineFlags *flags)
{
    char *objname = get_filename_without_suffix(module);
    objname = realloc(objname, strlen(objname) + 10);
#ifdef _WIN32
    strcat(objname, ".obj");
#else
    strcat(objname, ".o");
#endif

    char *objpath = get_path_to_file_in_jou_compiled(module, objname);
    free(objname);

    compile_to_object_file(module, objpath, flags);
    link(objpath, exepath, flags);
    free(objpath);
}

int run_program(LLVMModuleRef module, const CommandLineFlags *flags)
{
    char *exename = get_filename_without_suffix(module);
#ifdef _WIN32
    exename = realloc(exename, strlen(exename) + 10);
    strcat(exename, ".exe");
#endif

    char *exepath = get_path_to_file_in_jou_compiled(module, exename);
    free(exename);

    compile_to_exe(module, exepath, flags);

    char *command = malloc(strlen(exepath) + 50);
    sprintf(command, "\"%s\"", exepath);
    free(exepath);

    if (flags->verbose)
        puts(command);

    int ret = system(command);
    free(command);
    return !!ret;
}
