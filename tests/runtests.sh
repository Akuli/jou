#!/bin/bash
set -e -o pipefail

# Go to project root.
cd "$(dirname "$0")"/..

rm -rf tests/tmp
mkdir tests/tmp
mkdir tests/tmp/diffs

succeeded=0
failed=0

function generate_expected_output()
{
    local filename="$1"
    (grep -o '# Output: .*' $joufile || true) | sed s/'^# Output: '// | dos2unix
    (grep -onH '# Error: .*' $joufile || true) | sed -E s/'(.*):([0-9]*):# Error: '/'compiler error in file "\1", line \2: '/

    case $filename in
        tests/should_fail/*)
            echo "Exit code: 1"
            ;;
        *)
            echo "Exit code: 0"
            ;;
    esac
}

for joufile in examples/*.jou tests/should_work/*.jou tests/should_fail/*.jou; do
    command="./jou $joufile"
    diffpath=$(mktemp -p tests/tmp/diffs/)
    printf "\n\n*** Command: %s ***\n" "$command" > $diffpath

    if diff -u --color=always \
        <(generate_expected_output $joufile) \
        <(bash -c "$command; echo Exit code: \$?" 2>&1) \
        &>> $diffpath
    then
        echo -ne "\x1b[32m.\x1b[0m"
        succeeded=$((succeeded + 1))
        rm $diffpath
    else
        echo -ne "\x1b[31mF\x1b[0m"
        failed=$((failed + 1))
    fi
done

echo ""
echo ""

if [ $failed != 0 ]; then
    echo "------- FAILURES -------"
    cat tests/tmp/diffs/*
fi

if [ $failed = 0 ]; then
    echo -e "\x1b[32m$succeeded succeeded\x1b[0m"
else
    echo -e "$succeeded succeeded, \x1b[31m$failed failed\x1b[0m"
    exit 1
fi
