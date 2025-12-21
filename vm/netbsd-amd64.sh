#!/bin/bash
set -e -o pipefail

if [ $# != 1 ]; then
    echo "Usage: $0 <arch>"
    exit 1
fi

arch=$1
case $arch in
    amd64)
        qemu=qemu-system-x86_64
        sha256=92a40431b2488785172bccf589de2005d03c618e7e2618a6a4dd0465af375bfd
        ;;
    *)
        echo "$0: Unsupported architecture '$arch'"
        exit 2
    fi
esac

mkdir -vp "$(dirname "$0")/netbsd-$arch"
cd "$(dirname "$0")/netbsd-$arch"

# Download NetBSD
if ! [ -f NetBSD-10.1-$arch-live.img ]; then
    ../download.sh https://cdn.netbsd.org/pub/NetBSD/images/10.1/NetBSD-10.1-$arch-live.img.gz $sha256
    gunzip NetBSD-10.1-$arch-live.img.gz
fi

# Make disk image large enough for LLVM and other tools.
#
# When NetBSD boots, it detects that the disk has been resized and adjusts its
# partitions to use the whole disk.
echo "Resizing disk..."
truncate -s 4G NetBSD-10.1-$arch-live.img

if [ -f pid.txt ] && kill -0 "$(cat pid.txt)"; then
    qemu_pid=$(cat pid.txt)
    echo "qemu is already running (PID $qemu_pid), not restarting."
else
    echo "Starting qemu..."
    # Explanations of qemu options:
    #   -m: amount of RAM (linux typically uses all available RAM due to disk caching)
    #   -smp: number of CPUs available inside the VM
    #   -drive: where to boot from
    #   -nic: enable networking so that port 2222 on host is port 22 (ssh) in VM
    #   -monitor tcp:localhost:4444,server,nowait: allow sending keystrokes from localhost to VM
    #
    # setsid is needed so that qemu doesn't die when this shell script is
    # interrupted with Ctrl+C.
    setsid $qemu \
        -m 1G \
        -smp 2 \
        -drive file=NetBSD-10.1-$arch-live.img,format=raw \
        -nic user,hostfwd=tcp:127.0.0.1:2222-:22 \
        -monitor tcp:localhost:4444,server,nowait \
        &
    qemu_pid=$!
    echo $qemu_pid > pid.txt
    disown
    echo "qemu is now running with PID $qemu_pid. The PID was saved to $(pwd)/pid.txt in case you want to kill qemu later."
fi

# I can't get netbsd to output to serial console, so here goes some image
# processing madness.
function wait_for_text() {
    while true; do
        echo "Waiting for '$1' to appear on screen..."

        # Take screenshot
        if ! (echo screendump screenshot.ppm | nc -q1 localhost 4444) >/dev/null; then
            echo "  qemu monitor is not yet ready"
            sleep 1
            continue
        fi

        # Grab last line of text into new image. 4% is one row of a 24 row terminal.
        convert screenshot.ppm -gravity southwest -crop 50%x4%+0+0 screenshot-bottom-left.png

        # Extract text from image and convert it from image to text
        # "--psm 6" apparently means single line of text and makes detection better
        tesseract -l eng --psm 6 screenshot-bottom-left.png screenshot-text
        cat screenshot-text.txt
        if grep "$1" screenshot-text.txt; then
            break
        fi
        sleep 3
    done
}

function press_key() {
    case "$1" in
        [a-z0-9]) echo "sendkey $1" ;;
        [A-Z]) echo "sendkey shift-$(echo $1 | tr A-Z a-z)" ;;
        "/") echo "sendkey slash" ;;
        " ") echo "sendkey spc" ;;
        "@") echo "sendkey shift-2" ;;
        "-") echo "sendkey minus" ;;
        "_") echo "sendkey shift-minus" ;;
        ".") echo "sendkey dot" ;;
        ">") echo "sendkey shift-dot" ;;
        $'\n') echo "sendkey ret" ;;
        *)
            echo "Unsupported character: $ch" >&2
            exit 2
            ;;
    esac | nc -q1 localhost 4444

    # I had some issues with long strings. If I don't send keystrokes
    # separately, it just doesn't work.
    #
    # qemu's help says that it holds keys down 100ms, so this sleep should
    # be enough.
    sleep 0.15
}

function type_on_keyboard() {
    local string="$1"
    for (( i=0; i<${#string}; i++ )); do
        press_key "${string:$i:1}"
    done
}

ssh_flags="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -i key -p 2222"
ssh_works=no
if [ -f key ]; then
    echo "Checking if ssh works..."
    if timeout 5 ssh $ssh_flags root@localhost ls -l; then
        ssh_works=yes
    else
        echo "ssh doesn't work. Let's set it up again."
    fi
fi

if [ $ssh_works = no ]; then
    # Let's do the ssh setup
    wait_for_text 'login:'
    echo "Logging in as root..."
    type_on_keyboard $'root\n'
    wait_for_text 'netbsd#'
    echo "Configuring ssh..."
    (yes || true) | ssh-keygen -t ed25519 -f key -N ''
    type_on_keyboard $'mkdir .ssh\n'
    type_on_keyboard $'chmod 700 .ssh\n'
    type_on_keyboard "echo $(cat key.pub) > .ssh/authorized_keys"$'\n'
fi

echo "Let's try ssh!"
ssh $ssh_flags root@localhost ls -a
