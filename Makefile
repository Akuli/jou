# FIXME: Presumably we should compile with clang-11, because we depend on it anyway.

LLVM_CONFIG ?= llvm-config-11

SRC := $(wildcard src/*.c)

CFLAGS += -Wall -Wextra -Wpedantic
CFLAGS += -Werror=switch -Werror=incompatible-pointer-types
CFLAGS += -Wno-format-truncation
CFLAGS += -std=c11
CFLAGS += -g
CFLAGS += $(shell $(LLVM_CONFIG) --cflags)
LDFLAGS += $(shell $(LLVM_CONFIG) --ldflags --libs)

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
	rm -rvf obj jou tests/tmp

.PHONY: test
test: all
	tests/runtests.sh
