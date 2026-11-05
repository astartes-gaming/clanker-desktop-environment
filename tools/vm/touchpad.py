#!/usr/bin/python3
"""Create a virtual touchpad (uinput) for the VM self-test: touchpad.py NAME"""
import sys, time
from evdev import UInput, AbsInfo, ecodes as e
name = sys.argv[1] if len(sys.argv) > 1 else 'Clanker Test TouchPad'
cap = {
    e.EV_KEY: [e.BTN_LEFT, e.BTN_TOOL_FINGER, e.BTN_TOUCH,
               e.BTN_TOOL_DOUBLETAP, e.BTN_TOOL_TRIPLETAP],
    e.EV_ABS: [
        (e.ABS_X, AbsInfo(0, 0, 1200, 0, 0, 12)),
        (e.ABS_Y, AbsInfo(0, 0, 800, 0, 0, 12)),
        (e.ABS_MT_SLOT, AbsInfo(0, 0, 4, 0, 0, 0)),
        (e.ABS_MT_POSITION_X, AbsInfo(0, 0, 1200, 0, 0, 12)),
        (e.ABS_MT_POSITION_Y, AbsInfo(0, 0, 800, 0, 0, 12)),
        (e.ABS_MT_TRACKING_ID, AbsInfo(0, 0, 65535, 0, 0, 0)),
    ],
}
ui = UInput(cap, name=name, input_props=[e.INPUT_PROP_POINTER, e.INPUT_PROP_BUTTONPAD])
time.sleep(1000000)
