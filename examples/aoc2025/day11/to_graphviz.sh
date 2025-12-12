#!/bin/bash
set -e

(
    echo 'digraph G {'
    echo '"you" [style=filled, fillcolor=red, fontcolor=white];'  # thanks AI
    cat sampleinput.txt | while read line; do
        start=$(echo $line | cut -d: -f1)
        for part in $(echo $line | cut -d: -f2); do
            echo "$start -> $part;"
        done
    done
    echo '}'
) | dot -Tpng -o graph.png
echo "Wrote graph.png"
