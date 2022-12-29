#!/bin/bash
set -e -o pipefail

which shuf head rev

make
rm -rf tmp/fuzzer
mkdir -vp tmp/fuzzer

# How long to run the fuzzer if it doesn't crash?
time="1 minute"

end=$(date +%s -d "+ $time")
while [ $(date +%s) -lt $end ]; do
    # A few different kinds of inputs:
    #   - Random bytes to find bugs in tokenizer
    #   - Mixed up Jou code and C code to find bugs in parser
    head -c 1000 /dev/urandom               > tmp/fuzzer/input1.jou
    cat tests/*/*.jou | shuf | head         > tmp/fuzzer/input2.jou
    cat tests/*/*.jou | shuf | head | rev   > tmp/fuzzer/input3.jou

    for file in tmp/fuzzer/input*.jou; do
        # The "sh" shell prints "Segmentation fault" to stderr when that happens.
        # I couldn't get bash to make it redirectable.
        (sh -c "./jou $file" || true) &> tmp/fuzzer/log.txt

        # One-line compiler error message made up of printable ASCII chars = good
        # https://unix.stackexchange.com/a/194538
        if [ $(cat tmp/fuzzer/log.txt | wc -l) = 1 ] &&
            grep -q 'compiler error in file' tmp/fuzzer/log.txt &&
            ! LC_ALL=C grep -q '[^[:print:]]' tmp/fuzzer/log.txt
        then
            echo -n .
        else
            echo ""
            echo "*** found a possible bug ***"
            echo ""
            echo "To reproduce, run:  ./jou $file"
            if [ "$CI" = "true" ]; then
                echo ""
                echo "For CI environments and such, here's a hexdump of $file:"
                hexdump -C $file
            fi
            exit 1
        fi
    done
done

echo ""
echo ""
echo "Ran $time, no bugs found"
