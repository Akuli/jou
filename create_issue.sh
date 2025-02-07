#!/bin/bash

# This script is invoked from GitHub actions that run every night.
# It creates an issue on GitHub that lets me know the tests failed.

set -e -o pipefail

if [ -z "$GITHUB_RUN_ID" ]; then
    echo "This script can only be ran in GitHub Actions"
    exit 1
fi

if [ $# != 2 ]; then
    echo "Usage: $0 <github-token> <issue-title>"
    exit 2
fi

auth_header="Authorization: token $1"
issue_title="$2"

# Usage: find_issue <title>
# Echos issue number, if any
function find_issue() {
    curl -s "https://api.github.com/repos/Akuli/jou/issues?state=open" \
        | jq -r --arg t "$1" '.[] | select(.title==$t) | .number'
}

# Usage: get_body <issue_number>
function get_body() {
    curl -s "https://api.github.com/repos/Akuli/jou/issues/$1" | jq -r .body
}

# Usage: echo body | set_body <issue_number>
function set_body() {
    curl -s -X PATCH \
        -H "$auth_header" \
        -H "Accept: application/vnd.github.v3+json" \
        "https://api.github.com/repos/Akuli/jou/issues/$1" \
        -d "$(jq -n --arg b "$(cat)" '{body: $b}')"
}

# Usage: echo body | new_issue <title>
function new_issue() {
    curl -s -X POST \
        -H "$auth_header" \
        -H "Accept: application/vnd.github.v3+json" \
        "https://api.github.com/repos/Akuli/jou/issues" \
        -d "$(jq -n --arg t "$1" --arg b "$(cat)" '{title: $t, body: $b}')"
}

link="- [$(date)](https://github.com/Akuli/jou/actions/runs/$GITHUB_RUN_ID)"
echo $link

echo "Checking if there is already an issue with title \"$issue_title\"..."
n=$(find_issue "$issue_title")

if [ -n "$n" ]; then
    echo "There is already an open issue (#$n). Appending link to issue description."
    ( (get_body $n | awk 1) && echo "$link") | set_body $n
else
    echo "No open issue found, creating a new one."
    (echo "Test outputs:" && echo "$link") | new_issue "$issue_title"
fi
