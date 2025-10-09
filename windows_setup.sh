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
Usage: $0 [--small] [--offline-mingw64 path/to/file.zip]

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

if [ -d mingw64 ]; then
    echo "mingw64 has already been downloaded and extracted."
    echo "If you want to download it again, delete the mingw64 folder."
else
    if [ $small = yes ]; then
        # User has slow internet and doesn't want to download the whole mingw64.
        # Instead, download a release of Jou, and extract mingw and Jou compiler from there.
        url=https://github.com/Akuli/jou/releases/download/2025-04-08-2200/jou_windows_64bit_2025-04-08-2200.zip
        filename=jou_windows_64bit_2025-04-08-2200.zip
        sha=2945093e2ef7229729f010e45aac9bbe4635a4ee1289857d6aa0cdfb81b0d24b
    else
        # This is a mingw64 version that comes with LLVM 19.
        url=https://github.com/brechtsanders/winlibs_mingw/releases/download/14.2.0posix-19.1.7-12.0.0-msvcrt-r3/winlibs-x86_64-posix-seh-gcc-14.2.0-llvm-19.1.7-mingw-w64msvcrt-12.0.0-r3.zip
        filename=mingw64.zip
        sha=5937a482247bebc2eca8c0b93fa43ddb17d94968adfff3f2e0c63c94608ee76b
    fi

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
        mv -v tmp/windows_setup_extracted/jou/jou.exe jou_bootstrap.exe
        touch jou_bootstrap.exe  # make sure time stamp is reasonable
        rm -rf tmp/windows_setup_extracted
    else
        unzip -q $filename
    fi
    rm $filename
fi

echo ""

if [ $small = no ]; then
    # We will be bootstrapping this thing ourselves, so we need Python.
    if [ -d bootstrap-python ]; then
        echo "Python has already been downloaded and extracted."
        echo "If you want to download it again, delete the bootstrap-python folder."
    else
        python="$(command -v py python python3 | grep -v Microsoft/WindowsApps | head -1)"
        if [ -n "$python" ] && "$python" --version; then
            echo "Found Python: $python"
            "$python" --version
        else
            url=https://www.python.org/ftp/python/3.9.5/python-3.9.5-embed-amd64.zip
            filename=python-3.9.5-embed-amd64.zip
            sha=f6954b64af18386c523988a23c512452fd289e3591218e7dbb76589b9b326d34

            echo "Downloading $filename..."
            curl -L -o $filename $url

            echo "Verifying $filename..."
            if [ "$(sha256sum $filename | cut -d' ' -f1)" != "$sha" ]; then
                echo "Verifying $filename failed! Please try again or create an issue on GitHub." >&2
                exit 1
            fi

            echo "Extracting $filename..."
            mkdir -v bootstrap-python
            unzip -q $filename -d bootstrap-python
            rm $filename
        fi
    fi
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
