#!/bin/bash

# Produces test data for URI parsing and formatting used in langserver.
#
# Uses vscode-uri from github. Runs it with bun because vscode-uri is written
# in typescript and I didn't want to download more stuff just to run it.

set -e -o pipefail

if [ "$(uname -sm)" != "Linux x86_64" ]; then
    echo "$0: unsupported system, sorry" >&2
    exit 1
fi

cd "$(dirname "$0")/.."

mkdir -vp tmp/langserver_uri_tests
cd tmp/langserver_uri_tests
echo "Working directory: $(pwd)"

if ! [ -d bun-linux-x64-baseline ]; then
    echo "Downloading bun..."
    curl -Lf -C - --retry 6 --retry-all-errors -o bun.zip https://github.com/oven-sh/bun/releases/download/bun-v1.4.5/bun-linux-x64-baseline.zip

    echo "Verifying bun..."
    if [ "$(sha256sum bun.zip | cut -d' ' -f1)" != "dcb23a18a1c2fff12d57bcef696dbbb42eb67fde29f43816ecb595459a24c463" ]; then
        echo "Verifying failed! Please delete $(pwd) and try again." >&2
        exit 1
    fi

    echo "Extracting bun..."
    unzip bun.zip
fi

if ! [ -d vscode-uri-3.2.0 ]; then
    echo "Downloading vscode-uri..."
    curl -Lf -C - --retry 6 --retry-all-errors -o vscode-uri.zip https://github.com/microsoft/vscode-uri/archive/refs/tags/v3.2.0.zip

    echo "Verifying vscode-uri..."
    if [ "$(sha256sum vscode-uri.zip | cut -d' ' -f1)" != "fc1dc97a936df6659fdbf0232c495c1e95d0e60c2cb341ae9be0b3d9867f615a" ]; then
        echo "Verifying failed! Please delete $(pwd) and try again." >&2
        exit 1
    fi

    echo "Extracting vscode-uri..."
    unzip vscode-uri.zip
fi

echo "Creating patched copy of vscode-uri that thinks it's running on windows..."
rm -rf vscode-uri
cp -r vscode-uri-3.2.0 vscode-uri
echo isWindows=true >> vscode-uri/src/platform.ts
diff -r -u --color vscode-uri-3.2.0 vscode-uri || true

echo -e "Running javascript code...\n\n"
bun-linux-x64-baseline/bun - <<'EOF'

import { URI } from './vscode-uri/src/index.ts';

for (const path of [
    'C:\\Users\\Alice\\project\\index.ts',
    'D:\\Folder with Spaces\\file Name!.txt',
    'C:\\data\\item#1?draft=True.md',
    '\\\\server\\share\\docs\\spec.pdf',
    'C:/mixed/slashes\\path.js',
    'C:/Örkki/Mörkö',
    'C:\\',
]) {
    const uri = URI.file(path).toString();
    console.log(JSON.stringify(path));
    console.log(JSON.stringify(uri));
    console.log("");
}

EOF
