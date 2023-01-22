#!/bin/bash
#
#
#
# *** If you edit this file, make sure that the instructions
# *** in README stay up to date.
#
#
#

export LANG=C  # "Segmentation fault" must be in english for this script to work
set -e -o pipefail

if [ $# == 0 ]; then
    # No arguments --> run tests in the basic/simple way
    if [ -n "$MINGW_PREFIX" ]; then
        # windows + git bash
        command_template='./jou.exe %s'
    else
        command_template='./jou %s'
    fi
elif [ $# == 1 ] && [[ "$1" =~ ^[^-] ]]; then
    command_template="$1"
else
    echo "Usage: $0 [command_template]" >&2
    echo "command_template can be e.g. './jou %s', where %s will be replaced by a jou file." >&2
    exit 2
fi

if which make >/dev/null; then
    make
fi

rm -rf tmp/tests
mkdir -vp tmp/tests

function generate_expected_output()
{
    local joufile="$1"
    local correct_exit_code="$2"

    # In verbose mode, the output is silenced, see below. The point of
    # testing with --verbose is that the compiler shouldn't crash (#65).
    if [[ "$command_template" =~ --verbose ]]; then
        echo "A lot of output hidden..."
    else
        (grep -onH '# Warning: .*' $joufile || true) | sed -E s/'(.*):([0-9]*):# Warning: '/'compiler warning for file "\1", line \2: '/
        (grep -onH '# Error: .*' $joufile || true) | sed -E s/'(.*):([0-9]*):# Error: '/'compiler error in file "\1", line \2: '/
        (grep -o '# Output: .*' $joufile || true) | sed s/'^# Output: '//
    fi
    echo "Exit code: $correct_exit_code"
}

function post_process_output()
{
    local joufile="$1"
    if [[ "$command_template" =~ --verbose ]]; then
        # There is a LOT of output. We don't want to write the expected
        # output precisely somewhere, that would be a lot of work.
        # Instead, ignore the output and check only the exit code.
        echo "A lot of output hidden..."
        grep "^Exit code:"
    elif [[ $joufile =~ ^tests/crash/ ]]; then
        # Hide most of the output. We really only care about whether it
        # mentions "Segmentation fault" somewhere inside it.
        grep -oE "Segmentation fault|Exit code: .*"
    else
        # Pass the output through almost unchanged, but replace
        # path to the project (e.g. /home/akuli/jou) with <joudir>.
        sed "s,$(pwd),<joudir>,g"
    fi
}

YELLOW="\x1b[33m"
GREEN="\x1b[32m"
RED="\x1b[31m"
RESET="\x1b[0m"

function run_test()
{
    local joufile="$1"
    local correct_exit_code="$2"
    local counter="$3"

    local command="$(printf "$command_template" $joufile)"
    local diffpath=tmp/tests/diff$(printf "%04d" $counter).txt  # consistent alphabetical order
    printf "\n\n\x1b[33m*** Command: %s ***\x1b[0m\n\n" "$command" > $diffpath

    # Skip tests when:
    #   * the test is supposed to crash, but optimizations are enabled (unpredictable by design)
    #   * the test is supposed to fail (crash or otherwise) and we use valgrind (see README)
    if ( [[ "$command_template" =~ -O[1-3] ]] && [[ $joufile =~ ^tests/crash/ ]] ) \
        || ( [[ "$command_template" =~ valgrind ]] && [ $correct_exit_code != 0 ] )
    then
        # Skip
        echo -ne ${YELLOW}s${RESET}
        mv $diffpath $diffpath.skip
    elif diff -u --color=always \
        <(generate_expected_output $joufile $correct_exit_code | tr -d '\r') \
        <(bash -c "$command; echo Exit code: \$?" 2>&1 | post_process_output $joufile | tr -d '\r') \
        &>> $diffpath
    then
        # Ran successfully
        echo -ne ${GREEN}.${RESET}
        rm -f $diffpath
    else
        # Failed
        echo -ne ${RED}F${RESET}
    fi
}

counter=0
for joufile in examples/*.jou tests/*/*.jou; do
    case $joufile in
        examples/* | tests/should_succeed/*) correct_exit_code=0; ;;
        tests/crash/*) correct_exit_code=139; ;;  # segfault
        *) correct_exit_code=1; ;;  # compiler or runtime error
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
skipped=$( (ls -1 tmp/tests/diff*.txt.skip 2>/dev/null || true) | wc -l)
succeeded=$((counter - failed - skipped))

if [ $failed != 0 ]; then
    echo "------- FAILURES -------"
    cat tmp/tests/diff*.txt
fi

echo -ne "${GREEN}${succeeded} succeeded"
if [ $failed != 0 ]; then
    echo -ne ", ${RED}${failed} failed"
fi
if [ $skipped != 0 ]; then
    echo -ne ", ${YELLOW}${skipped} skipped"
fi
echo -e $RESET

exit $((!!failed))
