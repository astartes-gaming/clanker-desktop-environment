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
- [ ] Qt theming (Settings > Color) with qt5ct and qt6ct.
- [ ] Network, Bluetooth and Updates panels on a real machine (Wi-Fi
      with WPA, pairing a device, an update run with each backend).

## Features

- [ ] Horizontal scrolling (mouse buttons 6/7, touchpad two-finger
      sideways) in Motif applications.
- [ ] Translations for the new strings (dtstyle.msg sets 4 and 20,
      Workspace menu power entries, laptop key bindings) in the
      localized catalogs under `cde/programs/localized/`.
- [ ] Replace ToolTalk with D-Bus where both are available, keeping
      ToolTalk for old systems. Stages 1 and 2 are done (see below); still
      to do:
  - [ ] DtSvc actions (`DtActionInvoke`) and dtexec over D-Bus.
  - [ ] libtt session messages over D-Bus, with ToolTalk as the
        fallback when no session bus is running.

- [ ] UTF-8 locales: Motif warns "Missing charsets in String to FontSet
      conversion" at start (cosmetic).

## Code health

- [ ] Work down the compiler warnings (about 6,090 sites with
      `-Wall -Wextra`), starting with the root and setuid programs.
- [ ] Move touched code to C99 idioms (`stdbool.h`, `stdint.h`,
      declarations where used) as it is worked on; no mass rewrites.

## Done

- dtlogin opens the PAM session in the process that waits for the
  session, so logind no longer marks CDE sessions "logged out" as they
  start (pam_systemd's descriptor was closed by the exec of Xsession).
- selftest.sh: picks the user's X11 session (not a text console one) and
  gives the virtual touchscreen real axis ranges.

- Touchscreen gestures (dttouch, XInput 2.2 touch grab + XTest): long
  press = right click, two-finger drag = scroll; taps and drags pass
  through.  Started when a touchscreen is present (DT_NO_TOUCH=1: off).
- On-screen keyboard: a Front Panel button (below the lock) shows or
  hides an established keyboard (onboard, florence, xvkbd or
  matchbox-keyboard; DT_KEYBOARD overrides).
- Settings > Accessibility: sticky, slow and bounce keys, mouse keys,
  keyboard toggles (XKB AccessX), a large pointer, the on-screen
  keyboard at login; applied at login by dtaccess.

- Style Manager is called Settings (window titles, Front Panel label,
  action name; English).  The program is still dtstyle.

- Front Panel system tray (a `systray` control, freedesktop System Tray
  protocol over XEMBED) after Trash; works under dtwm and xfwm4.
- The busy light above Exit opens a task manager (dttaskmgr: the first
  of xfce4-taskmanager, lxtask, MATE/GNOME/KDE monitors, else htop/top).
- Under xfwm4 the window menu (and tooltips) use the CDE-wm GTK
  stylesheet made from the palette by dtxfwmtheme.
- Settings > Front Panel: add applications to any button's drawer
  (a drawer is created if it has none), remove or restore drawer items.
  A changed button now keeps its drawer.
- A panel-only dtwm quits on SIGTERM/SIGHUP without a confirmation box
  it could not show.

- Under xfwm4, Motif-style title bars and frames in the CDE palette
  (dtxfwmtheme), following palette changes.
- Settings > Front Panel: choose which application each main panel
  button opens (an installed application or a command).
- Settings > Session: the ~/.dtprofile options (D-Bus, autostart,
  window manager, ...) as switches.
- Under xfwm4 the Front Panel's handles move it (it reserves its screen
  edge only while it sits on one).  tools/vm/selftest-wayland.sh tests
  the Wayland session headless (cage, no login needed).
- Help volumes show bullets, accented letters, (c) and other symbols
  again, in C and UTF-8 locales (DtHelp's own glyphs via <SPC>).
- The whole tree builds as C99 and as C23 (GCC 15's default) with no
  old-style definitions or prototype-less declarations; CI checks both
  (`c-standards.yml`).
- Wayland: `dtwayland` and the "CDE (Xwayland)" session run CDE in a
  rootful Xwayland under cage or labwc (Linux, opt-in).
- xfwm4 manages the windows when installed (opt out with
  `DT_WINDOW_MANAGER=dtwm`); `dtwm -panelonly` keeps the Front Panel,
  backdrops and Workspace menu, with workspaces and desktops in step.
- C99 is the language baseline; configure stops with a clear message
  on a compiler without a C99 mode (GCC 3+, Clang, Oracle Studio 12+
  and XL C 10+ all have one).
- D-Bus stage 1: a shared D-Bus layer in DtSvc driven by the Xt main
  loop, and dtsession provides `org.freedesktop.ScreenSaver`, so
  browsers and video players keep the screen from locking. Optional
  (`--without-dbus`) and skipped when no session bus is running.
- D-Bus stage 2: dtfile provides `org.freedesktop.FileManager1`, so
  "Show in folder" opens a File Manager view with the item selected.
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
