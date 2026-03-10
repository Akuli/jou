#!/usr/bin/env bash
# This script uses the official TOML test suite to verify that stdlib/toml.jou is
# working correctly.

set -e -o pipefail

cd "$(dirname "$0")"

RED="\x1b[31m"
RESET="\x1b[0m"

if [ "$(uname -sm)" != "Linux x86_64" ]; then
    echo "Currently this script assumes Linux on x86_64." >&2
    exit 1
fi

function usage() {
    echo "Usage: $0 [--valgrind] [--jou-flags \"-O3 ...\"] [TEST_FILTER]"
    echo ""
    echo "If TEST_FILTER is given, only the tests that contain it as a substring"
    echo 'will be ran. It can be e.g. "string" or "valid/key/escapes.toml".'
    exit 2
}

valgrind=no
jou_flags=""
test_filter=""

while [ $# != 0 ]; do
    case "$1" in
        --valgrind)
            valgrind=yes
            shift
            ;;
        --jou-flags)
            if [ $# == 1 ]; then
                usage
            fi
            jou_flags="$jou_flags $2"
            shift 2
            ;;
        -*)
            usage
            ;;
        *)
            if [ -n "$test_filter" ]; then
                usage
            fi
            test_filter="$1"
            shift
            ;;
    esac
done

if ! [ -x ./toml-test ]; then
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
fi

# Make sure that our file specifies an error messages for every file that is
# supposed to fail with an error.
#
# The language version (-toml 1.1) is specified again below in this script.
if ! diff -u --color=always <(./toml-test list -toml 1.1 | grep ^invalid) <(grep '^"' expected_errors.toml | cut -d'"' -f2); then
    echo ""
    echo -e "Error: ${RED}expected_errors.toml is not up to date.${RESET}"
    exit 1
fi

# Compile our Jou program that parses TOML on stdin and outputs JSON
(cd ../.. && make jou)
../../jou -o parser_program $jou_flags parser_program.jou

if [ $valgrind = yes ]; then
    # Exit codes that are already in use:
    #   0   valid TOML
    #   1   invalid TOML
    #   2   assertion failed
    parser_program_command="valgrind -q --leak-check=full --show-leak-kinds=all --error-exitcode=3 ./parser_program"
else
    parser_program_command="./parser_program"
fi

# Let's put this in an array so we can have comments between the arguments.
toml_test_command=(
    ./toml-test

    # What it should do
    test

    # Give the test runner our Jou program that parses TOML on stdin, outputs
    # JSON, and exits with 1 on error.
    -decoder "$parser_program_command"

    # TOML language version
    -toml 1.1

    # When we say -skip below, we actually mean "make sure this fails".
    #
    # This doesn't work as well as you might hope, because a different error
    # message than expected is enough to count as worthy of skip. But this is
    # still probably better than nothing.
    -skip-must-err

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

if [ $valgrind = yes ]; then
    # On my system, valgrind takes about 0.7 seconds to start. The default
    # timeout is 1 second and that seems a bit tight to me.
    toml_test_command+=(-timeout 5s)
fi

# The default is to run everything. If an argument is given, let's use it to
# run only the tests that contain that argument.
if [ "$test_filter" != "" ]; then
    # To check that we output the correct error messages, it is possible to pass a
    # JSON or TOML file with the expected errors to toml-test. But it doesn't work
    # quite like you would hope/expect: toml-test refuses to run the test if your
    # error file contains errors that don't apply to any of the tests.
    #
    # So if we run only some specific tests, we must filter them out.
    errors_file="$(mktemp --suffix=.toml)"
    trap 'rm -f "$errors_file"' EXIT

    comma_separated=""
    for test_name in $(./toml-test list -toml 1.1 | grep '\.toml$'); do
        if [[ "$test_name" == *"$test_filter"* ]]; then
            echo "  Selecting test '$test_name'"
            if [ -n "$comma_separated" ]; then
                comma_separated="$comma_separated,"
            fi
            comma_separated="${comma_separated}${test_name%%.toml}"
            if [[ "$test_name" = invalid/* ]]; then
                grep "^\"$test_name\" = " expected_errors.toml >> $errors_file || (echo "grepping failed"; exit 1)
            fi
        fi
    done

    if [ -z "$comma_separated" ]; then
        echo -e "${RED}Error: No test name contains $test_filter.${RESET}" >&2
        exit 1
    fi

    toml_test_command+=(-run $comma_separated)
else
    errors_file=expected_errors.toml
fi

toml_test_command+=(-errors $errors_file)

"${toml_test_command[@]}"
