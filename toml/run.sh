#!/bin/bash
set -e

jou -o toml_parser toml_parser.jou

# Make sure all error messages are mentioned in expected_errors.toml
# The language version (-toml 1.1) is specified again below in this script.
diff -u --color=always <(./toml-test-* list -toml 1.1 | grep ^invalid) <(grep '^"' expected_errors.toml | cut -d'"' -f2)

tests_to_skip=(
    # These tests contain dates and times so they don't work. We don't support
    # TOML dates and times.
    valid/array/array
    valid/datetime/datetime
    valid/datetime/edge
    valid/datetime/leap-year
    valid/datetime/local
    valid/datetime/local-date
    valid/datetime/local-time
    valid/datetime/milliseconds
    valid/datetime/no-seconds
    valid/datetime/timezone
    valid/comment/everywhere
    valid/example
    valid/spec-example-1-compact
    valid/spec-example-1
    valid/spec-1.1.0/common-27
    valid/spec-1.1.0/common-28
    valid/spec-1.1.0/common-29
    valid/spec-1.1.0/common-30
    valid/spec-1.1.0/common-31
    valid/spec-1.1.0/common-32
    valid/spec-1.1.0/common-33
    valid/spec-1.1.0/common-34
    valid/spec-1.1.0/common-44

    # Zero byte in key. We support zero bytes in string values, but not in keys
    # given as strings.
    valid/key/quoted-unicode
)

./toml-test-* test -decoder ./toml_parser -toml 1.1 -skip-must-err -errors expected_errors.toml $(printf " -skip %s" ${tests_to_skip[@]}) "$@"
