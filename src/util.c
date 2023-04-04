#ifndef _WIN32
    // readlink() stuff
    #define _POSIX_C_SOURCE 200112L
    #include <unistd.h>
#endif

#ifdef __APPLE__
    #include <mach-o/dyld.h>  // _NSGetExecutablePath
#endif

#include "util.h"
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>

static void delete_slice(char *start, char *end)
{
    memmove(start, end, strlen(end) + 1);
}

void trim_whitespace(char *s)
{
    char *start = s;
    while (*start && isspace(*start))
        start++;

    char *end = &s[strlen(s)];
    while (end > start && isspace(end[-1]))
        end--;

    *end = '\0';
    delete_slice(s, start);
}

void simplify_path(char *path)
{
#ifdef _WIN32
    // Backslash to forward slash.
    for (char *p = path; *p; p++)
        if (*p == '\\')
            *p = '/';
#endif

    // Delete "." components.
    while (!strncmp(path, "./", 2))
        delete_slice(path, path+2);
    char *p;
    while ((p = strstr(path, "/./")))
        delete_slice(p, p+2);

    // Delete unnecessary ".." components.
    while ((p = strstr(path, "/../"))) {
        char *delstart = p;
        while (delstart > path && delstart[-1] != '/')
            delstart--;
        delete_slice(delstart, p+4);
    }
}


// argv[0] doesn't work as expected when Jou is ran through PATH.
char *find_current_executable(void)
{
    char *result = NULL;
    const char *err = NULL;

#ifdef _WIN32
    extern char *_pgmptr;  // A documented global variable in Windows. Full path to executable.
    result = strdup(_pgmptr);
#elif defined(__APPLE__)
    uint32_t n = 1;
    result = malloc(n);
    if (_NSGetExecutablePath(result, &size) < 0) {  // didn't fit
        result = realloc(result, n);
        int ret = _NSGetExecutablePath(result, &n);
        assert(ret == 0);
    }
#else
    ssize_t ret;
    int n = 1;
    do {
        n *= 2;
        result = realloc(result, n);
        memset(result, 0, n);  // readlink() doesn't nul terminate
        ret = readlink("/proc/self/exe", result, n);
    } while (ret == n);
    if (ret<0)
        err = strerror(errno);
#endif

    if(err) {
        fprintf(stderr, "error finding current executable, needed to find the Jou standard library: %s\n", err);
        exit(1);
    }
    return result;
}
