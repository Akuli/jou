LLVM_CONFIG ?= $(shell which llvm-config-11 || which llvm-config-13)

SRC := $(wildcard src/*.c)

ifeq ($(CC),cc)
	# default c compiler --> use clang
	CC := $(shell $(LLVM_CONFIG) --bindir)/clang
endif
CFLAGS += -Wall -Wextra -Wpedantic
CFLAGS += -Werror=switch -Werror=implicit-function-declaration -Werror=incompatible-pointer-types -Werror=implicit-fallthrough
CFLAGS += -std=c11
CFLAGS += -g
CFLAGS += $(shell $(LLVM_CONFIG) --cflags)
LDFLAGS ?= $(shell $(LLVM_CONFIG) --ldflags --libs)

obj/%.o: src/%.c $(wildcard src/*.h)
	mkdir -vp obj && $(CC) -c $(CFLAGS) $< -o $@

all: jou compile_flags.txt

# point clangd to the right include folder so i don't get red squiggles in my editor
compile_flags.txt:
	echo "-I$(shell $(LLVM_CONFIG) --includedir)"  > compile_flags.txt

jou: $(SRC:src/%.c=obj/%.o)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -rvf obj jou jou.exe tests/tmp
