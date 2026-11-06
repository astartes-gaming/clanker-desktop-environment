#!/bin/sh
#####################################################################
##  selftest-wayland.sh - check the CDE Wayland session (dtwayland).
##
##  Run INSIDE the test VM as root (cage, xwayland and xauth
##  installed; no login needed):
##
##      sh /root/payload/selftest-wayland.sh
##
##  Starts "dtwayland" as the test user with cage's headless backend,
##  so no display, seat or console login is involved, checks the
##  session inside it, then ends it.  Stops dtlogin (and the X11
##  session) first.  Exit status: number of failures.
#####################################################################

U=${SELFTEST_USER:-tester}
uid=`id -u $U`
pass=0; fail=0; skip=0
ok()   { echo "PASS  $*"; pass=$((pass+1)); }
bad()  { echo "FAIL  $*"; fail=$((fail+1)); }
skp()  { echo "SKIP  $*"; skip=$((skip+1)); }
check() {
    desc=$1; shift
    if "$@" >/dev/null 2>&1; then ok "$desc"; else bad "$desc"; fi
}

for p in cage Xwayland xauth; do
    if ! command -v $p >/dev/null 2>&1; then
        skp "Wayland session ($p is not installed)"
        echo; echo "== $pass passed, $fail failed, $skip skipped"; exit 0
    fi
done

echo "== Wayland session (cage headless)"

# One CDE session at a time: the X11 login screen and session go away.
if systemctl is-active -q dtlogin 2>/dev/null; then
    systemctl stop dtlogin; sleep 3
fi
pkill -u $U -x dtsession 2>/dev/null; sleep 2

# A private runtime directory, so this works without a login session.
rt=/tmp/selftest-wl-run
rm -rf $rt; mkdir -m 700 $rt; chown $U $rt
log=/tmp/selftest-wl.log
setsid runuser -u $U -- env -i HOME=/home/$U USER=$U LOGNAME=$U \
    SHELL=/bin/bash PATH=/usr/dt/bin:/usr/bin:/bin LANG=C.UTF-8 \
    XDG_RUNTIME_DIR=$rt WLR_BACKENDS=headless WLR_RENDERER=pixman \
    WLR_LIBINPUT_NO_DEVICES=1 \
    /usr/dt/bin/dtwayland > $log 2>&1 < /dev/null &

# Wait for the session manager inside it.
i=0
while [ $i -lt 60 ]; do
    pgrep -u $U -f 'dtwayland --inside' >/dev/null &&
        pgrep -u $U -x dtsession >/dev/null && break
    sleep 1; i=$((i+1))
done
sleep 15

check "cage runs dtwayland" pgrep -u $U -x cage
xw=`pgrep -u $U -a -x Xwayland`
check "rootful Xwayland runs ($xw)" test -n "$xw"
check "Xwayland does not listen on TCP" \
      sh -c "echo '$xw' | grep -q -- '-nolisten tcp'"
d=`echo "$xw" | sed -n 's/.*Xwayland \(:[0-9]*\).*/\1/p'`
xa=$rt/dtwayland-Xauthority.${d#:}
check "the session has a private X cookie ($xa)" test -s $xa

x() {
    runuser -u $U -- env DISPLAY=$d XAUTHORITY=$xa HOME=/home/$U \
        PATH=/usr/dt/bin:/usr/bin:/bin "$@"
}
check "clients with the cookie get in" x xdpyinfo
check "clients without it are refused" \
      sh -c "! runuser -u $U -- env DISPLAY=$d XAUTHORITY=/dev/null xdpyinfo"
size=`x xdpyinfo 2>/dev/null | awk '/dimensions:/ {print $2}'`
check "the X screen fills the output, not 640x480 ($size)" \
      sh -c "test -n '$size' && test '$size' != 640x480"
check "dtsession runs" pgrep -u $U -x dtsession
check "a window manager runs" \
      sh -c "pgrep -u $U -x xfwm4 || pgrep -u $U -x dtwm"
sleep 10
check "the session's File Manager opened" \
      sh -c "runuser -u $U -- env DISPLAY=$d XAUTHORITY=$xa xdotool search --onlyvisible --name '^File Manager' | grep -q ."

# End the session the way logging out does: dtsession exits, then
# dtwayland stops Xwayland and removes the cookie.
pkill -u $U -x dtsession
i=0
while [ $i -lt 20 ] && pgrep -u $U -x cage >/dev/null; do sleep 1; i=$((i+1)); done
check "the compositor ends with the session" sh -c "! pgrep -u $U -x cage"
check "the cookie is removed" test ! -e $xa
pkill -u $U -x cage 2>/dev/null
rm -rf $rt

echo
echo "== $pass passed, $fail failed, $skip skipped"
exit $fail
