#!/usr/bin/env bash

# Run this script in Git Bash to get started with developing Jou on Windows.
# It downloads everything you need (except a text editor).

set -e

if ! [[ "$OS" =~ Windows ]]; then
    echo "This script must be ran on Windows." >&2
    exit 1
fi

small=no
offline_zip=

while [ $# != 0 ]; do
    case "$1" in
        --small)
            small=yes
            shift
            ;;
        --offline-zip)
            offline_zip="$2"
            shift 2
            ;;
        *)
            echo "\
Usage: $0 [--small] [--offline-zip path/to/file.zip]

    --small
        Use this if you have slow internet. See CONTRIBUTING.md for a detailed
        explanation about what this does.

    --offline-zip path/to/file.zip
        Usually this script downloads one zip file, but if this option is used,
        nothing will be downloaded from internet. The file is instead copied
        from the given path. This can be used to set up a Jou dev environment
        on a system with no internet connection.
"
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
    if [ $small = yes ]; then
        # User has slow internet and doesn't want to download the whole mingw64.
        # Instead, download a release of Jou, and extract mingw and Jou compiler from there.
        url=https://github.com/Akuli/jou/releases/download/2025-12-23-0400/jou_windows_64bit_2025-12-23-0400.zip
        filename=jou_windows_64bit_2025-12-23-0400.zip
        sha=2dcfe26a7bb127f436f0df193387a1ecaaf7a83b15833928810ae8ebe0633cd4
        # This is the folder where the downloaded Jou compiler (jou.exe) will go.
        # Placing it here makes bootstrap.sh use our downloaded Jou compiler
        # instead of starting from scratch.
        jou_exe_folder=tmp/bootstrap_cache/026_eeb1a89b82c9bdee5c8942604b3f8b2b9a2e786d
    else
        # This is a mingw64 version that comes with LLVM 19.
        url=https://github.com/brechtsanders/winlibs_mingw/releases/download/14.2.0posix-19.1.7-12.0.0-msvcrt-r3/winlibs-x86_64-posix-seh-gcc-14.2.0-llvm-19.1.7-mingw-w64msvcrt-12.0.0-r3.zip
        filename=mingw64.zip
        sha=5937a482247bebc2eca8c0b93fa43ddb17d94968adfff3f2e0c63c94608ee76b
    fi

    # TODO: don't download if file exists, just verify and tell user to delete if that fails
    if [ -z "$offline_zip" ]; then
        echo "Downloading $filename..."
        curl -L -o $filename $url
    else
        echo "Copying $offline_zip to ./$filename..."
        cp "$offline_zip" "$filename"
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
        mv -v tmp/windows_setup_extracted/jou/mingw64 ./
        mv -v tmp/windows_setup_extracted/jou/*.dll mingw64/bin/  # will be in PATH while developing
        mv -v tmp/windows_setup_extracted/jou/jou.exe $jou_exe_folder/
        rm -rf tmp/windows_setup_extracted
    else
        unzip -q $filename
    fi
    rm $filename
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
