#!/bin/bash
set -e -o pipefail

if [ $# -ne 1 ]; then
    echo "Usage: $0 <architecture>"
    exit 1
fi

arch=$1
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
    *)
        echo "$0: Unsupported architecture '$arch' (must be x86)"
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

if ! [ -f disk.img ]; then
    # To install alpine, you run the "setup-alpine" program on alpine.
    # So, we first download an intermediate version of alpine to boot from.
    #
    # There are other ways to set up alpine, such as downloading a separate
    # rootfs, but using their install script seems to be easy and reliable.
    echo "Downloading alpine linux..."
    ../download.sh https://dl-cdn.alpinelinux.org/v3.23/releases/$arch/alpine-virt-3.23.2-$arch.iso $sha

    echo "Creating disk..."
    trap 'rm -v disk.img' EXIT  # Avoid leaving behind a broken disk image
    truncate -s 4G disk.img

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
    until echo | ../wait_for_string.sh 'localhost login:' nc.traditional localhost 4444; do
        sleep 1
        kill -0 $qemu_pid  # Stop if qemu dies
    done

    echo "Logging in to temporary VM..."
    echo root | ../wait_for_string.sh 'localhost:~#' nc.traditional localhost 4444

    echo "Installing alpine..."
    echo "
setup-alpine -c answerfile
echo 'DISKOPTS=\"-m sys /dev/$disk_name_inside_vm\"' >> answerfile
echo 'ROOTSSHKEY=\"$(../keygen.sh)\"' >> answerfile
setup-alpine -f answerfile -e
y
sync
poweroff
" | nc.traditional localhost 4444
    wait  # Make sure qemu has died before we proceed further
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
    # Explanations of qemu options:
    #   -m: amount of RAM (linux typically uses all available RAM due to disk caching)
    #   -smp: number of CPUs available inside the VM
    #   -drive: where to boot from
    #   -nic: enable networking so that port 2222 on host is port 22 (ssh) in VM
    $qemu \
        -m 1G \
        -smp $(nproc) \
        -drive file=disk.img,format=raw \
        -nic user,hostfwd=tcp:127.0.0.1:2222-:22 \
        &
    qemu_pid=$!
    echo $qemu_pid > pid.txt
    disown
fi

echo "Waiting for VM to boot..."
until ../ssh.sh echo hello; do
    sleep 1
    kill -0 $qemu_pid  # Stop if qemu dies
done

echo "Checking if repo needs to be copied over..."
if [ "$($ssh 'cd jou && git rev-parse HEAD' || true)" != "$(git rev-parse HEAD)" ]; then
    echo "Installing packages (if not already installed)..."
    $ssh 'which git || apk add bash clang llvm-dev make git grep xz-static'

echo "Checking if repo needs to be copied over..."
if [ "$(../ssh.sh 'cd jou && git rev-parse HEAD' || true)" == "$(git rev-parse HEAD)" ]; then
    echo "Commit $(git rev-parse HEAD) is already checked out in VM."
else
    echo "Copying repository to VM..."
    git bundle create jou.bundle --all
    cat jou.bundle | ../ssh.sh 'cat > jou.bundle'
    rm jou.bundle

    echo "Checking out repository inside VM..."
    ../ssh.sh "
    set -e
    [ -d jou ] || git init jou
    cd jou
    git fetch ../jou.bundle
    git checkout -f $(git rev-parse HEAD)  # The rev-parse runs on host, not inside VM
    "
fi
