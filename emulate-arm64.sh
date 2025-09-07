#!/bin/bash
#
# This script uses qemu to emulate a 64-bit ARM processor. It it used to test
# Jou on 64-bit ARM. This is support mainly for using Jou on a raspberry pi.

set -e -o pipefail


function cd_temp_folder() {
    mkdir -vp tmp/qemu
    cd tmp/qemu
}

function download() {
    if ! [ -f disk.qcow2 ]; then
        # Download in a way that handles interrupting. Useful if using slow internet.
        wget --continue -O disk-download.qcow2 https://cloud.debian.org/images/cloud/trixie/20250814-2204/debian-13-nocloud-arm64-20250814-2204.qcow2
        if [ "$(sha256sum disk-download.qcow2 | cut -d' ' -f1)" != "bda283e7c7037220897e201a5387773e48f688d33795cccd84d20db01dbffcbc" ]; then
            echo "Error: Download checksum failed!" >&2
            exit 1
        fi
        mv disk-download.qcow2 disk.qcow2
    fi
}

function start() {
    rm -vf input output
    touch input output

    echo "Running qemu..."

    # Without -bios the machine gets stuck with no output.
    # See e.g. https://superuser.com/questions/1684886/qemu-aarch64-on-arm-mac-never-boots-and-only-shows-qemu-prompt
    tail -f input | \
        qemu-system-aarch64 \
        -machine virt \
        -cpu cortex-a72 \
        -smp 2 \
        -m 1G \
        -hda disk.qcow2 \
        -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
        -serial stdio \
        -monitor none \
        -nographic \
        > output &

    local pid=$!
    echo "qemu PID: $pid"

    echo $pid > pid.txt
    disown

    # Display output until it is fully started. The 'tail -f' job gets killed
    # when this shell script exits.
    #
    # This also filters away screen clearing and cursor moving ANSI escape sequences.
    tail -f output | sed -r 's/\x1b\[[0-9;]*[HJf]//g' &
    tailpid=$!

    # When exiting, kill tail and reset terminal colors
    trap "printf '\x1b[0m'; kill $tailpid" EXIT

    # Wait for grub
    while ! grep -q 'Press enter to boot the selected OS' output; do sleep 1; done

    # Press enter in grub menu
    echo >> input

    # Wait for login prompt to appear
    while ! grep -q 'localhost login:' output; do sleep 1; done

    # Log in as root
    echo root >> input

    # Wait for bash prompt to appear
    while ! grep -q 'root@localhost:~#' output; do sleep 1; done
}

function stop() {
    # This does not need to be perfectly robust. It is intended only to make
    # local development and experimenting less annoying.
    if [ -f pid.txt ] && grep qemu /proc/$(cat pid.txt)/cmdline &>/dev/null; then
        local pid=$(cat pid.txt)
        kill $pid

        # Wait for it to die, max 1 second
        timeout 1 bash -c "while [ -d /proc/$pid ]; do sleep 0.1; done"

        if [ -d /proc/$pid ]; then
            echo "Error: Could not stop old qemu process (PID $pid)" >&2
            exit 1
        fi
        echo "Stopped old qemu process (pid $pid)."
    else
        echo "Currently qemu is not running."
    fi
    rm -vf pid.txt
}

function run_command() {
    local command="$1"

    local id=$(head -c 12 /dev/urandom | base64 | tr -cd '[:alnum:]')

    (
        # Wait for tail to be ready below
        sleep 0.1

        echo "$command ; ret=\$?; echo -n \"=== End running command $id \$ret ===\"" >> input

        while ! grep -q "=== End running command $id .* " output; do
            sleep 0.1
        done

        # Make sure the tail below shows all output
        sleep 0.1
    ) &

    # Display output until it is done
    tail -s 0.1 -c 0 -f output --pid=$! | sed "s/=== End running command $id .*//" | sed 1d

    exit $(grep -o "=== End running command $id [0-9]* " output | cut -d' ' -f6)
}

if [ $# == 1 ] && [ $1 == start ]; then
    cd_temp_folder
    download
    stop
    start
elif [ $# == 1 ] && [ $1 == stop ]; then
    cd_temp_folder
    stop
elif [ $# == 2 ] && [ $1 == run ]; then
    cd_temp_folder
    run_command "$2"
else
    echo "Usage:"
    echo ""
    echo "  $0 start            Start VM"
    echo "  $0 stop             Stop VM"
    echo "  $0 run 'command'    Run command in VM"
    exit 2
fi
