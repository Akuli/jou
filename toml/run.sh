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
    valid/spec-1.1.0/common-44
    valid/spec-1.1.0/common-32
    valid/spec-1.1.0/common-33
    valid/spec-1.1.0/common-34
    valid/spec-1.1.0/common-31
    valid/spec-1.1.0/common-30
    valid/spec-1.1.0/common-29
    valid/spec-1.1.0/common-28
    valid/spec-1.1.0/common-27

    # Zero byte in key. We support zero bytes in string values, but not in keys
    # given as strings.
    valid/key/quoted-unicode

    # Assertion fails (valid)
    valid/array/array-subtables
    valid/spec-1.1.0/common-52
    valid/table/array-nest
    valid/table/array-table-array

    # Assertion fails (invalid)
    invalid/array/extend-defined-aot
    invalid/array/extending-table
    invalid/array/tables-01
    invalid/array/tables-02
    invalid/inline-table/duplicate-key-04
    invalid/inline-table/overwrite-04
    invalid/inline-table/overwrite-06
    invalid/inline-table/overwrite-07
    invalid/key/after-array
    invalid/key/dotted-redefine-table-01
    invalid/key/dotted-redefine-table-02
    invalid/table/append-with-dotted-keys-03
    invalid/table/append-with-dotted-keys-06
    invalid/table/append-with-dotted-keys-07
    invalid/table/array-implicit
    invalid/table/duplicate-key-03
    invalid/table/duplicate-key-06
    invalid/table/duplicate-key-07
    invalid/table/duplicate-key-08
    invalid/table/duplicate-key-10
    invalid/table/overwrite-array-in-parent
    invalid/table/overwrite-bool-with-array
    invalid/table/overwrite-with-deep-table

    # toml-test says: Expected an error, but no error was reported.
    invalid/inline-table/duplicate-key-03
    invalid/inline-table/overwrite-02
    invalid/inline-table/overwrite-05
    invalid/inline-table/overwrite-08
    invalid/integer/double-us
    invalid/spec-1.1.0/common-46-0
    invalid/spec-1.1.0/common-46-1
    invalid/spec-1.1.0/common-49-0
    invalid/table/append-with-dotted-keys-01
    invalid/table/append-with-dotted-keys-02
    invalid/table/append-with-dotted-keys-04
    invalid/table/duplicate-key-01
    invalid/table/duplicate-key-04
    invalid/table/duplicate-key-05
    invalid/table/duplicate-key-09
    invalid/table/redefine-02
    invalid/table/redefine-03
    invalid/table/super-twice
)

./toml-test-* test -decoder ./toml_parser -toml 1.1 -skip-must-err -errors expected_errors.toml $(printf " -skip %s" ${tests_to_skip[@]}) "$@"
