#!/usr/bin/env bash
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
    echo "Usage: $0 [--valgrind] [--verbose] [--stage1 | --stage2] [--dont-run-make] [--jou-flags \"-O3 ...\"] [FILE_FILTER]" >&2
    echo ""
    echo "By default, this tests the stage 3 compiler (see bootstrapping in README). Use"
    echo "--stage1 or --stage2 to test other stages as needed."
    echo ""
    echo "If a FILE_FILTER is given, runs only test files whose path contains it."
    echo "For example, you can use \"$0 class\" to run class-related tests."
    exit 2
}

valgrind=no
verbose=no
run_make=yes
jou_flags=""
file_filter=""
stage=3

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
        --stage1)
            stage=1
            shift
            ;;
        --stage2)
            stage=2
            shift
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

if [ $valgrind = yes ]; then
    case "${OS:=$(uname)}" in
        Windows*|NetBSD*)
            echo "valgrind doesn't work on $OS." >&2
            exit 2
            ;;
    esac
fi

DIFF=$(which gdiff || which diff)
if $DIFF --help | grep -q -- --color; then
    diff_color="--color=always"
fi

rm -rf tmp/tests
mkdir -p tmp/tests

joudir="$(pwd)"
if [[ "$OS" =~ Windows ]]; then
    if [ $stage = 3 ]; then
        jouexe="jou.exe"
    else
        jouexe="bootstrap/stage$stage.exe"
    fi
    if [[ "$joudir" =~ ^/[A-Za-z]/ ]]; then
        # Rewrite funny mingw path: /d/a/jou/jou --> D:/a/jou/jou
        letter=${joudir:1:1}
        joudir="${letter^^}:/${joudir:3}"
        unset letter
    fi
else
    if [ $stage = 3 ]; then
        jouexe="./jou"
    else
        jouexe="./bootstrap/stage$stage"
    fi
fi
echo "<joudir> in expected output will be replaced with $joudir."
echo "<jouexe> in expected output will be replaced with $jouexe."

if [ $run_make = yes ]; then
    if [[ "${OS:=$(uname)}" =~ Windows ]]; then
        source activate
        mingw32-make $jouexe
    elif [[ "$OS" =~ NetBSD ]]; then
        gmake $jouexe
    else
        make $jouexe
    fi
fi

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
            (grep --binary-files=text -onH '# Warning: .*' $joufile || true) | sed -E s/'(.*):([0-9]*):# Warning: '/'compiler warning for file "\1", line \2: '/
            (grep --binary-files=text -onH '# Error: .*' $joufile || true) | sed -E s/'(.*):([0-9]*):# Error: '/'compiler error in file "\1", line \2: '/
            (grep --binary-files=text -oE '# Output:.*' $joufile || true) | sed -E s/'^# Output: ?'//
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
        if [ "$(uname -s)" != Linux ]; then
            # Windows and macos don't seem to say "Segmentation fault" when
            # a program crashes
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

    # For stages 1 and 2, error handling is not important (users won't see it),
    # so only test that we support all features of Jou. Skip everything else.
    if [ $stage != 3 ] && (grep -qE 'Warning:|Error:' $joufile || ! [[ $joufile =~ should_succeed ]]); then
        return 0
    fi

    # When optimizations are enabled, skip tests that are supposed to crash.
    # Running them would be unpredictable by design.
    if [[ $joufile =~ ^tests/crash/ ]] && ! [[ "$jou_flags" =~ -O0 ]]; then
        return 0
    fi

    # When running valgrind, skip tests that are supposed to fail, because:
    #   - error handling is easier if you don't free memory (OS will do it)
    #   - in Jou compilers, error handling is simple and not very likely to contain UB
    #   - valgrinding is slow, this means we valgrind a lot less
    if [ $valgrind = yes ] && [ $correct_exit_code != 0 ]; then
        return 0
    fi

    # compiler_cli.jou hard-codes ./jou or ./jou.exe, so it tests stage 3 anyway
    if [ $stage != 3 ] && [[ $joufile =~ compiler_cli ]]; then
        return 0
    fi

    # Skip special programs that don't interact nicely with automated tests
    if [ $joufile = examples/x11_window.jou ] || [ $joufile = examples/memory_leak.jou ]; then
        return 0
    fi

    return 1  # false, don't skip
}

function run_test()
{
    local joufile="$1"
    local correct_exit_code="$2"
    local counter="$3"

    local command=""

    if [ $stage = 3 ]; then
        command="jou"
    else
        command="stage$stage"  # in "bootstrap" folder, PATH is adjusted in other part of this script
    fi

    if [[ "$OS" =~ Windows ]]; then
        command="$command.exe"
    fi

    if [ $valgrind = yes ] && [ $correct_exit_code == 0 ]; then
        # Valgrind the compiler process and the compiled executable
        command="valgrind -q --leak-check=full --show-leak-kinds=all --suppressions='$(pwd)/valgrind-suppressions.sup' $jouexe --valgrind"
    fi

    # jou flags start with space when non-empty
    command="$command$jou_flags"

    if [[ "$joufile" =~ ^examples/aoc ]] || [[ $joufile == *double_dotdot_import* ]]; then
        # AoC files use fopen("sampleinput.txt", "r").
        # We don't do this for all files, because I like relative paths in error messages.
        # jou_flags starts with a space whenever it isn't empty.
        #
        # Also, double_dotdot_import test had a bug that was reproducible only with
        # relative path.
        command="cd $(dirname $joufile) && $command $(basename $joufile)"
    else
        command="$command $joufile"
    fi

    show_run "$command"

    local diffpath
    diffpath=tmp/tests/diff$(printf "%04d" $counter).txt  # consistent alphabetical order

    printf "\n\n\x1b[33m*** Command: %s ***\x1b[0m\n\n" "$command" > $diffpath

    if $DIFF --text -u $diff_color <(
        generate_expected_output $joufile $correct_exit_code | tr -d '\r'
    ) <(
        if [[ "$OS" =~ Windows ]]; then
            PATHSEP=";"
        else
            PATHSEP=":"
        fi

        if [ $stage == 3 ]; then
            export PATH="$PWD$PATHSEP$PATH"
        else
            # stage1 and stage2 executables are in "bootstrap/" folder
            export PATH="$PWD/bootstrap$PATHSEP$PATH"
        fi

        if [ $valgrind = no ]; then
            ulimit -v 500000 2>/dev/null
        fi
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

for joufile in examples/*.jou examples/aoc*/day*/part*.jou tests/*/*.jou tests/should_succeed/double_dotdot_import/*/*.jou; do
    if ! [[ $joufile == *"$file_filter"* ]]; then
        # Skip silently, without showing that this is skipped.
        # This produces less noisy output when you select only a few tests.
        continue
    fi

    case $joufile in
        examples/* | tests/should_succeed/*) correct_exit_code=0; ;;
        *) correct_exit_code=1; ;;
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
