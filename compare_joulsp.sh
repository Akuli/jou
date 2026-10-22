#!/usr/bin/env bash

set -e -o pipefail

echo "make jou"
make jou
echo "jou -o joulsp tools/joulsp/main.jou"
jou -o joulsp tools/joulsp/main.jou

for file in tests/syntax_error/*.jou; do
    echo $file
    diff -u --color <(./jou --check $file 2>&1 || true) <(./joulsp $file 2>&1 || true)
done
