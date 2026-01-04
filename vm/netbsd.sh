#!/bin/bash
set -e -o pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <arch> <command inside VM>"
    exit 1
fi

arch=$1
shift
case $arch in
    amd64)
        qemu=kvm
        sha256=92a40431b2488785172bccf589de2005d03c618e7e2618a6a4dd0465af375bfd
        ;;
    *)
        echo "$0: Unsupported architecture '$arch' (must be amd64)"
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

mkdir -vp "$(dirname "$0")/netbsd-$arch"
cd "$(dirname "$0")/netbsd-$arch"

# Download NetBSD
if ! [ -f NetBSD-10.1-$arch-live.img ]; then
    ../download.sh https://cdn.netbsd.org/pub/NetBSD/images/10.1/NetBSD-10.1-$arch-live.img.gz $sha256

    echo "Extracting..."
    gunzip NetBSD-10.1-$arch-live.img.gz

    echo "Enabling getty on serial port..."
    # Modify file content of /etc/ttys directly in the image.
    # This is a bit hacky.
    # Must not change length of file, otherwise offsets get messed up!
    offset=$(grep --text --byte-offset 'tty00."/usr/libexec/getty std.9600".unknown off secure' NetBSD-10.1-amd64-live.img | cut -d: -f1)
    echo "  Start of line in config file is at $offset"
    (dd if=NetBSD-10.1-amd64-live.img bs=1 skip=$offset count=1000 status=none || true) | head -1  # show the line of text
    printf "on " | dd of=NetBSD-10.1-amd64-live.img bs=1 seek=$((offset+44)) conv=notrunc status=none
    (dd if=NetBSD-10.1-amd64-live.img bs=1 skip=$offset count=1000 status=none || true) | head -1  # show the line of text

    # Make disk image large enough for LLVM and other tools.
    #
    # When NetBSD boots, it detects that the disk has been resized, adjusts its
    # partitions to use the whole disk, and automatically reboots.
    echo "Resizing disk..."
    truncate -s 4G NetBSD-10.1-$arch-live.img
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
    #   -serial: make serial console available on host's port 4444, used to configure ssh
    $qemu \
        -m 1G \
        -smp $(nproc) \
        -drive file=NetBSD-10.1-$arch-live.img,format=raw \
        -nic user,hostfwd=tcp:127.0.0.1:2222-:22 \
        -serial tcp:localhost:4444,server=on,wait=off \
        &
    qemu_pid=$!
    echo $qemu_pid > pid.txt
    disown
fi

ssh="ssh root@localhost -o StrictHostKeyChecking=no -o UserKnownHostsFile=my_known_hosts -i key -p 2222"

echo "Checking if ssh works..."
if ! [ -f key ] || ! timeout 5 $ssh echo hello; then
    # ssh doesn't work either because we didn't set it up yet or we need to
    # wait for the VM to start.
    #
    # We consider the VM started when it shows login prompt on serial port.
    # At that point it has also started ssh.
    echo "Waiting for VM to boot..."
    until echo | ../wait_for_string.sh 'login:' nc.traditional localhost 4444; do
        sleep 1
        kill -0 $qemu_pid  # Stop if qemu dies
    done
    echo "Checking again if ssh works..."
    if ! [ -f key ] || ! timeout 5 $ssh echo hello; then
        echo "ssh doesn't work. Let's set it up using serial port."
        (yes || true) | ssh-keygen -t ed25519 -f key -N ''
        rm -vf my_known_hosts
        # Log in as root and set up ssh key
        printf 'root\nmkdir .ssh\nchmod 700 .ssh\necho "%s" > .ssh/authorized_keys\necho ALL"DONE"NOW\nexit\n' "$(cat key.pub)" | ../wait_for_string.sh 'ALLDONENOW' nc.traditional localhost 4444
        echo "Now ssh setup is done, let's check one last time..."
        $ssh echo hello  # Check that it works
    fi
fi

echo "Checking if repo needs to be copied over..."
if [ "$($ssh 'cd jou && git rev-parse HEAD' || true)" != "$(git rev-parse HEAD)" ]; then
    echo "Checking if packages are installed..."
    if ! $ssh which git; then
        # Set up networking. NetBSD uses IPv6 by default, but qemu doesn't support
        # IPv6 at all (at least not in the configuration we use) so everything that
        # connects to internet hangs forever if we don't do this.
        echo "Checking if IPv4 setup has been done..."
        if ! $ssh grep ipv4_prefer /etc/rc.conf; then
            echo "Telling VM to prefer IPv4..."
            $ssh '(echo ip6addrctl=YES && echo ip6addrctl_policy=ipv4_prefer) >> /etc/rc.conf'
            $ssh /sbin/reboot || true  # couldn't figure out a way to do this without rebooting
            while true; do
                echo "Waiting for it to reboot..."
                sleep 5
                if timeout 5 $ssh echo hello; then
                    break
                fi
            done
        fi

        echo "Installing packages..."
        $ssh "PATH=\"/usr/pkg/sbin:/usr/pkg/bin:/usr/sbin:\$PATH\" && PKG_PATH=https://cdn.NetBSD.org/pub/pkgsrc/packages/NetBSD/$arch/10.1/All && export PATH PKG_PATH && pkg_add pkgin && pkgin -y install clang diffutils git bash"
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
