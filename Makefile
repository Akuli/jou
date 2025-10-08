ifneq (,$(findstring Windows,$(OS)))
	include Makefile.windows
else
	include Makefile.posix
endif

bcompiler.c: Makefile jou2c.py $(wildcard compiler/*.jou compiler/*/*.jou compiler/*/*/*.jou)
	python3 jou2c.py compiler/main.jou > bcompiler.c

bcompiler: bcompiler.c config.jou Makefile
	cc -g -O1 bcompiler.c -o bcompiler $(shell grep ^link config.jou | cut -d'"' -f2)
