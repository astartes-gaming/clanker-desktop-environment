# Clanker roadmap

What's left to do, roughly in order of priority. Tick items off as they
land; add new ones at the bottom of their section.

Everything here follows the project rule: old systems (Solaris, AIX, the
BSDs, 32-bit, old compilers) keep building and working, and modern
features stay optional or detected at run time.

## Next up

- [ ] **Build CI for old and non-Linux systems.** CI only builds on
      Linux (Debian, Ubuntu, Fedora, Arch, all 64-bit). Add FreeBSD,
      OpenBSD, NetBSD and illumos jobs (in VMs), and a 32-bit Debian
      (i386) job, so nothing breaks the systems the project promises to
      support.
- [ ] **Sanitizer CI job.** One job that builds with
      `-fsanitize=address,undefined` and runs the self-test in a VM, to
      catch memory bugs before they ship.
- [ ] **Unsafe-string sweep in dtlogin and the screen lock.** These run
      as root or guard the session; replace `strcpy`/`sprintf`/`strcat`
      with bounded versions (still C89: `snprintf` is POSIX.1-2001).
- [ ] **virt-manager VM in tools/vm.** Commit the libvirt domain
      definition as a template and document using it with
      virt-manager (user session, no root). Drop the AQEMU notes: AQEMU
      is unmaintained and fails on QEMU 10.

## Testing gaps

- [ ] Compositor on a real GPU (only tested on llvmpipe in the VM).
- [ ] Brightness and volume keys on real laptop hardware (the VM has no
      backlight or audio).
- [ ] A screen-lock unlock failure seen once after resume with the QEMU
      keyboard; not reproduced since.
- [ ] Builds without libpng/libjpeg, and visuals other than TrueColor.
- [ ] Qt theming (Style Manager > Color) with qt5ct and qt6ct.
- [ ] Network, Bluetooth and Updates panels on a real machine (Wi-Fi
      with WPA, pairing a device, an update run with each backend).

## Features

- [ ] Horizontal scrolling (mouse buttons 6/7, touchpad two-finger
      sideways) in Motif applications.
- [ ] Translations for the new strings (dtstyle.msg sets 4 and 20,
      Workspace menu power entries, laptop key bindings) in the
      localized catalogs under `cde/programs/localized/`.
- [ ] Experimental Wayland session: CDE running in a rootful Xwayland
      inside a small compositor (cage or labwc), the way Xfce 4.20
      started. Optional, Linux only, off by default.
- [ ] Replace ToolTalk with D-Bus where both are available, keeping
      ToolTalk for old systems.

## Code health

- [ ] Work down the compiler warnings (about 6,090 sites with
      `-Wall -Wextra`), starting with the root and setuid programs.
- [ ] Decide whether new code may use C99 (`stdint.h`, `//`, mixed
      declarations), and on which compilers that would drop support.
      Until then, new code stays C89.

## Done

- Phase 1: temp-file and privilege fixes, undefined-behaviour fixes,
  GTK/Qt theming, logind/D-Bus/XDG session integration, window
  snapping (PR #1).
- Laptops: touchpad settings, laptop keys, lock before suspend, monitor
  hotplug (PR #2).
- Graphics: per-monitor picture wallpapers, PNG fix, optional GPU
  compositor (PR #3).
- Security: dtappgather no longer runs as root, RPC services local
  only, build hardening, `--with-malloc=` (PR #4).
- Settings: Network, Bluetooth, Updates and Autostart panels; Lock,
  Suspend, Restart and Shut Down in the Workspace menu;
  `graphical-session.target`; README how-to (PR #5).
- Test VM with a 37-check self-test (`tools/vm`).
