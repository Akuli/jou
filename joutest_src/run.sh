#!/bin/bash
set -e

# Only a very basic command, you will need to mess around with the flags so
# this is intended only to get you started

jou -o toml_parser toml_parser.jou && ./toml-test-* test -decoder ./toml_parser -run "valid/string/basic-*"
