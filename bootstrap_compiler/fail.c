#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>

#include "jou_compiler.h"

static void print_message(Location location, const char *start_fmt, const char *fmt, va_list ap)
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
    fflush(stderr);  // Make sure compiler warnings appear before program output
}

void show_warning(Location location, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    print_message(location, "bootstrap compiler warning for file \"%s\"", fmt, ap);
    va_end(ap);
}

noreturn void fail(Location location, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    print_message(location, "bootstrap compiler error in file \"%s\"", fmt, ap);
    va_end(ap);
    exit(1);
}
