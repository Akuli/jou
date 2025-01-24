#!/bin/bash
set -ex
make
./jou -o x --linker-flags "-L/usr/lib/llvm-16/lib  -lLLVM-16" compiler/main.jou
./x bug.jou
