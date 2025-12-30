#!/bin/bash
#
# This script creates a Raspberry PI OS virtual machine running on armv6. It is
# quite close to raspberry pi 1.

set -e -o pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <arch> <command inside VM>"
    exit 1
fi

arch=$1
shift
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

if ! [ -f key ] || ! [ -f key.pub ]; then
    echo "Creating ssh key..."
    (yes || true) | ssh-keygen -t ed25519 -f key -N ''
    rm -vf my_known_hosts
fi

if ! [ -f disk.img ]; then
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

    echo "Downloading Raspberry Pi OS disk image..."
    ../download.sh https://downloads.raspberrypi.com/raspios_lite_armhf/images/raspios_lite_armhf-2025-12-04/2025-12-04-raspios-trixie-armhf-lite.img.xz 1b3e49b67b15050a9f20a60267c145e6d468dc9559dd9cd945130a11401a49ff

    echo "Extracting disk image..."
    xz -d 2025-12-04-raspios-trixie-armhf-lite.img.xz
    mv -v 2025-12-04-raspios-trixie-armhf-lite.img disk.img

    # The user 'pi' used to have a default password 'raspberry', but that is no
    # longer true. Instead the password is disabled, so you cannot log in.
    #
    # The official thing to do is to create /boot/userconf.txt. Note that even
    # though there are two partitions, /boot is still on the second (main)
    # partition, because the first partition is just /boot/firmware:
    #
    #    pi@raspberrypi:~ $ lsblk
    #    NAME      MAJ:MIN RM  SIZE RO TYPE MOUNTPOINTS
    #    loop0       7:0    0   78M  0 loop 
    #    sr0        11:0    1 1024M  0 rom  
    #    mtdblock0  31:0    0   64M  0 disk 
    #    vda       254:0    0  2.5G  0 disk 
    #    ├─vda1    254:1    0  512M  0 part /boot/firmware
    #    └─vda2    254:2    0    2G  0 part /
    #
    # So adding userconf.txt would be quite similar to adding userconf.txt.

    echo "Taking main partition from disk image..."
    # These are in sectors (512-byte blocks), not bytes, so that we don't need
    # to make dd go one byte at a time. That would be ridiculously slow.
    offset=$(/sbin/parted --json disk.img unit s print | jq -r '.disk.partitions[1].start' | tr -d s)
    size=$(/sbin/parted --json disk.img unit s print | jq -r '.disk.partitions[1].size' | tr -d s)
    echo "  $size blocks at $offset"
    dd if=disk.img of=partition.img bs=512 skip=$offset count=$size

    echo "Adding ssh key to home folder of the 'pi' user..."
    # The mode specified in debugfs is  not only permission bits but also the
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
    #    akuli@Akuli-Desktop ~/jou/vm/raspios-armv6 $ ls -la /mnt/home/pi
    #    ls: cannot access '/mnt/home/pi/.ssh': Structure needs cleaning
    #    total 20
    #    drwx------ 3 akuli akuli 4096 Dec  4 16:41 .
    #    drwxr-xr-x 3 root  root  4096 Dec  4 16:41 ..
    #    -rw-r--r-- 1 akuli akuli  220 Dec  4 16:41 .bash_logout
    #    -rw-r--r-- 1 akuli akuli 3523 Dec  4 16:41 .bashrc
    #    -rw-r--r-- 1 akuli akuli  807 Dec  4 16:41 .profile
    #    d????????? ? ?     ?        ?            ? .ssh
    #    akuli@Akuli-Desktop ~/jou/vm/raspios-armv6 $
    #
    # (File owner appears as "akuli" because on this system, user ID of my
    # "akuli" user is 1000, and user ID of the "pi" user is also 1000.)
    echo '
cd /home/pi
mkdir .ssh
write key.pub .ssh/authorized_keys

set_inode_field .ssh mode 040700
set_inode_field .ssh uid 1000
set_inode_field .ssh gid 1000

set_inode_field .ssh/authorized_keys mode 0100600
set_inode_field .ssh/authorized_keys uid 1000
set_inode_field .ssh/authorized_keys gid 1000

quit' | /sbin/debugfs -w partition.img

    echo "Writing main partition back to disk image..."
    dd if=partition.img of=disk.img bs=512 seek=$offset conv=notrunc
    rm -f partition.img

    echo "Making disk image bigger..."
    # Three things have to happen:
    #   1. Disk or disk image gets bigger (e.g. user wrote the image to a huge sd card)
    #   2. Second (main) partition must be resized to fill the rest of the disk
    #   3. File system on the partition must be resized to fill the entire partition
    #
    # Usually Raspberry Pi OS would do steps 2 and 3. Here it can do only step 3,
    # because we boot it in a weird/crude way that skips a bunch of things.
    truncate -s +2G disk.img  # Step 1
    /sbin/parted --script disk.img resizepart 2 100%  # Step 2
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
    until echo | ../wait_for_string.sh 'raspberrypi login:' nc localhost 4444; do
        sleep 1
        kill -0 $qemu_pid  # Stop if qemu dies
    done
fi

ssh="ssh pi@localhost -o StrictHostKeyChecking=no -o UserKnownHostsFile=my_known_hosts -i key -p 2222"

echo "Waiting for VM to boot..."
until $ssh echo hello; do
    sleep 1
    kill -0 $qemu_pid  # Stop if qemu dies
done

echo "DEBUG STOP!!"
exit

echo "Checking if repo needs to be copied over..."
if [ "$($ssh 'cd jou && git rev-parse HEAD' || true)" != "$(git rev-parse HEAD)" ]; then
    echo "Mounting shared folder if not already mounted..."
    $ssh 'mount | grep shared_folder || (mkdir -vp shared_folder && mount -t 9p share shared_folder)'

    echo "Checking if packages are installed..."
    packages='bash clang llvm-dev make git grep'
    if ! $ssh which git; then
        echo "Getting ready to install packages..."
        # Internet inside the VM is really slow for some reason, so even
        # `apt update` takes a very long time (about 10 minutes).
        $ssh sudo apt update

        # Download the packages on host and transfer them to the VM using a
        # shared folder.
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
