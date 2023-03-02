ifeq (,$(findstring Windows,$(OS)))
	# Non-Windows
	LLVM_CONFIG ?= $(shell which llvm-config-13 || which llvm-config-11)
	ifeq ($(CC),cc)
		# default c compiler --> use clang
		CC := $(shell $(LLVM_CONFIG) --bindir)/clang
	endif
	CFLAGS += $(shell $(LLVM_CONFIG) --cflags)
	LDFLAGS ?= $(shell $(LLVM_CONFIG) --ldflags --libs)
else
	# Windows
	EXE := .exe
endif

SRC := $(wildcard src/*.c)

CFLAGS += -Wall -Wextra -Wpedantic
CFLAGS += -Werror=switch -Werror=implicit-function-declaration -Werror=incompatible-pointer-types -Werror=implicit-fallthrough
CFLAGS += -std=c11
CFLAGS += -g

all: jou$(EXE) compile_flags.txt

# point clangd to the right include folder so i don't get red squiggles in my editor
compile_flags.txt:
	echo "-I$(shell $(LLVM_CONFIG) --includedir)"  > compile_flags.txt

config.h:
	echo "// auto-generated by Makefile" > $@
	(if echo $$OS | (! grep Windows); then \
		echo "#define JOU_CLANG_PATH \"`$(LLVM_CONFIG) --bindir`/clang\"" >> $@; \
	fi)

config.jou:
	echo "# auto-generated by Makefile" > $@
	(if echo $$OS | (! grep Windows); then \
		echo "def get_jou_clang_path() -> byte*:" >> $@; \
		echo "    return \"`$(LLVM_CONFIG) --bindir`/clang\"" >> $@; \
	fi)

obj/%.o: src/%.c $(wildcard src/*.h) config.h
	mkdir -vp obj && $(CC) -c $(CFLAGS) $< -o $@

jou$(EXE): $(SRC:src/%.c=obj/%.o)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

self_hosted_compiler$(EXE): jou$(EXE) config.jou $(wildcard self_hosted/*.jou)
	./jou -O1 -o $@ --linker-flags "$(LDFLAGS)" self_hosted/main.jou

.PHONY: clean
clean:
	rm -rvf obj jou jou.exe tmp config.h config.jou compile_flags.txt self_hosted_compiler self_hosted_compiler.exe
	find -name jou_compiled -print -exec rm -rf '{}' +
