#!/bin/bash
while true; do
    for i in `seq 1 9`; do
        echo -n $i
        sleep $(printf "0.%02d" $((RANDOM % 20)) )
    done
done
