#!/bin/sh

set -e

sudo apt update
sudo apt --no-install-recommends install qemu-system-x86 expect

mkdir -p alpine && cd alpine

DOWNLOAD_PATH="https://dl-cdn.alpinelinux.org/latest-stable/releases/x86"
../download.sh $DOWNLOAD_PATH/alpine-minirootfs-3.23.2-x86.tar.gz 99457a300f13d1971d4627a0ccdf4ae17db18807804e3b5867d4edf8afa16d23
../download.sh $DOWNLOAD_PATH/alpine-virt-3.23.2-x86.iso 4c6c76a7669c1ec55c6a7e9805b387abdd9bc5df308fd0e4a9c6e6ac028bc1cc

mv -v alpine-minirootfs-*-x86.tar.gz rootfs.tar.gz
mv -v alpine-virt-*-x86.iso virt.iso

mkdir rootfs
tar xf rootfs.tar.gz -C rootfs

git bundle create rootfs/root/jou.bundle --all

truncate -s 4G rootfs.img
/sbin/mkfs.ext4 rootfs.img -d rootfs
rm -rf rootfs

expect <<END
set timeout 300

spawn sudo kvm -m 2G -smp 2 -drive file=virt.iso,format=raw,media=cdrom -drive file=rootfs.img,format=raw -nographic

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
        send "git clone ./jou.bundle jou && cd jou && "
        send "./runtests.sh --verbose && mv jou jou_bootstrap && ./runtests.sh should_succeed && ./doctest.sh && "
        send "echo SUCCESS >~/status \r"
    }
}
expect {
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
