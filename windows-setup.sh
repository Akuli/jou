#!/bin/bash

# Run this script in Git Bash to get started with developing Jou on Windows.
# It downloads everything you need (except a text editor).

set -e

# Make this script kinda work on operating systems other than Windows.
if ! [[ "$OS" =~ Windows ]]; then
    run=wine
fi

# These files can be left behind if this script is interrupted mid-way
# *.o and *.s come from dlltool.exe and there can be very many of them :D
echo "Cleaning up..."
rm -rvf *.o *.s *.def mingw64.zip

# Keep the size in the command below up to date if you update WinLibs:
if [ -d mingw64 ] && [ $(du -s mingw64 | cut -f1) == 989496 ]; then
    echo "mingw64 has already been downloaded and extracted."
else
    # The WinLibs version we use ships with LLVM 14, which is latest LLVM that Jou can use.
    # This is due to opaque pointer types. Scroll down to "Version Support" here: https://llvm.org/docs/OpaquePointers.html
    # All WinLibs versions and download links: https://winlibs.com/
    echo "Downloading mingw64 (WinLibs)..."
    curl -L -o mingw64.zip https://github.com/brechtsanders/winlibs_mingw/releases/download/12.1.0-14.0.6-10.0.0-msvcrt-r3/winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3.zip

    echo "Verifying mingw64..."
    if [ "$(sha256sum mingw64.zip | cut -d' ' -f1)" != "9ffef7f7a8dab893bd248085fa81a5a37ed6f775ae220ef673bea8806677836d" ]; then
        echo "Verifying $filename failed." >&2
        exit 1
    fi

    echo "Extracting mingw64..."
    rm -rf mingw64
    unzip -q mingw64.zip
    rm mingw64.zip
fi

# Blog post that explains how .a library is generated from dll file on Windows:
# https://dev.my-gate.net/2018/06/02/generate-a-def-file-from-a-dll/
#
# A simple "grep -r LLVMCreatePassManager" reveals that only libLLVMCore.dll
# contains the functions we need, so we generate a corresponding .a file.
if [ -f libLLVMCore.a ]; then
    echo "libLLVMCore.a has already been generated."
else
    echo "Generating libLLVMCore.a (this takes a while)"
    $run mingw64/bin/gendef.exe mingw64/bin/libLLVMCore.dll
    $run mingw64/bin/dlltool.exe -d libLLVMCore.def -l libLLVMCore.a
    rm libLLVMCore.def
fi

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
echo "Your Jou development environment has been set up."
echo "To compile Jou, run:"
echo ""
echo "   $run mingw64/bin/mingw32-make.exe"
echo ""




#echo "Extracting LLVM..."
#mkdir downloads/llvm
#(cd downloads/llvm && ../7z/x64/7za x ../LLVM-13.0.1-win64.exe >/dev/null)


# TODO: Stuck at not finding command-line 7z that would extract NSIS installers (for LLVM)
#       After extracting 7z's own nsis installer from https://sourceforge.net/projects/sevenzip/files/7-Zip/22.01/ it came with 7z.exe that seems to work??? (in wine)


# standalone make https://sourceforge.net/projects/gnuwin32/files/make/3.81/
