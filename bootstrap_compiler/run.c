#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "jou_compiler.h"


int run_exe(const char *exepath, bool valgrind)
{
    char *command = malloc(strlen(exepath) + 1000);
#ifdef _WIN32
    assert(!valgrind);
    sprintf(command, "\"%s\"", exepath);
    char *p;
    while ((p = strchr(command, '/')))
        *p = '\\';
#else
    if (valgrind)
        sprintf(command, "valgrind -q --leak-check=full --show-leak-kinds=all --error-exitcode=1 '%s'", exepath);
    else
        sprintf(command, "'%s'", exepath);
#endif

    // Make sure that everything else shows up before the user's prints.
    fflush(stdout);
    fflush(stderr);

    int ret = system(command);
    free(command);
    return !!ret;
}
