// Self-update: "jou --update" updates the Jou compiler.
#include <libgen.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "util.h"

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

static bool detect_jou_git_repo(const char *dir)
{
    char *git_config_path = malloc(strlen(dir) + 20);
    sprintf(git_config_path, "%s/.git/config", dir);

    FILE *f = fopen(git_config_path, "rb");
    free(git_config_path);
    if (!f)
        return false;

    char line[1024];
    while (fgets(line, sizeof line, f)) {
        trim_whitespace(line);
        if (!strcmp(line, "url = https://github.com/Akuli/jou")) {
            fclose(f);
            return true;
        }
    }

    fclose(f);
    return false;
}

// Return values:
//    NULL = not downloaded from github releases,
//    other = a version number
static char *detect_github_releases_download(const char *dir)
{
    char *jouversiontxt_path = malloc(strlen(dir) + 20);
    sprintf(jouversiontxt_path, "%s/jouversion.txt", dir);

    FILE *f = fopen(jouversiontxt_path, "rb");
    free(jouversiontxt_path);
    if (!f)
        return false;

    char line[100] = {0};
    fgets(line, sizeof line, f);
    fclose(f);

    trim_whitespace(line);
    if (strlen(line) > 2)
        return strdup(line);
    return NULL;
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

static void fail()
{
    char *s =
        "\n"
        "Updating Jou failed. If you need help, please create an issue on GitHub:\n"
        "   https://github.com/Akuli/jou/issues/new"
        ;
    puts(s);
    exit(1);
}

void update_jou_compiler()
{
    char *exe = find_current_executable();
    const char *exedir = dirname(exe);
    printf("Installation directory: %s\n\n", exedir);

    if (detect_jou_git_repo(exedir)) {
        printf("Jou has been installed with \"git clone\".\n");
        printf("Run \"git pull && make\"?");
        confirm();
        if (system("git pull && make"))
            fail();
    } else if (detect_github_releases_download(exedir)) {
        printf("Not implemented yet :(\n");
        fail();
    } else {
        printf("Cannot detect how Jou is installed.\n");
        fail();
    }

    free(exe);
    printf("\n\nYou now have the latest version of Jou :)\n");
}
