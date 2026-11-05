/*
 * Touchpad.h - touchpad settings through XInput device properties.
 *
 * Shared by dtstyle (Mouse dialog) and dttouchpad (applies the saved
 * settings at login and whenever a touchpad appears).  Works with the
 * X.Org libinput and synaptics drivers.  Everything is detected at run
 * time: without the XInput extension, without device properties
 * (XI < 1.5) or without a touchpad, TpProbe() returns 0 and nothing
 * else does anything.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifndef _Touchpad_h
#define _Touchpad_h

#include <X11/Xlib.h>

/* Capability bits returned by TpProbe() */
#define TP_CAP_TAP      (1 << 0)
#define TP_CAP_NATURAL  (1 << 1)
#define TP_CAP_SCROLL   (1 << 2)	/* two-finger vs edge */
#define TP_CAP_DWT      (1 << 3)	/* disable while typing */
#define TP_CAP_SPEED    (1 << 4)

#define TP_UNSET        (-1000)		/* leave the driver's value alone */

#define TP_SCROLL_TWOFINGER 1
#define TP_SCROLL_EDGE      2

typedef struct {
    int tapping;	/* 0, 1 or TP_UNSET */
    int natural;	/* 0, 1 or TP_UNSET */
    int scroll;		/* TP_SCROLL_*  or TP_UNSET */
    int dwt;		/* 0, 1 or TP_UNSET */
    int speed;		/* -100..100    or TP_UNSET */
} TpSettings;

extern void TpClear(TpSettings *s);
extern int  TpProbe(Display *dpy);
extern void TpGetCurrent(Display *dpy, TpSettings *s);
extern void TpGetDefaults(Display *dpy, TpSettings *s);
extern void TpApply(Display *dpy, const TpSettings *s);
extern int  TpLoad(TpSettings *s);
extern int  TpSave(const TpSettings *s);

#endif /* _Touchpad_h */
