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
        ;;
    *)
        echo "$0: Unsupported architecture '$arch' (must be x86)"
        exit 2
esac

if [ "$GITHUB_ACTIONS" = "true" ]; then
    if [ $qemu = kvm ]; then
        qemu='sudo kvm'
    fi
    # Don't show any kind of GUI in GitHub Actions
    qemu="$qemu -nographic -monitor none"
else
    # During local development, let the VM stay alive after this script dies
    qemu="setsid $qemu"
fi

mkdir -vp "$(dirname "$0")/alpine-$arch"
cd "$(dirname "$0")/alpine-$arch"

# rootfs is where Jou will be stored. It is not where we boot from: we need two
# separate disk images, because the bootable alpine image is too small for
# installing Jou's dependencies.
if ! [ -f rootfs-$arch.img ]; then
    echo "Creating rootfs disk image... (rootfs-$arch.img)"
#    ../download.sh https://dl-cdn.alpinelinux.org/v3.23/releases/$arch/alpine-minirootfs-3.23.2-$arch.tar.gz 99457a300f13d1971d4627a0ccdf4ae17db18807804e3b5867d4edf8afa16d23
#    mkdir rootfs
#    tar xf alpine-minirootfs-3.23.2-$arch.tar.gz -C rootfs
#    rm alpine-minirootfs-3.23.2-$arch.tar.gz
    truncate -s 4G rootfs-$arch.img
#    /sbin/mkfs.ext4 -q rootfs-$arch.img -d rootfs
#    rm -rf rootfs
fi

# The "virt" image is where we actually boot from.
if ! [ -f virt-$arch.img ]; then
    echo "Downloading boot disk... (virt-$arch.img)"
    ../download.sh https://dl-cdn.alpinelinux.org/v3.23/releases/$arch/alpine-virt-3.23.2-$arch.iso 4c6c76a7669c1ec55c6a7e9805b387abdd9bc5df308fd0e4a9c6e6ac028bc1cc
    mv -v alpine-virt-3.23.2-$arch.iso virt-$arch.img
fi

if [ -f pid.txt ] && kill -0 "$(cat pid.txt)"; then
    qemu_pid=$(cat pid.txt)
    echo "qemu is already running (PID $qemu_pid), not restarting."
else
    rm -f pid.txt
    echo "Starting qemu..."
    # Explanations of qemu options:
    #   -m: amount of RAM (linux typically uses all available RAM due to disk caching)
    #   -smp: number of CPUs available inside the VM
    #   -drive: where to boot from (cdrom), another disk that will appear inside VM
    #   -nic: enable networking so that port 2222 on host is port 22 (ssh) in VM
    #   -serial: make serial console available on host's port 4444, used to configure ssh
    $qemu \
        -m 1G \
        -smp $(nproc) \
        -drive file=virt-$arch.img,format=raw,media=cdrom \
        -drive file=rootfs-$arch.img,format=raw \
        -nic user,hostfwd=tcp:127.0.0.1:2222-:22 \
        -serial tcp:localhost:4444,server=on,wait=off \
        &
    qemu_pid=$!
    echo $qemu_pid > pid.txt
    disown
    sleep 1
    kill -0 $qemu_pid  # stop if qemu died instantly
fi

#   9 setup-alpine -c x
#  20 sed -i /DISKOPTS=none/d x
#  21 echo 'DISKOPTS="-m sys /dev/sda"' >> x
#  26 setup-alpine -f x
#       y
#  27 lsblk
#  28 mount /dev/sda2 /mnt
#  29 mount /dev/sda3 /mnt
#  30 ls /mnt
#  31 chroot /mnt
#  32 history
#  33 history >h

exit

ssh="ssh root@localhost -o StrictHostKeyChecking=no -o UserKnownHostsFile=my_known_hosts -i key -p 2222"

echo "Checking if ssh works..."
if ! [ -f key ] || ! timeout 5 $ssh echo hello; then
    # ssh doesn't work either because we didn't set it up yet or we need to
    # wait for the VM to start.
    #
    # We consider the VM started when it shows login prompt on serial port.
    # At that point it has also started ssh.
    echo "Waiting for VM to boot..."
    until echo | ../wait_for_string.sh 'localhost login:' nc localhost 4444; do sleep 1; done
    echo "Checking again if ssh works..."
    if ! [ -f key ] || ! timeout 5 $ssh echo hello; then
        echo "ssh doesn't work. Let's set it up using serial port."
        (yes || true) | ssh-keygen -t ed25519 -f key -N ''
        # Log in as root
        echo root | ../wait_for_string.sh 'localhost:~#' nc localhost 4444
        # Go to our rootfs disk with chroot and set up ssh
        echo "
mount -t ext4 /dev/sda /mnt
cd /mnt
mount -t proc /proc proc/
mount -t sysfs /sys sys/
mount --rbind /dev dev/
mount --rbind /run run/
chroot .
echo 'iface eth0 inet dhcp' > /etc/network/interfaces
ifup eth0
apk update
apk add openssh-server
ssh-keygen -A
chown root:root /var/empty  # don't know why sshd wants this
/usr/sbin/sshd
mkdir ~/.ssh
echo '$(cat key.pub)' > ~/.ssh/authorized_keys
chown root:root ~
chmod 700 ~/.ssh
chmod 600 ~/.ssh/authorized_keys
exit
echo ALL'DONE'NOW
exit
" | ../wait_for_string.sh ALLDONENOW nc localhost 4444
        echo "Now ssh setup is done, let's check one last time..."
        $ssh echo hello  # Check that it works
    fi
fi

echo "Checking if repo needs to be copied over..."
if [ "$($ssh 'cd jou && git rev-parse HEAD' || true)" != "$(git rev-parse HEAD)" ]; then
    echo "Installing packages (if not already installed)..."
    $ssh 'which git || apk add bash clang llvm-dev make git grep libx11-dev'

    echo "Copying repository to VM..."
    git bundle create jou.bundle --all
    cat jou.bundle | $ssh 'cat > jou.bundle'  # easier and faster than using scp here
    rm jou.bundle

    echo "Checking out repository inside VM..."
    $ssh "
    set -e
    [ -d jou ] || git init jou
    cd jou
    git fetch ../jou.bundle
    git checkout -f $(git rev-parse HEAD)  # The rev-parse runs on host, not inside VM
    "
fi

echo "Running command in VM's jou folder: $@"
$ssh cd jou '&&' "$@"
