#!/usr/bin/env bash
#
# This file uses the official TOML test suite to verify that stdlib/toml.jou is
# working correctly.
#
# TOML test suite: https://github.com/toml-lang/toml-test

set -e -o pipefail

RED="\x1b[31m"
RESET="\x1b[0m"

if [ "$(uname)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
    echo "Currently this script assumes Linux on x86_64." >&2
    exit 1
fi

# TODO: add --valgrind flag

if ! [ -x tmp/toml-test ]; then
    (
        mkdir -vp tmp
        cd tmp

        echo "Downloading toml-test..."
        wget -q https://github.com/toml-lang/toml-test/releases/download/v2.1.0/toml-test-v2.1.0-linux-amd64.gz

        echo "Verifying..."
        if [ "$(sha256sum toml-test-v2.1.0-linux-amd64.gz | cut -d' ' -f1)" != "99fd36c93b297ebde9719ec174266765f56a28924d7fca799d911f3f4354c25d" ]; then
            echo "Verifying failed!!!" >&2
            exit 1
        fi

        echo "Extracting toml-test..."
        gunzip toml-test-v2.1.0-linux-amd64.gz
        mv -v toml-test-v2.1.0-linux-amd64 toml-test
        chmod +x toml-test
    )
fi

# Make sure that our file specifies an error messages for every file that is
# supposed to fail with an error.
#
# The language version (-toml 1.1) is specified again below in this script.
if ! diff -u --color=always <(tmp/toml-test list -toml 1.1 | grep ^invalid) <(grep '^"' tests/data/expected_toml_parser_errors.toml | cut -d'"' -f2); then
    echo ""
    echo -e "Error: ${RED}tests/data/expected_toml_parser_errors.toml is not up to date.${RESET}"
    exit 1
fi

make jou

# Compile our Jou program that parses TOML on stdin and outputs JSON
./jou -o tmp/toml_test_jou_program tests/special/toml_test_program.jou

# Let's put this in an array so we can have comments next to each argument
command=(
    tmp/toml-test

    # What it should do
    test

    # Give the test runner our Jou program that parses TOML on stdin, outputs
    # JSON, and exits with 1 on error.
    -decoder 'tmp/toml_test_jou_program'

    # TOML language version
    -toml 1.1

    # When we say -skip below, we actually mean "make sure this fails".
    #
    # This doesn't work as well as you might hope, because a different error
    # message than expected is enough to count as worthy of skip. But this is
    # still probably better than nothing.
    -skip-must-err

    # Check each error message
    -errors tests/data/expected_toml_parser_errors.toml

    # These tests contain dates and times so they don't work. We don't support
    # TOML dates and times.
    -skip valid/array/array
    -skip valid/datetime/datetime
    -skip valid/datetime/edge
    -skip valid/datetime/leap-year
    -skip valid/datetime/local
    -skip valid/datetime/local-date
    -skip valid/datetime/local-time
    -skip valid/datetime/milliseconds
    -skip valid/datetime/no-seconds
    -skip valid/datetime/timezone
    -skip valid/comment/everywhere
    -skip valid/example
    -skip valid/spec-example-1-compact
    -skip valid/spec-example-1
    -skip valid/spec-1.1.0/common-27
    -skip valid/spec-1.1.0/common-28
    -skip valid/spec-1.1.0/common-29
    -skip valid/spec-1.1.0/common-30
    -skip valid/spec-1.1.0/common-31
    -skip valid/spec-1.1.0/common-32
    -skip valid/spec-1.1.0/common-33
    -skip valid/spec-1.1.0/common-34
    -skip valid/spec-1.1.0/common-44

    # This test has a key that contains a zero byte. We support zero bytes in
    # string values, but not in keys given as strings.
    #
    # TODO: test the behavior in some other way?
    -skip valid/key/quoted-unicode
)

"${command[@]}"
