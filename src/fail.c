#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>

#include "jou_compiler.h"

static void print_message(struct Location location, const char *start_fmt, const char *fmt, va_list ap)
{
    // When stdout is redirected to same place as stderr, and not line-buffered,
    // make sure to show normal printf()s before our error message
    fflush(stdout);
    fflush(stderr);

    fprintf(stderr, start_fmt, location.filename);
    if (location.lineno != 0)
        fprintf(stderr, ", line %d", location.lineno);
    fprintf(stderr, ": ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

void show_warning(struct Location location, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    print_message(location, "compiler warning for file \"%s\"", fmt, ap);
    va_end(ap);
}

noreturn void fail_with_error(struct Location location, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    print_message(location, "compiler error in file \"%s\"", fmt, ap);
    va_end(ap);
    exit(1);
}
