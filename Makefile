CFLAGS += -Wall -Wextra -Wpedantic
CFLAGS += -Werror=switch -Werror=implicit-function-declaration -Werror=incompatible-pointer-types -Werror=implicit-fallthrough
CFLAGS += -std=c11
CFLAGS += -g

ifneq (,$(findstring Windows,$(OS)))
	include Makefile.windows
else
	include Makefile.posix
endif
