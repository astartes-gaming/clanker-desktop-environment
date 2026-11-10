#!/bin/sh
#####################################################################
##  selftest.sh - end-to-end checks of a running Clanker/CDE session.
##
##  Run INSIDE the test VM as root while the test user is logged in
##  at the dtlogin screen (tools/vm/README.md):
##
##      sh /root/payload/selftest.sh [PASSWORD]
##
##  PASSWORD is the test user's; without it the screen-lock and
##  suspend checks are skipped.  Every check prints PASS, FAIL or SKIP;
##  the exit status is the number of failures.  The checks change the
##  session (wallpaper, touchpad file, resolution) and restore what
##  they reasonably can.
#####################################################################

U=${SELFTEST_USER:-tester}
PW=$1
P=$(cd "$(dirname "$0")" && pwd)
uid=`id -u $U`
pass=0; fail=0; skip=0

x() {
    timeout 30 runuser -u $U -- env DISPLAY=:0 \
        XAUTHORITY=/home/$U/.Xauthority XDG_RUNTIME_DIR=/run/user/$uid \
        DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$uid/bus \
        HOME=/home/$U PATH=/usr/dt/bin:/usr/bin:/bin "$@"
}
# Long-running clients (no timeout).
# Detached from the console: a background runuser on a terminal gets
# stopped by job control.
xbg() {
    setsid runuser -u $U -- env DISPLAY=:0 XAUTHORITY=/home/$U/.Xauthority \
        XDG_RUNTIME_DIR=/run/user/$uid \
        DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$uid/bus HOME=/home/$U "$@" \
        </dev/null >/dev/null 2>&1 &
}
ok()   { echo "PASS  $*"; pass=$((pass+1)); }
bad()  { echo "FAIL  $*"; fail=$((fail+1)); }
skp()  { echo "SKIP  $*"; skip=$((skip+1)); }
check() {
    desc=$1; shift
    if "$@" >/dev/null 2>&1; then ok "$desc"; else bad "$desc"; fi
}
section() {
    echo; echo "== $*"
    # SELFTEST_TRACE=1: how many client windows are visible, to spot a
    # section that hides other applications' windows.
    [ -n "$SELFTEST_TRACE" ] &&
        echo "      (visible windows: `x xdotool search --onlyvisible --name . 2>/dev/null | wc -l`)"
}
locked() { x xwininfo -name coverDialog 2>/dev/null | grep -q IsViewable; }
notlocked() { ! locked; }
tp_probe() { x /usr/dt/bin/dttouchpad -probe | grep -q 'capabilities=.*tapping'; }
prop_on() { x xinput list-props "$1" | grep "$2 (" | grep -q '1$'; }
geom() {        # geom TITLE -> "x y w h" of the client window
    x xwininfo -name "$1" 2>/dev/null | awk '
        /Absolute upper-left X/ {X=$4} /Absolute upper-left Y/ {Y=$4}
        /Width:/ {W=$2} /Height:/ {H=$2} END {print X, Y, W, H}'
}
screen_size() { x xdpyinfo | awk '/dimensions:/ {print $2; exit}'; }

# The user's X11 session on seat0 (not a text console session).
sid=
for s in `loginctl list-sessions --no-legend |
          awk -v u=$U '$3==u && $4=="seat0" {print $1}'`; do
    [ "`loginctl show-session $s -p Type --value 2>/dev/null`" = x11 ] &&
        [ "`loginctl show-session $s -p State --value 2>/dev/null`" != closing ] &&
        sid=$s
done
if [ -z "$sid" ]; then
    echo "No graphical session of $U: log in at the dtlogin screen first."
    exit 99
fi

#####################################################################
section "Session"
check "dtsession is running"        pgrep -u $U -x dtsession
sp=`pgrep -u $U -x dtsession | head -1`
# The session's window manager: xfwm4 when installed (dtwm then only
# draws the Front Panel), or dtwm (DT_WINDOW_MANAGER=dtwm).
WM=`tr '\0' '\n' < /proc/$sp/environ 2>/dev/null |
    sed -n 's/^DT_WINDOW_MANAGER=//p'`
WM=`basename "${WM:-dtwm}"`
if [ "$WM" = dtwm ]; then
    check "dtwm is running"         pgrep -u $U -x dtwm
else
    check "$WM manages the windows"  pgrep -u $U -x "$WM"
    check "dtwm -panelonly draws the Front Panel" \
          pgrep -u $U -f 'dtwm -panelonly'
fi
envf=/tmp/selftest-env.$$
tr '\0' '\n' < /proc/$sp/environ > $envf 2>/dev/null
check "session has XDG_RUNTIME_DIR (PAM env)" \
      grep -q "^XDG_RUNTIME_DIR=/run/user/$uid" $envf
check "session uses the user D-Bus" \
      grep -q "^DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$uid/bus" $envf
check "logind session is active"    sh -c "loginctl show-session $sid -p Active | grep -q yes"
check "XDG_CURRENT_DESKTOP=CDE"     grep -q '^XDG_CURRENT_DESKTOP=CDE' $envf
rm -f $envf
# Loading the action database must not warn (long EXEC_STRINGs were
# cut at 1024 bytes; cpp left CDE_INSTALLATION_TOP in quoted commands).
# The database is parsed when a client starts, so look at what the
# running dtfile logged then.
elog=/home/$U/.dt/errorlog
dbwarn=0
for p in `pgrep -u $U -x dtfile`; do
    n=`grep -A4 "dtfile: PID $p:" $elog 2>/dev/null | grep -c 'unrecognized field'`
    dbwarn=`expr $dbwarn + $n`
done
check "the action database loads without warnings ($dbwarn)" test "$dbwarn" = 0
check "installed actions have real paths" \
      sh -c "! grep -l 'CDE_INSTALLATION_TOP\|CDE_CONFIGURATION_TOP' /usr/dt/appconfig/types/C/*.dt"

#####################################################################
section "Security and hardening"
check "dtappgather is not setuid" sh -c "! test -u /usr/dt/bin/dtappgather"
check "appmanager directory is sticky and shared" \
      sh -c "stat -c %a /var/dt/appconfig/appmanager | grep -q '^1777$'"
check "the user's App Manager directory exists" \
      sh -c "ls -l /var/dt/appconfig/appmanager | grep -q ' $U '"
if command -v readelf >/dev/null 2>&1; then
    check "dtwm is built with full RELRO" \
          sh -c "readelf -d /usr/dt/bin/dtwm | grep -q BIND_NOW"
    check "dtsession uses stack protection" \
          sh -c "readelf --dyn-syms /usr/dt/bin/dtsession | grep -q __stack_chk_fail"
else
    skp "binary hardening (install binutils to check)"
fi

#####################################################################
section "systemd session"
units() { x systemctl --user is-active "$@"; }
check "cde-session.target is active"       units cde-session.target
check "graphical-session.target is active" units graphical-session.target
check "power actions are installed"        test -x /usr/dt/bin/dtpower

#####################################################################
section "Network exposure"
open=`ss -ltnpH | grep -E 'dt|ttsession|rpc.ttdbserver' |
      awk '{print $4}' | grep -vE '^(127\.0\.0\.1|\[::1\]):' | head -3`
if [ -z "$open" ]; then ok "CDE TCP listeners are loopback-only"
else bad "CDE listens on: $open"; fi
check "XDMCP (udp/177) is off"      sh -c "! ss -lunH | grep -q ':177 '"
check "ToolTalk session is running" pgrep -u $U -x ttsession

#####################################################################
section "Touchpad"
if [ -f "$P/touchpad.py" ] && python3 -c 'import evdev' 2>/dev/null; then
    rm -f /home/$U/.dt/touchpad
    setsid python3 "$P/touchpad.py" "Selftest TouchPad" </dev/null >/dev/null 2>&1 &
    sleep 3
    check "dttouchpad sees the touchpad" tp_probe
    printf 'tapping=1\nnatural_scrolling=1\n' > /home/$U/.dt/touchpad
    chown $U: /home/$U/.dt/touchpad
    setsid python3 "$P/touchpad.py" "Selftest Hotplug" </dev/null >/dev/null 2>&1 &
    sleep 4
    check "hot-plugged touchpad gets tap-to-click" \
          prop_on "Selftest Hotplug" "libinput Tapping Enabled"
    check "hot-plugged touchpad gets natural scrolling" \
          prop_on "Selftest Hotplug" "libinput Natural Scrolling Enabled"
    pkill -f "touchpad.py Selftest" 2>/dev/null
    rm -f /home/$U/.dt/touchpad
else
    skp "touchpad (python3-evdev or touchpad.py missing)"
fi

#####################################################################
section "Laptop keys"
hk=/usr/dt/bin/dthwkeys
cp $hk $hk.selftest
printf '#!/bin/sh\necho "$*" >> /tmp/selftest-keys.log\n' > $hk
rm -f /tmp/selftest-keys.log
x xdotool key XF86AudioRaiseVolume XF86AudioMute XF86MonBrightnessUp
sleep 2
check "volume keys reach dthwkeys" grep -q volume-up /tmp/selftest-keys.log
check "brightness keys reach dthwkeys" grep -q brightness-up /tmp/selftest-keys.log
mv $hk.selftest $hk
check "no key-binding warnings in errorlog" \
      sh -c "! grep -q 'Invalid key specification' /home/$U/.dt/errorlog"

#####################################################################
section "Window snapping"
# Also the test window of the RandR section, which closes it.
xbg xterm -geometry 40x10+300+200 -title selftest-snap -e sleep 900
sleep 3
if [ "$WM" != dtwm ]; then
    skp "window snapping (a dtwm feature; $WM manages the windows)"
else
set -- `geom selftest-snap`
if [ -n "$3" ]; then
    x xdotool mousemove $(($1 + 20)) $(($2 + 20)) click 1
    sleep 1
    sw=`screen_size | cut -dx -f1`
    x xdotool key super+Left; sleep 2
    set -- `geom selftest-snap`
    check "Super+Left tiles to the left half" \
          test "$1" -lt 20 -a "$3" -gt $((sw * 4 / 10)) -a "$3" -le $((sw / 2))
    x xdotool key super+Up; sleep 2
    set -- `geom selftest-snap`
    check "Super+Up maximizes"      test "$3" -gt $((sw * 9 / 10))
    x xdotool key super+Down; sleep 1
else
    bad "test window did not appear"
fi
fi

#####################################################################
section "Wallpaper"
wp=/home/$U/.dt/backdrops/selftest.png
wsm=/tmp/selftest-wsmbackdrop
[ -x "$P/wsmbackdrop" ] && install -m 755 "$P/wsmbackdrop" $wsm
if [ -x $wsm ] && [ -f "$P/selftest.png" ]; then
    install -d -o $U /home/$U/.dt/backdrops
    install -o $U -m 644 "$P/selftest.png" $wp
    x $wsm set $wp 3            # Fill
    sleep 4
    size=`screen_size`
    info=`x $wsm info`
    check "Fill wallpaper is screen-sized ($info)" \
          sh -c "echo '$info' | grep -q '^$size '"
    check "RGB PNG keeps its colours (centre 20,120,60)" \
          sh -c "echo '$info' | awk '{split(\$2,c,\",\"); exit !(c[1]<40 && c[2]>100 && c[2]<140 && c[3]<80)}'"
else
    skp "wallpaper (wsmbackdrop or selftest.png missing)"
fi

#####################################################################
section "Resolution change (RandR)"
out=`x xrandr | awk '/ connected/ {print $1; exit}'`
mode=`x xrandr | awk '/\*/ {print $1; exit}'`
if [ -n "$out" ] && x xrandr | grep -q '^ *1024x768'; then
    x xrandr --output $out --mode 1024x768; sleep 5
    set -- `geom selftest-snap` 0 0 0 0
    check "windows stay on screen at 1024x768" \
          test "$3" -gt 0 -a "$1" -ge 0 -a $(($1 + $3)) -le 1024 -a $(($2 + $4)) -le 768
    if [ -x $wsm ]; then
        info=`x $wsm info`
        check "wallpaper re-rendered for 1024x768 ($info)" \
              sh -c "echo '$info' | grep -q '^1024x768 '"
    fi
    x xrandr --output $out --mode $mode; sleep 4
else
    skp "RandR (no 1024x768 mode)"
fi
pkill -u $U -f 'title selftest-snap' 2>/dev/null

#####################################################################
section "Screen lock"
if [ -n "$PW" ]; then
    loginctl lock-session $sid; sleep 4
    check "loginctl lock-session locks the screen" locked
    x xdotool key shift; sleep 1
    x xdotool type --delay 80 "wrong-password"; x xdotool key Return; sleep 5
    check "a wrong password is refused" locked
    x xdotool type --delay 80 "$PW"; x xdotool key Return; sleep 5
    check "the right password unlocks after a wrong one" notlocked

    check "a sleep inhibitor is held for locking" \
          sh -c "systemd-inhibit --list --no-pager | grep -q 'Lock the screen before sleeping'"
    if command -v rtcwake >/dev/null 2>&1 &&
       rtcwake -m no -s 15 >/dev/null 2>&1; then
        # through logind, like closing the lid; the RTC alarm wakes us
        systemctl suspend
        sleep 25
        check "the screen is locked after suspend/resume" locked
        x xdotool key shift; sleep 1
        x xdotool type --delay 80 "$PW"; x xdotool key Return; sleep 5
        check "unlocks after resume" notlocked
    else
        skp "suspend (no rtcwake to wake the VM)"
    fi
else
    skp "screen lock and suspend (no password given)"
fi

#####################################################################
section "Screen saver inhibit (D-Bus)"
# dtsession answers org.freedesktop.ScreenSaver, which browsers and
# video players call to keep the screen from locking during playback.
ssq() {
    x dbus-send --session --print-reply=literal \
        --dest=org.freedesktop.ScreenSaver /org/freedesktop/ScreenSaver \
        org.freedesktop.ScreenSaver."$@" 2>/dev/null | awk '{print $2}'
}
unlock() {
    x xdotool key shift; sleep 2
    if locked; then
        x xdotool type --delay 80 "$PW"; x xdotool key Return; sleep 5
    fi
}
sspid=`x dbus-send --session --print-reply=literal \
        --dest=org.freedesktop.DBus /org/freedesktop/DBus \
        org.freedesktop.DBus.GetConnectionUnixProcessID \
        string:org.freedesktop.ScreenSaver 2>/dev/null | awk '{print $2}'`
check "dtsession owns org.freedesktop.ScreenSaver (pid $sspid)" \
      test "$sspid" = "`pgrep -u $U -x dtsession | head -1`"
active=`ssq GetActive`
check "GetActive is false while unlocked ($active)" test "$active" = false
idle=`ssq GetSessionIdleTime`
check "GetSessionIdleTime answers ($idle s)" test -n "$idle"
ssi=/tmp/selftest-ssinhibit
[ -x "$P/ssinhibit" ] && install -m 755 "$P/ssinhibit" $ssi
if [ -n "$PW" ] && [ -x $ssi ]; then
    saved=`x xset q | awk '/timeout:/ {print $2}' | sed -n 1p`
    x xset s 5 0                # saver after 5 idle seconds
    xbg $ssi 15; sleep 12       # held for 15 s, then it leaves the bus
    check "an inhibit keeps the screen saver and lock off" notlocked
    sleep 13
    check "the screen locks once the inhibiting application exits" locked
    active=`ssq GetActive`
    check "GetActive is true while the screen is covered ($active)" \
          test "$active" = true
    unlock
    check "unlocks after the saver" notlocked

    xbg $ssi -u 8; sleep 15     # released at 8 s, still on the bus
    check "the screen locks after UnInhibit" locked
    unlock
    pkill -u $U -f selftest-ssinhibit
    x xset s "${saved:-600}" 0
else
    skp "inhibit (no password given, or ssinhibit not built)"
fi

#####################################################################
section "Show in folder (D-Bus FileManager1)"
# Browsers call org.freedesktop.FileManager1 for "Show in folder".
fm1() {
    x dbus-send --session --print-reply --dest=org.freedesktop.FileManager1 \
        /org/freedesktop/FileManager1 org.freedesktop.FileManager1."$1" \
        array:string:"$2" string: >/dev/null 2>&1
}
# dtfile keeps closed views as unmapped windows for reuse: only count
# visible ones.
fmwins() { x xdotool search --onlyvisible --name "^File Manager - $1\$" 2>/dev/null; }
fmwin() { test -n "`fmwins "$1"`"; }
nofmwin() { ! fmwin "$1"; }
fmclose() {         # close with the window manager, only once focused
    for w in `fmwins "$1"`; do
        x xdotool windowactivate --sync $w >/dev/null 2>&1
        [ "`x xdotool getactivewindow 2>/dev/null`" = "$w" ] &&
            x xdotool key alt+F4
        sleep 2
    done
}
fmpid=`x dbus-send --session --print-reply=literal \
        --dest=org.freedesktop.DBus /org/freedesktop/DBus \
        org.freedesktop.DBus.GetConnectionUnixProcessID \
        string:org.freedesktop.FileManager1 2>/dev/null | awk '{print $2}'`
check "dtfile owns org.freedesktop.FileManager1 (pid $fmpid)" \
      test "`ps -o comm= -p ${fmpid:-0}`" = dtfile
d=/home/$U/selftest-fm
rm -rf $d; x mkdir -p "$d/show folder"; x touch "$d/item one.txt"
check "ShowFolders answers" fm1 ShowFolders "file://$d/show%20folder"
sleep 4
check "ShowFolders opens a view of the folder" fmwin "show folder"
fmclose "show folder"
check "ShowItems answers" fm1 ShowItems "file://localhost$d/item%20one.txt"
sleep 4
check "ShowItems opens the item's folder" fmwin selftest-fm
fm1 ShowItems "file://$d/show%20folder"; sleep 3
n=`fmwins selftest-fm | wc -l`
check "ShowItems reuses the open view ($n window)" test "$n" = 1
fmclose selftest-fm
fm1 ShowFolders "http://example.com/$d"
fm1 ShowFolders "file://not-this-host$d"
sleep 3
check "non-local URIs are ignored" nofmwin selftest-fm
check "dtfile is still running" pgrep -u $U -x dtfile
rm -rf $d

#####################################################################
section "Compositor"
if [ "$WM" != dtwm ]; then
    skp "compositor ($WM composites itself)"
elif x /usr/dt/bin/dtcompositor available; then
    x /usr/dt/bin/dtcompositor start
    check "compositor started"      x /usr/dt/bin/dtcompositor status
    echo "      (`pgrep -a -u $U -x picom || pgrep -a -u $U -x xcompmgr`)"
    xbg xterm -geometry 40x10+200+150 -title selftest-drag -e sleep 900
    sleep 3
    set -- `geom selftest-drag`
    if [ -n "$1" ]; then
        x xdotool mousemove $(($1 + 60)) $(($2 - 12)) mousedown 1 \
            sleep 0.3 mousemove $(($1 + 40)) $(($2 + 40)) \
            sleep 0.3 mousemove $(($1 + 260)) $(($2 + 120)) sleep 0.5 \
            mouseup 1
        rc=$?
        sleep 1
        set -- `geom selftest-drag` 0
        check "outline drag works without a server grab" \
              test $rc -eq 0 -a "$1" -gt 300
    fi
    pkill -u $U -f 'title selftest-drag' 2>/dev/null
    x /usr/dt/bin/dtcompositor stop
else
    skp "compositor (none installed)"
fi

#####################################################################
section "Front Panel under $WM"
if [ "$WM" = dtwm ]; then
    skp "the session runs dtwm"
else
    root() { x xprop -root "$1" | sed 's/.* = //'; }
    dtcur() {
        x xprop -id `x xprop -root _MOTIF_WM_INFO | awk '{print $NF}'` \
            _DT_WORKSPACE_CURRENT | awk '{print $NF}'
    }
    fp=`x xdotool search --classname FrontPanel 2>/dev/null | while read w; do
        x xprop -id $w _NET_WM_WINDOW_TYPE | grep -q DOCK && echo $w; done |
        head -1`
    check "the Front Panel is a dock" test -n "$fp"
    viewable() { x xwininfo -id "$1" 2>/dev/null | grep -q IsViewable; }
    check "the panel is visible" viewable ${fp:-0}
    nd=`root _NET_NUMBER_OF_DESKTOPS`
    check "$WM has the 4 workspaces as desktops ($nd)" test "$nd" = 4
    names() { x xprop -root _NET_DESKTOP_NAMES | grep -q '"One", "Two"'; }
    check "desktop names are the workspace names" names
    x xdotool set_desktop 2; sleep 2
    check "switching in $WM switches the panel (`dtcur`)" test "`dtcur`" = ws2
    # The "Two" button of the default panel, relative to its corner
    set -- `x xwininfo -id ${fp:-0} | awk '/Absolute upper-left [XY]/ {print $4}'`
    if [ -n "$2" ]; then
        x xdotool mousemove $(($1 + 568)) $(($2 + 20)) click 1; sleep 2
        check "the panel's workspace buttons switch $WM (`root _NET_CURRENT_DESKTOP`)" \
              test "`root _NET_CURRENT_DESKTOP`" = 1
    fi
    x xdotool set_desktop 0; sleep 2
    # The menu is an override-redirect window without a class.
    before=`x xdotool search --onlyvisible --name '' | wc -l`
    x xdotool mousemove 300 300 click 3; sleep 2
    after=`x xdotool search --onlyvisible --name '' | wc -l`
    check "right-clicking the desktop posts the Workspace menu" \
          test "$after" -gt "$before"
    x xdotool key Escape; sleep 1

    # Drag the panel by its left handle: up, then back to the bottom.
    fppos() { x xwininfo -id ${fp:-0} | awk '/Absolute upper-left X/ {x=$4} /Absolute upper-left Y/ {y=$4} END {print x, y}'; }
    fpstrut() { x xprop -id ${fp:-0} _NET_WM_STRUT | awk -F', ' '{print $4}'; }
    fpdrag() {          # fpdrag DY: drag the left handle DY pixels down
        dy=$1
        set -- `fppos`
        hx=$(($1 + 5)); hy=$(($2 + 40))
        x xdotool mousemove $hx $hy mousedown 1 sleep 0.3 \
            mousemove $hx $((hy + dy / 2)) sleep 0.2 \
            mousemove $hx $((hy + dy)) sleep 0.3 mouseup 1
        sleep 2
    }
    set -- `fppos`; y0=$2
    fpdrag -300
    set -- `fppos`; s=`fpstrut`
    check "the panel's handle moves it (y $y0 -> $2)" test "$2" -lt "$y0"
    check "a panel off the screen edge reserves no space ($s)" test "$s" = 0
    fpdrag 600
    set -- `fppos`; s=`fpstrut`
    check "dragged back, it sits on the bottom edge (y $2)" test "$2" = "$y0"
    check "and reserves its height again ($s)" test "$s" -gt 0
fi

#####################################################################
section "System tray, Task Manager, window menu"
# A test icon docks into the Front Panel's tray.
if x python3 -c 'import ctypes' 2>/dev/null; then
    # (the test user cannot read this directory)
    cp $P/trayicon.py /tmp/selftest-trayicon.py; chmod 644 /tmp/selftest-trayicon.py
    out=`x python3 /tmp/selftest-trayicon.py 2266CC 3 2>&1`
    rm -f /tmp/selftest-trayicon.py
    check "the Front Panel owns the system tray" \
          sh -c "echo '$out' | grep -q 'tray owner 0x[1-9a-f]'"
    check "a tray icon docks into the panel" \
          sh -c "echo '$out' | grep -q '^docked'"
else
    skp "system tray (no python3)"
fi
# The busy light, above Exit, opens a task manager.
fp=`x xdotool search --classname FrontPanel 2>/dev/null | while read w; do
    x xprop -id $w _NET_WM_WINDOW_TYPE | grep -q DOCK && echo $w; done | head -1`
set -- `x xwininfo -id ${fp:-0} 2>/dev/null | awk '/Absolute upper-left [XY]/ {print $4}'`
if [ -n "$2" ]; then
    x xdotool mousemove $(($1 + 649)) $(($2 + 15)) click 1; sleep 6
    tm=`x xdotool search --onlyvisible --name '^Task Manager' 2>/dev/null | head -1`
    check "the busy light opens the Task Manager" test -n "$tm"
    [ -n "$tm" ] && x xdotool windowclose $tm 2>/dev/null
    [ -n "$tm" ] && x xdotool windowkill $tm 2>/dev/null
else
    skp "busy light (no Front Panel)"
fi
if [ "$WM" = xfwm4 ]; then
    wmpid=`pgrep -u $U -x xfwm4 | head -1`
    check "xfwm4's window menu uses the CDE-wm stylesheet" \
          sh -c "tr '\\0' '\\n' < /proc/$wmpid/environ | grep -qx GTK_THEME=CDE-wm"
    check "the stylesheet is in the palette's colors" \
          grep -q '@define-color cde_bg #' \
          /home/$U/.local/share/themes/CDE-wm/gtk-3.0/gtk.css
fi

#####################################################################
section "Touchscreen gestures"
# A virtual touchscreen (uinput) plays a tap, a long press, a two-finger
# drag and a one-finger drag over a window that logs its mouse buttons.
if [ ! -x /usr/dt/bin/dttouch ]; then
    skp "touchscreen gestures (dttouch not built: no XInput 2.2)"
elif ! python3 -c 'import evdev' 2>/dev/null; then
    skp "touchscreen gestures (python3-evdev missing)"
else
    cp $P/touchscreen.py $P/buttonlog.py /tmp/; chmod 644 /tmp/touchscreen.py /tmp/buttonlog.py
    sz=`screen_size`
    # (real axis ranges: libinput may reject a device whose range is 0..0)
    python3 /tmp/touchscreen.py ${sz%x*} ${sz#*x} wait:15000 >/dev/null 2>&1 &
    tsdev=$!
    # the X server adds the new device a moment later
    probe() { i=0; while [ $i -lt 8 ]; do
        x /usr/dt/bin/dttouch -probe && return 0; sleep 1; i=$((i+1)); done
        return 1; }
    check "dttouch finds the touchscreen" probe
    kill $tsdev 2>/dev/null; wait $tsdev 2>/dev/null
    pgrep -u $U -x dttouch >/dev/null || xbg /usr/dt/bin/dttouch
    sleep 1
    bl=/tmp/selftest-buttons.log; rm -f $bl
    xbg sh -c "python3 /tmp/buttonlog.py 200 200 500 350 25 > $bl 2>&1"
    sleep 3
    python3 /tmp/touchscreen.py ${sz%x*} ${sz#*x} tap:400:350 wait:500 \
        hold:420:360:1200 wait:500 drag2:450:450:0:-200 wait:500 \
        drag1:450:300:0:100 >/dev/null 2>&1
    sleep 2
    n() { grep -c "^press $1\$" $bl 2>/dev/null; }
    check "a tap clicks (button 1)" test "`n 1`" -ge 1
    check "a long press is a right click (button 3)" test "`n 3`" = 1
    check "two fingers scroll (button 5: `n 5`)" test "`n 5`" -ge 3
    check "a one-finger drag reaches the window as a drag" test "`n 1`" = 2
    rm -f /tmp/touchscreen.py /tmp/buttonlog.py $bl
fi

#####################################################################
section "Resources"
free -m | awk 'NR==2 {printf "      memory: %s MB used of %s MB\n", $3, $2}'
df -h / | awk 'NR==2 {printf "      disk:   %s used of %s\n", $3, $2}'

echo
echo "== $pass passed, $fail failed, $skip skipped"
exit $fail
