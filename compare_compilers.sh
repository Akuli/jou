#!/bin/bash
#
# There are two Jou compilers: one written in C and another written in Jou.
# They should be able to tokenize and parse each Jou file in exactly the same way.
# If tokenizing/parsing a Jou file fails, both compilers should fail with the same error message.

set -e

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
    mapfile -t files < <( find stdlib examples tests -name '*.jou' | sort )
fi
if [ ${#actions[@]} = 0 ]; then
    actions=(tokenize parse run)
fi

rm -rf tmp/compare_compilers
mkdir -vp tmp/compare_compilers

YELLOW="\x1b[33m"
GREEN="\x1b[32m"
RED="\x1b[31m"
RESET="\x1b[0m"

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
if [[ "$OS" =~ Windows ]]; then
    mingw32-make self_hosted_compiler.exe
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

    (./jou $flag $file || true) &> tmp/compare_compilers/compiler_written_in_c.txt
    (./self_hosted_compiler $flag $file || true) &> tmp/compare_compilers/self_hosted.txt

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
        fi
    else
        if diff -u --color=always tmp/compare_compilers/compiler_written_in_c.txt tmp/compare_compilers/self_hosted.txt; then
            echo "  Compilers behave the same as expected"
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
