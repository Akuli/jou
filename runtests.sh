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

function usage() {
    echo "Usage: $0 [--valgrind] [--verbose] [--dont-run-make] [--jou-flags \"-O3 ...\"] [FILE_FILTER]" >&2
    echo "If a FILE_FILTER is given, runs only test files whose path contains it."
    echo "For example, you can use \"$0 class\" to run class-related tests."
    exit 2
}

valgrind=no
verbose=no
run_make=yes
jou_flags=""
file_filter=""

while [ $# != 0 ]; do
    case "$1" in
        --valgrind)
            valgrind=yes
            shift
            ;;
        --verbose)
            verbose=yes
            shift
            ;;
        --dont-run-make)
            run_make=no
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
            if [ -n "$file_filter" ]; then
                usage
            fi
            file_filter="$1"
            shift
            ;;
    esac
done

if [ $valgrind = yes ] && [[ "$OS" =~ Windows ]]; then
    echo "valgrind doesn't work on Windows." >&2
    exit 2
fi

if [ $run_make = yes ]; then
    if [[ "$OS" =~ Windows ]]; then
        source activate
        mingw32-make
    else
        make
    fi
fi

rm -rf tmp/tests
mkdir -p tmp/tests

joudir="$(pwd)"
if [[ "$OS" =~ Windows ]]; then
    jouexe="jou.exe"
    if [[ "$joudir" =~ ^/[A-Za-z]/ ]]; then
        # Rewrite funny mingw path: /d/a/jou/jou --> D:/a/jou/jou
        letter=${joudir:1:1}
        joudir="${letter^^}:/${joudir:3}"
        unset letter
    fi
else
    jouexe="./jou"
fi
echo "<joudir> in expected output will be replaced with $joudir."
echo "<jouexe> in expected output will be replaced with $jouexe."

function generate_expected_output()
{
    local joufile="$1"
    local correct_exit_code="$2"

    # In verbose mode, the output is silenced, see below. The point of
    # testing with --verbose is that the compiler shouldn't crash (#65).
    if [[ "$jou_flags" =~ --verbose ]]; then
        echo "A lot of output hidden..."
    else
        (
            (grep -onH '# Warning: .*' $joufile || true) | sed -E s/'(.*):([0-9]*):# Warning: '/'compiler warning for file "\1", line \2: '/
            (grep -onH '# Error: .*' $joufile || true) | sed -E s/'(.*):([0-9]*):# Error: '/'compiler error in file "\1", line \2: '/
            (grep -oE '# Output:.*' $joufile || true) | sed -E s/'^# Output: ?'//
        ) | sed "s,<joudir>,$joudir,g" | sed "s,<jouexe>,$jouexe,g"
    fi
    echo "Exit code: $correct_exit_code"
}

function post_process_output()
{
    local joufile="$1"
    if [[ "$jou_flags" =~ --verbose ]]; then
        # There is a LOT of output. We don't want to write the expected
        # output precisely somewhere, that would be a lot of work.
        # Instead, ignore the output and check only the exit code.
        echo "A lot of output hidden..."
        grep "^Exit code:"
    elif [[ $joufile =~ ^tests/crash/ ]]; then
        if [[ "$OS" =~ Windows ]]; then
            # Windows doesn't say "Segmentation fault" when a program crashes
            echo "Segmentation fault"
        fi
        # Hide most of the output. We really only care about whether it
        # mentions "Segmentation fault" somewhere inside it.
        grep -oE "Segmentation fault|Exit code: .*"
    else
        # Pass the output through unchanged.
        cat
    fi
}

YELLOW="\x1b[33m"
GREEN="\x1b[32m"
RED="\x1b[31m"
RESET="\x1b[0m"

if [ $verbose = yes ]; then
    function show_run() { echo "run: $1"; }
    function show_skip() { echo -e "${YELLOW}skip${RESET} $1"; }
    function show_ok() { echo -e "${GREEN}ok${RESET}   $1"; }
    function show_fail() { echo -e "${RED}FAIL${RESET} $1"; }
else
    function show_run() { true; }
    function show_skip() { echo -ne ${YELLOW}s${RESET}; }
    function show_ok() { echo -ne ${GREEN}.${RESET}; }
    function show_fail() { echo -ne ${RED}F${RESET}; }
fi

function should_skip()
{
    local joufile="$1"
    local correct_exit_code="$2"

    # Skip tests when:
    #   * the test is supposed to crash, but optimizations are enabled (unpredictable by design)
    #   * the test is supposed to fail (crash or otherwise) and we use valgrind (see README)
    #   * the "test" is actually a GUI program in examples/
    if ( [[ $joufile =~ ^tests/crash/ ]] && ! [[ "$jou_flags" =~ -O0 ]] ) \
        || ( [ $valgrind = yes ] && [ $correct_exit_code != 0 ] ) \
        || [ $joufile = examples/x11_window.jou ] \
        || [ $joufile = examples/memory_leak.jou ]
    then
        return 0  # true
    else
        return 1  # false
    fi
}

function run_test()
{
    local joufile="$1"
    local correct_exit_code="$2"
    local counter="$3"

    local command=""

    if [ $valgrind = yes ] && [ $correct_exit_code == 0 ]; then
        # Valgrind the compiler process and the compiled executable
        command="valgrind -q --leak-check=full --show-leak-kinds=all --suppressions='$(pwd)/valgrind-suppressions.sup' jou --valgrind"
    elif [[ "$OS" =~ Windows ]]; then
        command="jou.exe"
    else
        command="jou"
    fi

    # jou flags start with space when non-empty
    command="$command$jou_flags"

    if [[ "$joufile" =~ ^examples/aoc ]]; then
        # AoC files use fopen("sampleinput.txt", "r").
        # We don't do this for all files, because I like relative paths in error messages.
        # jou_flags starts with a space whenever it isn't empty.
        command="cd $(dirname $joufile) && $command $(basename $joufile)"
    else
        command="$command $joufile"
    fi

    show_run "$command"

    local diffpath
    diffpath=tmp/tests/diff$(printf "%04d" $counter).txt  # consistent alphabetical order

    printf "\n\n\x1b[33m*** Command: %s ***\x1b[0m\n\n" "$command" > $diffpath

    if diff --text -u --color=always <(
        generate_expected_output $joufile $correct_exit_code | tr -d '\r'
    ) <(
        export PATH="$PWD:$PATH"
        ulimit -v 500000 2>/dev/null
        bash -c "$command; echo Exit code: \$?" 2>&1 | post_process_output $joufile | tr -d '\r'
    ) &>> $diffpath; then
        show_ok "$command"
        rm -f $diffpath
    else
        show_fail "$command"
        # Do not delete diff file. It will be displayed at end of test run.
    fi
}

counter=0
skipped=0

for joufile in examples/*.jou examples/aoc2023/day*/*.jou tests/*/*.jou; do
    if ! [[ $joufile == *"$file_filter"* ]]; then
        # Skip silently, without showing that this is skipped.
        # This produces less noisy output when you select only a few tests.
        continue
    fi

    case $joufile in
        examples/* | tests/should_succeed/*) correct_exit_code=0; ;;
        *) correct_exit_code=1; ;;  # compiler or runtime error
    esac
    counter=$((counter + 1))

    if should_skip $joufile $correct_exit_code; then
        show_skip $joufile
        skipped=$((skipped + 1))
        continue
    fi

    # Run 2 tests in parallel.
    while [ $(jobs -p | wc -l) -ge 2 ]; do wait -n; done
    run_test $joufile $correct_exit_code $counter &
done
wait

if [ $counter = 0 ]; then
    echo -e "${RED}found no tests whose filename contains \"$file_filter\"${RESET}" >&2
    exit 1
fi

echo ""
echo ""

failed=$( (ls -1 tmp/tests/diff*.txt 2>/dev/null || true) | wc -l)
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
