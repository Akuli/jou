#!/bin/bash
#
# This script uses qemu to emulate a 64-bit ARM processor. It it used to test
# Jou on 64-bit ARM. This is support mainly for using Jou on a raspberry pi.

set -e -o pipefail

mkdir -vp tmp/qemu
cd tmp/qemu

# Download in a way that handles interrupting. Useful if using slow internet.
wget --continue -O disk-download.qcow2 https://cloud.debian.org/images/cloud/trixie/20250814-2204/debian-13-nocloud-arm64-20250814-2204.qcow2
echo 'bda283e7c7037220897e201a5387773e48f688d33795cccd84d20db01dbffcbc  disk-download.qcow2' | sha256sum --check

cp -v disk-download.qcow2 disk.qcow2

rm -vf input output
touch input output

echo "Running qemu..."

# Without -bios the machine gets stuck with no output.
# See e.g. https://superuser.com/questions/1684886/qemu-aarch64-on-arm-mac-never-boots-and-only-shows-qemu-prompt
#
# Networking (-nic) took a lot of trial and error and searching to get
# right. Almost everything I tried broke internet access from inside the VM.
# You should also listen only on localhost, not 0.0.0.0 (default).
#
# TODO: is that really true or was i dumb?...
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
    -nic user,model=e1000,hostfwd=tcp:127.0.0.1:2222-:22 \
    -nographic \
    > output &

qemu_pid=$!
echo "
*********************************************************************
qemu is now running with PID $qemu_pid.
If something doesn't work, use the following commands to debug:

    \$ tail -f tmp/qemu/output      # Show VM console
    \$ echo ls >> tmp/qemu/input    # Run command in VM

*********************************************************************
"

# When exiting, kill the processes and reset terminal colors
trap "printf '\x1b[0m'; kill $qemu_pid $tail_pid" EXIT

echo "Waiting for grub to show up..."
while ! grep -q 'Press enter to boot the selected OS' output; do sleep 1; done

echo "Pressing Enter in grub menu..."
echo >> input

echo "Waiting for login prompt. This will take a while..."
while ! grep -q 'localhost login:' output; do sleep 1; done

echo "Logging in..."
echo root >> input

echo "Waiting for bash prompt to appear..."
while ! grep -q 'root@localhost:~#' output; do sleep 1; done

#function run_command() {
#    local command="$1"
#
#    local id="RunCommandID_$RANDOM$RANDOM$RANDOM$RANDOM"
#    local oldsize=$(wc -c output | cut -d ' ' -f1)
#
#    # Start running the command
#    echo "$command ; echo Exit status: \$?; echo '$id'" >> input
#
#    # Wait for it to be ready. We grep from output excluding the first `oldsize` bytes.
#    while ! tail -c +$((oldsize+1)) output | grep --text -q "^$id"; do sleep 0.2; done
#
#    # Check exit status
#    local status=$(grep --text -o "Exit status: [0-9]*" output | tail -1 | cut -d ' ' -f 3)
#    if [ $status != 0 ]; then
#        echo "*** Command failed: $command --> $status ***"
#        exit 1
#    fi
#}

echo "Installing ssh server..."

# The success message is in two parts so that when we search for it, we don't find the
# command that produces it.
echo "
apt update &&
apt install -y openssh-server &&
echo $(cat ~/.ssh/id*.pub | base64 | tr -d '\n') | base64 -d > .ssh/authorized_keys &&
systemctl start ssh &&
echo -n 'EVERYTHING IS ALL ' &&
echo 'DONE NOW YEAH'" >> input

while ! grep -q 'EVERYTHING IS ALL DONE NOW YEAH' output; do sleep 1; done

# Silence big scary warning about changed host identity.
# Yes, of course it changes if I re-run this script multiple times.
# I understand why the warning exists and why it is essential in most use cases.
echo "Silencing ssh warning..."
ssh-keygen -f ~/.ssh/known_hosts -R "[localhost]:2222"

echo "Waiting for ssh to be working..."
while ! ssh -o StrictHostKeyChecking=accept-new -p 2222 root@localhost echo hello; do sleep 1; done

echo "Now this works:  ssh -p 2222 root@localhost"

set -x
ssh -p 2222 root@localhost mkdir jou
(cd ../.. && tar c .git) | ssh -p 2222 root@localhost 'cd jou && tar xf -'
ssh -p 2222 root@localhost apt install git llvm-19-dev clang-19 make
ssh -p 2222 root@localhost make
ssh -p 2222 root@localhost ./runtests.sh --verbose
ssh -p 2222 root@localhost './jou -o jou2 compiler/main.jou && mv jou2 jou'
ssh -p 2222 root@localhost ./runtests.sh --verbose
ssh -p 2222 root@localhost ./doctest.sh
