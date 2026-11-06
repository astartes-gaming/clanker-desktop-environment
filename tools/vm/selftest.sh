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
        HOME=/home/$U PATH=/usr/dt/bin:/usr/bin:/bin "$@"
}
# Long-running clients (no timeout).
# Detached from the console: a background runuser on a terminal gets
# stopped by job control.
xbg() {
    setsid runuser -u $U -- env DISPLAY=:0 XAUTHORITY=/home/$U/.Xauthority \
        XDG_RUNTIME_DIR=/run/user/$uid HOME=/home/$U "$@" \
        </dev/null >/dev/null 2>&1 &
}
ok()   { echo "PASS  $*"; pass=$((pass+1)); }
bad()  { echo "FAIL  $*"; fail=$((fail+1)); }
skp()  { echo "SKIP  $*"; skip=$((skip+1)); }
check() {
    desc=$1; shift
    if "$@" >/dev/null 2>&1; then ok "$desc"; else bad "$desc"; fi
}
section() { echo; echo "== $*"; }
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

sid=`loginctl list-sessions --no-legend |
     awk -v u=$U '$3==u && $4=="seat0" {print $1}' | tail -1`
if [ -z "$sid" ]; then
    echo "No graphical session of $U: log in at the dtlogin screen first."
    exit 99
fi

#####################################################################
section "Session"
check "dtsession is running"        pgrep -u $U -x dtsession
check "dtwm is running"             pgrep -u $U -x dtwm
sp=`pgrep -u $U -x dtsession | head -1`
envf=/tmp/selftest-env.$$
tr '\0' '\n' < /proc/$sp/environ > $envf 2>/dev/null
check "session has XDG_RUNTIME_DIR (PAM env)" \
      grep -q "^XDG_RUNTIME_DIR=/run/user/$uid" $envf
check "session uses the user D-Bus" \
      grep -q "^DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$uid/bus" $envf
check "logind session is active"    sh -c "loginctl show-session $sid -p Active | grep -q yes"
check "XDG_CURRENT_DESKTOP=CDE"     grep -q '^XDG_CURRENT_DESKTOP=CDE' $envf
rm -f $envf

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
xbg xterm -geometry 40x10+300+200 -title selftest-snap -e sleep 900
sleep 3
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
section "Compositor"
if x /usr/dt/bin/dtcompositor available; then
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
section "Resources"
free -m | awk 'NR==2 {printf "      memory: %s MB used of %s MB\n", $3, $2}'
df -h / | awk 'NR==2 {printf "      disk:   %s used of %s\n", $3, $2}'

echo
echo "== $pass passed, $fail failed, $skip skipped"
exit $fail
