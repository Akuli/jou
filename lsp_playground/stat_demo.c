#include <sys/stat.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>

int main(){
	struct stat sb;
	int ret = stat("stat_demo.c", &sb);
	assert(ret == 0);
	printf("mtim = %lld, %d\n", sb.st_mtim.tv_sec, (int)sb.st_mtim.tv_nsec);
	printf("mtim offset = %zd\n", offsetof(struct stat, st_mtim));
	printf("st_size = %lld\n", sb.st_size);
	printf("st_size offset = %zd\n", offsetof(struct stat, st_size));
	printf("sizeof(stat) = %zd\n", sizeof(struct stat));
	return 0;
}
