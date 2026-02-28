#!/bin/bash
set -e

# Only a very basic command, you will need to mess around with the flags so
# this is intended only to get you started

jou -o toml_parser toml_parser.jou

# Reasons to skip:
#   valid/spec-example-1-compact: contains a date
#   valid/spec-example-1: contains a date
#   valid/string/quoted-unicode: uses \u0000 which is not supported
#   valid/string/unicode-escape: uses \u0000 and \U00000000 which are not supported
./toml-test-* test -decoder ./toml_parser -run "valid/a*"
./toml-test-* test -decoder ./toml_parser -run "valid/b*"
./toml-test-* test -decoder ./toml_parser -run "valid/c*"
./toml-test-* test -decoder ./toml_parser -run "valid/d*"
./toml-test-* test -decoder ./toml_parser -run "valid/e*" \
    -skip valid/example \
    -skip-must-err
./toml-test-* test -decoder ./toml_parser -run "valid/f*"
./toml-test-* test -decoder ./toml_parser -run "valid/g*"
./toml-test-* test -decoder ./toml_parser -run "valid/h*"
./toml-test-* test -decoder ./toml_parser -run "valid/i*"
./toml-test-* test -decoder ./toml_parser -run "valid/j*"
./toml-test-* test -decoder ./toml_parser -run "valid/k*"
./toml-test-* test -decoder ./toml_parser -run "valid/l*"
./toml-test-* test -decoder ./toml_parser -run "valid/m*"
./toml-test-* test -decoder ./toml_parser -run "valid/s*" \
    -skip valid/spec-example-1-compact \
    -skip valid/spec-example-1 \
    -skip valid/string/quoted-unicode \
    -skip valid/string/unicode-escape \
    -skip-must-err
