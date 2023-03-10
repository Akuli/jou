#!/bin/bash

# Run this script in Git Bash to get started with developing Jou on Windows.
# It downloads everything you need (except a text editor).

set -e

if ! [[ "$OS" =~ Windows ]]; then
    echo "This script must be ran on Windows." >&2
    exit 1
fi

if [ $# = 1 ] && [ $1 = --small ]; then
    small=yes
elif [ $# = 0 ]; then
    small=no
else
    echo "Usage: $0 [--small]" >&2
    exit 2
fi

echo ""
echo ""
echo "This script is meant for developing Jou. If you only want to use Jou, it"
echo "is easier to download and extract a zip file instead (see README.md)."
echo ""
echo ""

# Keep the size in the command below up to date if you update WinLibs:
if [ -d mingw64 ]; then
    echo "mingw64 has already been downloaded and extracted."
    echo "If you want to download it again, delete the mingw64 folder."
else
    # The WinLibs version we use ships with LLVM 14, which is latest LLVM that Jou can use.
    # This is due to opaque pointer types. Scroll down to "Version Support" here: https://llvm.org/docs/OpaquePointers.html
    # All WinLibs versions and download links: https://winlibs.com/
    if [ $small = yes ]; then
        # A special small version of mingw64 that comes with the repo, for people with slow internet.
        echo "Downloading mingw64-small.zip..."
        url=https://akuli.github.io/mingw64-small.zip
        filename=mingw64-small.zip
        sha=4d858bd22f084ae362ee6a22a52c2c5b5281d996f96693984a31336873b92686
    else
        echo "Downloading mingw64 (WinLibs)..."
        url=https://github.com/brechtsanders/winlibs_mingw/releases/download/12.1.0-14.0.6-10.0.0-msvcrt-r3/winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3.zip
        filename=mingw64.zip
        sha=9ffef7f7a8dab893bd248085fa81a5a37ed6f775ae220ef673bea8806677836d
    fi
    curl -L -o $filename $url

    echo "Verifying mingw64..."
    if [ "$(sha256sum $filename | cut -d' ' -f1)" != "$sha" ]; then
        echo "Verifying $filename failed! Please try again or create an issue on GitHub." >&2
        exit 1
    fi

    echo "Extracting mingw64..."
    rm -rf mingw64
    unzip -q $filename
    rm $filename
fi

# Blog post that explains how .a library is generated from dll file on Windows:
# https://dev.my-gate.net/2018/06/02/generate-a-def-file-from-a-dll/
#
# A simple "grep -r LLVMCreatePassManager" reveals that only libLLVMCore.dll
# contains the functions we need, so we generate a corresponding .a file.
# There are also a few other files that I found similarly.
mkdir -vp libs
echo "Generating .a files (this can take a while)"
for name in \
        libLLVMCore libLLVMX86CodeGen libLLVMAnalysis libLLVMTarget \
        libLLVMipo libLLVMLinker libLTO libLLVMX86AsmParser \
        libLLVMX86Info libLLVMX86Desc
do
    if [ -f libs/$name.a ]; then
        echo "  libs/$name.a has already been generated."
    else
        echo "  Generating libs/$name.a..."
        cd libs
        ../mingw64/bin/gendef.exe ../mingw64/bin/$name.dll
        ../mingw64/bin/dlltool.exe -d $name.def -l $name.a
        rm $name.def
        cd ..
        echo "    done"
    fi
done

# These headers are a bit of a hack. They have been taken from a different
# LLVM version, specifically, LLVM 13 compiled for Linux. We really only
# need the headers to declare the functions and structs and enums we use.
#
# But still, committing a zip file to Git just doesn't seem great...
if [ -d llvm ] && [ -d llvm-c ]; then
    echo "LLVM headers have already been extracted."
else
    echo "Extracting LLVM headers..."
    rm -rf llvm llvm-c
    unzip -q llvm_headers.zip
fi

echo ""
echo ""
echo ""
echo ""
echo "Your Jou development environment is now ready."
echo "Next you can compile the Jou compiler and run hello world:"
echo ""
echo "    source activate"
echo "    mingw32-make"
echo "    ./jou.exe examples/hello.jou"
echo ""
