#!/usr/bin/env bash

set -e

if [ $# != 1 ] || [[ "$1" =~ ^- ]]; then
    echo "Usage: $0 input.txt"
    exit 2
fi

dot -T png -o /tmp/aoc20-graph.png <(
    echo "digraph G {"
    cat "$1" | sed s/'#.*'// | sed '/^$/d' | sed -E s/'^%([A-Za-z0-9]*)'/'\1[label="flip-flop \1"]\n\1'/g | sed -E s/'^&([A-Za-z]*)'/'\1[label="conjunction \1"]\n\1'/g
    echo "}"
)
open /tmp/aoc20-graph.png
