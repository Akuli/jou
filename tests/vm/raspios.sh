#!/bin/bash
#
# This script creates a Raspberry PI OS virtual machine running on armv6. It is
# quite close to raspberry pi 1.

set -e -o pipefail

if [ $# -ne 1 ]; then
    echo "Usage: $0 <architecture>"
    exit 1
fi

arch=$1
case $arch in
    armv6)
        qemu=qemu-system-arm
        ;;
    *)
        echo "$0: Unsupported architecture '$arch' (must be armv6)"
        exit 2
esac

if [ "$GITHUB_ACTIONS" = "true" ]; then
    # Don't show any kind of GUI in GitHub Actions
    qemu="$qemu -nographic -monitor none"
else
    # During local development, let the VM stay alive after this script dies
    qemu="setsid $qemu"
fi

mkdir -vp "$(dirname "$0")/raspios-$arch"
cd "$(dirname "$0")/raspios-$arch"

if ! [ -f disk.img ]; then
    # Running armv6 on qemu is complicated, and AI will probably explain it
    # better than me. But basically, qemu doesn't emulate all the required
    # hardware to boot Raspberry Pi images as is. Instead you need to grab a
    # linux kernel and device tree from this random github repo, and use
    # them with the file system of whatever linux distro you want.
    echo "Downloading ARM kernel and device tree made for QEMU..."
    commit=9fb4fcf463df4341dbb7396df127374214b90841  # latest commit on master at the time of writing this
    ../download.sh https://github.com/dhruvvyas90/qemu-rpi-kernel/raw/$commit/kernel-qemu-5.10.63-bullseye 19f348e9fe2b9b7e9330ce2eb4e7f177a71585651080ca9add378a497ebed9ae
    ../download.sh https://github.com/dhruvvyas90/qemu-rpi-kernel/raw/$commit/versatile-pb-bullseye-5.10.63.dtb 0bc0c0b0858cefd3c32b385c0d66d97142ded29472a496f4f490e42fc7615b25

    echo "Downloading Raspberry Pi OS disk image..."
    ../download.sh https://downloads.raspberrypi.com/raspios_lite_armhf/images/raspios_lite_armhf-2025-12-04/2025-12-04-raspios-trixie-armhf-lite.img.xz 1b3e49b67b15050a9f20a60267c145e6d468dc9559dd9cd945130a11401a49ff

    echo "Extracting disk image..."
    xz -d 2025-12-04-raspios-trixie-armhf-lite.img.xz
    mv -v 2025-12-04-raspios-trixie-armhf-lite.img disk.img
    trap 'rm -v disk.img' EXIT  # Avoid leaving behind a broken disk image

    # We separate the main partition into a file for two reasons:
    #   - adding files to it (we could mount it, but then we need sudo)
    #   - resizing
    echo "Taking main partition from disk image..."
    # These are in sectors (512-byte blocks), not bytes, so that we don't need
    # to make dd go one byte at a time. That would be ridiculously slow.
    offset=$(/sbin/parted --json disk.img unit s print | jq -r '.disk.partitions[1].start' | tr -d s)
    size=$(/sbin/parted --json disk.img unit s print | jq -r '.disk.partitions[1].size' | tr -d s)
    echo "  $size blocks at $offset"
    dd if=disk.img of=partition.img bs=512 skip=$offset count=$size

    echo "Resizing disk..."
    # Three things have to happen to get more storage:
    #   1. Disk or disk image gets bigger (e.g. user wrote the image to a huge sd card)
    #   2. Partition table must be updated so that second (main) partition fills rest of the disk
    #   3. File system on the partition must be resized to fill the entire partition
    #
    # Usually Raspberry Pi OS would do steps 2 and 3 automatically, but it
    # doesn't work, because we boot it in a weird/crude way that skips a bunch
    # of things.

    # Step 1
    truncate -s +3G disk.img
    truncate -s +3G partition.img

    # Step 2
    /sbin/parted --script disk.img resizepart 2 100%

    # Step 3
    /sbin/resize2fs partition.img

    echo "Adding ssh key..."
    # The user 'pi' used to have a default password 'raspberry', but that is no
    # longer true. Instead the password is disabled, so you cannot log in.
    #
    # The official thing to do is to add a userconf.txt file. But if we're
    # going to add a file, we might as well add the ssh key directly, since we
    # want to use ssh anyway.
    #
    # We set up ssh as root user to be consistent with other VMs.
    #
    # The mode specified in debugfs is not only permission bits but also the
    # inode type:
    #
    #   directory    = 0x4000 = octal  40000
    #   regular file = 0x8000 = octal 100000
    #
    # Extra 0 in beginning is needed for octal in debugfs (just like in C).
    #
    # If you don't specify the inode type, it becomes zero and you get some
    # "interesting" results...
    #
    #    akuli@Akuli-Desktop ~/jou/vm/raspios-armv6 $ sudo mount partition.img /mnt/
    #    akuli@Akuli-Desktop ~/jou/vm/raspios-armv6 $ sudo ls -l /mnt/root/.ssh
    #    ls: cannot access '/mnt/root/.ssh/authorized_keys': Structure needs cleaning
    #    total 0
    #    -????????? ? ? ? ?            ? authorized_keys
    #    akuli@Akuli-Desktop ~/jou/vm/raspios-armv6 $
    ../keygen.sh > key.pub
    echo '
write key.pub /root/.ssh/authorized_keys
set_inode_field /root/.ssh/authorized_keys mode 0100600' | /sbin/debugfs -w partition.img
    rm key.pub

    echo "Adding 1GB swap file..."
    # I tried filling the file initially with zero bytes, but the zeros got
    # special-cased somewhere:
    #
    #    ~ # swapon /swapfile
    #    swapon: /swapfile: file has holes
    #
    head -c 1G /dev/urandom > swapfile
    /sbin/mkswap swapfile
    echo '
write swapfile /swapfile
set_inode_field /swapfile mode 0100600
dump /etc/fstab fstab' | /sbin/debugfs -w partition.img
    echo '/swapfile none swap sw 0 0' >> fstab
    echo '
rm /etc/fstab
write fstab /etc/fstab' | /sbin/debugfs -w partition.img
    rm swapfile
    rm fstab

    echo "Writing main partition back to disk image..."
    dd if=partition.img of=disk.img bs=512 seek=$offset conv=notrunc
    rm partition.img
    trap - EXIT  # Don't delete disk.img when we exit
fi

if [ -f pid.txt ] && kill -0 "$(cat pid.txt)"; then
    qemu_pid=$(cat pid.txt)
    echo "qemu is already running (PID $qemu_pid), not restarting."
else
    rm -f pid.txt
    echo "Starting qemu..."
    mkdir -vp shared_folder
    # Start with init=/bin/sh for now, minirootfs is so minimal it doesn't have a proper init system
    #
    # Explanations of VM options:
    #   -M: machine model that the github repo mentioned above happens to support
    #   -cpu: basically the same CPU as in raspberry pi 0 and 1 i guess? TODO: is it? change?
    #   -m 256M: the largest supported memory size with versatilepb
    #   -kernel: load this linux kernel and boot directly from it with no grub or other bootloader
    #   -dtb: device tree file, tells linux kernel what things the computer has
    #   -drive: hard drive / SD card
    #   -device virtio-blk-pci: no idea why we need this or what it does
    #   -device virtio-rng-pci: gives the kernel access to random data, so it doesn't get stuck waiting for randomness when it boots
    #   -append: flags for linux kernel, corresponds to "linux" line in grub (in case you're familiar with that) or /boot/firmware/cmdline.txt
    #   -serial: make serial console available on host's port 4444
    #   -nic: set up networking so that we emulate a versatilepb-compatible device and port 2222 on host is port 22 (ssh) in VM
    #   -virtfs: shared_folder is available in the VM with name "share"
    $qemu \
        -M versatilepb \
        -cpu arm1176 \
        -m 256M \
        -kernel kernel-qemu-5.10.63-bullseye \
        -dtb versatile-pb-bullseye-5.10.63.dtb \
        -drive file=disk.img,format=raw,if=none,id=disk0 \
        -device virtio-blk-pci,drive=disk0,disable-modern=on,disable-legacy=off \
        -device virtio-rng-pci \
        -append "root=/dev/vda2 rw console=ttyAMA0 resize" \
        -serial tcp:localhost:4444,server=on,wait=off \
        -nic user,model=smc91c111,hostfwd=tcp:127.0.0.1:2222-:22 \
        -virtfs local,path=shared_folder,mount_tag=share,security_model=none \
        &
    qemu_pid=$!
    echo $qemu_pid > pid.txt
    disown

    echo "Waiting for VM to boot..."
    until echo | ../wait_for_string.sh 'raspberrypi login:' ncat --no-shutdown localhost 4444; do
        sleep 1
        kill -0 $qemu_pid  # Stop if qemu dies
    done
fi

echo "Waiting for VM to boot..."
until ../ssh.sh echo hello; do
    sleep 1
    kill -0 $qemu_pid  # Stop if qemu dies
done

echo "Mounting shared folder if not already mounted..."
../ssh.sh 'mount | grep shared_folder || (mkdir -vp shared_folder && mount -t 9p share shared_folder)'

echo "Checking if packages are installed..."
packages='git llvm-19-dev clang-19 make'
if ! ../ssh.sh which git; then
    # Internet inside the VM is really slow for some reason, so even
    # "apt update" takes a very long time (about 8 minutes).
    #
    # To work around this, we ask apt in the VM what files it would like
    # to download, put them to the right places through our shared folder,
    # and then run "apt update".
    echo "Downloading 'apt update' files into shared folder..."
    rm -rf shared_folder/apt_update  # Prevents a bunch of corner cases we don't care about
    mkdir -vp shared_folder/apt_update
    ../ssh.sh apt-get update --print-uris | tr -d "'" | while read -r url filename ignore_the_rest; do
        echo "$url"
        (
            cd shared_folder/apt_update
            if wget -q -O "$filename" "$url"; then
                if [[ "$url" == *.xz ]]; then
                    # File is xz compressed
                    mv "$filename" "$filename.xz"
                    xz -d "$filename.xz"
                fi
                echo "--> ok"
            else
                # Some of the URLs return 404, seems to be intentional
                rm -vf "$filename"
                echo "--> not working but probably doesn't matter"
            fi
        )
    done

    echo "Doing 'apt update' with files from shared folder..."
    ../ssh.sh "cp -v shared_folder/apt_update/* /var/lib/apt/lists/ && apt update"

    # Download the packages on host and transfer them to the VM using a
    # shared folder.
    echo "Downloading packages into shared folder..."
    mkdir -vp shared_folder/apt_install
    ../ssh.sh apt-get install --print-uris -y $packages | grep "^'http" | tr -d "'" | while read -r url filename ignore_the_rest; do
        echo "  $filename"
        wget -q --continue -O "shared_folder/apt_install/$filename" "$url"
    done

    echo "Installing packages from shared folder..."
    ../ssh.sh "cp -v shared_folder/apt_install/*.deb /var/cache/apt/archives/ && apt install -y $packages"
fi

echo "Checking if repo needs to be copied over..."
if [ "$(../ssh.sh 'cd jou && git rev-parse HEAD' || true)" != "$(git rev-parse HEAD)" ]; then
    echo "Exporting git repository to shared folder..."
    git bundle create -q shared_folder/jou.bundle --all

    echo "Checking out git repository in VM..."
    ../ssh.sh "
    set -e
    [ -d jou ] || git init jou
    cd jou
    git fetch ../shared_folder/jou.bundle
    git checkout -f $(git rev-parse HEAD)  # The rev-parse runs on host, not inside VM
    "
fi
