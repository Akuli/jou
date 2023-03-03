#!/bin/bash
set -e

#echo "Cleaning up..."
#rm -rf mingw64 *.o *.s *.a *.def

function download_and_verify() {
    local name=$1
    local checksum=$2
    local url=$3
    local filename=$(basename $url)

    if [ -f downloads/$filename ] && [ "$(sha256sum downloads/$filename |cut -d' ' -f1)" == $checksum ]; then
        echo "$name is already downloaded."
        return
    fi

    echo "Downloading $name"
    mkdir -vp downloads
    curl -L -o downloads/$filename $url

    if [ "$(sha256sum downloads/$filename | cut -d' ' -f1)" != $checksum ]; then
        echo "Verifying $filename failed." >&2
        exit 1
    fi
}

# Keep the size in the command below up to date if you update WinLibs:
if [ -d mingw64 ] && [ $(du -s mingw64 | cut -f1) == 989488 ]; then
    echo "mingw64 has already been downloaded and extracted."
else
    # The WinLibs version we use ships with LLVM 14, which is latest LLVM that Jou can use.
    # This is due to opaque pointer types. Scroll down to "Version Support" here: https://llvm.org/docs/OpaquePointers.html
    download_and_verify "7zr" 5e47d0900fb0ab13059e0642c1fff974c8340c0029decc3ce7470f9aa78869ab https://www.7-zip.org/a/7zr.exe
    download_and_verify "WinLibs" c992da83bbda4c5ed5a384aa44c18d1e848f1ef615c6e5000dba628c14290682 https://github.com/brechtsanders/winlibs_mingw/releases/download/12.1.0-14.0.6-10.0.0-msvcrt-r3/winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3.7z

    echo "Extracting mingw64 from WinLibs..."
    rm -rf mingw64
    wine downloads/7zr.exe x downloads/winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3.7z
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
    rm -vf libLLVMCore.def  # in case it was left behind by a previous, interrupted run
    wine mingw64/bin/gendef.exe mingw64/bin/libLLVMCore.dll
    wine mingw64/bin/dlltool.exe -d libLLVMCore.def -l libLLVMCore.a
    rm -f libLLVMCore.def
fi



#echo "Extracting LLVM..."
#mkdir downloads/llvm
#(cd downloads/llvm && ../7z/x64/7za x ../LLVM-13.0.1-win64.exe >/dev/null)


# TODO: Stuck at not finding command-line 7z that would extract NSIS installers (for LLVM)
#       After extracting 7z's own nsis installer from https://sourceforge.net/projects/sevenzip/files/7-Zip/22.01/ it came with 7z.exe that seems to work??? (in wine)


# standalone make https://sourceforge.net/projects/gnuwin32/files/make/3.81/
