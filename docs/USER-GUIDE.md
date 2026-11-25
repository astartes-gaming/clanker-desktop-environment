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
- **Finding an application**: press and release the **Windows key**
  (the application menu opens above the Front Panel; the key again,
  Escape or a click elsewhere closes it), or Workspace menu >
  **Applications...**, **Alt+F2**, or the Applications drawer's **Find
  Application**. Type part of a name (or what it does: "browser",
  "music"), use Up and Down, and press Enter. Text that matches no
  application runs as a command. The menu has the same entries and
  categories as other desktops' menus (Accessories, Development,
  Education, Games, Graphics, Internet, Multimedia, Office, Settings,
  System, Other), listed on its left: click one, or press Left and
  Right while the search box is empty. CDE's own applications are
  filed there too. It lists what is installed, Flatpak and Snap
  applications included, and follows installs and removals within a
  couple of seconds, even while it is open; an entry whose program
  is gone is left out. Every entry shows its icon, found the way other
  desktops find it (icon themes, /usr/share/pixmaps); icons that come
  only as SVG are converted once with `rsvg-convert` or ImageMagick
  when either is installed (`apt install librsvg2-bin`).
- **Windows key shortcuts**: **Windows+E** Home folder, **Windows+R**
  Run, **Windows+T** Terminal, **Windows+A** Application Manager,
  **Windows+I** Settings, **Windows+L** Lock, **Windows+P** displays
  (extend, mirror, one screen), **Windows+V** clipboard history,
  **Windows+S** Search,
  **Windows+arrows** tile
  windows (dtwm). `DT_NO_SUPERKEY=1` in `~/.dtprofile` stops the key
  on its own from opening the menu.
- **Application Manager**: type in its **Search** box at the bottom to
  show only the applications (and the groups holding them) whose
  names contain the text.
- **Adding an application to a drawer**: click **Install Icon** at the
  top of an open drawer and choose the application (Enter or a
  double-click), or drag one from the application menu or the
  Application Manager onto **Install Icon**.
- **Screenshots**: **Print** saves the whole screen, **Alt+Print** the
  active window, as a PNG in your Pictures folder (with scrot, maim,
  ImageMagick or xwd, whichever is installed).
- **Notifications** from applications (mail, downloads, chat, low
  battery) appear at the top right, in your palette when
  [dunst](https://dunst-project.org) is installed (`apt install
  dunst`); with Xfce applications chosen, Xfce's notifications are
  used. Otherwise the first installed notification program runs.
  `~/.config/dunst/dunstrc` of your own is used as is. `DT_NOTIFY` in
  `~/.dtprofile` picks another program, `DT_NO_NOTIFY=1` none.
- **Volume and brightness keys** show the new level in a small popup.
- **Clipboard history**: **Windows+V** lists what you copied recently;
  pick an entry to paste it. It uses CopyQ, Diodon, Clipman, ClipIt
  or Parcellite, whichever is installed (`apt install copyq`), started
  at login. Copied text also survives closing the program it came
  from. `DT_NO_CLIPBOARD=1` turns this off.
- **Workspace menu**: right-click the desktop for applications, Lock
  Screen, Suspend, Restart and Shut Down.
- **Settings**: everything you can change (it was called Style Manager in
  CDE). Open it from the Front Panel's Settings button.
- **File Manager**, **Text Editor**, **Terminal**, **Calendar**,
  **Mail**, **Help**: CDE's own applications, or Xfce's if you choose
  (see [Using Xfce applications](#using-xfce-applications)).

## Starting up

To install, use a package from the project's releases page (`.deb`
for Debian and Ubuntu, `.rpm` for Fedora, or a tarball), or build it
yourself; the README explains both.  Afterwards, choose "CDE" on your
login screen.

While the desktop starts, a splash screen shows the CDE logo, the
desktop's name and a moving bar on a night-sky background, with the
copyright notice below; it disappears when the window manager takes
over. Displays with 256 colors or fewer, and `DT_HELLO_CLASSIC=1` in
`~/.dtprofile`, keep the classic blue screen.

## The Front Panel

| Part | What it does |
|---|---|
| Clock, calendar, Home Folder, Text Editor, Mail | Start those applications. Click the small arrow above a button to open its drawer; it closes when you start something from it, click the arrow again, or click the desktop. |
| Lock (padlock) | Locks the screen. |
| Search (below the lock) | Search applications, settings, windows, files and the web; calculate; run a command (see [Search](#search)). Accessibility is in the Settings drawer. |
| Workspace buttons | Switch workspaces. Double-click a name to rename it. |
| Light above Exit | Blinks while an application starts. Click it to open the **Task Manager**. |
| Exit | Logs out. |
| Printer, Settings, Applications, Help, Trash | Start those. |
| Settings drawer (arrow above Settings) | Volume up and down, mute, screen brightness up and down, the sound settings (pavucontrol, alsamixer… whichever is installed), and Accessibility. |
| System tray (right end) | Icons of running applications (network, sound, chat, …). Click or right-click them as usual. |

**Moving the panel**: drag it by the handles at either end. It reserves
its screen edge only while it sits on one.
Settings > Front Panel > **Position** puts it at the top or bottom of
the primary monitor at every login, or leaves it where you dragged it.

**Hiding the panel**: Settings > Front Panel > **Hide automatically**.
The panel slides off its edge (top or bottom) a moment after the pointer
leaves it, unless a drawer or menu is open, and comes back when the
pointer touches that edge. While hidden it doesn't reserve the edge, so
maximized windows use the whole screen.

**Choosing what a button opens**: Settings > Front Panel. Select a
button, press **Change...**, and pick an installed application or type
a command. **Reset** puts the original back.

**Drawers**: in the same window, each button is followed by the items
of its drawer.

- **Add to Drawer...** adds an application to the selected button's
  drawer. A button without a drawer gets one. Type part of the name to narrow the
  list of installed applications, then press Enter.
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
| Session | Options in `~/.dtprofile` (below), Xfce or KDE applications, the terminal, and games in Gamescope (when installed) |
| Accessibility | Sticky, slow and bounce keys, mouse keys, large pointer, on-screen keyboard at login |
| Shortcuts | Your own keyboard shortcuts: **Add...**, press the keys (with Ctrl, Alt or Super, or a function key), type the command. They work in every application, also with xfwm4. The desktop's own shortcuts are listed below yours; one of yours with the same keys replaces it (e.g. your own Windows+E). Kept in `~/.dt/shortcuts.dtwmrc`. |
| Input | Keyboard layouts (add, remove; **Windows+Space**, Alt+Shift or Ctrl+Shift switches between them), Caps Lock as Ctrl or Escape, a Compose key for accents |
| Controllers | Game controllers that are connected (with their battery when reported): **Test...** (jstest-gtk), **Buttons and Mouse...** (AntiMicroX: use a controller as a mouse and keyboard, remap buttons), **Pair...** (Bluetooth) |
| Sound | Output and input devices: pick the one in use, volume, mute, the mixer (PipeWire or PulseAudio) |
| Displays | Monitors: extend, mirror, only one, primary, **Arrange...** (resolution, rotation, position); remembered per set of monitors. **Night light**: warmer colors in the evening, easier on the eyes; **Night Hours...** and **Warmth...** set when and how warm (with gammastep or redshift if installed, else the monitors' gamma) |
| Borders | Window frames: which title bar buttons (minimize, maximize, close), border width (thicker is easier to grab) |
| Search | What the Front Panel's **Search** finds (below) |
| Users | Your account: password, full name, picture (`~/.face`, shown by login screens); **Other Users...** opens the system's user tool |
| Region | Language, and separately date/time, number, currency and paper formats (from the next login, kept in `~/.dt/region`); time zone; setting the clock from the Internet |
| Drives | USB drives, memory cards and discs: mount them when inserted, open a File Manager window, notify. Programs on them are never run |
| Network, Bluetooth, Updates | Wi-Fi; **VPN...** (turn VPN and WireGuard connections on and off), **Airplane Mode** (all radios off, or back on); Bluetooth devices; system updates (shown when the system supports them). With [topgrade](https://github.com/topgrade-rs/topgrade) installed, Updates also has **Update Everything**: Flatpak, Snap, firmware and more |

Changes take effect at once, except the Session options, which are used
at the next login.

## Search

The magnifier button on the Front Panel, or **Windows+S**, opens
**Search**, like Spotlight or KRunner. (The magnifier is where the
Accessibility button was; Accessibility is now in the Settings drawer
and in Settings.) Type, then pick a row with Up/Down and Enter:

- **Applications**, by name, what they are ("browser") or keywords.
- A **calculation**: `2*(3+4)` shows `= 14`; Enter copies the result
  (with `xclip` or `xsel` installed; otherwise it goes in the field).
  **Units** too: `10 km in mi`, `100 f to c`, `2 gb to mib`,
  `5 lb to kg` (length, mass, volume, data, time, speed, temperature).
- **Settings pages** by what they set ("wallpaper", "wifi", "layout").
- **Open windows**, by title: Enter switches to it.
- **Recent files**, and **files and folders by name**, searched in the
  background: with KDE's Baloo index, plocate or locate when present,
  otherwise the home folder five levels deep (hidden folders skipped).
- **Search the web** with your engine (DuckDuckGo, Google, Bing,
  Startpage, Brave Search, Ecosia, Qwant, Wikipedia).
- **Run** what you typed, when it starts with a program.

Settings > Search turns each of these on or off, and sets the engine,
the file search tool and how many rows show. The button again, Escape,
or clicking elsewhere closes it. It is `dtappfind -search`; the
settings are in `~/.dt/search`.

## Windows

- **Title bar buttons**: on the right, minimize, maximize and **close
  (X)**, as on other desktops; the button on the left opens the window
  menu.
- **Tiling**: drag a window to the left or right edge to fill that half,
  to the top to maximize. **Super+Left/Right** tiles, **Super+Up**
  maximizes, **Super+Down** restores. Double-click a title bar to
  maximize.
- **Switching windows**: hold **Alt** and press **Tab** to see the open
  windows; each Tab moves down (Shift+Tab up), releasing Alt switches,
  Escape cancels. A quick Alt+Tab goes back to the previous window.
  (With xfwm4, xfwm4's own switcher does this.)
- **Window menu**: the button at the left of each title bar (or
  **Alt+Space**): maximize, minimize, move, resize, always on top, move
  to another workspace, close.
- **Window manager**: when xfwm4 (Xfce's window manager) is installed it
  manages the windows, with CDE-style frames and window menu in your
  palette. Without it, or with `DT_WINDOW_MANAGER=dtwm`, CDE's dtwm
  does everything as before. Settings > Session > *Window manager*
  switches between them (next login).

## Multiple monitors

- **Plugging in a monitor** turns it on at once, to the right of the
  others, as on other desktops. Change the arrangement once and it is
  remembered for that set of monitors: plugging the same ones in again
  (or logging in with them) brings it back. Unplugging a monitor moves
  its windows, and the Front Panel if it was there, to the others.
- **Windows+P** switches between *Extend* (side by side), *Mirror* (the
  same picture everywhere, for projectors), *Built-in display only* and
  *External display only*, showing which in a notification.
- **Displays** in the application menu (Settings) opens the display
  settings of KDE, Xfce or LXQt, lxrandr or arandr, whichever is
  installed, for resolution, refresh rate, rotation, position and the
  primary monitor; the result is remembered as above.
- The **Front Panel** starts on the primary monitor, at its bottom
  middle. Window tiling, maximizing and the wallpaper work per monitor.
- With [autorandr](https://github.com/phillipberndt/autorandr) set up
  (`~/.config/autorandr`), its profiles are used instead.
  `DT_NO_DISPLAYS=1` in `~/.dtprofile` leaves the monitors alone.

## Games

- Steam, Lutris, Heroic and other games are in the application menu
  under **Games**.
- **Gamescope**: with [gamescope](https://github.com/ValveSoftware/gamescope)
  installed, Settings > Session > **Games** can start the games in the
  menu inside it, at the size and refresh rate of the monitor the
  pointer is on. Gamescope gives a game its own compositor: FSR
  upscaling, a frame limiter and tear-free fullscreen.
  `DT_GAMESCOPE_ARGS` in `~/.dtprofile` adds options, for example
  `-F fsr -w 1280 -h 720` (draw at 720p, upscale to the monitor).
  From a terminal: `dtgamescope COMMAND`.
- Steam itself isn't wrapped. For a Steam game, put
  `gamescope -f -- %command%` in the game's *Launch Options* in Steam.

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

With a touchscreen, CDE's applications also get **larger controls**
(scroll bars, menus, buttons, list rows) that are easier to hit with a
finger; Settings > Accessibility turns them off or on.

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
- **Larger controls for touch**: wider scroll bars, taller menu items,
  buttons and list rows, bigger check boxes, for applications started
  afterwards. On when a touchscreen is found at login, unless you turn
  it off here.

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
  LibreOffice, GTK and Qt applications use the palette too:
  - **Qt and KDE applications** (Konsole, Dolphin, Kate, Konqueror,
    VLC…) follow the CDE GTK theme through Qt's GTK platform theme, or
    use qt5ct/qt6ct with the CDE palette when those are installed. A
    `QT_QPA_PLATFORMTHEME` of your own in `~/.dtprofile` wins. A "CDE"
    color scheme is also written for KDE, so Plasma's Colors settings
    can use the palette too if you want it there.
  - **Firefox and LibreWolf**: their tab strip, menus, dialogs,
    scroll bars and form controls take the palette (a marked line in
    each profile's `user.js` turns off Firefox's libadwaita colors; it
    is set back when you untick the box). Firefox's address bar row
    keeps its own light or dark color.
  - **Chrome, Chromium, Brave, Edge, Vivaldi and Opera** (also from
    Flatpak): at login each profile that uses the browser's default
    theme is switched to *GTK* (Settings > Appearance > Theme in the
    browser), so the window takes the palette. A profile with a theme
    from the Web Store is left alone, and a browser that is open is
    done at the next login. Unticking the box sets back what was
    there. Electron applications (VS Code, Discord…) follow GTK too.
  - **Flatpak applications** are allowed to read the theme
    (`flatpak override --user`, read-only, done once).
- GTK applications **change color at once**, without restarting them
  or logging out, when `xsettingsd` is installed (`apt install
  xsettingsd`). Without it they pick up the new colors when restarted.
- **CDE's icons everywhere**: Settings > Session > **Icons** switches GTK
  applications (and Qt ones that follow GTK, see above) and the
  application menu to CDE's own icons: folders, the home folder,
  terminal, editor, calculator, mail, trash, printer, help, and Motif
  versions of Firefox, Thunderbird, GIMP, VLC, LibreOffice and others.
  Icons CDE has no picture for come from your usual theme. The theme
  is made once in `~/.local/share/icons/CDE` (`dticontheme`); it needs
  `xsettingsd` for running applications to follow.
- Window frames under xfwm4 change at once (xfwm4 restarts in the
  background; your windows stay open).
- CDE's own applications (File Manager, Text Editor, Terminal, Help,
  Settings, the Front Panel, …) change color at once too. A few icons
  drawn in the old colors, and the workspace buttons' own colors, keep
  them until the application is restarted. `DT_NO_LIVE_PALETTE=1` turns
  this off.

## The terminal

The Terminal button and every action that opens a terminal run
`dtterminal`, which picks **kitty**, then **Ghostty**, then KDE's
**Konsole**, then the classic **dtterm**, whichever is installed
(dtterm on machines with less than 768 MB of memory, where the others
are too heavy; choosing one in Settings still uses it). kitty and
Ghostty get the current CDE palette: the text-area color as background, the
active-window color for the cursor, selection and current tab, flat
Motif-style tabs, and the window manager's Motif frame instead of
their own title bar. The colors follow the palette each time a
terminal opens (`~/.dt/terminal-theme/`); fonts, keys and other
settings in your own `kitty.conf` or Ghostty config stay as they are.
Konsole keeps your own profile and only takes a "CDE" color scheme
made from the palette.

- **Choose the terminal:** Settings > Session > *Terminal* (Change
  cycles through Automatic and the installed ones of kitty, Ghostty,
  Konsole and dtterm), or write the name to `~/.dt/terminal`.
- **Their own colors instead:** `DT_TERMINAL_THEME=no` in
  `~/.dtprofile`.
- Old actions that use dtterm-only options (`-C`, `-xrm`, `-sb` …)
  still open dtterm. If kitty or Ghostty cannot start (no OpenGL, as
  on very old graphics), dtterm opens instead.
- Install: `apt install kitty` or `apt install konsole` (Ghostty
  comes from its own packages).

## Using Xfce or KDE applications

Settings > Session > **Applications** chooses which desktop's
applications the Text Editor, Terminal and Home Folder buttons (and
the same actions elsewhere) open. Change offers only the sets that
have something installed:

| CDE | Xfce | KDE |
|---|---|---|
| Text Editor (dtpad) | Mousepad | Kate (or KWrite) |
| Terminal (dtterm) | xfce4-terminal | Konsole |
| File Manager (dtfile) | Thunar | Dolphin |
| Mail (dtmail) | Thunderbird, Evolution, Geary, Claws Mail | KMail (or Thunderbird) |
| Calendar (dtcm) | Orage, GNOME Calendar, Evolution, Thunderbird | KOrganizer, Merkuro |
| Calculator (dtcalc) | Galculator, GNOME Calculator, MATE's, Qalculate | KCalc, Kalk |
| Print Manager (dtprintinfo) | system-config-printer | KDE's printer settings |

For mail, calendar and calculator the first one installed is used (the
"Xfce" column also covers the GNOME and MATE ones). If you chose a set
before this version, choose it again once so the new buttons follow it.

The Xfce applications use your CDE palette and follow palette changes
live; Konsole gets the CDE color scheme. When the chosen application is
not installed, CDE's own opens instead, so nothing breaks. Install them
with e.g. `apt install thunar mousepad xfce4-terminal xsettingsd` or
`apt install dolphin kate konsole`.

### KDE applications in a CDE session

When KDE Frameworks are installed, the CDE session presents itself to
KDE programs the way a Plasma session does (`KDE_FULL_SESSION`), while
the menu and autostart still follow CDE's own entries:

- Konsole, Dolphin, Kate and the rest use your KDE settings, KDE file
  dialogs, and "Open with" lists the installed applications.
- Chromium and Electron programs (Chrome, Brave, VS Code, Claude,
  Discord...) keep saved logins in **KWallet**, as in Plasma, so moving
  between Plasma and CDE no longer asks you to sign in again. When you
  log in through CDE's own login screen, KWallet and GNOME Keyring are
  unlocked with your password.
- Files open with KDE's opener (`xdg-open` uses `kde-open`).

`DT_NO_KDE=1` in `~/.dtprofile` turns this off.

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
| `DT_NO_DISPLAYS=1` | Don't turn on or arrange monitors when they are plugged in |
| `DT_GAMESCOPE_ARGS=...` | More gamescope options for games started in Gamescope |
| `DT_NOTIFY=command` / `DT_NO_NOTIFY=1` | The notification program to use / none |
| `DT_NO_CLIPBOARD=1` | No clipboard manager at login |
| `DTSOURCEPROFILE=true` | Also read `~/.profile` at login |
| `TT_ALLOW_REMOTE=1`, `DT_ICE_TCP=1` | Allow ToolTalk / the session manager over the network (off by default) |

## Files Clanker keeps in your home folder

| File | Written by |
|---|---|
| `~/.dtprofile` | Settings > Session (a marked block at the end; the rest is yours) |
| `~/.dt/types/clanker-panel.fp`, `.dt` | Settings > Front Panel |
| `~/.dt/types/clanker-modern.dt`, `~/.dt/modern-apps` | Settings > Session > Applications (Xfce or KDE) |
| `~/.dt/gamescope` | Settings > Session > Games: in Gamescope |
| `~/.dt/displays/` | Monitor arrangements, one file per set of connected monitors |
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
- **A browser or Electron app (Claude, VS Code...) asks you to sign
  in again after using another desktop** (TDE, a window manager on its
  own): each desktop made it keep logins in a different place. Start it
  with `--password-store=kwallet6` everywhere (KWallet, as Plasma and
  CDE use): copy its entry from `/usr/share/applications` to
  `~/.local/share/applications` and add the option after the program
  on its `Exec=` lines.
- **Calendar says "rpc.cmsd is not responding"**: the calendar server
  isn't running. Systems without inetd (most current Linux) start it
  with systemd; once, as root:
  `systemctl enable --now /usr/dt/config/systemd/dtcmsd.service`.
  It only answers this computer; add `-n` to its `ExecStart` line to
  share calendars over the network.
- **Two Wi-Fi icons, one floating; "an authentication agent already
  exists"**: older versions saved tray applets and agents in the
  session and started them a second time at login. Now programs that
  start at login anyway (autostart entries, the window manager) are
  neither saved nor restored, so this clears up by itself at the next
  login.
- **Return to plain CDE**: put `DT_WINDOW_MANAGER=dtwm` and the
  `DT_NO_...` variables you want in `~/.dtprofile`.
