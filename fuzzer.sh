#!/bin/bash
set -e -o pipefail

#make
rm -rf tmp/fuzzer
mkdir -vp tmp/fuzzer

# How long to run the fuzzer if it doesn't crash?
time="1 minute"

end=$(date +%s -d "+ $time")
while [ $(date +%s) -lt $end ]; do
    # Random bytes to find bugs in tokenizer
    head -c 1000 /dev/urandom > tmp/fuzzer/input1.jou

    # TODO: some way to fuzz other parts than just the tokenizer?
    # The commented lines below caused false positives (#6) and never found real bugs.
    #cat tests/*/*.jou | shuf -n 10 > tmp/fuzzer/input2.jou
    #cat tests/*/*.jou | shuf -n 10 | rev > tmp/fuzzer/input3.jou

    for file in tmp/fuzzer/input*.jou; do
        # The "sh" shell prints "Segmentation fault" to stderr when that happens.
        # I couldn't get bash to make it redirectable.
        (bash -c "./jou $file 2>&1" || true) | tr -d '\r' > tmp/fuzzer/log.txt

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
