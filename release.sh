#!/bin/bash
#
# This scripts generates a new Jou release. It is invoked from GitHub Actions
# automatically as documented in CONTRIBUTING.md.

set -e -o pipefail

dry_run=no
auth_header=""

while [ $# != 0 ]; do
    case "$1" in
        --dry-run)
            dry_run=yes
            shift
            ;;
        --github-token)
            auth_header="Authorization: token $2"
            shift 2
            ;;
        *)
            echo "Usage: $0 [--github-token TOKEN] [--dry-run]"
            echo ""
            echo "With --dry-run, no release is made. This can be used to develop many"
            echo "features of this script."
            echo ""
            echo "Passing a GitHub API token may be useful even with --dry-run. GitHub API"
            echo "is used to check which pull requests have the skip-release label, and you"
            echo "may run into rate-limit issues if you don't specify a token."
            exit 2
            ;;
    esac
done


echo "Generating release description"
rm -rvf tmp/release
mkdir -vp tmp/release
cd tmp/release

# Make sure git knows about the latest tags in github
git fetch --tags -q

latest_tag=$(git describe --tags --abbrev=0)
for commit in $(git log --pretty=%H $latest_tag..origin/main --reverse); do
    # Get first line of commit message (the summary line)
    summary="$(git show -s --format=%s $commit)"
    echo "Found commit ${commit:0:10}: $summary"

    pr_number="$(echo "$summary" | grep -oE '\(#[0-9]+\)$' | grep -oE '[0-9]+')"
    if [ -n "$pr_number" ]; then
        if curl -s -H "$auth_header" https://api.github.com/repos/Akuli/jou/pulls/$pr_number | jq -r '.labels[].name' | grep -q '^skip-release$'; then
            echo "  Skipping because pull request #$pr_number has the 'skip-release' label"
            continue
        fi
        link=https://github.com/Akuli/jou/pull/$pr_number
    else
        echo "  No associated pull request, the commit has probably been pushed directly to main"
        link=https://github.com/Akuli/jou/commit/$commit
    fi

    if ! [ -f desc.md ]; then
        echo "This release contains the following changes:" > desc.md
    fi
    echo "- [$summary]($link)" >> desc.md
done

if ! [ -f desc.md ]; then
    echo "No changes to release. Stopping."
    exit 0
fi

echo ""
cat desc.md
echo ""

echo "Finding latest Windows CI run for main branch..."
commit=$(git rev-parse origin/main)
curl -s -H "$auth_header" "https://api.github.com/repos/Akuli/jou/actions/runs?branch=main&head_sha=$commit" | jq '.workflow_runs[] | select(.path==".github/workflows/windows.yml")' > run.json
echo "  Run ID: $(jq -r .id < run.json)"
echo "  Status: $(jq -r .status < run.json)"
echo "  Artifacts URL: $(jq -r .artifacts_url < run.json)"
echo ""

if [ $(jq -r .status < run.json) != completed ]; then
    echo "Seems like CI is still running. Not releasing."
    exit 0
fi

echo "Finding windows-zip artifact..."
archive_download_url="$(curl -s -H "$auth_header" "$(jq -r .artifacts_url < run.json)" | jq -r '.artifacts[] | select(.name == "windows-zip") | .archive_download_url')"
echo "  Archive download URL: $archive_download_url"
echo ""

if [ -z "$auth_header" ]; then
    echo "Error: GitHub requires authentication to download artifacts. Cannot continue without a token."
    exit 1
fi

# We get a zip file inside a zip file:
#   * Inner zip file: The build creates a releasing-ready zip file. This is
#     good because you can test the zip file before Jou is released.
#   * Outer zip file: It is possible to include multiple files to the same
#     GitHub Actions artifact, and downloadArtifact() gives a zip of all
#     files that the artifact consists of.
echo "Downloading windows-zip artifact..."
curl -L -H "$auth_header" "$archive_download_url" -o nested-zip-file.zip
unzip nested-zip-file.zip
echo ""

datetag=$(date +'%Y-%m-%d-%H00')
jq -n --arg tag $datetag --arg desc "$(cat desc.md)" '{tag_name: $tag, body: $desc}' > release-params.json
echo "Release parameters:"
cat release-params.json
echo ""

if [ $dry_run = yes ]; then
    echo "Aborting because this is a dry-run."
    exit 0
fi

# To test this, you can change Akuli/jou to a temporary private repo
echo "CREATING RELEASE!!!"
curl -X POST "https://api.github.com/repos/Akuli/jou/releases" -H "$auth_header" -d @release-params.json | tee release-response.json
echo ""

echo "Attaching the Windows zip file to the release..."
# Response contains:
#     "upload_url": "https://uploads.github.com/repos/Akuli/jou/releases/xxxxxxxxx/assets{?name,label}",
upload_url="$(jq -r .upload_url < release-response.json | cut -d'{' -f1)?name=jou_windows_64bit_$datetag.zip"
echo "  POSTing to $upload_url"
curl -L -X POST -H "$auth_header" -H "Content-Type: application/zip" "$upload_url" --data-binary "@jou.zip"
