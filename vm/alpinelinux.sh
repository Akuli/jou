#!/bin/bash
set -e -o pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <arch> <command inside VM>"
    exit 1
fi

arch=$1
shift
case $arch in
    aarch64)
        qemu='qemu-system-aarch64 -M virt -cpu cortex-a57 -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd'
        sha256=e2250daa46d0160d789da70a22db1f855adc89793b01687c7f8893d72d7934b4
        ;;
    *)
        echo "$0: Unsupported architecture '$arch' (must be aarch64)"
        exit 2
esac

# TODO: rename this script to alpinelinux.sh
mkdir -vp "$(dirname "$0")/alpinelinux-$arch"
cd "$(dirname "$0")/alpinelinux-$arch"

if ! [ -f disk.img ] || ! [ -f disk-is-ready.txt ]; then
    # Install alpine linux onto a disk

    ../download.sh https://dl-cdn.alpinelinux.org/v3.23/releases/aarch64/alpine-standard-3.23.2-aarch64.iso $sha256

    echo "Creating empty disk image..."
    truncate -s 4G disk.img

    echo "Running alpine linux from CD image to install it..."
    $qemu \
        -m 1G \
        -smp $(nproc) \
        -drive if=virtio,format=raw,file=alpine-standard-3.23.2-aarch64.iso,media=cdrom \
        -drive if=virtio,format=raw,file=disk.img \
        -serial tcp:localhost:4444,server=on,wait=off \
        -nic user \
        -nographic \
        -monitor none \
        &

    while true; do
        echo "Waiting for VM to boot from CD image..."
        if echo | (timeout 1 nc localhost 4444 || true) | grep 'login:'; then
            break
        fi
        sleep 5
    done

    # This was a bit tricky to set up. We want to wait until "localhost:~#"
    # appears, but when it does appears, there's no trailing newline so most
    # (line-oriented) tools get stuck to reading the last line.
    #
    # Turns out that awk is less line-oriented than other tools, and works
    # fine for this if we tell it to split the input by '#' characters.
    echo "Logging in to CD image..."
    printf '\nroot\n' | (nc localhost 4444 || true) | awk -v RS='#' '{ print; fflush(); if ($0 ~ /localhost:~$/) exit }'

    echo "Running setup-alpine..."
    echo "
setup-alpine -c answerfile
sed -i 's:DISKOPTS=none:DISKOPTS=\"-m sys /dev/vdb\":' answerfile
setup-alpine -e -f answerfile
y
" | (nc localhost 4444 || true) | sed '/Installation is complete. Please reboot./q'

    echo "Now setup-alpine is done, powering off VM..."
    printf '\npoweroff\n' | (nc localhost 4444 || true)
    wait

    echo yes > disk-is-ready.txt
fi

exit

if [ "$GITHUB_ACTIONS" = "true" ]; then
    echo "TODO not implemented"
    exit 1
else
    # During local development, let the VM stay alive after this script dies
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
        -drive file=alpine-virt-3.23.2-$arch.iso,media=cdrom \
        -drive file=rootfs.img,format=raw \
        -nic user,hostfwd=tcp:127.0.0.1:2222-:22 \
        -serial tcp:localhost:4444,server=on,wait=off \
        &
    qemu_pid=$!
    echo $qemu_pid > pid.txt
    disown
    sleep 1
    kill -0 $qemu_pid  # stop if qemu died instantly
fi

ssh="ssh root@localhost -o StrictHostKeyChecking=no -o UserKnownHostsFile=my_known_hosts -i key -p 2222"

echo "Checking if ssh works..."
if ! [ -f key ] || ! timeout 5 $ssh echo hello; then
    # ssh doesn't work either because we didn't set it up yet or we need to
    # wait for the VM to start.
    #
    # We consider the VM started when it shows login prompt on serial port.
    # At that point it has also started ssh.
    while true; do
        echo "Waiting for VM to boot..."
        if echo | (timeout 1 nc localhost 4444 || true) | grep 'login:'; then
            break
        fi
        sleep 5
    done
    echo "Checking again if ssh works..."
    if ! [ -f key ] || ! timeout 5 $ssh echo hello; then
        echo "ssh doesn't work. Let's set it up using serial port."
        (yes || true) | ssh-keygen -t ed25519 -f key -N ''
        # This was a bit tricky to set up. See netbsd.sh for a simpler thing.
        #
        # This uses awk to map '#' into a newline so that we don't get line
        # buffering. The last line with "localhost:~#" on it doesn't end with a
        # newline, so most (line-oriented) tools can't detect when it appears.
        # But awk can.
        printf '\nroot\n' | (nc localhost 4444 || true) | awk -v RS='#' '{ print; fflush(); if ($0 ~ /localhost:~$/) exit }'
        printf '
mount /dev/sda /mnt
cd /mnt
mount /proc proc
mount /sys sys
mount --rbind /dev dev
mount --rbind /run run
chroot .
echo "iface eth0 inet dhcp" > /etc/network/interfaces
ifup eth0
apk update
apk add openssh-server
ssh-keygen -A
chown root:root /var/empty   # sshd complains if I dont do this
mkdir -p /var/run/sshd
/usr/sbin/sshd
cd
chown -R root:root .
mkdir .ssh
chmod 700 .ssh
echo "%s" > .ssh/authorized_keys
echo ALL"DONE"NOW
' "$(cat key.pub)" | (nc localhost 4444 || true) | sed '/ALLDONENOW/q'
        echo "Now ssh setup is done, let's check one last time..."
        $ssh echo hello  # Check that it works
    fi
fi

echo "Checking if repo needs to be copied over..."
if [ "$($ssh 'cd jou && git rev-parse HEAD' || true)" != "$(git rev-parse HEAD)" ]; then
    echo "Checking if packages are installed..."
    if ! $ssh which git; then
        echo "Installing packages..."
        $ssh apk add bash clang llvm-dev make git grep libx11-dev
    fi

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
