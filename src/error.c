#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "error.h"


noreturn void fail_with_error(struct Location location, const char *fmt, ...)
{
    fprintf(stderr, "compiler error in file \"%s\"", location.filename);
    if (location.lineno != 0)
        fprintf(stderr, ", line %d", location.lineno);
    fprintf(stderr, ": ");

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");
    exit(1);
}
