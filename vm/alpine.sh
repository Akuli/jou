#!/bin/bash
set -e -o pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <arch> <command inside VM>"
    exit 1
fi

arch=$1
shift
case $arch in
    x86)
        qemu=kvm
        sha=4c6c76a7669c1ec55c6a7e9805b387abdd9bc5df308fd0e4a9c6e6ac028bc1cc
        disk_name_inside_vm=sda
        ;;
    aarch64)
        qemu='qemu-system-aarch64 -machine virt -cpu cortex-a72 -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd'
        sha=ce1e5fb1318365401ce309eb57c39521c11ac087e348ea7f9d3dd2122a58d0c2
        disk_name_inside_vm=vdb
        ;;
    armv6)
        qemu=qemu-system-arm
        # Heavily special-cased below
        ;;
    *)
        echo "$0: Unsupported architecture '$arch' (must be one of: x86, aarch64, armv6)"
        exit 2
esac

if [ "$GITHUB_ACTIONS" = "true" ]; then
    if [ "$qemu" = kvm ]; then
        qemu='sudo kvm'
    fi
    # Don't show any kind of GUI in GitHub Actions
    qemu="$qemu -nographic -monitor none"
fi

mkdir -vp "$(dirname "$0")/alpine-$arch"
cd "$(dirname "$0")/alpine-$arch"

if ! [ -f key ] || ! [ -f key.pub ]; then
    echo "Creating ssh key..."
    (yes || true) | ssh-keygen -t ed25519 -f key -N ''
    rm -vf my_known_hosts
fi

if ! [ -f disk.img ]; then
    echo "Creating disk..."
    trap 'rm -v disk.img' EXIT  # Avoid leaving behind a broken disk image

    if [ $arch == armv6 ]; then
        # Running armv6 on qemu is complicated, and AI will probably explain it
        # better than me. But basically, you cannot use most images available
        # online, including images made for for raspberry pi 1, because qemu
        # doesn't emulate all the required hardware. Instead you need to grab a
        # linux kernel and device tree from this random github repo, and use
        # them with the file system of alpine linux (or other distro).
        echo "Downloading ARM kernel and device tree made for QEMU..."
        commit=9fb4fcf463df4341dbb7396df127374214b90841  # latest commit on master at the time of writing this
        ../download.sh https://github.com/dhruvvyas90/qemu-rpi-kernel/raw/$commit/kernel-qemu-5.10.63-bullseye 19f348e9fe2b9b7e9330ce2eb4e7f177a71585651080ca9add378a497ebed9ae
        ../download.sh https://github.com/dhruvvyas90/qemu-rpi-kernel/raw/$commit/versatile-pb-bullseye-5.10.63.dtb 0bc0c0b0858cefd3c32b385c0d66d97142ded29472a496f4f490e42fc7615b25

        echo "Downloading alpine linux..."
        # Note that version number is also mentioned later in this script!
        # Use Ctrl+F or similar when you update it.
        ../download.sh https://dl-cdn.alpinelinux.org/v3.23/releases/armhf/alpine-minirootfs-3.23.2-armhf.tar.gz 80ab58e50bd7fee394ada49f43685a0dde2407cbe5cc714cfe5117f5fc5e9b16

        # fakeroot is needed so that ownership info is preserved. For example,
        # if a file is actually owned by root, here's what happens to it:
        #   - tar thinks it's root, so it extracts file and sets its owner to root
        #   - mkfs sees owner of file as root, so it becomes root-owned in VM disk
        #   - during the whole time, the file is actually owned by current user on host
        echo "Putting alpine minirootfs and 1GB swap to disk image..."
        rm -rf rootfs
        mkdir rootfs
        fakeroot bash -c 'set -e; tar xf alpine-minirootfs-3.23.2-armhf.tar.gz -C rootfs; head -c 1G /dev/zero > rootfs/swapfile; /sbin/mkfs.ext4 -d rootfs disk.img 3G'

        echo "Booting temporary VM to install alpine..."
        # Start with init=/bin/sh for now, minirootfs is so minimal it doesn't have a proper init system
        #
        # Explanations of VM options:
        #   -M: machine model that the github repo mentioned above happens to support
        #   -cpu: basically the same CPU as in raspberry pi 0 and 1 i guess? TODO: is it? change?
        #   -m 256M: the largest supported memory size with versatilepb
        #   -kernel: load this linux kernel and boot directly from it with no grub or other bootloader
        #   -dtb: device tree file, tells linux kernel what things the computer has
        #   -drive: the / file system
        #   -device virtio-blk-pci: no idea why we need this or what it does
        #   -device virtio-rng-pci: gives the kernel access to random data, so it doesn't get stuck waiting for randomness when it boots
        #   -append: flags for linux kernel, corresponds to "linux" line in grub (in case you're familiar with that)
        #   -serial: make serial console available on host's port 4444
        #   -nic: set up networking so that we emulate a versatilepb-compatible device
        $qemu \
            -M versatilepb \
            -cpu arm1176 \
            -m 256M \
            -kernel kernel-qemu-5.10.63-bullseye \
            -dtb versatile-pb-bullseye-5.10.63.dtb \
            -drive file=disk.img,format=raw,if=none,id=disk0 \
            -device virtio-blk-pci,drive=disk0,disable-modern=on,disable-legacy=off \
            -device virtio-rng-pci \
            -append "root=/dev/vda rw console=ttyAMA0 init=/bin/sh" \
            -serial tcp:localhost:4444,server=on,wait=off \
            -nic user,model=smc91c111 \
            &
        qemu_pid=$!

        echo "Waiting for temporary VM to boot so we can install alpine..."
        until echo | ../wait_for_string.sh '~ #' nc localhost 4444; do
            sleep 1
            kill -0 $qemu_pid  # Stop if qemu dies
        done

        echo "Installing alpine..."
        # Set up networking
        # Install setup-alpine
        # Run setup-alpine and let it setup ssh (a few errors, mostly works)
        echo "
mount -t sysfs sysfs /sys
mount -t proc proc /proc
ip link set dev eth0 up
udhcpc -i eth0
apk update
apk add alpine-base
hostname alpine
setup-alpine -c answerfile
echo 'ROOTSSHKEY=\"$(cat key.pub)\"' >> answerfile
setup-alpine -f answerfile -e
sync  # Hopefully this is enough to get qemu to save changes?
echo ALL'DONE'NOW
" | ../wait_for_string.sh ALLDONENOW nc localhost 4444

        # We don't have a working "poweroff" command yet, because init=/bin/sh is a bit hacky
        kill -INT $qemu_pid

    else
        truncate -s 4G disk.img

        # To install alpine, you run the "setup-alpine" program on alpine.
        # So, we first download an intermediate version of alpine to boot from.
        #
        # There are other ways to set up alpine, such as downloading a separate
        # rootfs, but using their install script seems to be easy and reliable.
        echo "Downloading alpine linux..."
        ../download.sh https://dl-cdn.alpinelinux.org/v3.23/releases/$arch/alpine-virt-3.23.2-$arch.iso $sha

        echo "Booting temporary VM to install alpine..."
        # Explanations of qemu options:
        #   -m: amount of RAM (linux typically uses all available RAM due to disk caching)
        #   -smp: number of CPUs available inside the VM
        #   -drive: where to boot from (cdrom), another disk that will appear inside VM
        #   -nic: enable networking
        #   -serial: make serial console available on host's port 4444
        $qemu \
            -m 1G \
            -smp $(nproc) \
            -drive file=alpine-virt-3.23.2-$arch.iso,format=raw,media=cdrom \
            -drive file=disk.img,format=raw \
            -nic user \
            -serial tcp:localhost:4444,server=on,wait=off \
            &
        qemu_pid=$!

        echo "Waiting for temporary VM to boot so we can install alpine..."
        until echo | ../wait_for_string.sh 'localhost login:' nc localhost 4444; do
            sleep 1
            kill -0 $qemu_pid  # Stop if qemu dies
        done

        echo "Logging in to temporary VM..."
        echo root | ../wait_for_string.sh 'localhost:~#' nc localhost 4444

        echo "Installing alpine..."
        echo "
setup-alpine -c answerfile
echo 'DISKOPTS=\"-m sys /dev/$disk_name_inside_vm\"' >> answerfile
echo 'ROOTSSHKEY=\"$(cat key.pub)\"' >> answerfile
setup-alpine -f answerfile -e
y
sync
poweroff
" | nc localhost 4444
    fi

    echo "Waiting for temporary VM to shut down..."
    wait

    trap - EXIT  # Don't delete disk.img when we exit
fi

if [ "$GITHUB_ACTIONS" != "true" ]; then
    # During local development, let the VM stay alive after this script dies.
    #
    # This is not done for the temporary setup VM, because I don't want to
    # figure out when an interrupted setup can/cannot be completed without
    # starting from scratch.
    qemu="setsid $qemu"
fi

if [ -f pid.txt ] && kill -0 "$(cat pid.txt)"; then
    qemu_pid=$(cat pid.txt)
    echo "qemu is already running (PID $qemu_pid), not restarting."
else
    rm -f pid.txt
    echo "Starting qemu..."
    mkdir -vp shared_folder
    if [ $arch == armv6 ]; then
        # Same as before, but:
        #   - no init=/bin/sh, so we use proper init system (openrc) that setup-alpine installed
        #   - forward port 2222 on host is port 22 (ssh) in VM
        #   - no installation CD
        #   - no serial console on tcp port
        #   - shared_folder is available in the VM with name "share"
        $qemu \
            -M versatilepb \
            -cpu arm1176 \
            -m 256M \
            -kernel kernel-qemu-5.10.63-bullseye \
            -dtb versatile-pb-bullseye-5.10.63.dtb \
            -drive file=disk.img,format=raw,if=none,id=disk0 \
            -device virtio-blk-pci,drive=disk0,disable-modern=on,disable-legacy=off \
            -device virtio-rng-pci \
            -append "root=/dev/vda rw console=ttyAMA0" \
            -nic user,model=smc91c111,hostfwd=tcp:127.0.0.1:2222-:22 \
            -virtfs local,path=shared_folder,mount_tag=share,security_model=none \
            &
        qemu_pid=$!
    else
        # Same as before, but:
        #   - forward port 2222 on host is port 22 (ssh) in VM
        #   - no installation CD
        #   - no serial console on tcp port
        #   - shared_folder is available in the VM with name "share"
        $qemu \
            -m 1G \
            -smp $(nproc) \
            -drive file=disk.img,format=raw \
            -nic user,hostfwd=tcp:127.0.0.1:2222-:22 \
            -virtfs local,path=shared_folder,mount_tag=share,security_model=none \
            &
        qemu_pid=$!
    fi
    echo $qemu_pid > pid.txt
    disown
fi

ssh="ssh root@localhost -o StrictHostKeyChecking=no -o UserKnownHostsFile=my_known_hosts -i key -p 2222"

echo "Waiting for VM to boot..."
until $ssh echo hello; do
    sleep 1
    kill -0 $qemu_pid  # Stop if qemu dies
done

echo "Checking if repo needs to be copied over..."
if [ "$($ssh 'cd jou && git rev-parse HEAD' || true)" != "$(git rev-parse HEAD)" ]; then
    echo "Mounting shared folder if not already mounted..."
    $ssh 'mount | grep shared_folder || (mkdir -vp shared_folder && mount -t 9p share shared_folder)'

    echo "Checking if packages are installed..."
    packages='bash clang llvm-dev make git grep'
    if ! $ssh which git; then
        echo "Installing packages..."
        if [ $arch == armv6 ]; then
            # Internet inside the VM is really slow for some reason. Download the
            # packages on host and transfer them to the VM using a shared folder.
            echo "  Downloading packages (on host computer)..."
            rm -f shared_folder/*.apk
            for package in $($ssh apk fetch --simulate --recursive $packages | sed s/'Downloading '//); do
                echo "    $package"
                url1=https://dl-cdn.alpinelinux.org/alpine/v3.23/main/armhf/$package.apk
                url2=https://dl-cdn.alpinelinux.org/alpine/v3.23/community/armhf/$package.apk
                (
                    cd shared_folder
                    # First try with -q, then without -q to make errors visible
                    wget -q $url1 || wget -q $url2 || wget $url1 || wget $url2
                )
            done
            echo "  Installing packages from shared folder..."
            $ssh 'apk add --network=no /root/shared_folder/*.apk'
        else
            $ssh apk add $packages
        fi
    fi

    echo "Exporting git repository to shared folder..."
    git bundle create -q shared_folder/jou.bundle --all

    echo "Checking out git repository in VM..."
    $ssh "
    set -e
    [ -d jou ] || git init jou
    cd jou
    git fetch ../shared_folder/jou.bundle
    git checkout -f $(git rev-parse HEAD)  # The rev-parse runs on host, not inside VM
    "
fi

echo "Running command in VM's jou folder: $@"
$ssh cd jou '&&' "$@"
