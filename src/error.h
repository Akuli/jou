#ifndef ERROR_H
#define ERROR_H

#include <stdnoreturn.h>

struct Location {
    const char *filename;
    int lineno;
};

noreturn void fail_with_error(struct Location location, const char *fmt, ...);

#endif
