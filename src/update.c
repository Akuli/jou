// Self-update: "jou --update" updates the Jou compiler.

#ifdef _WIN32
    #include <direct.h>
    #define chdir _chdir
#else
    #include <unistd.h>
#endif

#include <libgen.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdnoreturn.h>
#include "util.h"

static noreturn void fail()
{
    char *s =
        "\n"
        "Updating Jou failed. If you need help, please create an issue on GitHub:\n"
        "   https://github.com/Akuli/jou/issues/new"
        ;
    puts(s);
    exit(1);
}

static void trim_whitespace(char *s)
{
    char *start = s;
    while (*start && isspace(*start))
        start++;

    char *end = &s[strlen(s)];
    while (end > start && isspace(end[-1]))
        end--;

    *end = '\0';
    memmove(s, start, end-start+1);
}

static void confirm()
{
    printf(" (y/n) ");
    fflush(stdout);

    char line[50] = {0};
    fgets(line, sizeof line, stdin);
    trim_whitespace(line);

    bool yes = !strcmp(line, "Y") || !strcmp(line, "y");
    if (!yes) {
        printf("Aborted.\n");
        exit(1);
    }
}

void update_jou_compiler()
{
    char *exe = find_current_executable();
    const char *exedir = dirname(exe);
    printf("Installation directory: %s\n\n", exedir);

    if (chdir(exedir) == -1) {
        fprintf(stderr, "chdir(\"%s\") failed: %s\n", exedir, strerror(errno));
        fail();
    }

#ifdef _WIN32
    printf("Download and install the latest version of Jou from GitHub releases?");
    confirm();
    if (system("powershell -ExecutionPolicy bypass -File update.ps1") != 0)
        fail();
#else
    printf("Run \"git pull && make\"?");
    confirm();
    if (system("git pull && make"))
        fail();
#endif

    free(exe);
    printf("\n\nYou now have the latest version of Jou :)\n");
}
