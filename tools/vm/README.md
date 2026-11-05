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
