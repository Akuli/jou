#!/bin/bash

# Run this script in Git Bash to get started with developing Jou on Windows.
# It downloads everything you need (except a text editor).

set -e

# Make this script work on linux, so that I can develop it without windows
if [[ "$OS" =~ Windows ]]; then
    run=
else
    run=wine
fi

# Keep the size in the command below up to date if you update WinLibs:
if [ -d mingw64 ] && [ $(du -s mingw64 | cut -f1) -gt 900000 ]; then
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
        $run ../mingw64/bin/gendef.exe ../mingw64/bin/$name.dll
        $run ../mingw64/bin/dlltool.exe -d $name.def -l $name.a
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
if [ "$run" = "" ]; then
    echo "    mingw32-make"
    echo "    ./jou.exe examples/hello.jou"
else
    echo "    $run mingw32-make"
    echo "    $run ./jou.exe examples/hello.jou"
fi
echo ""




#echo "Extracting LLVM..."
#mkdir downloads/llvm
#(cd downloads/llvm && ../7z/x64/7za x ../LLVM-13.0.1-win64.exe >/dev/null)


# TODO: Stuck at not finding command-line 7z that would extract NSIS installers (for LLVM)
#       After extracting 7z's own nsis installer from https://sourceforge.net/projects/sevenzip/files/7-Zip/22.01/ it came with 7z.exe that seems to work??? (in wine)


# standalone make https://sourceforge.net/projects/gnuwin32/files/make/3.81/
