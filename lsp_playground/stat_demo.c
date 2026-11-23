#include <sys/stat.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>

int main(){
	struct stat sb;
	int ret = stat("stat_demo.c", &sb);
	assert(ret == 0);
	printf("mtim = %lld, %d\n", sb.st_mtim.tv_sec, (int)sb.st_mtim.tv_nsec);
	printf("offset = %zd\n", offsetof(struct stat, st_mtim));
	return 0;
}
