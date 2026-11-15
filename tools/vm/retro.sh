#!/bin/sh
# retro.sh - boot the old-PC VM from build-retro.sh: a Pentium III
# (no SSE2), 512 MB, IDE disks, standard VGA, on 32-bit Debian.
# Driven with vmctl.py like the main VM:
#
#   tools/vm/retro.sh [payload.tar]
#   VM_OUT=tools/vm/out-retro tools/vm/vmctl.py run 'uname -a'
#
# KVM keeps it usable; RETRO_TCG=1 emulates the CPU fully (much slower,
# closer to real old hardware).  payload.tar, if given, is the second
# IDE disk (/dev/sdb), read with "tar -xf /dev/sdb".
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${VM_OUT:-$HERE/out-retro}
RAM=${RETRO_RAM:-512}
accel="-enable-kvm"
[ -n "$RETRO_TCG" ] && accel="-accel tcg"
payload=
[ -n "$1" ] && payload="-drive file=$1,if=ide,index=1,format=raw,snapshot=on"
rm -f "$OUT/serial.sock" "$OUT/monitor.sock"
exec qemu-system-i386 $accel -cpu pentium3 -machine pc -m "$RAM" \
    -drive file="$OUT/disk.qcow2",if=ide,index=0,format=qcow2 $payload \
    -kernel "$OUT/vmlinuz" -initrd "$OUT/initrd.img" \
    -append "root=/dev/sda rw console=tty0 console=ttyS0,115200 net.ifnames=0" \
    -nic user,model=ne2k_pci \
    -vga std -display none \
    -serial unix:"$OUT/serial.sock",server=on,wait=off \
    -monitor unix:"$OUT/monitor.sock",server=on,wait=off \
    -pidfile "$OUT/qemu.pid" -daemonize
