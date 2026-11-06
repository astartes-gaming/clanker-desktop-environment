# Clanker Desktop Environment

Clanker is a fork of the [Common Desktop Environment](https://sourceforge.net/projects/cdesktopenv/)
(CDE), the classic Motif desktop of commercial UNIX. It keeps CDE's look,
its programs and the systems it runs on, and adds what a desktop needs
today: modern logins and sessions, laptops and touchpads, picture
wallpapers, a GPU compositor, settings panels, and a lot of security
fixes.

It is developed with heavy assistance from AI (Claude). CDE's source is
licensed under the LGPL 2.0 or later; so is everything Clanker adds.

Old systems stay supported. New code is C99 (every compiler on the
supported systems has a C99 mode), uses POSIX.1-2001 interfaces, and
every modern feature is optional at build time or detected at run time:
on Solaris, AIX, the BSDs or a 32-bit machine without those features,
CDE behaves as it always did.

## What's new

**Desktop and windows**
- Window snapping and tiling. Drag a window to a screen edge to tile it
  to that half, or to the top to maximize it. **Super+Left/Right**
  tiles, **Super+Up** maximizes, **Super+Down** restores, and
  double-clicking a title bar maximizes.
- Monitors that are plugged in or removed, and resolution changes, are
  handled. Windows stay on screen and the front panel stays at the
  bottom centre.
- Picture wallpapers (PNG, JPEG) on every monitor: Center, Fit, Fill
  (cropped) or Stretch, scaled on the GPU. Choose them in Style
  Manager > Backdrop; **Add Picture...** takes a picture from anywhere.
- An optional compositor (picom) for GPU-accelerated, tear-free drawing:
  Style Manager > Window > *Use Compositor*.
- GTK and Qt applications can follow the CDE colour palette: Style
  Manager > Color > *Apply colors to GTK and Qt applications*.

**Laptops**
- Touchpad settings in Style Manager > Mouse: tap to click, natural
  scrolling, two-finger scrolling, disable while typing, and speed.
- Brightness, volume, mute and microphone keys work.
- The screen locks before the computer suspends (lid closed, sleep key).

**Settings and session**
- Style Manager panels for **Network** (NetworkManager), **Bluetooth**
  (BlueZ), **Updates** (PackageKit, apt, dnf, zypper, pacman, BSD
  package tools) and **Autostart** (start applications at login).
- **Lock Screen**, **Suspend**, **Restart...** and **Shut Down...** in
  the Workspace menu (right-click the desktop).
- Browsers and video players can keep the screen from blanking and
  locking while they play: the session answers
  `org.freedesktop.ScreenSaver` on D-Bus.
- "Show in folder" in browsers and other applications opens the File
  Manager with the file selected (`org.freedesktop.FileManager1`).
- Logins work on current Linux-PAM. Sessions are registered with logind
  and use the user D-Bus, XDG autostart and systemd's
  `graphical-session.target`.

**Security**
- Temporary-file races, privilege-drop bugs, heap overflows and
  use-after-free bugs are fixed throughout.
- dtappgather no longer runs as root (it had a public root exploit).
- ToolTalk, the session manager and dtlogin listen only on the local
  machine; XDMCP is off by default.
- The screen lock no longer locks you out after a single typo.
- Builds use stack protection, `_FORTIFY_SOURCE` and full RELRO by
  default. `--with-malloc=` links an alternative allocator.

## Install

### Dependencies (Debian/Ubuntu)

```sh
sudo apt install build-essential autoconf automake libtool pkgconf \
    libxt-dev libxmu-dev libxinerama-dev libxpm-dev libmotif-dev \
    libxaw7-dev libx11-dev libxext-dev libxrender-dev libxau-dev \
    libice-dev libsm-dev libxdmcp-dev libxss-dev libxi-dev libxrandr-dev \
    libpng-dev libjpeg-dev libtirpc-dev rpcsvc-proto libutempter-dev \
    libpam-dev libcrypt-dev liblmdb-dev libdbus-1-dev tcl-dev locales-all opensp flex \
    bison ksh m4 ncompress x11-xserver-utils xfonts-utils xbitmaps
```

The authoritative list is `cde/debian/control`. `libxi-dev`,
`libxrandr-dev`, `libpng-dev` and `libdbus-1-dev` are optional: without
them you lose, respectively, the touchpad settings, monitor hotplug, the
PNG fix and the D-Bus services (`--without-dbus` turns them off).

For the optional extras: `picom` (compositor), `network-manager`,
`bluez`, `packagekit`, `dex` (XDG autostart), `brightnessctl`, and
`qt5ct`/`qt6ct` (Qt theming).

### Build

```sh
cd cde
./autogen.sh
./configure            # --disable-hardening, --with-malloc=mimalloc ...
make -j"$(nproc)"
sudo make install      # installs into /usr/dt
```

On the BSDs use `gmake`, and pass `--with-tcl=/usr/local/lib/tcl8.6`
to configure.

### Log in

- **With dtlogin**, CDE's own login screen:

  ```sh
  sudo cp cde/contrib/rc/systemd/dtlogin.service /etc/systemd/system/
  sudo systemctl disable --now sddm   # or gdm, lightdm...
  sudo systemctl enable --now dtlogin
  ```

- **With your current login screen** (SDDM, GDM, LightDM): install the
  session entry, then pick **CDE** at the login screen.

  ```sh
  sudo cp cde/contrib/desktopentry/cde.desktop /usr/share/xsessions/
  ```

## Use

| To... | Go to |
|---|---|
| Set a picture wallpaper | Style Manager > Backdrop > *Add Picture...*, then choose Fill or Fit |
| Set up the touchpad | Style Manager > Mouse > *Touchpad* |
| Connect to Wi-Fi | Style Manager > Network |
| Pair a Bluetooth device | Style Manager > Bluetooth > *Scan*, then *Pair* |
| Update the system | Style Manager > Updates > *Update Now...* |
| Start an application at login | Style Manager > Autostart > *Add...* |
| Turn on the compositor | Style Manager > Window > *Use Compositor* |
| Theme GTK and Qt applications | Style Manager > Color |
| Lock, suspend, restart, shut down | Right-click the desktop |
| Tile windows | Drag them to a screen edge, or Super+arrow keys |

### Opting out

To turn a session feature off, set the variable in `~/.dtprofile`:

| Variable | Turns off |
|---|---|
| `DT_NO_DBUS=1` | starting a D-Bus session bus |
| `DT_NO_XDG_AUTOSTART=1` | XDG autostart |
| `DT_NO_TOUCHPAD=1` | applying the touchpad settings |
| `DT_NO_LOCK_ON_SLEEP=1` | locking before suspend |
| `DT_NO_COMPOSITOR=1` | the compositor, even if turned on |
| `DT_NO_SYSTEMD_SESSION=1` | `graphical-session.target` |
| `DT_WINDOW_MANAGER=dtwm` | xfwm4 (use dtwm for everything) |
| `TT_ALLOW_REMOTE=1` | (re-enables) ToolTalk over the network |
| `DT_ICE_TCP=1` | (re-enables) the session manager over TCP |

Window snapping is set with dtwm resources: `Dtwm*snapDistance`,
`Dtwm*snapToWindows` and `Dtwm*edgeTiling`.

### Window manager

When xfwm4 (Xfce's window manager) is installed it manages the windows,
and dtwm still draws the Front Panel, the backdrops and the Workspace
menu (`dtwm -panelonly`); the four workspaces are xfwm4's desktops.
Without xfwm4, or with this line in `~/.dtprofile`, dtwm does
everything as before:

    DT_WINDOW_MANAGER=dtwm

`DT_WINDOW_MANAGER` can also name another EWMH window manager. It is
started by `dtaltwm`, which restarts it if it crashes and falls back
to dtwm if it is missing or keeps failing. Window snapping and the
optional picom compositor are dtwm features; xfwm4 has its own.

### Wayland

On Linux, CDE can also run on Wayland the way Xfce 4.20 first did: as
an X11 desktop in a rootful Xwayland inside a small Wayland compositor
(cage, or labwc). Install `cage` (or `labwc`) and `xwayland`, then
choose **CDE (Xwayland)** in a Wayland display manager, or run
`/usr/dt/bin/dtwayland` from a text console. For source builds, copy
the session entry:

    sudo cp cde/contrib/desktopentry/cde-xwayland.desktop /usr/share/wayland-sessions/

The X display is private to the session (its own cookie, no TCP).
Inside an existing Wayland session `dtwayland` opens CDE in a window.

## Test it

`tools/vm/` builds a small Debian VM without root and runs an end-to-end
self-test of the desktop. It checks logins, touchpad, keys, snapping,
wallpapers, resolution changes, the screen lock, screen-saver inhibit, "Show in folder",
suspend and the compositor. See [tools/vm/README.md](tools/vm/README.md).

```sh
tools/vm/build-vm.sh && tools/vm/vmctl.py boot && tools/vm/vmctl.py provision
```

## Credits

CDE was developed by HP, IBM, Sun and Novell, released as open source by
The Open Group in 2012, and is maintained at
[cdesktopenv](https://sourceforge.net/projects/cdesktopenv/). See
[cde/README.md](cde/README.md) and `cde/COPYING`.
