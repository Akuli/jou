#!/bin/bash
set -ex

wine ~/wjou/jou.exe -o joutest.exe tools/joutest/main.jou

cd tests/tests_of_joutest/should_succeed/markdown
wine ../../../../joutest.exe -v
