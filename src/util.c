#ifndef _WIN32
    // readlink() stuff
    #define _POSIX_C_SOURCE 200112L
    #include <unistd.h>
#endif // _WIN32

#include "util.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

// argv[0] doesn't work as expected when Jou is ran through PATH.
char *find_current_executable(void)
{
    char *result;
    const char *err;

#ifdef _WIN32
    extern char *_pgmptr;  // A documented global variable in Windows. Full path to executable.
    result = strdup(_pgmptr);
    err = NULL;
#else
    int n = 10000;
    result = calloc(1, n);
    ssize_t ret = readlink("/proc/self/exe", result, n);

    if (ret < 0)
        err = strerror(errno);
    else if (ret == n) {
        static char s[100];
        sprintf(s, "path is more than %d bytes long", n);
        err=s;
    } else {
        assert(0<ret && ret<n);
        err = NULL;
    }
#endif

    if(err) {
        fprintf(stderr, "error finding current executable, needed to find the Jou standard library: %s\n", err);
        exit(1);
    }
    return result;
}


