#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
int main()
{
    printf("%d\n", (int) offsetof(struct dirent, d_name));
    return 0;
}
