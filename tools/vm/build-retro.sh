#!/bin/sh
# build-retro.sh - an "old PC" test VM: archived 32-bit Debian on
# emulated Pentium III-class hardware (IDE disk, standard VGA), built
# without root (mmdebstrap --mode=unshare, virt-make-fs).
#
#   tools/vm/build-retro.sh            # Debian 10 "buster", i386
#   RETRO_SUITE=stretch tools/vm/build-retro.sh
#
# Output in $RETRO_OUT (default tools/vm/out-retro): disk.qcow2,
# vmlinuz, initrd.img, credentials.  Boot it with tools/vm/retro.sh.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SUITE=${RETRO_SUITE:-buster}
OUT=${RETRO_OUT:-$HERE/out-retro}
MIRROR=${RETRO_MIRROR:-http://archive.debian.org/debian}
mkdir -p "$OUT"

if [ ! -f "$OUT/credentials" ]; then
    pw=$(head -c 12 /dev/urandom | base64 | tr -d '/+=' | cut -c1-12)
    printf 'user=tester\npassword=%s\nrootpw=%s\n' "$pw" "$pw" >"$OUT/credentials"
    chmod 600 "$OUT/credentials"
fi
PW=$(sed -n 's/^password=//p' "$OUT/credentials")

# Build dependencies as in README.md, minus libcrypt-dev and
# rpcsvc-proto (part of glibc before Debian 11).
BUILD="build-essential autoconf automake libtool pkg-config libxt-dev
libxmu-dev libxinerama-dev libxpm-dev libmotif-dev libxaw7-dev libx11-dev
libxext-dev libxrender-dev libxau-dev libice-dev libsm-dev libxdmcp-dev
libxss-dev libxi-dev libxrandr-dev libpng-dev libjpeg-dev libtirpc-dev
libutempter-dev libpam0g-dev liblmdb-dev libdbus-1-dev tcl-dev locales-all
opensp flex bison ksh m4 ncompress x11-xserver-utils xfonts-utils xbitmaps"
SYSTEM="linux-image-686 initramfs-tools systemd-sysv udev ifupdown
isc-dhcp-client openssh-client curl ca-certificates sudo rpcbind xauth
xserver-xorg-core xserver-xorg-video-vesa xserver-xorg-video-cirrus
xserver-xorg-input-evdev xserver-xorg-input-libinput x11-utils xdotool
xfonts-base xfonts-100dpi xfonts-75dpi dbus-x11 imagemagick"

mmdebstrap --mode=unshare --arch=i386 --variant=minbase \
    --aptopt='Acquire::Check-Valid-Until "false"' \
    --include="$(echo $BUILD $SYSTEM | tr ' ' ',')" \
    --customize-hook='echo retro > "$1/etc/hostname"' \
    --customize-hook='printf "127.0.0.1 localhost\n127.0.1.1 retro\n" > "$1/etc/hosts"' \
    --customize-hook='printf "/dev/sda / ext4 defaults 0 1\n" > "$1/etc/fstab"' \
    --customize-hook='printf "auto lo\niface lo inet loopback\nauto ens3\niface ens3 inet dhcp\nauto eth0\niface eth0 inet dhcp\n" > "$1/etc/network/interfaces"' \
    --customize-hook="chroot \"\$1\" useradd -m -s /bin/bash -G sudo,video,audio tester" \
    --customize-hook="printf 'root:%s\ntester:%s\n' '$PW' '$PW' | chroot \"\$1\" chpasswd" \
    --customize-hook='chroot "$1" systemctl enable serial-getty@ttyS0.service' \
    --customize-hook='mkdir -p "$1/etc/systemd/system/serial-getty@ttyS0.service.d"; printf "[Service]\nExecStart=\nExecStart=-/sbin/agetty --autologin root --keep-baud 115200,38400,9600 %%I \$TERM\n" > "$1/etc/systemd/system/serial-getty@ttyS0.service.d/autologin.conf"' \
    --customize-hook='chroot "$1" sh -c "ls -l /boot >&2; ls /boot/initrd.img-* >/dev/null 2>&1 || update-initramfs -c -k all; cp /boot/vmlinuz-* /retro-vmlinuz && cp /boot/initrd.img-* /retro-initrd && chmod 644 /retro-vmlinuz /retro-initrd"' \
    --customize-hook="copy-out /retro-vmlinuz /retro-initrd '$OUT'" \
    "$SUITE" "$OUT/rootfs.tar" \
    "deb $MIRROR $SUITE main"

# A plain ext4 disk (no partition table, as an old IDE drive would
# carry for this purpose); room to build CDE.
mv "$OUT/retro-vmlinuz" "$OUT/vmlinuz"; mv "$OUT/retro-initrd" "$OUT/initrd.img"
rm -f "$OUT/disk.qcow2"
virt-make-fs --format=qcow2 --type=ext4 --size=+6G \
    "$OUT/rootfs.tar" "$OUT/disk.qcow2"
rm -f "$OUT/rootfs.tar"
echo "retro VM ready in $OUT"
