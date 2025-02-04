#!/bin/bash

# Prints the description of the next Jou release, based on commit messages of
# the latest commits. Runs in GitHub Actions when creating a new release.
#
# Empty output means that there are no new commits and a new release is not needed.

set -e -o pipefail

if [ $# == 2 ] && [ $1 == "--github-token" ]; then
    auth_header="Authorization: token $2"
elif [ $# == 0 ]; then
    auth_header=""
else
    echo "Usage: $0 [--github-token TOKEN]"
    echo ""
    echo "Passing a GitHub API token is optional, but helps avoid rate limit issues."
    echo "GitHub API is used to check which pull requests have the skip-release label."
    exit 1
fi

# Make sure we have the latest tags
git fetch --tags

first=yes
latest_tag=$(git describe --tags --abbrev=0)

for commit in $(git log --pretty=%H $latest_tag..origin/main --reverse); do
    # Get first line of commit message (the summary line)
    summary="$(git show -s --format=%s $commit)"

    pr_number="$(echo "$summary" | grep -oE '\(#[0-9]+\)$' | grep -oE '[0-9]+')"
    if [ -n "$pr_number" ]; then
        # It is associated with a pull request
        if curl -s -H "$auth_header" https://api.github.com/repos/Akuli/jou/pulls/$pr_number | jq -r '.labels[].name' | grep -q '^skip-release$'; then
            # Pull request was marked with skip-release label, ignore it
            continue
        fi
        link=https://github.com/Akuli/jou/pulls/$pr_number
    else
        # A commit was pushed directly to main branch
        link=https://github.com/Akuli/jou/commit/$commit
    fi

    if [ $first = yes ]; then
        echo "This release contains the following changes:"
        echo ""
        first=no
    fi

    echo "- [$summary]($link)"
done
