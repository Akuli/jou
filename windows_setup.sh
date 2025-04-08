#!/usr/bin/env bash

# Run this script in Git Bash to get started with developing Jou on Windows.
# It downloads everything you need (except a text editor).

set -e

if ! [[ "$OS" =~ Windows ]]; then
    echo "This script must be ran on Windows." >&2
    exit 1
fi

small=no
offline_mingw64=

while [ $# != 0 ]; do
    case "$1" in
        --small)
            small=yes
            shift
            ;;
        --offline-mingw64)
            offline_mingw64="$2"
            shift 2
            ;;
        *)
            echo "Usage: $0 [--small] [--offline-mingw64 path/to/file.zip]"
            echo ""
            echo "  --small                     Use this if you have slow internet"
            echo "  --offline-mingw64 <path>    Use this on systems with no internet"
            exit 2
    esac
done

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
    # The WinLibs version we use ships with LLVM 14, which was latest LLVM that Jou can use when I set this up.
    # This is due to opaque pointer types. Scroll down to "Version Support" here: https://llvm.org/docs/OpaquePointers.html
    # All WinLibs versions and download links: https://winlibs.com/
    if [ $small = yes ]; then
        # User has slow internet and doesn't want to download the whole mingw64 (about 1GB).
        # Instead, download a release of Jou (about 50MB), and extract mingw and Jou compiler from there.
        url=https://github.com/Akuli/jou/releases/download/2025-04-08-1600/jou_windows_64bit_2025-04-08-1600.zip
        filename=jou_windows_64bit_2025-04-08-1600.zip
        sha=4d858bd22f084ae362ee6a22a52c2c5b5281d996f96693984a31336873b92686
        # This is the folder where the downloaded Jou compiler (jou.exe) will go.
        # Placing it here makes bootstrap.sh use our downloaded Jou compiler
        # instead of starting from scratch.
        jou_exe_folder=tmp/bootstrap_cache/016_6f6622072f6b3a321e53619606dcc09a82c8232c
    else
        url=https://github.com/brechtsanders/winlibs_mingw/releases/download/14.2.0posix-19.1.7-12.0.0-msvcrt-r3/winlibs-x86_64-posix-seh-gcc-14.2.0-llvm-19.1.7-mingw-w64msvcrt-12.0.0-r3.zip
        filename=mingw64.zip
        sha=5937a482247bebc2eca8c0b93fa43ddb17d94968adfff3f2e0c63c94608ee76b
    fi

    if [ -z "$offline_mingw64" ]; then
        echo "Downloading $filename..."
        curl -L -o $filename $url
    else
        echo "Copying $offline_mingw64 to ./$filename..."
        cp "$offline_mingw64" "$filename"
    fi

    echo "Verifying $filename..."
    if [ "$(sha256sum $filename | cut -d' ' -f1)" != "$sha" ]; then
        echo "Verifying $filename failed! Please try again or create an issue on GitHub." >&2
        exit 1
    fi

    echo "Extracting $filename..."
    if [ $small = yes ]; then
        mkdir -vp tmp
        mkdir -vp $jou_exe_folder
        unzip -q $filename -d tmp/windows_setup_extracted
        mv tmp/windows_setup_extracted/jou/mingw64 ./
        mv tmp/windows_setup_extracted/jou/libLLVM*.dll mingw64/bin/
        mv tmp/windows_setup_extracted/jou/jou.exe $jou_exe_folder/
        rm -rf tmp/windows_setup_extracted
    else
        unzip -q $filename
    fi
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
# Please keep in sync with compiler/llvm.jou
for name in \
        libLLVMCore libLLVMX86CodeGen libLLVMAnalysis libLLVMTarget \
        libLLVMPasses libLLVMSupport libLLVMLinker libLTO libLLVMX86AsmParser \
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
