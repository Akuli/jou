#!/bin/bash

# sudo apt install qemu-system-arm

set -e -o pipefail

# Kill all running jobs when exiting
trap 'kill $(jobs -p) &>/dev/null || true' EXIT

function start() {
    rm -vf input
    touch input

    echo "Running qemu..."

    # Without -bios the machine gets stuck with no output.
    # See e.g. https://superuser.com/questions/1684886/qemu-aarch64-on-arm-mac-never-boots-and-only-shows-qemu-prompt
    tail -f input | \
        qemu-system-aarch64 \
        -machine virt \
        -cpu cortex-a72 \
        -m 1G \
        -hda debian-13-nocloud-arm64.qcow2 \
        -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
        -serial stdio \
        -monitor none \
        -nographic \
        > output &

    local pid=$!
    echo "qemu PID: $pid"

    echo $pid > qemu.pid
    disown

    # Display output until it is fully started. The 'tail -f' job gets killed
    # when this shell script exits.
    tail -f output &

    # Wait for grub
    while ! grep -q 'Press enter to boot the selected OS' output; do sleep 1; done

    # Press enter
    printf '\r\n' >> input

    # Wait for prompt to appear
    while ! grep -q 'localhost login:' output; do sleep 1; done

    # Log in as root
    echo root >> input

    # Wait for bash prompt to appear
    while ! grep -q 'root@localhost:~#' output; do sleep 1; done
}

function stop() {
    # This does not need to be perfectly robust. It is intended only to make
    # local development and experimenting less annoying.
    if [ -f qemu.pid ] && grep qemu /proc/$(cat qemu.pid)/cmdline &>/dev/null; then
        local pid=$(cat qemu.pid)
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
    rm -vf qemu.pid
}

if [ $# == 1 ] && [ $1 == start ]; then
    stop
    start
elif [ $# == 1 ] && [ $1 == stop ]; then
    stop
elif [ $# == 2 ] && [ $1 == run ]; then
    echo "Not Impl"
else
    echo "Usage:"
    echo ""
    echo "  $0 start            Start VM"
    echo "  $0 stop             Stop VM"
    echo "  $0 run 'command'    Run command in VM"
    exit 2
fi
