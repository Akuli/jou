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
    echo "Usage: $0 [--valgrind] [--verbose] [--dont-run-make] [--jou-flags \"-O3 ...\"] [--no-colors] [FILE_FILTER]" >&2
    echo ""
    echo "If a FILE_FILTER is given, runs only test files whose path contains it."
    echo "For example, you can use \"$0 class\" to run class-related tests."
    exit 2
}

valgrind=no
verbose=no
run_make=yes
jou_flags=""
colors=yes
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
        --no-colors)
            colors=no
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
if [ $(getconf LONG_BIT) = 32 ]; then
    intnative=int
else
    intnative=int64
fi

echo "<joudir> in expected output will be replaced with $joudir."
echo "<jouexe> in expected output will be replaced with $jouexe."
echo "<intnative> in expected output will be replaced with $intnative."

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

if [[ "${OS:=$(uname)}" =~ NetBSD ]] && command -v gdiff >/dev/null; then
    diff="gdiff"
else
    diff="diff"
fi
if [ $colors = yes ] && $diff --help | grep -q -- --color; then
    diff="$diff --color=always"
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
        ) | sed "s,<intnative>,$intnative,g" | sed "s,<joudir>,$joudir,g" | sed "s,<jouexe>,$jouexe,g"
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
    elif [[ $joufile =~ compiler_unit_tests ]] && [[ "${OS:=$(uname)}" =~ Darwin ]]; then
        # On MacOS, silence a linker warning that appears whenever linking
        # with LLVM. I don't know what causes the warning.
        #
        # See: https://github.com/Akuli/jou/issues/1005
        grep -v "ld: warning: reexported library with install name"
    else
        # Pass the output through unchanged.
        cat
    fi
}

if [ $colors = yes ]; then
    YELLOW="\x1b[33m"
    GREEN="\x1b[32m"
    RED="\x1b[31m"
    RESET="\x1b[0m"
else
    YELLOW=""
    GREEN=""
    RED=""
    RESET=""
fi

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

    # When optimizations are enabled, skip tests that are supposed to crash.
    # Running them would be unpredictable by design.
    if [[ $joufile =~ ^tests/crash/ ]] && ! [[ "$jou_flags" =~ -O0 ]]; then
        return 0
    fi

    # When running valgrind, skip tests that are supposed to fail, because:
    #   - error handling is easier if you don't free memory (OS will do it)
    #   - in the Jou compiler, error handling is simple and not very likely to contain UB
    #   - valgrinding is slow, this means we valgrind a lot less
    if [ $valgrind = yes ] && [ $correct_exit_code != 0 ]; then
        return 0
    fi

    # When running valgrind, skip compiler unit tests. They link with LLVM and
    # LLVM leaks memory when the program starts, even if it is never called.
    if [ $valgrind = yes ] &&  [ $joufile = tests/should_succeed/compiler_unit_tests.jou ]; then
        return 0
    fi

    # Skip special programs that don't interact nicely with automated tests
    if [ $joufile = examples/x11_window.jou ] || [ $joufile = examples/memory_leak.jou ]; then
        return 0
    fi

    # If liblzma is not installed in a pkg-config compatible way, skip tests that use it
    if [[ $joufile =~ link_with_liblzma ]]; then
        if ! (pkg-config --exists liblzma 2>/dev/null); then
            return 0
        fi
        if [[ $joufile =~ (relative|system)_path && ! -e "$(pkg-config --variable=libdir liblzma)/liblzma.a" ]]; then
            return 0
        fi
    fi

    # This file is very slow compared to other advent of codes
    if [ $joufile = examples/aoc2023/day24/part2.jou ]; then
        return 0
    fi

    return 1  # false, don't skip
}

function run_test()
{
    local joufile="$1"
    local correct_exit_code="$2"
    local counter="$3"

    local command="${jouexe#./}"

    if [ $valgrind = yes ] && [ $correct_exit_code == 0 ]; then
        # Valgrind the compiler process and the compiled executable
        command="valgrind -q --leak-check=full --show-leak-kinds=all --suppressions='$(pwd)/valgrind-suppressions.sup' $command --valgrind"
    fi

    # jou flags start with space when non-empty
    command="$command$jou_flags"

    if [[ "$joufile" =~ ^examples/aoc ]] || [[ $joufile == *double_dotdot_import* ]] || [[ $joufile == *perlin* ]]; then
        # AoC files use fopen("sampleinput.txt", "r").
        # We don't do this for all files, because I like relative paths in error messages.
        #
        # double_dotdot_import test had a bug that was reproducible only with relative path.
        #
        # Perlin noise example writes to a file. We want that file to end up in the examples
        # folder, not in project root.
        command="cd $(dirname $joufile) && $command $(basename $joufile)"
    else
        command="$command $joufile"
    fi

    if [[ $joufile =~ link_with_liblzma_relative_path ]]; then
        # This test passes a relative path to the "link" keyword and expects to find liblzma.a with it
        command="cp '$(pkg-config --variable=libdir liblzma)/liblzma.a' tmp/tests/ && $command"
    fi

    show_run "$command"

    local diffpath
    diffpath=tmp/tests/diff$(printf "%04d" $counter).txt  # consistent alphabetical order

    printf "\n\n${YELLOW}*** Command: %s ***${RESET}\n\n" "$command" > $diffpath

    if $diff --text -u <(
        generate_expected_output $joufile $correct_exit_code | tr -d '\r'
    ) <(
        export PATH="$PWD:$PATH"
        if [ $valgrind = no ]; then
            ulimit -v 1000000 2>/dev/null
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

# How many tests to run in parallel? Fall back to 2 if nproc fails or doesn't exist.
num_tests_in_parallel=$(nproc 2>/dev/null || echo 2)

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
        *) correct_exit_code=1; ;;  # compiler or runtime error
    esac
    counter=$((counter + 1))

    if should_skip $joufile $correct_exit_code; then
        show_skip $joufile
        skipped=$((skipped + 1))
        continue
    fi

    while [ $(jobs -p | wc -l) -ge $num_tests_in_parallel ]; do wait -n; done
    run_test $joufile $correct_exit_code $counter &
done
wait

if [ $counter = 0 ]; then
    echo -e "${RED}found no tests whose filename contains \"$file_filter\"${RESET}" >&2
    exit 1
fi

echo ""
echo ""

failed=$( (ls -1 tmp/tests/diff*.txt 2>/dev/null || true) | wc -l | tr -d ' ')
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
