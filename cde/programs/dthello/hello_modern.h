/*
 * hello_modern.h - the true-color startup splash (hello_modern.c).
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifndef HELLO_MODERN_H
#define HELLO_MODERN_H

#include <X11/Intrinsic.h>

/* True when the display can show it (and DT_HELLO_CLASSIC is unset). */
Boolean ModernSplashInit(XtAppContext app, const char *title,
                         const char *status);
void    ModernSplashPaint(void);	/* draw everything (on Expose) */
void    ModernSplashStart(void);	/* start the busy bar */
Boolean ModernSplashOn(void);

#endif
