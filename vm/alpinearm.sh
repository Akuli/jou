#!/bin/bash
set -e -o pipefail

arch=armv6

mkdir -v "$(dirname "$0")/alpine-$arch"
cd "$(dirname "$0")/alpine-$arch"

# The situation for running ARM on qemu is complicated, and AI tools will
# probably do a better job explaining it than me. But basically, you cannot use
# most images available online, including images made for for raspberry pi 1,
# because qemu doesn't emulate all the required hardware. Instead you need to
# grab a linux kernel and device tree from this random github repo, and use
# them with the file system of whatever distro you actually want to run.
echo "Downloading ARM kernel and device tree made for QEMU..."
commit=9fb4fcf463df4341dbb7396df127374214b90841  # latest commit on master at the time of writing this
../download.sh https://github.com/dhruvvyas90/qemu-rpi-kernel/raw/$commit/kernel-qemu-5.10.63-bullseye 19f348e9fe2b9b7e9330ce2eb4e7f177a71585651080ca9add378a497ebed9ae
../download.sh https://github.com/dhruvvyas90/qemu-rpi-kernel/raw/$commit/versatile-pb-bullseye-5.10.63.dtb 0bc0c0b0858cefd3c32b385c0d66d97142ded29472a496f4f490e42fc7615b25

echo "Downloading alpine linux..."
../download.sh https://dl-cdn.alpinelinux.org/v3.23/releases/armhf/alpine-minirootfs-3.23.2-armhf.tar.gz 80ab58e50bd7fee394ada49f43685a0dde2407cbe5cc714cfe5117f5fc5e9b16

# fakeroot is needed so that ownership info is preserved. For example, if a
# file is actually owned by root, here's what happens to it:
#   - tar thinks it's root, so it extracts file and sets its owner to root
#   - fakeroot sees owner of file as root, so it becomes root-owned in VM disk
#   - during the whole time, the file is actually owned by current user on host
mkdir rootfs
fakeroot bash -c 'set -e; tar xf alpine-minirootfs-3.23.2-armhf.tar.gz -C rootfs; /sbin/mkfs.ext4 -d rootfs alpine.img 2G'

# Start with init=/bin/sh for now, minirootfs is so minimal it doesn't have proper init system
qemu-system-arm \
    -M versatilepb \
    -cpu arm1176 \
    -m 256 \
    -kernel kernel-qemu-5.10.63-bullseye \
    -dtb versatile-pb-bullseye-5.10.63.dtb \
    -drive file=alpine.img,format=raw,if=none,id=disk0 \
    -device virtio-blk-pci,drive=disk0,disable-modern=on,disable-legacy=off \
    -append "root=/dev/vda rw console=ttyAMA0 init=/bin/sh" \
    -serial tcp:localhost:4444,server=on,wait=off \
    -netdev user,id=net0 \
    -net nic,model=smc91c111,netdev=net0 &
qemu_pid=$!

until ../wait_for_string.sh '~ #' nc localhost 4444; do
    sleep 1
    kill -0 $qemu_pid  # Stop if qemu dies
done

bash  # DEBUG
