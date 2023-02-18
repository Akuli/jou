#ifdef _WIN32
    #include <direct.h>
    #define jou_mkdir(x) _mkdir((x))
#else
    #define jou_mkdir(x) mkdir((x), 0777)  // this is what mkdir in bash does according to strace
    #include "../config.h"  // specifies path where clang is installed
#endif

#include "jou_compiler.h"
#include <libgen.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <llvm-c/TargetMachine.h>

static void compile_to_object_file(LLVMModuleRef module, const char *path, const CommandLineFlags *flags)
{
    const char *triple = LLVMGetTarget(module);
    if (flags->verbose)
        printf("Emitting object file \"%s\" for %s\n", path, triple);

    char *error = NULL;
    LLVMTargetRef target = NULL;
    if (LLVMGetTargetFromTriple(LLVMGetTarget(module), &target, &error)) {
        assert(error);
        fprintf(stderr, "LLVMGetTargetFromTriple failed: %s\n", error);
        exit(1);
    }
    assert(!error);
    assert(target);

    // The CPU name is the first component of the target triple.
    // But it's not quite that simple, achthually the typical cpu name is x86-64 instead of x86_64...
    char cpu[100];
    snprintf(cpu, sizeof cpu, "%.*s", (int)strcspn(triple, "-"), triple);
    if (!strcmp(cpu, "x86_64"))
        strcpy(cpu, "x86-64");

    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target, triple, cpu, "", LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault);
    assert(machine);

    char *tmppath = strdup(path);
    if (LLVMTargetMachineEmitToFile(machine, module, tmppath, LLVMObjectFile, &error)) {
        assert(error);
        fprintf(stderr, "failed to emit object file \"%s\": %s\n", path, error);
        exit(1);
    }
    free(tmppath);
    assert(!error);

    LLVMDisposeTargetMachine(machine);
}

static char *malloc_sprintf(const char *fmt, ...)
{
    int size;

    va_list ap;
    va_start(ap, fmt);
    size = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    assert(size >= 0);
    char *str = malloc(size+1);
    assert(str);

    va_start(ap, fmt);
    vsprintf(str, fmt, ap);
    va_end(ap);

    return str;
}

static void run_linker(const char *objpath, const char *exepath, const CommandLineFlags *flags)
{
    char *jou_exe = find_current_executable();
    const char *instdir = dirname(jou_exe);

    char *linker_flags;
    if (flags->linker_flags)
        linker_flags = malloc_sprintf("-lm %s", flags->linker_flags);
    else
        linker_flags = strdup("-lm");

    char *command;
#ifdef _WIN32
    char *gcc = malloc_sprintf("%s\\mingw64\\bin\\gcc.exe", instdir);
    if (stat(gcc, &(struct stat){0}) != -1) {
        // The Windows builds come with the GNU linker.
        // Windows quoting is weird, in this command it strips the outermost quotes.
        command = malloc_sprintf("\"\"%s\" \"%s\" -o \"%s\" %s\"", gcc, objpath, exepath, linker_flags);
    } else {
        // Use clang from PATH. Convenient when developing Jou locally.
        command = malloc_sprintf("clang \"%s\" -o \"%s\" %s", objpath, exepath, linker_flags);
    }
    free(gcc);
#else
    // Assume clang is installed and use it to link. Could use lld, but clang is needed anyway.
    (void)instdir;
    command = malloc_sprintf("'%s' '%s' -o '%s' %s", JOU_CLANG_PATH, objpath, exepath, linker_flags);
#endif

    if (flags->verbose)
        puts(command);
    if (system(command))
        exit(1);

    free(jou_exe);
    free(command);
    free(linker_flags);
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
    run_linker(objpath, exepath, flags);
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
#ifdef _WIN32
    sprintf(command, "\"%s\"", exepath);
    char *p;
    while ((p = strchr(command, '/')))
        *p = '\\';
#else
    sprintf(command, "'%s'", exepath);
#endif
    free(exepath);

    if (flags->verbose)
        puts(command);

    // Make sure that everything else shows up before the user's prints.
    fflush(stdout);
    fflush(stderr);

    int ret = system(command);
    free(command);
    return !!ret;
}
