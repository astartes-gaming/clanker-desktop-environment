#!/usr/bin/env python3
# trayicon.py - a minimal XEMBED system tray icon for selftest.sh (ctypes,
# no toolkit): asks the tray to dock a colored 22x22 window, prints
# "docked" or "NOT docked", then stays for SECONDS.
# Usage: trayicon.py [RRGGBB] [SECONDS]
import ctypes, ctypes.util, sys, time
X = ctypes.cdll.LoadLibrary(ctypes.util.find_library('X11'))
X.XOpenDisplay.restype = ctypes.c_void_p
for f in ('XDefaultRootWindow', 'XInternAtom', 'XGetSelectionOwner', 'XCreateSimpleWindow'):
    getattr(X, f).restype = ctypes.c_ulong
d = ctypes.c_void_p(X.XOpenDisplay(None))
root = X.XDefaultRootWindow(d)
A = lambda n: ctypes.c_ulong(X.XInternAtom(d, n, 0))
color = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0xCC2222
w = X.XCreateSimpleWindow(d, ctypes.c_ulong(root), 0, 0, 22, 22, 0, ctypes.c_ulong(0), ctypes.c_ulong(color))
info = (ctypes.c_long * 2)(0, 1)
X.XChangeProperty(d, ctypes.c_ulong(w), A(b'_XEMBED_INFO'), A(b'_XEMBED_INFO'), 32, 0, info, 2)
owner = X.XGetSelectionOwner(d, A(b'_NET_SYSTEM_TRAY_S0'))
print('tray owner', hex(owner))
class CM(ctypes.Structure):
    _fields_ = [('type', ctypes.c_int), ('serial', ctypes.c_ulong), ('send_event', ctypes.c_int),
                ('display', ctypes.c_void_p), ('window', ctypes.c_ulong), ('message_type', ctypes.c_ulong),
                ('format', ctypes.c_int), ('l', ctypes.c_long * 5), ('pad', ctypes.c_long * 12)]
ev = CM(); ev.type = 33; ev.window = owner; ev.message_type = A(b'_NET_SYSTEM_TRAY_OPCODE').value
ev.format = 32; ev.l[0] = 0; ev.l[1] = 0; ev.l[2] = w
X.XSendEvent(d, ctypes.c_ulong(owner), 0, 0, ctypes.byref(ev)); X.XFlush(d)
time.sleep(2)
r = ctypes.c_ulong(); p = ctypes.c_ulong(); ch = ctypes.c_void_p(); n = ctypes.c_uint()
X.XQueryTree(d, ctypes.c_ulong(w), ctypes.byref(r), ctypes.byref(p), ctypes.byref(ch), ctypes.byref(n))
print('docked' if p.value != root else 'NOT docked', hex(p.value)); sys.stdout.flush()
time.sleep(float(sys.argv[2]) if len(sys.argv) > 2 else 15)
