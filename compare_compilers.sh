#!/bin/bash
#
# There are two Jou compilers: one written in C and another written in Jou.
# They should be able to tokenize and parse each Jou file in exactly the same way.
# If tokenizing/parsing a Jou file fails, both compilers should fail with the same error message.

if [[ "$OS" =~ Windows ]]; then
    dotexe=.exe
else
    dotexe=
fi

set -e

rm -rf tmp/tokenizers
mkdir -vp tmp/tokenizers

echo "Compiling the self-hosted compiler..."
./jou${dotexe} -O1 -o tmp/tokenizers/self_hosted${dotexe} self_hosted/main.jou 

for file in $(find examples tests -name '*.jou'); do
    for action in tokenize parse; do
        echo "$action $file"
        flag=--${action}-only
        error_list_file=self_hosted/${action}s_wrong.txt

        (./jou${dotexe} $flag $file || true) &> tmp/tokenizers/compiler_written_in_c.txt
        (tmp/tokenizers/self_hosted${dotexe} $flag $file || true) &> tmp/tokenizers/self_hosted.txt

        if grep -qxF $file $error_list_file; then
            # The file is skipped, so the two compilers should behave differently
            if diff tmp/tokenizers/compiler_written_in_c.txt tmp/tokenizers/self_hosted.txt >/dev/null; then
                echo "  Error: Compilers behave the same even though the file is listed in $error_list_file."
                echo "  To fix this error, delete the \"$file\" line from $error_list_file."
                exit 1
            else
                echo "  Compilers behave differently as expected (listed in $error_list_file)"
            fi
        else
            if diff -u --color=always tmp/tokenizers/compiler_written_in_c.txt tmp/tokenizers/self_hosted.txt; then
                echo "  Compilers behave the same as expected"
            else
                echo "  Error: Compilers behave differently when given \"$file\"."
                echo "  You can silence this error by adding \"$file\" to $error_list_file."
                echo "  Ideally the tokenizers would behave in the same way for all files, but we aren't there yet."
                exit 1
            fi
        fi
    done
done

echo ""
echo ""
echo "success :)"
