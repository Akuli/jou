#!/bin/bash
set -e

if ! [[ $OS =~ Windows ]]; then
    echo not windows
    exit 1
fi

mkdir -vp downloads

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
	curl -L -o downloads/$filename $url

	if [ "$(sha256sum downloads/$filename | cut -d' ' -f1)" != $checksum ]; then
		echo "Verifying $filename failed." >&2
		exit 1
	fi
}

# 7z is needed to extract other downloads. But it comes in a 7z file.
# 7zr is a command line tool to extract 7z files, and fortunately it comes as a .exe file.
#
# TODO: try to find the 7zr executable from some URL that includes the version number.
#       Eventually this might change and break...
#       Maybe: https://sourceforge.net/projects/sevenzip/files/7-Zip/
download_and_verify "7zr" 5e47d0900fb0ab13059e0642c1fff974c8340c0029decc3ce7470f9aa78869ab https://www.7-zip.org/a/7zr.exe
download_and_verify "7z" fb776489799cd5ca0e151830cf2e6a9819c5c16c8e7571ff706aeeee07da2883 https://www.7-zip.org/a/7z2201-extra.7z
download_and_verify "LLVM" 9d15be034d52ec57cfc97615634099604d88a54761649498daa7405983a7e12f https://github.com/llvm/llvm-project/releases/download/llvmorg-13.0.1/LLVM-13.0.1-win64.exe
download_and_verify "MinGW" 774916c4403c5219f8af3a3ee3012de6c017c034895c2c92bc4de99895c2c924 https://github.com/niXman/mingw-builds-binaries/releases/download/12.2.0-rt_v10-rev1/x86_64-12.2.0-release-win32-seh-rt_v10-rev1.7z

# TODO: use https://winlibs.com/ ?


echo "Cleaning up..."
rm -rf downloads/{7z,7zr,llvm,mingw}/

echo "Extracting 7z..."
mkdir downloads/7z
(cd downloads/7z && ../7zr x ../7z2201-extra.7z >/dev/null)

echo "Extracting LLVM..."
mkdir downloads/llvm
(cd downloads/llvm && ../7z/x64/7za x ../LLVM-13.0.1-win64.exe >/dev/null)


# TODO: Stuck at not finding command-line 7z that would extract NSIS installers (for LLVM)
#       After extracting 7z's own nsis installer from https://sourceforge.net/projects/sevenzip/files/7-Zip/22.01/ it came with 7z.exe that seems to work??? (in wine)
