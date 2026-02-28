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
./toml-test-* test -decoder ./toml_parser -run "valid/*" \
    -skip valid/example \
    -skip valid/spec-example-1-compact \
    -skip valid/spec-example-1 \
    -skip valid/string/quoted-unicode \
    -skip valid/string/unicode-escape \
    -skip-must-err
