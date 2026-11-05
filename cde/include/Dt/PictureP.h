/*
 * PictureP.h - load backdrop pictures (private to CDE).
 *
 * Motif's XmGetPixmap reads XPM, XBM and, in Motif 2.3, PNG and JPEG.
 * Its PNG reader only handles RGBA files correctly: plain RGB, palette
 * and greyscale PNGs come out with wrong colours.  These functions read
 * PNG files with libpng themselves (when CDE was built with it) and
 * leave every other format to Motif.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifndef _Dt_PictureP_h
#define _Dt_PictureP_h

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Like XmGetPixmap(): a pixmap of the screen depth for image 'name'
 * (a full path, or a name found through the icon search path), or
 * XmUNSPECIFIED_PIXMAP.  Free it with _DtDestroyPicturePixmap(). */
extern Pixmap _DtGetPicturePixmap(Screen *screen, char *name,
                                  Pixel fg, Pixel bg);

/* Free a pixmap from _DtGetPicturePixmap() (or XmGetPixmap()). */
extern void _DtDestroyPicturePixmap(Screen *screen, Pixmap pixmap);

#ifdef __cplusplus
}
#endif

#endif /* _Dt_PictureP_h */
