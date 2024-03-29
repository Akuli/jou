#ifdef _WIN32
    #include <direct.h>
    #define jou_mkdir(x) _mkdir((x))
#else
    #define jou_mkdir(x) mkdir((x), 0777)  // this is what mkdir in bash does according to strace
    #include "../config.h"  // specifies path where clang is installed
#endif

#include "jou_compiler.h"
#include <unistd.h>
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
    if (strrchr(path, '\\'))
        path = strrchr(path, '\\') + 1;
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

    char *linker_flags = malloc_sprintf("-lm %s", command_line_args.linker_flags ? command_line_args.linker_flags : "");

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

    if (command_line_args.verbosity >= 2)
        printf("Running linker: %s\n", command);
    else if (command_line_args.verbosity >= 1)
        printf("Running linker\n");

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

static void write_gitinore(const char *p)
{
    char *filename = malloc_sprintf("%s/.gitignore", p);
    if (access(filename, F_OK) == 0) {
        free(filename);
        return;
    }
    FILE *gitinore = fopen(filename, "w");
    if (gitinore != NULL) {
        fprintf(gitinore, "*");
        fclose(gitinore);
    }
    free(filename);
}

static char *get_path_to_file_in_jou_compiled(const char *filename)
{
    /*
    Place compiled files so that it's difficult to get race conditions when
    compiling multiple Jou files simultaneously (tests do that)
    */
    char *tmp = strdup(command_line_args.infile);
    char *path1 = strdup(dirname(tmp));
    free(tmp);
    char *path2 = malloc_sprintf("%s/jou_compiled", path1);
    tmp = get_filename_without_suffix(command_line_args.infile);
    char *path3 = malloc_sprintf("%s/%s", path2, tmp);
    free(tmp);
    char *path4 = malloc_sprintf("%s/%s", path3, filename);

    // path1 is known to exist
    mkdir_exist_ok(path2);
    write_gitinore(path2);
    mkdir_exist_ok(path3);
    free(path1);
    free(path2);
    free(path3);
    return path4;
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
