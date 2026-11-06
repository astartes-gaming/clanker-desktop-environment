# Test VM

A small Debian 13 VM for testing Clanker/CDE end to end, built and run
**without root** (QEMU + KVM, user-mode networking).

```sh
# build CDE first (cd cde && ./autogen.sh && ./configure && make), then:
tools/vm/build-vm.sh             # base image, staged CDE payload, overlay disk
tools/vm/vmctl.py boot --ram 384 # starts headless in the background
tools/vm/vmctl.py provision      # installs Xorg + CDE runtime + CDE (once)
tools/vm/vmctl.py run 'systemctl reboot'
tools/vm/vmctl.py screenshot out/screen.png
```

- Base: Debian's official `debian-13-nocloud-amd64.qcow2`, verified
  against `SHA512SUMS`, used read-only under a qcow2 overlay.
- The CDE build is staged with `make install DESTDIR=` and attached as a
  raw tar "disk" (`/dev/vdb`); `provision.sh` unpacks it inside the VM.
- After rebuilding CDE: `build-vm.sh --payload-only`, then in the VM
  `tar -xf /dev/vdb -C /root/payload` and unpack `cde-stage.tar.gz` to `/`.
- `vmctl.py` drives the serial console (root shell) and the QEMU monitor
  (`type`, `key`, `screenshot`). Generated test passwords for `root` and
  the `tester` account are in `out/credentials`.
- A full CDE desktop fits in 384 MB of RAM (~145 MB used) and ~1.5 GB of
  disk.

Everything generated lives in `out/` (git-ignored).

## Self-test

`selftest.sh` checks a running session end to end. Each check prints
PASS, FAIL or SKIP:
- session environment (PAM, D-Bus, XDG)
- loopback-only listeners, no XDMCP
- touchpad settings and hotplug (with a virtual uinput touchpad)
- laptop keys
- window snapping
- wallpapers, including PNG colours
- resolution changes
- screen lock and unlock, and lock-before-suspend
- screen-saver inhibit over D-Bus (`ssinhibit` plays a video player)
- "Show in folder" (`org.freedesktop.FileManager1`) opens dtfile views
- the compositor

Log in as `tester` at the dtlogin screen, then on the serial console
(or as root in a terminal):

```sh
tools/vm/vmctl.py run "sh /root/payload/selftest.sh PASSWORD" -t 900
```

`PASSWORD` is the test user's password from `out/credentials`.
Without it, the lock and suspend checks are skipped.

## Offline packages

If the VM keeps failing to download some packages, fetch them on the
host and pass them in with the payload:

```sh
mkdir debs && (cd debs && apt-get download xterm ...)
VM_DEBS=$PWD/debs tools/vm/build-vm.sh --payload-only
```

## A second VM

```sh
VM_OUT=$PWD/tools/vm/out-512 DISK_SIZE=32G tools/vm/build-vm.sh
VM_OUT=$PWD/tools/vm/out-512 tools/vm/vmctl.py boot --ram 512 --cpus 2
VM_OUT=$PWD/tools/vm/out-512 tools/vm/vmctl.py provision
```

`provision.sh` grows the root filesystem to fill the disk.
`qemu-img convert -O qcow2 out-512/disk.qcow2 standalone.qcow2` gives
an image without the backing file, e.g. for AQEMU or virt-manager.


## Wayland session

`sh /root/payload/selftest-wayland.sh` (as root in the VM, with cage,
xwayland and xauth installed) starts `dtwayland` for the test user on
cage's headless backend and checks the session; it stops dtlogin
first.

### By hand

The VM needs `cage`, `xwayland` and `xauth` (add their .deb files to
`VM_DEBS`). Stop dtlogin (`systemctl stop dtlogin`), log in as the
test user on the text console with `vmctl.py type`, and run
`/usr/dt/bin/dtwayland`. Clients of the inner session use `DISPLAY=:1`
and `XAUTHORITY=/run/user/UID/dtwayland-Xauthority.1`.

## Login screen

`sh /root/payload/selftest-greeter.sh` (as root in the VM) restarts
dtlogin, ending any X11 session, and checks that the login screen
comes back after SIGHUPs to dtlogin.
