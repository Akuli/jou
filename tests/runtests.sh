#!/bin/bash
#
#
#
# *** If you edit this file, make sure that the instructions
# *** in README stay up to date.
#
#
#

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

rm -rf tmp/tests
mkdir -vp tmp/tests

function generate_expected_output()
{
    (grep -onH '# Warning: .*' $joufile || true) | sed -E s/'(.*):([0-9]*):# Warning: '/'compiler warning for file "\1", line \2: '/
    (grep -onH '# Error: .*' $joufile || true) | sed -E s/'(.*):([0-9]*):# Error: '/'compiler error in file "\1", line \2: '/
    (grep -o '# Output: .*' $joufile || true) | sed s/'^# Output: '// | dos2unix
    echo "Exit code: $correct_exit_code"
}

function run_test()
{
    local joufile="$1"
    local correct_exit_code="$2"
    local counter="$3"

    local command="$(printf "$command_template" $joufile)"
    local diffpath=tmp/tests/diff$(printf "%04d" $counter).txt  # consistent alphabetical order
    printf "\n\n\x1b[33m*** Command: %s ***\x1b[0m\n\n" "$command" > $diffpath

    if diff -u --color=always \
        <(generate_expected_output) \
        <(bash -c "ulimit -v 500000; $command; echo Exit code: \$?" 2>&1) \
        &>> $diffpath
    then
        echo -ne "\x1b[32m.\x1b[0m"
        rm $diffpath
    else
        echo -ne "\x1b[31mF\x1b[0m"
    fi
}

counter=0
for joufile in examples/*.jou tests/*/*.jou; do
    case $joufile in
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
    counter=$((counter + 1))

    # Run 2 tests in parallel.
    while [ $(jobs -p | wc -l) -ge 2 ]; do wait -n; done
    run_test $joufile $correct_exit_code $counter &
done
wait

echo ""
echo ""

failed=$( (ls -1 tmp/tests/diff*.txt 2>/dev/null || true) | wc -l)
succeeded=$((counter - failed))

if [ $failed != 0 ]; then
    echo "------- FAILURES -------"
    cat tmp/tests/diff*.txt
fi

if [ $failed = 0 ]; then
    echo -e "\x1b[32m$succeeded succeeded\x1b[0m"
else
    echo -e "$succeeded succeeded, \x1b[31m$failed failed\x1b[0m"
    exit 1
fi
