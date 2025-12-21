#!/bin/bash
set -e -o pipefail

if [ $# != 2 ]; then
    echo "Usage: $0 <url> <sha256>"
    exit 2
fi

url="$1"
sha256="$2"

cache_dir="$(dirname "$0")/cache"
cache_file="$cache_dir/$(basename "$url")"

mkdir -vp "$cache_dir"

verified=no
if [ -f "$cache_file" ]; then
    echo "$0: verifying..."
    if [ $(sha256sum "$cache_file" | cut -d' ' -f1) = $sha256 ]; then
        echo "$0: reusing $cache_file"
        verified=yes
    else
        echo "$0: verifying failed, assuming file is partially downloaded and downloading the rest of it..."
        (cd "$cache_dir" && wget --continue "$url")
    fi
else
    echo "$0: downloading $url"
    (cd "$cache_dir" && wget "$url")
fi

echo "$0: verifying..."
if [ $(sha256sum "$cache_file" | cut -d' ' -f1) != $sha256 ]; then
    echo "$0: verifying $cache_file failed!!!" >&2
    ls -lh "$cache_file"
    exit 1
fi

cp -v "$cache_file" .
echo "$0: done!"
