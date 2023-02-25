#!/bin/bash
#
# There are two Jou compilers: one written in C and another written in Jou.
# They should be able to tokenize each Jou file in exactly the same way.
# If tokenizing a Jou file fails, both tokenizers should fail with the same error message.

if [[ "$OS" =~ Windows ]]; then
    dotexe=.exe
else
    dotexe=
fi

set -e

rm -rf tmp/tokenizers
mkdir -vp tmp/tokenizers

echo "Compiling the self-hosted compiler..."
./jou${dotexe} -O1 -o tmp/tokenizers/self_hosted${dotexe} self_hosted/tokenizer.jou 

for file in $(find examples tests -name '*.jou' | sort); do
    echo $file
    (./jou${dotexe} --tokenize-only $file || true) &> tmp/tokenizers/compiler_written_in_c.txt
    (tmp/tokenizers/self_hosted${dotexe} $file || true) &> tmp/tokenizers/self_hosted.txt

    if grep -qxF $file self_hosted/tokenizes_wrong.txt; then
        # The file is skipped, so the two compilers should behave differently
        if diff tmp/tokenizers/compiler_written_in_c.txt tmp/tokenizers/self_hosted.txt >/dev/null; then
            echo "  Error: Tokenizers behave the same even though the file is listed in self_hosted/tokenizes_wrong.txt."
            echo "  To fix this error, delete the \"$file\" line from self_hosted/tokenizes_wrong.txt."
            #grep -vxF $file self_hosted/tokenizes_wrong.txt > /tmp/x; mv /tmp/x self_hosted/tokenizes_wrong.txt
            exit 1
        else
            echo "  Tokenizers behave differently as expected (listed in self_hosted/tokenizes_wrong.txt)"
        fi
    else
        if diff -u --color=always tmp/tokenizers/compiler_written_in_c.txt tmp/tokenizers/self_hosted.txt; then
            echo "  Tokenizers behave the same as expected"
        else
            echo "  Error: Tokenizers behave differently when given \"$file\"."
            echo "  You can silence this error by adding \"$file\" to self_hosted/tokenizes_wrong.txt."
            echo "  Ideally the tokenizers would behave in the same way for all files, but we aren't there yet."
            exit 1
        fi
    fi
done

echo ""
echo ""
echo "success :)"
