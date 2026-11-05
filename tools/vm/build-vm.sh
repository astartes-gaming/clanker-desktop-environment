#!/bin/sh
# Build a small Debian 13 test VM for Clanker/CDE without root.
#
#   tools/vm/build-vm.sh        (CDE must already be built in ../../cde)
#   tools/vm/build-vm.sh --payload-only   refresh the CDE payload, keep disk
#   tools/vm/vmctl.py boot      then: vmctl.py provision, screenshot, ...
#
# Uses Debian's official "nocloud" image (root on the console, no
# password) as a read-only base with a qcow2 overlay, and passes the
# staged CDE build in as a raw tar "disk".  Nothing here needs root;
# everything lands in tools/vm/out/ (git-ignored).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
CDE=$(cd "$HERE/../../cde" && pwd)
OUT=$HERE/out
BASE_URL=https://cloud.debian.org/images/cloud/trixie/latest
BASE=debian-13-nocloud-amd64.qcow2
DISK_SIZE=${DISK_SIZE:-4G}
mkdir -p "$OUT"

# 1. Base image, verified against Debian's SHA512SUMS.
if [ ! -f "$OUT/$BASE" ]; then
    curl -fsSL -o "$OUT/SHA512SUMS" "$BASE_URL/SHA512SUMS"
    curl -fSL -o "$OUT/$BASE.part" "$BASE_URL/$BASE"
    sum=$(awk -v f="$BASE" '$2 == f {print $1}' "$OUT/SHA512SUMS")
    echo "$sum  $OUT/$BASE.part" | sha512sum -c -
    mv "$OUT/$BASE.part" "$OUT/$BASE"
fi

# 2. Stage the CDE build.  The dtappgather "chown root" step cannot run
#    unprivileged; provision.sh fixes ownership inside the VM.
STAGE=$OUT/stage
rm -rf "$STAGE"
mkdir -p "$STAGE"
sed -i -E '/^[[:space:]]+-?chown[[:space:]]+root[[:space:]].*dtappgather/s@^.*@\t: chown done in VM@' \
    "$CDE/programs/dtsearchpath/dtappg/Makefile" 2>/dev/null || true
make -C "$CDE" install DESTDIR="$STAGE" >"$OUT/install.log" 2>&1

# 3. Runtime packages: map every library the CDE binaries need to the
#    Debian package providing it (the host runs the same release).
LD_LIBRARY_PATH=$STAGE/usr/dt/lib find "$STAGE/usr/dt" -type f -perm -u+x \
    -exec sh -c 'file -b "$1" | grep -q ELF && ldd "$1" 2>/dev/null' _ {} \; |
    awk '/=>/ && $3 ~ /^\// {print $3}' | grep -v "$STAGE" | sort -u |
while read -r lib; do
    for p in "$lib" "/usr$lib" "$(readlink -f "$lib")" "${lib#/usr}"; do
        r=$(dpkg -S "$p" 2>/dev/null | grep -v '^diversion' | head -1 | cut -d: -f1)
        if [ -n "$r" ]; then echo "$r"; break; fi
    done
done | sort -u | tr '\n' ' ' >"$OUT/runtime-pkgs.txt"

# 4. Payload "disk": a plain tar the VM reads from /dev/vdb.
PAY=$OUT/payload
rm -rf "$PAY"
mkdir -p "$PAY"
tar --owner=0 --group=0 -C "$STAGE" -czf "$PAY/cde-stage.tar.gz" .
cp "$HERE/provision.sh" "$OUT/runtime-pkgs.txt" "$PAY/"
cp "$CDE/contrib/rc/systemd/dtlogin.service" "$PAY/"
if [ ! -f "$OUT/credentials" ]; then
    pw=$(head -c 12 /dev/urandom | base64 | tr -d '/+=' | cut -c1-12)
    rpw=$(head -c 12 /dev/urandom | base64 | tr -d '/+=' | cut -c1-12)
    printf 'user=tester\npassword=%s\nrootpw=%s\n' "$pw" "$rpw" >"$OUT/credentials"
    chmod 600 "$OUT/credentials"
fi
sed -n 's/^password=//p' "$OUT/credentials" >"$PAY/tester-password"
tar -C "$PAY" -cf "$OUT/payload.tar" .

if [ "${1:-}" = "--payload-only" ]; then
    echo "payload refreshed: $(du -h "$OUT/payload.tar" | cut -f1)"
    exit 0
fi

# 5. Fresh overlay disk on top of the pristine base image.
rm -f "$OUT/disk.qcow2"
qemu-img create -q -f qcow2 -b "$BASE" -F qcow2 "$OUT/disk.qcow2" "$DISK_SIZE"

echo "VM ready: $(du -h "$OUT/payload.tar" | cut -f1) payload; boot with tools/vm/vmctl.py boot"
