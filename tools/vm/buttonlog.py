#!/usr/bin/python3
"""A test window that prints the mouse buttons it gets (ctypes Xlib).

buttonlog.py X Y W H SECONDS   -> prints "press N" per ButtonPress
"""
import ctypes, ctypes.util, sys, time
X = ctypes.cdll.LoadLibrary(ctypes.util.find_library('X11'))
X.XOpenDisplay.restype = ctypes.c_void_p
X.XDefaultRootWindow.restype = ctypes.c_ulong
X.XCreateSimpleWindow.restype = ctypes.c_ulong
x, y, w, h, secs = [int(a) for a in sys.argv[1:6]]
d = ctypes.c_void_p(X.XOpenDisplay(None))
win = X.XCreateSimpleWindow(d, ctypes.c_ulong(X.XDefaultRootWindow(d)), x, y, w, h, 0,
                            ctypes.c_ulong(0), ctypes.c_ulong(0x88AACC))
X.XStoreName(d, ctypes.c_ulong(win), b'buttonlog')
X.XSelectInput(d, ctypes.c_ulong(win), 1 << 2)      # ButtonPressMask
X.XMapWindow(d, ctypes.c_ulong(win))
X.XMoveWindow(d, ctypes.c_ulong(win), x, y)
X.XFlush(d)
class BE(ctypes.Structure):
    _fields_ = [('type', ctypes.c_int), ('serial', ctypes.c_ulong), ('send_event', ctypes.c_int),
                ('display', ctypes.c_void_p), ('window', ctypes.c_ulong), ('root', ctypes.c_ulong),
                ('subwindow', ctypes.c_ulong), ('time', ctypes.c_ulong), ('x', ctypes.c_int),
                ('y', ctypes.c_int), ('x_root', ctypes.c_int), ('y_root', ctypes.c_int),
                ('state', ctypes.c_uint), ('button', ctypes.c_uint), ('pad', ctypes.c_long * 16)]
ev = BE(); end = time.time() + secs
while time.time() < end:
    while X.XPending(d):
        X.XNextEvent(d, ctypes.byref(ev))
        if ev.type == 4:
            print('press', ev.button, flush=True)
    time.sleep(0.02)
