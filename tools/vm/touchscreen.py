#!/usr/bin/python3
"""A virtual touchscreen (uinput) for the VM self-test.

touchscreen.py W H GESTURE...   (screen size, then gestures in order)
  tap:X:Y             a quick tap
  hold:X:Y:MS         a finger held still for MS milliseconds
  drag2:X:Y:DX:DY     two fingers (10 px apart) moved by DX,DY
  drag1:X:Y:DX:DY     one finger moved by DX,DY
  wait:MS
"""
import sys, time
from evdev import UInput, AbsInfo, ecodes as e

W, H = int(sys.argv[1]), int(sys.argv[2])
cap = {
    e.EV_KEY: [e.BTN_TOUCH],
    e.EV_ABS: [
        (e.ABS_X, AbsInfo(0, 0, W - 1, 0, 0, 10)),
        (e.ABS_Y, AbsInfo(0, 0, H - 1, 0, 0, 10)),
        (e.ABS_MT_SLOT, AbsInfo(0, 0, 4, 0, 0, 0)),
        (e.ABS_MT_POSITION_X, AbsInfo(0, 0, W - 1, 0, 0, 10)),
        (e.ABS_MT_POSITION_Y, AbsInfo(0, 0, H - 1, 0, 0, 10)),
        (e.ABS_MT_TRACKING_ID, AbsInfo(0, 0, 65535, 0, 0, 0)),
    ],
}
ui = UInput(cap, name='Clanker Test TouchScreen', input_props=[e.INPUT_PROP_DIRECT])
time.sleep(2)               # let the X server add the device
tid = [100]

def down(slot, x, y):
    tid[0] += 1
    ui.write(e.EV_ABS, e.ABS_MT_SLOT, slot)
    ui.write(e.EV_ABS, e.ABS_MT_TRACKING_ID, tid[0])
    ui.write(e.EV_ABS, e.ABS_MT_POSITION_X, x)
    ui.write(e.EV_ABS, e.ABS_MT_POSITION_Y, y)

def move(slot, x, y):
    ui.write(e.EV_ABS, e.ABS_MT_SLOT, slot)
    ui.write(e.EV_ABS, e.ABS_MT_POSITION_X, x)
    ui.write(e.EV_ABS, e.ABS_MT_POSITION_Y, y)

def up(slot):
    ui.write(e.EV_ABS, e.ABS_MT_SLOT, slot)
    ui.write(e.EV_ABS, e.ABS_MT_TRACKING_ID, -1)

def frame(x, y, touch):
    ui.write(e.EV_KEY, e.BTN_TOUCH, 1 if touch else 0)
    ui.write(e.EV_ABS, e.ABS_X, x)
    ui.write(e.EV_ABS, e.ABS_Y, y)
    ui.syn()

for g in sys.argv[3:]:
    a = g.split(':'); k = a[0]; v = [int(n) for n in a[1:]]
    if k == 'tap':
        down(0, v[0], v[1]); frame(v[0], v[1], 1); time.sleep(0.08)
        up(0); frame(v[0], v[1], 0)
    elif k == 'hold':
        down(0, v[0], v[1]); frame(v[0], v[1], 1); time.sleep(v[2] / 1000)
        up(0); frame(v[0], v[1], 0)
    elif k in ('drag1', 'drag2'):
        x, y, dx, dy = v; two = k == 'drag2'
        down(0, x, y)
        if two: down(1, x + 10, y)
        frame(x, y, 1)
        for i in range(1, 21):
            cx, cy = x + dx * i // 20, y + dy * i // 20
            move(0, cx, cy)
            if two: move(1, cx + 10, cy)
            frame(cx, cy, 1); time.sleep(0.02)
        up(0)
        if two: up(1)
        frame(x + dx, y + dy, 0)
    elif k == 'wait':
        time.sleep(v[0] / 1000)
    time.sleep(0.3)
time.sleep(1)
