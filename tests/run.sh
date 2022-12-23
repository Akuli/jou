#!/bin/bash
set -e -o pipefail

# Go to project root.
cd "$(dirname "$0")"/..

rm -rf tests/tmp
mkdir tests/tmp
mkdir tests/tmp/diffs

succeeded=0
failed=0

function run_file()
{
    local output_filename="$1"
    local command="$2"

    local diff_filename=tests/tmp/diffs/$(echo -n "$command" | base64)
    if diff -u --color=always "$output_filename" <(bash -c "$command") &> "$diff_filename"; then
        echo -ne "\x1b[32m.\x1b[0m"
        succeeded=$((succeeded + 1))
    else
        echo -ne "\x1b[31mF\x1b[0m"
        failed=$((failed + 1))
    fi
}

for file in examples/*.jou; do
    dir=$(basename $(dirname $file))
    name_without_extension=$(basename $file | cut -d. -f1)
    run_file tests/$dir-output/$name_without_extension.txt "./jou $file"
done

echo ""
echo ""

if [ $failed != 0 ]; then
    echo "------- FAILURES -------"
    echo ""
    cd tests/tmp/diffs
    for b64filename in *; do
        echo "*** Command: $(echo $b64filename | base64 -d) ***"
        cat $b64filename
        echo ""
        echo ""
    done
fi

if [ $failed = 0 ]; then
    echo -e "\x1b[32m$succeeded succeeded\x1b[0m"
else
    echo -e "$succeeded succeeded, \x1b[31m$failed failed\x1b[0m"
    exit 1
fi
