#!/bin/bash
set -e

jou -o toml_parser toml_parser.jou

# Make sure all error messages are mentioned in expected_errors.toml
diff -u --color=always <(./toml-test-* list | grep ^invalid) <(grep '^"' expected_errors.toml | cut -d'"' -f2)

tests_to_skip=(
    # These tests contain dates or times, so they are not supported
    valid/array/array
    valid/comment/everywhere
    valid/datetime/datetime
    valid/datetime/edge
    valid/datetime/leap-year
    valid/datetime/local
    valid/datetime/local-date
    valid/datetime/local-time
    valid/datetime/milliseconds
    valid/datetime/timezone
    valid/example
    valid/spec-1.0.0/local-date-0
    valid/spec-1.0.0/local-date-time-0
    valid/spec-1.0.0/local-time-0
    valid/spec-1.0.0/offset-date-time-0
    valid/spec-1.0.0/offset-date-time-1
    valid/spec-1.0.0/table-7
    valid/spec-example-1
    valid/spec-example-1-compact

    # Contains zero byte in a key, which is not supported
    valid/key/quoted-unicode

    # TODO: Figure out why these fail, fix as appropriate
    valid/array/array-subtables
    valid/array/open-parent-table
    valid/inline-table/key-dotted-04
    valid/key/dotted-04
    valid/key/like-date
    valid/spec-1.0.0/array-of-tables-0
    valid/spec-1.0.0/array-of-tables-1
    valid/table/array-empty
    valid/table/array-empty-name
    valid/table/array-implicit
    valid/table/array-implicit-and-explicit-after
    valid/table/array-many
    valid/table/array-nest
    valid/table/array-one
    valid/table/array-table-array
    valid/table/array-within-dotted

    invalid/array/extend-defined-aot
    invalid/array/extending-table
    invalid/array/tables-01
    invalid/array/tables-02
    invalid/datetime/y10k
    invalid/inline-table/duplicate-key-03
    invalid/inline-table/duplicate-key-04
    invalid/inline-table/linebreak-01
    invalid/inline-table/linebreak-02
    invalid/inline-table/linebreak-03
    invalid/inline-table/linebreak-04
    invalid/inline-table/overwrite-02
    invalid/inline-table/overwrite-04
    invalid/inline-table/overwrite-05
    invalid/inline-table/overwrite-06
    invalid/inline-table/overwrite-07
    invalid/inline-table/overwrite-08
    invalid/inline-table/trailing-comma
    invalid/integer/double-us
    invalid/key/after-array
    invalid/key/dotted-redefine-table-01
    invalid/key/dotted-redefine-table-02
    invalid/key/no-eol-04
    invalid/spec-1.0.0/inline-table-2-0
    invalid/spec-1.0.0/inline-table-3-0
    invalid/spec-1.0.0/key-value-pair-1
    invalid/spec-1.0.0/keys-2
    invalid/spec-1.0.0/string-4-0
    invalid/spec-1.0.0/string-7-0
    invalid/spec-1.0.0/table-9-0
    invalid/spec-1.0.0/table-9-1
    invalid/string/basic-byte-escapes
    invalid/table/append-with-dotted-keys-01
    invalid/table/append-with-dotted-keys-02
    invalid/table/append-with-dotted-keys-03
    invalid/table/append-with-dotted-keys-04
    invalid/table/append-with-dotted-keys-05
    invalid/table/append-with-dotted-keys-06
    invalid/table/append-with-dotted-keys-07
    invalid/table/array-empty
    invalid/table/array-implicit
    invalid/table/array-no-close-01
    invalid/table/array-no-close-02
    invalid/table/array-no-close-03
    invalid/table/array-no-close-04
    invalid/table/bare-invalid-character-01
    invalid/table/bare-invalid-character-02
    invalid/table/dot
    invalid/table/dotdot
    invalid/table/duplicate-key-01
    invalid/table/duplicate-key-02
    invalid/table/duplicate-key-03
    invalid/table/duplicate-key-04
    invalid/table/duplicate-key-05
    invalid/table/duplicate-key-06
    invalid/table/duplicate-key-07
    invalid/table/duplicate-key-08
    invalid/table/duplicate-key-09
    invalid/table/duplicate-key-10
    invalid/table/empty
    invalid/table/newline-01
    invalid/table/newline-02
    invalid/table/newline-03
    invalid/table/newline-05
    invalid/table/no-close-03
    invalid/table/overwrite-array-in-parent
    invalid/table/overwrite-bool-with-array
    invalid/table/overwrite-with-deep-table
    invalid/table/redefine-02
    invalid/table/redefine-03
    invalid/table/rrbrace
    invalid/table/super-twice
)

./toml-test-* test -decoder ./toml_parser -skip-must-err -errors expected_errors.toml $(printf " -skip %s" ${tests_to_skip[@]}) "$@"
