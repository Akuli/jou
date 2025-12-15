#!/bin/sh

set -e

sudo apt update
sudo apt install qemu-system-x86 qemu-system-gui- qemu-system-modules-opengl- expect

DOWNLOAD_PATH="https://dl-cdn.alpinelinux.org/latest-stable/releases/x86"
for f in $(wget -O - ${DOWNLOAD_PATH}/latest-releases.yaml | grep -E "file:.*(minirootfs|virt)" | cut -d ":" -f 2); do
    wget ${DOWNLOAD_PATH}/$f
    wget ${DOWNLOAD_PATH}/$f.asc
done

mv -v alpine-minirootfs-*-x86.tar.gz alpine-rootfs.tar.gz
gpgv --keyring ./alpine.gpg alpine-minirootfs-*-x86.tar.gz.asc alpine-rootfs.tar.gz
mv -v alpine-virt-*-x86.iso alpine-virt.iso
gpgv --keyring ./alpine.gpg alpine-virt-*-x86.iso.asc alpine-virt.iso

rm alpine-*.asc

mkdir rootfs
tar xf alpine-rootfs.tar.gz -C rootfs

git bundle create rootfs/root/jou.bundle --all

truncate -s 4G rootfs.img
/sbin/mkfs.ext4 rootfs.img -d rootfs
qemu-img convert rootfs.img -O qcow2 alpine-rootfs.qcow2

rm -rf rootfs rootfs.img

expect <<END
set timeout 300

spawn sudo kvm -m 2G -smp 2 -drive file=alpine-virt.iso,format=raw,media=cdrom -drive file=alpine-rootfs.qcow2 -nographic

expect {
    "localhost login: " {
        send "root\r"
    }
}
expect {
    "localhost:~# " {
        send "mount -t ext4 /dev/sda /mnt && cd /mnt && "
        send "mount -t proc /proc proc/ && "
        send "mount -t sysfs /sys sys/ && "
        send "mount --rbind /dev dev/ && "
        send "mount --rbind /run run/ && "
        send "chroot . \r"
    }
}
expect {
    "/ # " {
        send "cd \r"
    }
}
expect {
    "~ # " {
        send "echo 'FAIL!!' >status && echo 'iface eth0 inet dhcp' >/etc/network/interfaces && ifup eth0 && "
        send "apk update && apk upgrade && TERM=dumb apk add bash clang llvm-dev make git grep patch libx11-dev && "
        send "git clone ./jou.bundle jou && cd jou && git fetch https://github.com/Akuli/jou && "
        send "./runtests.sh --verbose && mv jou jou_bootstrap && ./runtests.sh should_succeed && ./doctest.sh && "
        send "echo SUCCESS >status \r"
    }
}
expect {
    -re {[0-9] failed} {
        exit 1
    }
    "~ # " {
        exit 1
    }
    "~/jou # " {
        send "cat ~/status \r"
    }
    timeout {
        exit 67
    }
}
expect {
    "SUCCESS" {
        exit 0
    }
    "FAIL!!" {
        exit 1
    }
}
wait
END
