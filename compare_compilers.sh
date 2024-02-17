#!/usr/bin/env bash
#
# There are two Jou compilers: one written in C and another written in Jou.
# They should be able to tokenize and parse each Jou file in exactly the same way.
# If tokenizing/parsing a Jou file fails, both compilers should fail with the same error message.

set -e
ulimit -c unlimited

files=()
actions=()
fix=no

for arg in "$@"; do
    if [ "$arg" = --fix ]; then
        fix=yes
    elif [ "$arg" = --tokenize ]; then
        actions+=(tokenize)
    elif [ "$arg" = --parse ]; then
        actions+=(parse)
    elif [ "$arg" = --run ]; then
        actions+=(run)
    elif [[ "$arg" =~ ^- ]]; then
        echo "Usage: $0 [--tokenize] [--parse] [--run] [--fix] [FILENAME1.jou FILENAME2.jou ...]" >&2
        exit 2
    else
        files+=($arg)
    fi
done

if [ ${#files[@]} = 0 ]; then
    # skip compiler_cli, because it has a race condition when two compilers simultaneously run it
    # TODO: do not skip Advent Of Code files
    files=( $(find stdlib examples tests -name '*.jou' | grep -v aoc2023 | grep -v tests/should_succeed/compiler_cli | grep -v tests/crash | sort) )
fi
if [ ${#actions[@]} = 0 ]; then
    actions=(tokenize parse run)
fi

rm -rf tmp/compare_compilers
mkdir -p tmp/compare_compilers

# shellcheck disable=SC2010
if ls | grep -q core$; then
    for core in *core; do
        mv -v -- "$core" "$core"~
    done
fi

YELLOW="\x1b[33m"
GREEN="\x1b[32m"
RED="\x1b[31m"
RESET="\x1b[0m"

DIFF=$(which gdiff || which diff)
if $DIFF --help | grep -q -- --color; then
    diff_color="--color=always"
fi

function append_line()
{
    local file="$1"
    local line="$2"
    echo -e "  ${YELLOW}Adding $line to $file${RESET}"

    if [ -f $file ] && grep -q $'\r' $file; then
        # CRLF line endings (likely Windows, but depends on git settings)
        printf "%s\r\n" "$line" >> "$file"
    else
        printf "%s\n" "$line" >> "$file"
    fi
}

function remove_line()
{
    local file="$1"
    local line="$2"
    echo -e "  ${YELLOW}Deleting $line from $file${RESET}"
    # Filter out the line regardless of whether it has CRLF or LF after it
    cat "$file" | grep -vxF "$line" | grep -vxF "$line"$'\r' > tmp/compare_compilers/newlist.txt
    mv tmp/compare_compilers/newlist.txt "$file"
}

for error_list_file in self_hosted/*s_wrong.txt; do
    echo "Checking that all files mentioned in $error_list_file exist..."
    for line in $(cat $error_list_file | tr -d '\r' | grep -v '^#'); do
        if ! [ -f $line ]; then
            if [ $fix = yes ]; then
                remove_line $error_list_file $line
            else
                echo -e "  ${RED}Error: $error_list_file mentions file \"$line\" which does not exist.${RESET}"
                echo "  To fix this error, delete the \"$line\" line from $error_list_file (or run again with --fix)."
                exit 1
            fi
        fi
    done
done

echo "Compiling the self-hosted compiler..."
if [[ "${OS:=$(uname)}" =~ Windows ]]; then
    mingw32-make self_hosted_compiler.exe
elif [[ "$OS" =~ NetBSD ]]; then
    gmake self_hosted_compiler
else
    make self_hosted_compiler
fi

for file in ${files[@]}; do
for action in ${actions[@]}; do
    echo "$action $file"
    error_list_file=self_hosted/${action}s_wrong.txt
    if [ $action == run ]; then
        flag=
    else
        flag=--${action}-only
    fi

    # Run both compilers, and filter out lines that are known to differ but it doesn't matter (mostly linker errors)
    # Run compilers in parallel to speed up.
    (
        set +e
        ./jou $flag $file 2>&1 | grep -vE 'undefined reference to|multiple definition of|\bld: |compiler warning for file'
    ) > tmp/compare_compilers/compiler_written_in_c.txt &
    (
        set +e
        ./self_hosted_compiler $flag $file 2>&1 | grep -vE 'undefined reference to|multiple definition of|\bld: |linking failed|compiler warning for file'
    ) > tmp/compare_compilers/self_hosted.txt &
    wait

    if [ -f $error_list_file ] && grep -qxF $file <(cat $error_list_file | tr -d '\r'); then
        # The file is skipped, so the two compilers should behave differently
        if diff tmp/compare_compilers/compiler_written_in_c.txt tmp/compare_compilers/self_hosted.txt >/dev/null; then
            if [ $fix = yes ]; then
                remove_line $error_list_file $file
            else
                echo -e "  ${RED}Error: Compilers behave the same even though the file is listed in $error_list_file.${RESET}"
                echo "  To fix this error, delete the \"$file\" line from $error_list_file (or run again with --fix)."
                exit 1
            fi
        else
            echo "  Compilers behave differently as expected (listed in $error_list_file)"
            echo -en ${YELLOW}
            rm -vf -- *core | xargs -r echo Core dumped and ignored:
            echo -en ${RESET}
        fi
    else
        if $DIFF -u $diff_color tmp/compare_compilers/compiler_written_in_c.txt tmp/compare_compilers/self_hosted.txt; then
            # shellcheck disable=SC2010
            if ls | grep -q core$; then
                echo -e "  ${RED}Error: Core dumped.${RESET}"
                exit 1
            else
                echo "  Compilers behave the same as expected"
            fi
        else
            if [ $fix = yes ]; then
                append_line $error_list_file $file
            else
                echo -e "  ${RED}Error: Compilers behave differently when given \"$file\".${RESET}"
                echo "  Ideally the compilers would behave in the same way for all files, but we aren't there yet."
                echo "  To silence this error, add \"$file\" to $error_list_file (or run again with --fix)."
                exit 1
            fi
        fi
    fi
done
done

echo ""
echo ""
echo -e "${GREEN}success :)${RESET}"
