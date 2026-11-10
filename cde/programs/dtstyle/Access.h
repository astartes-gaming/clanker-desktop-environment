/*
 * Access.h - accessibility settings (Settings > Accessibility, dtaccess).
 */
#ifndef _Access_h
#define _Access_h

#include <X11/Xlib.h>

enum {
    ACC_STICKY,		/* sticky keys: modifiers stay pressed */
    ACC_SLOW,		/* slow keys: keys must be held a moment */
    ACC_BOUNCE,		/* bounce keys: quick repeats are ignored */
    ACC_MOUSEKEYS,	/* the keypad moves the pointer */
    ACC_TOGGLES,	/* Shift x5 / held Shift turn these on and off */
    ACC_BIGCURSOR,	/* a large mouse pointer (new applications) */
    ACC_KEYBOARD,	/* show the on-screen keyboard at login */
    ACC_TOUCHSIZE,	/* larger controls for fingers (new applications) */
    ACC_COUNT
};

extern const char *const AccessKeys[ACC_COUNT];

/* Read ~/.dt/accessibility (missing entries are off). */
void AccessRead(const char *home, int on[ACC_COUNT]);
/* Write it; 0 on failure. */
int  AccessWrite(const char *home, const int on[ACC_COUNT]);
/* Apply the keyboard and pointer settings to the display. */
void AccessApply(Display *dpy, const int on[ACC_COUNT]);

#endif
