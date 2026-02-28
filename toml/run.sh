#!/bin/bash
set -e

# Only a very basic command, you will need to mess around with the flags so
# this is intended only to get you started

jou -o toml_parser toml_parser.jou

# Reasons to skip:
#   valid/string/unicode-escape: uses \u0000 and \U00000000 which are not supported
#   valid/string/quoted-unicode: uses \u0000 which is not supported
./toml-test-* test -decoder ./toml_parser -run "valid/string/*" \
    -skip valid/string/unicode-escape \
    -skip valid/string/quoted-unicode
