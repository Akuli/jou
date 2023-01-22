#include "util.h"

static void delete_slice(char *start, char *end)
{
    memmove(start, end, strlen(end) + 1);
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
}
