/*
 * SmDbus.h - dtsession's services on the D-Bus session bus.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifndef _SmDbus_h
#define _SmDbus_h

#include <X11/Intrinsic.h>

/* Connect to the session bus and offer org.freedesktop.ScreenSaver.
 * Does nothing without D-Bus support or a running bus. */
extern void SmDbusInit(void);

/* The screen saver or lock covered (True) or uncovered (False) the
 * screen; emits ActiveChanged. */
extern void SmDbusScreenActive(Boolean active);

#endif /* _SmDbus_h */
