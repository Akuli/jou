#!/bin/bash
set -ex

wine ~/wjou/jou.exe -o joutest.exe tools/joutest/main.jou

cd tests/tests_of_joutest/should_succeed/basic_ok
echo '
import "stdlib/io.jou"
def main() -> int:
    puts("wrong")
    return 0' | wine ../../../../joutest.exe
