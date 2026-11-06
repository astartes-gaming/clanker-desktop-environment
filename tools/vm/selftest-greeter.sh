#!/bin/sh
#####################################################################
##  selftest-greeter.sh - check dtlogin's login screen and SIGHUP.
##
##  Run INSIDE the test VM as root:
##
##      sh /root/payload/selftest-greeter.sh
##
##  Restarts dtlogin (ending any X11 session), then sends SIGHUP to
##  the display's dtlogin, to the master dtlogin, and a burst of five,
##  and checks that a single login screen comes back each time.
##  Exit status: number of failures.
#####################################################################

pass=0; fail=0
ok()  { echo "PASS  $*"; pass=$((pass+1)); }
bad() { echo "FAIL  $*"; fail=$((fail+1)); }
greeter() {
    i=0
    while [ $i -lt 40 ]; do
        g=`pgrep -x dtgreet` && { echo $g; return 0; }
        sleep 1; i=$((i+1))
    done
    return 1
}

echo "== Login screen and SIGHUP"
systemctl restart dtlogin; sleep 5
g1=`greeter` && ok "the login screen is up" || bad "no login screen"
master=`pgrep -f '^/usr/dt/bin/dtlogin'`
child=`pgrep -f 'dtlogin <:0>'`

kill -HUP $child; sleep 3
g2=`greeter`
if [ -n "$g2" ] && [ "$g2" != "$g1" ]; then
    ok "SIGHUP to the display's dtlogin gives a new login screen"
else
    bad "after SIGHUP to the display's dtlogin: greeter '$g2', was '$g1'"
fi
kill -0 $master 2>/dev/null && ok "the master dtlogin still runs" ||
    bad "the master dtlogin died"

kill -HUP $master; sleep 4
greeter >/dev/null && ok "after SIGHUP to the master, a login screen is up" ||
    bad "no login screen after SIGHUP to the master"

c=`pgrep -f 'dtlogin <:0>'`
for n in 1 2 3 4 5; do kill -HUP $c 2>/dev/null; done; sleep 6
greeter >/dev/null && ok "after five SIGHUPs in a row, a login screen is up" ||
    bad "no login screen after five SIGHUPs"
n=`pgrep -x dtgreet | wc -l`
[ "$n" = 1 ] && ok "exactly one login screen" || bad "$n login screens"
z=`ps -eo stat,comm | awk '$1 ~ /^Z/ && $2 ~ /dtlogin|dtgreet/' | wc -l`
[ "$z" = 0 ] && ok "no zombie dtlogin or dtgreet" || bad "$z zombies"
x=`pgrep -x Xorg | wc -l`
[ "$x" = 1 ] && ok "one X server" || bad "$x X servers"

echo
echo "== $pass passed, $fail failed"
exit $fail
