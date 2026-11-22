#!/bin/bash

#tee input.txt | env/bin/ty server 2>errors.txt | tee output.txt
tee input.txt | ~/jou/joulsp --record=vscodium-joulsp.bin 2>errors.txt | tee output.txt
