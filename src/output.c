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

static char *get_filename_without_suffix(const char *path)
{
    if (strrchr(path, '/'))
        path = strrchr(path, '/') + 1;
#ifdef _WIN32
    if (strrchr(filename, '\\'))
        filename = strrchr(path, '\\') + 1;
#endif

    int len = strlen(path);
    if (len>4 && !strcmp(&path[len-4], ".jou"))
        len -= 4;

    char *result = malloc(len+1);
    memcpy(result, path, len);
    result[len] = '\0';

    return result;
}

void run_linker(const char *const *objpaths, const char *exepath)
{
    char *jou_exe = find_current_executable();
    const char *instdir = dirname(jou_exe);

    char *linker_flags;
    if (command_line_args.linker_flags)
        linker_flags = malloc_sprintf("-lm %s", command_line_args.linker_flags);
    else
        linker_flags = strdup("-lm");

    List(char) quoted_object_files = {0};
    for (int i = 0; objpaths[i]; i++) {
        if (i)
            Append(&quoted_object_files, ' ');
        Append(&quoted_object_files, '"');
        AppendStr(&quoted_object_files, objpaths[i]);
        Append(&quoted_object_files, '"');
    }
    Append(&quoted_object_files, '\0');

    char *command;
#ifdef _WIN32
    // Assume mingw with clang has been downloaded with windows_setup.sh.
    // Could also use clang, but gcc has less dependencies so we can make the Windows zips smaller.
    // Windows quoting is weird. The outermost quotes get stripped here.
    command = malloc_sprintf("\"\"%s\\mingw64\\bin\\gcc.exe\" %s -o \"%s\" %s\"", instdir, quoted_object_files.ptr, exepath, linker_flags);
#else
    // Assume clang is installed and use it to link. Could use lld, but clang is needed anyway.
    (void)instdir;
    command = malloc_sprintf("'%s' %s -o '%s' %s", JOU_CLANG_PATH, quoted_object_files.ptr, exepath, linker_flags);
#endif
    free(quoted_object_files.ptr);
    free(jou_exe);
    free(linker_flags);

    if (command_line_args.verbosity >= 1)
        printf("Running linker command: %s\n", command);
    if (system(command))
        exit(1);
    free(command);
}

static void mkdir_exist_ok(const char *p)
{
    if (jou_mkdir(p) == 0 || errno == EEXIST)
        return;
    fprintf(stderr, "%s: cannot create directory \"%s\": %s\n", command_line_args.argv0, p, strerror(errno));
    exit(1);
}

static char *get_path_to_file_in_jou_compiled(const char *filename)
{
    // Place it to current working directory. We need subdirectories based on the
    // jou file passed as argument, otherwise there's a race condition when running
    // the tests (they compile and run 2 jou files in parallel)
    char *foldername = get_filename_without_suffix(command_line_args.infile);
    char *path = malloc(strlen(foldername) + strlen(filename) + 100);
    strcpy(path, "jou_compiled");
    sprintf(path, "jou_compiled/%s", foldername);
    free(foldername);

    mkdir_exist_ok("jou_compiled");
    mkdir_exist_ok(path);
    strcat(path, "/");
    strcat(path, filename);
    return path;
}

char *get_default_exe_path(void)
{
    char *name = get_filename_without_suffix(command_line_args.infile);
#ifdef _WIN32
    name = realloc(name, strlen(name) + 10);
    strcat(name, ".exe");
#endif

    char *path = get_path_to_file_in_jou_compiled(name);
    free(name);
    return path;
}

char *compile_to_object_file(LLVMModuleRef module)
{
    char *objname = get_filename_without_suffix(LLVMGetSourceFileName(module, (size_t[]){0}));
    objname = realloc(objname, strlen(objname) + 10);
#ifdef _WIN32
    strcat(objname, ".obj");
#else
    strcat(objname, ".o");
#endif

    char *path = get_path_to_file_in_jou_compiled(objname);
    free(objname);

    if (command_line_args.verbosity >= 1)
        printf("Emitting object file: %s\n", path);

    char *tmppath = strdup(path);
    char *error = NULL;
    if (LLVMTargetMachineEmitToFile(get_target()->target_machine_ref, module, tmppath, LLVMObjectFile, &error)) {
        assert(error);
        fprintf(stderr, "failed to emit object file \"%s\": %s\n", path, error);
        exit(1);
    }
    free(tmppath);
    assert(!error);
    return path;
}

int run_exe(const char *exepath)
{
    char *command = malloc(strlen(exepath) + 50);
#ifdef _WIN32
    sprintf(command, "\"%s\"", exepath);
    char *p;
    while ((p = strchr(command, '/')))
        *p = '\\';
#else
    sprintf(command, "'%s'", exepath);
#endif

    // Make sure that everything else shows up before the user's prints.
    fflush(stdout);
    fflush(stderr);

    int ret = system(command);
    free(command);
    return !!ret;
}
