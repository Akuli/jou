#!/bin/bash
set -e -o pipefail

skip_expected_fails=no
while [[ "$1" =~ ^- ]] || [ $# != 1 ]; do
    if [ "$1" = --skip-expected-fails ]; then
        skip_expected_fails=yes
        shift
    else
        echo "Usage: $0 [--skip-expected-fails] 'jou %s'" >&2
        echo "The %s will be replaced by the name of a jou file." >&2
        exit 2
    fi
done
command_template="$1"

# Go to project root.
cd "$(dirname "$0")"/..

rm -rf tests/tmp
mkdir tests/tmp
mkdir tests/tmp/diffs

succeeded=0
failed=0

function generate_expected_output()
{
    (grep -o '# Output: .*' $joufile || true) | sed s/'^# Output: '// | dos2unix
    (grep -onH '# Error: .*' $joufile || true) | sed -E s/'(.*):([0-9]*):# Error: '/'compiler error in file "\1", line \2: '/
    echo "Exit code: $correct_exit_code"
}

for joufile in examples/*.jou tests/*/*.jou; do
    case $joufile in
        */broken/*)
            continue
            ;;
        examples/* | tests/should_succeed/*)
            correct_exit_code=0
            ;;
        *)
            if [ $skip_expected_fails = yes ]; then
                continue
            fi
            correct_exit_code=1
            ;;
    esac

    command="$(printf "$command_template" $joufile)"
    diffpath=tests/tmp/diffs/diff$(printf "%04d" $failed)  # consistent alphabetical order
    printf "\n\n\x1b[33m*** Command: %s ***\x1b[0m\n\n" "$command" > $diffpath

    if diff -u --color=always \
        <(generate_expected_output) \
        <(bash -c "ulimit -v 500000; $command; echo Exit code: \$?" 2>&1) \
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
