// Self-update: "jou --update" updates the Jou compiler.
#ifndef _WIN32
    // setenv() stuff
    #define _POSIX_C_SOURCE 200112L
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

    char *end = &s[strlen(s) - 1];
    while (end > start && isspace(end[-1]))
        end--;

    *end = '\0';
    memmove(s, start, end-start+1);
}

#ifdef _WIN32
static char *read_jouversiontxt(const char *dir)
{
    char *jouversiontxt_path = malloc(strlen(dir) + 20);
    sprintf(jouversiontxt_path, "%s/jouversion.txt", dir);

    FILE *f = fopen(jouversiontxt_path, "rb");
    if (!f)
        goto error;

    char line[100] = {0};
    fgets(line, sizeof line, f);
    fclose(f);

    trim_whitespace(line);
    if (!line[0])
        goto error;

    free(jouversiontxt_path);
    return strdup(line);

error:
    fprintf(stderr, "error: cannot read version from %s\n", jouversiontxt_path);
    fail();
}
#endif

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

#ifdef _WIN32
    char *current_version = read_jouversiontxt(exedir);
    char *latest_version = get_latest_github_release();
    if (!strcmp(current_version, latest_version)) {
        printf("You already have the latest version of Jou.\n");
        exit(0);
    }
    printf("TODO: should update now i guess\n");
    free(current_version);
    free(latest_version);
#else
    printf("Run \"git pull && make\"?");
    confirm();

    setenv("JOU_DIR", exedir, true);
    if (system("cd \"$JOU_DIR\" && git pull && make"))
        fail();
#endif

    free(exe);
    printf("\n\nYou now have the latest version of Jou :)\n");
}
