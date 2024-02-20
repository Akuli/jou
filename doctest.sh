#!/usr/bin/env bash
#
# This file runs code snippets in doc/*.md files.

set -e -o pipefail

for arg in "$@"; do
    if [[ "$arg" =~ ^- ]]; then
        echo "Usage: $0 [doc/file1.md doc/file2.md ...]" >&2
        exit 2
    fi
done

if [ $# == 0 ]; then
    files=(doc/*.md)
else
    files=("$@")
fi

if [[ "${OS:=$(uname)}" =~ Windows ]]; then
    source activate
    mingw32-make
    jou="$PWD/jou.exe"
elif [[ "$OS" =~ NetBSD ]]; then
    gmake
    jou="$PWD/jou"
else
    make
    jou="$PWD/jou"
fi


DIFF=$(which gdiff || which diff)
if $DIFF --help | grep -q -- --color; then
    diff_color="--color=always"
fi

function slice()
{
    local first_lineno="$1"
    local last_lineno="$2"
    local num_lines=$((last_lineno - first_lineno + 1))
    head -n $last_lineno | tail -n $num_lines
}

function generate_expected_output()
{
    local joufile="$1"

    (grep -onH '# Warning: .*' "$joufile" || true) | sed -E s/'(.*):([0-9]*):# Warning: '/'compiler warning for file "test.jou", line \2: '/
    (grep -onH '# Error: .*' "$joufile" || true) | sed -E s/'(.*):([0-9]*):# Error: '/'compiler error in file "\1", line \2: '/
    (grep -oE '# Output:.*' "$joufile" || true) | sed -E s/'^# Output: ?'//
}

rm -rf tmp/doctest
mkdir -p tmp/doctest

for file in "${files[@]}"; do
    echo "Extracting doctests from $file..."
    temp_dir="tmp/doctest/$(echo -n "$file" | base64)"  # make it possible to display file path later
    mkdir "$temp_dir"

    for start_marker_lineno in $(grep -n '^```python$' "$file" | cut -d: -f1); do
        outfile="$temp_dir/$((start_marker_lineno + 1)).jou"
        awk -v n=$start_marker_lineno '(/^```$/ && line > n) { stop=1 } (++line > n && !stop) { print }' "$file" > "$outfile"

        # Do not test if there is no expected output/errors
        if [ -z "$(generate_expected_output "$outfile")" ]; then
            rm "$outfile"
        fi
    done
done

ntotal=0
nfail=0

cd tmp/doctest
for file in */*.jou; do
    # print file and line number, as in "doc/foo.md:123: "
    echo -n "$(basename "$(dirname "$file")" | tr -d '\n' | base64 -d):$(basename "$file" | cut -d'.' -f1): "

    cp "$file" test.jou
    if $DIFF --text -u $diff_color <(generate_expected_output test.jou | tr -d '\r') <( ("$jou" test.jou 2>&1 || true) | tr -d '\r'); then
        echo "ok"
    else
        ((nfail++)) || true
    fi
    ((ntotal++)) || true
done

if [ $ntotal == 0 ]; then
    echo "*** Error: no doctests found ***" >&2
    exit 1
fi

echo ""
echo ""

echo "$((ntotal-nfail)) succeeded, $nfail failed"
if [ $nfail != 0 ]; then
    exit 1
fi
