#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>

#include "jou_compiler.h"


noreturn void fail_with_error(struct Location location, const char *fmt, ...)
{
    // When stdout is redirected to same place as stderr, and not line-buffered,
    // make sure to show normal printf()s before our error message
    fflush(stdout);

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
