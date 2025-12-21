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

if [ -f "$cache_file" ]; then
    echo "$0: reusing $cache_file"
else
    echo "$0: downloading from $url..."
    mkdir "$cache_dir" || true
    (cd "$cache_dir" && wget --continue "$url")
fi

echo "$0: verifying..."
if [ $(sha256sum "$cache_file" | cut -d' ' -f1) != $sha256 ]; then
    echo "$0: verifying $cache_file failed!!!" >&2
    ls -lh "$cache_file"
    exit 1
fi

cp -v "$cache_file" .
