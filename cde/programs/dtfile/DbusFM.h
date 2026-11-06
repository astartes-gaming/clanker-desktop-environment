/*
 * DbusFM.h - dtfile's services on the D-Bus session bus.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifndef _DbusFM_h
#define _DbusFM_h

#include <X11/Intrinsic.h>

/* Offer org.freedesktop.FileManager1 ("Show in folder").  Does nothing
 * without D-Bus support, without a session bus, in restricted mode, or
 * when another file manager already owns the name. */
extern void DtfileDbusInit(Widget top);

#endif /* _DbusFM_h */
