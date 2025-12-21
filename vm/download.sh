#!/bin/bash
#
# This script downloads a file. But unlike plain old wget:
#
#   - If the file has been downloaded before (either fully or partially), it
#     will not be downloaded again.
#
#   - If the file has been deleted, it STILL won't be downloaded again, because
#     it is first placed into a separate "downloads/" folder and then copied to
#     wherever you need it.
#
#   - This file verifies that the downloaded file content matches a given
#     SHA256 hash.
#
# This assumes that you don't download two different files with the same name.

set -e -o pipefail

if [ $# != 2 ]; then
    echo "Usage: $0 <url> <sha256>"
    exit 2
fi

url="$1"
sha256="$2"

cache_dir="$(dirname "$0")/downloads"
cache_file="$cache_dir/$(basename "$url")"

mkdir -vp "$cache_dir"

verified=no
wget_flags=""
if [ -f "$cache_file" ]; then
    echo "$0: verifying..."
    if [ $(sha256sum "$cache_file" | cut -d' ' -f1) = $sha256 ]; then
        echo "$0: reusing $cache_file"
        verified=yes
    else
        echo "$0: verifying failed, assuming file is partially downloaded and downloading the rest of it..."
        wget_flags='--continue'
    fi
else
    echo "$0: downloading $url"
    if [ "$GITHUB_ACTIONS" = "true" ]; then
        wget_flags='-q'
    fi
fi

# Refactoring note: Please double check that verifying is always done!
if [ $verified = no ]; then
    (cd "$cache_dir" && wget $wget_flags "$url")
    echo "$0: verifying..."
    if [ $(sha256sum "$cache_file" | cut -d' ' -f1) != $sha256 ]; then
        echo "$0: verifying $cache_file failed!!!" >&2
        ls -lh "$cache_file"
        exit 1
    fi
fi

cp -v "$cache_file" .
echo "$0: done!"
