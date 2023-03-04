#!/bin/bash
#
# There are two Jou compilers: one written in C and another written in Jou.
# They should be able to tokenize and parse each Jou file in exactly the same way.
# If tokenizing/parsing a Jou file fails, both compilers should fail with the same error message.

if [ "$1" = --fix ]; then
    fix=yes
    shift
else
    fix=no
fi

if [[ "$1" =~ ^- ]]; then
    echo "Usage: $0 [--fix] [FILENAME1.jou FILENAME2.jou ...]" >&2
    exit 2
fi

if [ $# = 0 ]; then
    files=$(find stdlib examples tests -name '*.jou' | sort)
else
    files=$@
fi

if [[ "$OS" =~ Windows ]]; then
    source activate
    mingw32-make
else
    make
fi

set -e

rm -rf tmp/compare_compilers
mkdir -vp tmp/compare_compilers

echo "Compiling the self-hosted compiler..."
if [[ "$OS" =~ Windows ]]; then
    mingw32-make self_hosted_compiler.exe
else
    make self_hosted_compiler
fi

for file in $files; do
for action in tokenize parse run; do
    echo "$action $file"
    error_list_file=self_hosted/${action}s_wrong.txt
    if [ $action == run ]; then
        flag=
    else
        flag=--${action}-only
    fi

    (./jou $flag $file || true) &> tmp/compare_compilers/compiler_written_in_c.txt
    (./self_hosted_compiler $flag $file || true) &> tmp/compare_compilers/self_hosted.txt

    if grep -q '\r' $error_list_file; then
        newline=crlf
    else
        newline=lf
    fi

    if grep -qxF $file <(cat $error_list_file | tr -d '\r'); then
        # The file is skipped, so the two compilers should behave differently
        if diff tmp/compare_compilers/compiler_written_in_c.txt tmp/compare_compilers/self_hosted.txt >/dev/null; then
            if [ $fix = yes ]; then
                echo "  Deleting $file from $error_list_file"
                grep -vxF $file <(cat $error_list_file | tr -d '\r') > tmp/compare_compilers/newlist.txt
                mv tmp/compare_compilers/newlist.txt $error_list_file
            else
                echo "  Error: Compilers behave the same even though the file is listed in $error_list_file."
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
                echo "  Adding $file to $error_list_file"
                echo $file >> $error_list_file
            else
                echo "  Error: Compilers behave differently when given \"$file\"."
                echo "  Ideally the compilers would behave in the same way for all files, but we aren't there yet."
                echo "  To silence this error, add \"$file\" to $error_list_file (or run again with --fix)."
                exit 1
            fi
        fi
    fi

    if [ $newline = crlf ] && [ $fix = yes ]; then
        # Some of the edits (e.g. appending with echo) leave LF line endings
        unix2dos -q $error_list_file
    fi
done
done

echo ""
echo ""
echo "success :)"
