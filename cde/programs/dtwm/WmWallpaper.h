/*
 * WmWallpaper.h - render workspace backdrop pictures for the screen.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifndef _WmWallpaper_h
#define _WmWallpaper_h

#include "WmGlobal.h"

/* A new screen-sized pixmap with src placed on every monitor per
 * 'type' (DtWsmBackdropImageType), or None for Tiled / on failure.
 * The caller owns the result (XFreePixmap). */
extern Pixmap WallpaperRender (WmScreenData *pSD, Pixmap src, int type,
			       Pixel fg, Pixel bg);

/* A private copy of src (caller owns it), or None. */
extern Pixmap WallpaperCopy (WmScreenData *pSD, Pixmap src);

/* Set (or with None, delete) _XROOTPMAP_ID on the root window. */
extern void WallpaperSetRootProperty (WmScreenData *pSD, Pixmap pix);

#endif /* _WmWallpaper_h */
