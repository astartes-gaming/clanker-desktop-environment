# Clanker Desktop Environment — User Guide

Clanker is CDE, the classic UNIX desktop, with what a desktop needs
today. This guide explains how to use it. For building and installing,
see the [README](../README.md).

- [The desktop at a glance](#the-desktop-at-a-glance)
- [The Front Panel](#the-front-panel)
- [Settings](#settings)
- [Windows](#windows)
- [Touchscreens and tablets](#touchscreens-and-tablets)
- [Accessibility](#accessibility)
- [Colors and themes](#colors-and-themes)
- [Using Xfce applications](#using-xfce-applications)
- [Laptops](#laptops)
- [Session options (~/.dtprofile)](#session-options-dtprofile)
- [Files Clanker keeps in your home folder](#files-clanker-keeps-in-your-home-folder)
- [Troubleshooting](#troubleshooting)

## The desktop at a glance

- **Front Panel**: the bar at the bottom of the screen. It starts
  applications, switches workspaces, shows the system tray, and locks or
  ends the session.
- **Workspaces**: four desktops (One to Four), switched with the
  buttons in the middle of the Front Panel.
- **Workspace menu**: right-click the desktop for applications, Lock
  Screen, Suspend, Restart and Shut Down.
- **Settings**: everything you can change (it was called Style Manager in
  CDE). Open it from the Front Panel's Settings button.
- **File Manager**, **Text Editor**, **Terminal**, **Calendar**,
  **Mail**, **Help**: CDE's own applications, or Xfce's if you choose
  (see [Using Xfce applications](#using-xfce-applications)).

## The Front Panel

| Part | What it does |
|---|---|
| Clock, calendar, Home Folder, Text Editor, Mail | Start those applications. Click the small arrow above a button to open its drawer. |
| Lock (padlock) | Locks the screen. |
| Keyboard (below the lock) | Shows the on-screen keyboard; press again to hide it. |
| Workspace buttons | Switch workspaces. Double-click a name to rename it. |
| Light above Exit | Blinks while an application starts. Click it to open the **Task Manager**. |
| Exit | Logs out. |
| Printer, Settings, Applications, Help, Trash | Start those. |
| System tray (right end) | Icons of running applications (network, sound, chat, …). Click or right-click them as usual. |

**Moving the panel**: drag it by the handles at either end. It reserves
its screen edge only while it sits on one.

**Choosing what a button opens**: Settings > Front Panel. Select a
button, press **Change...**, and pick an installed application or type
a command. **Reset** puts the original back.

**Drawers**: in the same window, each button is followed by the items
of its drawer.

- **Add to Drawer...** adds an application to the selected button's
  drawer. A button without a drawer gets one.
- **Remove** takes an item out of a drawer. Items that came with the
  desktop are only hidden: select them and press **Reset** to bring
  them back.
- You can also drop an application's icon on a drawer's **Install
  Icon** area, as in CDE.

**Task Manager**: the light above Exit opens the first task manager
installed: xfce4-taskmanager, lxtask, the MATE, GNOME or KDE system
monitor, or qps. Without one, it shows `htop` or `top` in a terminal.
`DT_TASKMANAGER` in `~/.dtprofile` picks another command.

**System tray**: works with any application that uses the standard X11
tray (XEMBED). If another tray program is already running, it keeps the
icons. `DT_NO_SYSTRAY=1` turns the tray off.

## Settings

| Button | What you set there |
|---|---|
| Color | The palette (colors of everything), and whether GTK and Qt applications follow it |
| Font | The size of the desktop's text |
| Backdrop | The wallpaper, including your own pictures (Center, Fit, Fill, Stretch) |
| Keyboard, Mouse, Beep | Repeat rate, pointer speed, touchpad (tap to click, natural scrolling, …), sound |
| Screen | Screen saver and lock timing |
| Window | Focus behaviour, and the optional compositor |
| Startup, Autostart | What the session restores, and applications started at login |
| Front Panel | What each button opens, and the drawers (see above) |
| Session | Options in `~/.dtprofile` (below), and Xfce applications |
| Accessibility | Sticky, slow and bounce keys, mouse keys, large pointer, on-screen keyboard at login |
| Network, Bluetooth, Updates | Wi-Fi, Bluetooth devices, system updates (shown when the system supports them) |

Changes take effect at once, except the Session options, which are used
at the next login.

## Windows

- **Tiling**: drag a window to the left or right edge to fill that half,
  to the top to maximize. **Super+Left/Right** tiles, **Super+Up**
  maximizes, **Super+Down** restores. Double-click a title bar to
  maximize.
- **Window menu**: the button at the left of each title bar (or
  **Alt+Space**): maximize, minimize, move, resize, always on top, move
  to another workspace, close.
- **Window manager**: when xfwm4 (Xfce's window manager) is installed it
  manages the windows, with CDE-style frames and window menu in your
  palette. Without it, or with `DT_WINDOW_MANAGER=dtwm`, CDE's dtwm
  does everything as before. Settings > Session > *Window manager*
  switches between them (next login).

## Touchscreens and tablets

On a touchscreen Clanker adds what CDE's applications lack:

| Gesture | Does |
|---|---|
| Tap | Click |
| Drag with one finger | Drag (move windows, select text, drag files) |
| Long press (about half a second, finger still) | Right-click: opens context menus |
| Drag with two fingers | Scroll (up, down, left, right) |

The **on-screen keyboard** is the keyboard button below the lock in the
Front Panel. It uses an established keyboard application: onboard,
florence, xvkbd or matchbox-keyboard, whichever is installed (install
one with your package manager, e.g. `apt install onboard`).
`DT_KEYBOARD` in `~/.dtprofile` picks another. Settings >
Accessibility can show it at every login.

Tuning, in `~/.dtprofile`: `DT_TOUCH_HOLD_MS` (long-press time, default
600), `DT_TOUCH_SCROLL_PX` (finger travel per scroll step, default 40).
`DT_NO_TOUCH=1` turns the gestures off; `DT_TOUCH=1` turns them on even
when no touchscreen was found at login (for one plugged in later).
Gestures need X.Org 1.13 or newer (XInput 2.2).

## Accessibility

Settings > Accessibility. Each line is a switch: select it and press
**Change** (or double-click it).

- **Sticky keys**: press Shift, Ctrl or Alt, then the other key, instead
  of holding both. Press a modifier twice to lock it.
- **Slow keys**: a key counts only when held for a moment (avoids
  accidental presses).
- **Bounce keys**: a key pressed again right away is ignored.
- **Mouse keys**: the numeric keypad moves the pointer (5 clicks).
- **Turn these on and off from the keyboard**: press Shift five times
  for sticky keys, or hold Shift for eight seconds for slow keys.
- **Large mouse pointer**: for applications started afterwards.
- **Show the on-screen keyboard at login**.

**On-Screen Keyboard** in the same window shows or hides the keyboard
right away.

For **high contrast**, choose the *BlackWhite* or *WhiteBlack* palette in
Settings > Color. For **larger text**, use Settings > Font.

These settings work on any X server (they are the keyboard's built-in
AccessX features) and are applied again at every login.

## Colors and themes

- Settings > Color sets the palette. CDE's applications, the Front
  Panel and the window frames use it.
- **Apply colors to GTK and Qt applications** (same window) makes
  Firefox, LibreOffice, GTK and Qt applications use the palette too.
- GTK applications **change color at once**, without restarting them
  or logging out, when `xsettingsd` is installed (`apt install
  xsettingsd`). Without it they pick up the new colors when restarted.
- Window frames under xfwm4 change at once (xfwm4 restarts in the
  background; your windows stay open).
- CDE's own applications (File Manager, Text Editor, Terminal, Help,
  Settings, the Front Panel, …) change color at once too. A few icons
  drawn in the old colors, and the workspace buttons' own colors, keep
  them until the application is restarted. `DT_NO_LIVE_PALETTE=1` turns
  this off.

## Using Xfce applications

Settings > Session > **Use Xfce applications when installed** makes the
Text Editor, Terminal and Home Folder buttons (and the same actions
elsewhere) open Xfce's applications:

| CDE | Xfce |
|---|---|
| Text Editor (dtpad) | Mousepad |
| Terminal (dtterm) | xfce4-terminal |
| File Manager (dtfile) | Thunar |

They use your CDE palette, and follow palette changes live. When an Xfce
application is not installed, or the option is off, CDE's own
application opens instead, so nothing breaks. Install them with e.g.
`apt install thunar mousepad xfce4-terminal xsettingsd`.

## Laptops

- **Touchpad**: Settings > Mouse > *Touchpad*: tap to click, natural
  scrolling, two-finger scrolling, disable while typing, speed. Applied
  again when a touchpad is plugged in.
- **Keys**: brightness, volume, mute and microphone keys work.
- **Lid and sleep**: the screen locks before the computer suspends.
- **Monitors**: plugged-in and removed monitors and resolution changes
  are handled; windows stay on screen.

## Session options (~/.dtprofile)

Settings > Session writes these for you. You can also set them in
`~/.dtprofile` by hand (`NAME=value; export NAME`). They are read at
login.

| Variable | Effect |
|---|---|
| `DT_WINDOW_MANAGER=dtwm` / `xfwm4` | Choose the window manager (default: xfwm4 when installed) |
| `DT_NO_DBUS=1` | Don't start a D-Bus session bus |
| `DT_NO_XDG_AUTOSTART=1` | Don't run autostart applications |
| `DT_NO_TOUCHPAD=1` | Don't apply the touchpad settings |
| `DT_NO_LOCK_ON_SLEEP=1` | Don't lock before suspend |
| `DT_NO_COMPOSITOR=1` | Never start the compositor |
| `DT_NO_SYSTEMD_SESSION=1` | Don't start systemd's graphical session |
| `DT_ALTWM_NO_PANEL=1` | No Front Panel under other window managers |
| `DT_NO_TOUCH=1` / `DT_TOUCH=1` | Touchscreen gestures off / always on |
| `DT_TOUCH_HOLD_MS`, `DT_TOUCH_SCROLL_PX` | Tune the gestures |
| `DT_NO_SYSTRAY=1` | No system tray in the Front Panel |
| `DT_KEYBOARD=command` | The on-screen keyboard to use |
| `DT_TASKMANAGER=command` | The task manager to use |
| `DT_XFWM_GTK_THEME=name` | GTK theme for xfwm4's window menu (default CDE-wm) |
| `DTSOURCEPROFILE=true` | Also read `~/.profile` at login |
| `TT_ALLOW_REMOTE=1`, `DT_ICE_TCP=1` | Allow ToolTalk / the session manager over the network (off by default) |

## Files Clanker keeps in your home folder

| File | Written by |
|---|---|
| `~/.dtprofile` | Settings > Session (a marked block at the end; the rest is yours) |
| `~/.dt/types/clanker-panel.fp`, `.dt` | Settings > Front Panel |
| `~/.dt/types/clanker-modern.dt`, `~/.dt/modern-apps` | Settings > Session > Xfce applications |
| `~/.dt/accessibility` | Settings > Accessibility |
| `~/.dt/touchpad` | Settings > Mouse > Touchpad |
| `~/.dt/theming/env`, `~/.themes/CDE` | Settings > Color (GTK and Qt themes) |
| `~/.local/share/themes/CDE`, `CDE-wm` | Window frames and window menu under xfwm4 |
| `~/.dt/errorlog` | Messages from the desktop's programs |

Deleting one of these returns that part of the desktop to its defaults
(at the next login for `~/.dtprofile`).

## Troubleshooting

- **Something doesn't start or look right**: read `~/.dt/errorlog`.
- **The Front Panel is broken after changes**: delete
  `~/.dt/types/clanker-panel.fp` and `.dt`, then log in again (or use
  Workspace menu > Restart Workspace Manager).
- **A setting doesn't take effect**: Session options and the window
  manager are read at login; log out and in again.
- **Return to plain CDE**: put `DT_WINDOW_MANAGER=dtwm` and the
  `DT_NO_...` variables you want in `~/.dtprofile`.
