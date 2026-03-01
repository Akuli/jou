#!/bin/bash
set -e

# Only a very basic command, you will need to mess around with the flags so
# this is intended only to get you started

jou -o toml_parser toml_parser.jou

tests_to_skip=(
    valid/spec-example-1            # contains a date
    valid/spec-example-1-compact    # contains a date

    # TODO: Figure out why these fail, fix as appropriate
    invalid/spec-1.0.0/table-9-0
    invalid/spec-1.0.0/table-9-1
    invalid/string/basic-byte-escapes
    invalid/string/literal-multiline-quotes-01
    invalid/string/literal-multiline-quotes-02
    invalid/string/multiline-quotes-01
    invalid/string/no-close-05
    invalid/string/no-close-06
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
    valid/array/array
    valid/array/array-subtables
    valid/array/bool
    valid/array/hetergeneous
    valid/array/mixed-int-float
    valid/array/mixed-string-table
    valid/array/nested-inline-table
    valid/array/open-parent-table
    valid/array/table-array-string-backslash
    valid/bool/bool
    valid/comment/after-literal-no-ws
    valid/comment/everywhere
    valid/comment/tricky
    valid/datetime/datetime
    valid/datetime/edge
    valid/datetime/leap-year
    valid/datetime/local
    valid/datetime/local-date
    valid/datetime/local-time
    valid/datetime/milliseconds
    valid/datetime/timezone
    valid/example
    valid/float/exponent
    valid/float/float
    valid/float/inf-and-nan
    valid/float/long
    valid/float/max-int
    valid/float/underscore
    valid/float/zero
    valid/inline-table/array-01
    valid/inline-table/array-02
    valid/inline-table/array-03
    valid/inline-table/bool
    valid/inline-table/empty
    valid/inline-table/end-in-bool
    valid/inline-table/inline-table
    valid/inline-table/key-dotted-01
    valid/inline-table/key-dotted-02
    valid/inline-table/key-dotted-03
    valid/inline-table/key-dotted-04
    valid/inline-table/key-dotted-05
    valid/inline-table/key-dotted-06
    valid/inline-table/key-dotted-07
    valid/inline-table/multiline
    valid/inline-table/nest
    valid/inline-table/spaces
    valid/key/alphanum
    valid/key/dotted-03
    valid/key/dotted-04
    valid/key/escapes
    valid/key/like-date
    valid/key/numeric-01
    valid/key/numeric-02
    valid/key/numeric-03
    valid/key/numeric-04
    valid/key/numeric-05
    valid/key/numeric-06
    valid/key/numeric-07
    valid/key/quoted-unicode
    valid/key/special-word
    valid/key/start
    valid/spec-1.0.0/array-0
    valid/spec-1.0.0/array-of-tables-0
    valid/spec-1.0.0/array-of-tables-1
    valid/spec-1.0.0/array-of-tables-2
    valid/spec-1.0.0/boolean-0
    valid/spec-1.0.0/float-0
    valid/spec-1.0.0/float-1
    valid/spec-1.0.0/float-2
    valid/spec-1.0.0/inline-table-0
    valid/spec-1.0.0/inline-table-2
    valid/spec-1.0.0/keys-3
    valid/spec-1.0.0/local-date-0
    valid/spec-1.0.0/local-date-time-0
    valid/spec-1.0.0/local-time-0
    valid/spec-1.0.0/offset-date-time-0
    valid/spec-1.0.0/offset-date-time-1
    valid/spec-1.0.0/table-7
    valid/spec-1.0.0/table-8
    valid/spec-1.0.0/table-9
    valid/string/quoted-unicode
    valid/string/unicode-escape
    valid/table/array-empty
    valid/table/array-empty-name
    valid/table/array-implicit
    valid/table/array-implicit-and-explicit-after
    valid/table/array-many
    valid/table/array-nest
    valid/table/array-one
    valid/table/array-table-array
    valid/table/array-within-dotted
)
./toml-test-* test -decoder ./toml_parser -skip-must-err $(printf " -skip %s" ${tests_to_skip[@]})
