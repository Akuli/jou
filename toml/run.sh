#!/bin/bash
set -e

# Only a very basic command, you will need to mess around with the flags so
# this is intended only to get you started

jou -o toml_parser toml_parser.jou

tests_to_skip=(
    valid/spec-example-1            # contains a date
    valid/spec-example-1-compact    # contains a date
)
./toml-test-* test -decoder ./toml_parser $(printf " -skip %s" ${tests_to_skip[@]})
