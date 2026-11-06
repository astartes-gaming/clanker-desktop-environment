/*
 * WmWallpaper.c - render workspace backdrop pictures for the screen.
 *
 * A backdrop image (XPM/XBM pattern, or PNG/JPEG photo when Motif can
 * read them) is placed on every monitor according to its image type:
 *
 *   Tiled    repeated from the top left (classic CDE patterns)
 *   Center   unscaled, centered on each monitor
 *   Fit      scaled to fit inside each monitor, aspect kept
 *   Fill     scaled to cover each monitor, aspect kept, edges cropped
 *   Stretch  scaled to each monitor's size, aspect ignored
 *
 * Scaling uses the RENDER extension with a smoothing filter; on modern
 * X servers (glamor) that runs on the GPU.  Without RENDER, or for a
 * visual RENDER has no format for, a plain CPU nearest-neighbour scale
 * is used instead, so old servers still show the picture.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifdef HAVE_CONFIG_H
#include <cde_config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include "WmGlobal.h"
#include <Dt/DtXinerama.h>
#include <X11/Xatom.h>
#if defined(USE_XRENDER)
#include <X11/extensions/Xrender.h>
#endif
#include "WmWallpaper.h"

extern DtXineramaInfo_t *DtXI;		/* WmMultiHead.c */

typedef struct { int x, y, w, h; } WpRect;

/* v / 2 rounded toward zero for negative v too (C89 leaves it open). */
static int
Half (int v)
{
    return (v >= 0) ? v / 2 : -((-v) / 2);
}

static Boolean
Intersect (const WpRect *a, const WpRect *b, WpRect *out)
{
    int x1 = (a->x > b->x) ? a->x : b->x;
    int y1 = (a->y > b->y) ? a->y : b->y;
    int x2 = (a->x + a->w < b->x + b->w) ? a->x + a->w : b->x + b->w;
    int y2 = (a->y + a->h < b->y + b->h) ? a->y + a->h : b->y + b->h;

    if (x2 <= x1 || y2 <= y1)
	return False;
    out->x = x1; out->y = y1; out->w = x2 - x1; out->h = y2 - y1;
    return True;
}

/* Where an sw x sh image goes on monitor 'head' for 'type'. */
static void
PlaceImage (int type, int sw, int sh, const WpRect *head, WpRect *r)
{
    double sx = (double) head->w / (double) sw;
    double sy = (double) head->h / (double) sh;
    double s;

    switch (type)
    {
	case DtWSM_BACKDROP_IMAGETYPE_FIT:
	    s = (sx < sy) ? sx : sy;
	    r->w = (int) (sw * s + 0.5);
	    r->h = (int) (sh * s + 0.5);
	    break;
	case DtWSM_BACKDROP_IMAGETYPE_FILL:
	    s = (sx > sy) ? sx : sy;
	    r->w = (int) (sw * s + 0.5);
	    r->h = (int) (sh * s + 0.5);
	    break;
	case DtWSM_BACKDROP_IMAGETYPE_STRETCH:
	    r->w = head->w;
	    r->h = head->h;
	    break;
	default:		/* CENTER */
	    r->w = sw;
	    r->h = sh;
	    break;
    }
    if (r->w < 1) r->w = 1;
    if (r->h < 1) r->h = 1;
    r->x = head->x + Half (head->w - r->w);
    r->y = head->y + Half (head->h - r->h);
}

#if defined(USE_XRENDER)
/*
 * Scale src (sw x sh) to the destination rectangle 'r', drawing only
 * the part inside 'clip'.  Returns False if RENDER can't do it.
 */
static Boolean
ScaleRender (Display *dpy, int screen, Pixmap src, int sw, int sh,
	     Pixmap dst, const WpRect *r, const WpRect *clip)
{
    XRenderPictFormat *fmt;
    XRenderPictureAttributes pa;
    unsigned long mask = 0;
    Picture sp, dp;
    XTransform xf;
    int ev, er;

    if (!XRenderQueryExtension (dpy, &ev, &er))
	return False;
    fmt = XRenderFindVisualFormat (dpy, DefaultVisual (dpy, screen));
    if (!fmt)
	return False;

    memset (&pa, 0, sizeof (pa));
#ifdef RepeatPad
    /* Extend edge pixels, so smoothing doesn't darken the borders. */
    pa.repeat = RepeatPad;
    mask |= CPRepeat;
#endif
    sp = XRenderCreatePicture (dpy, src, fmt, mask, &pa);
    dp = XRenderCreatePicture (dpy, dst, fmt, 0, &pa);

    memset (&xf, 0, sizeof (xf));
    xf.matrix[0][0] = XDoubleToFixed ((double) sw / (double) r->w);
    xf.matrix[1][1] = XDoubleToFixed ((double) sh / (double) r->h);
    xf.matrix[2][2] = XDoubleToFixed (1.0);
    XRenderSetPictureTransform (dpy, sp, &xf);
    XRenderSetPictureFilter (dpy, sp, FilterGood, NULL, 0);

    XRenderComposite (dpy, PictOpSrc, sp, None, dp,
		      clip->x - r->x, clip->y - r->y,	/* in scaled space */
		      0, 0,
		      clip->x, clip->y, clip->w, clip->h);

    XRenderFreePicture (dpy, sp);
    XRenderFreePicture (dpy, dp);
    return True;
}
#endif /* USE_XRENDER */

/* CPU fallback: nearest-neighbour scale through XImages. */
static void
ScaleCPU (Display *dpy, int screen, XImage *in, int sw, int sh,
	  Pixmap dst, GC gc, const WpRect *r, const WpRect *clip)
{
    XImage *out;
    int x, y, depth = DefaultDepth (dpy, screen);
    long srcX, srcY;

    out = XCreateImage (dpy, DefaultVisual (dpy, screen), depth, ZPixmap,
			0, NULL, clip->w, clip->h, 32, 0);
    if (!out)
	return;
    out->data = malloc ((size_t) out->bytes_per_line * clip->h);
    if (!out->data)
    {
	XDestroyImage (out);
	return;
    }
    for (y = 0; y < clip->h; y++)
    {
	srcY = ((long) (clip->y - r->y + y) * sh) / r->h;
	if (srcY >= sh) srcY = sh - 1;
	for (x = 0; x < clip->w; x++)
	{
	    srcX = ((long) (clip->x - r->x + x) * sw) / r->w;
	    if (srcX >= sw) srcX = sw - 1;
	    XPutPixel (out, x, y, XGetPixel (in, (int) srcX, (int) srcY));
	}
    }
    XPutImage (dpy, dst, gc, out, 0, 0, clip->x, clip->y, clip->w, clip->h);
    XDestroyImage (out);		/* frees out->data too */
}

/* The monitors, or the whole screen without Xinerama. */
static int
GetHeads (WmScreenData *pSD, WpRect *heads, int max)
{
    unsigned int i, w, h, ux, uy;
    int n = 0;

    if (!DtXI)
	DtXI = _DtXineramaInit (DISPLAY);
    if (DtXI)
    {
	for (i = 0; n < max &&
		    _DtXineramaGetScreen (DtXI, i, &w, &h, &ux, &uy); i++)
	{
	    heads[n].x = (int) ux;  heads[n].y = (int) uy;
	    heads[n].w = (int) w;   heads[n].h = (int) h;
	    n++;
	}
    }
    if (n == 0)
    {
	heads[0].x = 0;
	heads[0].y = 0;
	heads[0].w = DisplayWidth (DISPLAY, pSD->screen);
	heads[0].h = DisplayHeight (DISPLAY, pSD->screen);
	n = 1;
    }
    return n;
}

#define MAX_HEADS 16

Pixmap
WallpaperRender (WmScreenData *pSD, Pixmap src, int type,
		 Pixel fg, Pixel bg)
{
    Display	*dpy = DISPLAY;
    int		 screen = pSD->screen;
    int		 depth = DefaultDepth (dpy, screen);
    Window	 root;
    int		 x, y, i, nHeads;
    unsigned int sw, sh, bw, sdepth;
    Pixmap	 dst, tmp = None;
    GC		 gc;
    XImage	*cpuSrc = NULL;
    WpRect	 heads[MAX_HEADS], r, clip;
    Boolean	 scaled;

    if (type == DtWSM_BACKDROP_IMAGETYPE_TILED || src == None)
	return None;
    if (!XGetGeometry (dpy, src, &root, &x, &y, &sw, &sh, &bw, &sdepth) ||
	sw == 0 || sh == 0)
	return None;

    dst = XCreatePixmap (dpy, pSD->rootWindow,
			 (unsigned int) DisplayWidth (dpy, screen),
			 (unsigned int) DisplayHeight (dpy, screen),
			 (unsigned int) depth);
    gc = XCreateGC (dpy, dst, 0, NULL);
    XSetForeground (dpy, gc, bg);
    XFillRectangle (dpy, dst, gc, 0, 0,
		    (unsigned int) DisplayWidth (dpy, screen),
		    (unsigned int) DisplayHeight (dpy, screen));

    /* A bitmap: expand it to the screen depth in fg/bg first. */
    if ((int) sdepth != depth)
    {
	if (sdepth != 1)
	{
	    XFreeGC (dpy, gc);
	    XFreePixmap (dpy, dst);
	    return None;
	}
	tmp = XCreatePixmap (dpy, pSD->rootWindow, sw, sh,
			     (unsigned int) depth);
	XSetForeground (dpy, gc, fg);
	XSetBackground (dpy, gc, bg);
	XCopyPlane (dpy, src, tmp, gc, 0, 0, sw, sh, 0, 0, 1);
	src = tmp;
    }

    nHeads = GetHeads (pSD, heads, MAX_HEADS);
    for (i = 0; i < nHeads; i++)
    {
	PlaceImage (type, (int) sw, (int) sh, &heads[i], &r);
	if (!Intersect (&r, &heads[i], &clip))
	    continue;

	if (r.w == (int) sw && r.h == (int) sh)
	{
	    XCopyArea (dpy, src, dst, gc, clip.x - r.x, clip.y - r.y,
		       (unsigned int) clip.w, (unsigned int) clip.h,
		       clip.x, clip.y);
	    continue;
	}

	scaled = False;
#if defined(USE_XRENDER)
	scaled = ScaleRender (dpy, screen, src, (int) sw, (int) sh,
			      dst, &r, &clip);
#endif
	if (!scaled)
	{
	    if (!cpuSrc)
		cpuSrc = XGetImage (dpy, src, 0, 0, sw, sh, AllPlanes,
				    ZPixmap);
	    if (cpuSrc)
		ScaleCPU (dpy, screen, cpuSrc, (int) sw, (int) sh,
			  dst, gc, &r, &clip);
	}
    }

    if (cpuSrc)
	XDestroyImage (cpuSrc);
    if (tmp != None)
	XFreePixmap (dpy, tmp);
    XFreeGC (dpy, gc);
    return dst;
}

Pixmap
WallpaperCopy (WmScreenData *pSD, Pixmap src)
{
    Window	 root;
    int		 x, y;
    unsigned int w, h, bw, depth;
    Pixmap	 dst;
    GC		 gc;

    if (src == None ||
	!XGetGeometry (DISPLAY, src, &root, &x, &y, &w, &h, &bw, &depth))
	return None;
    dst = XCreatePixmap (DISPLAY, pSD->rootWindow, w, h, depth);
    gc = XCreateGC (DISPLAY, dst, 0, NULL);
    XCopyArea (DISPLAY, src, dst, gc, 0, 0, w, h, 0, 0);
    XFreeGC (DISPLAY, gc);
    return dst;
}

/*
 * Publish the current backdrop as _XROOTPMAP_ID, the convention that
 * compositors and pseudo-transparent terminals use to find the
 * wallpaper.  ESETROOT_PMAP_ID is deliberately not set: wallpaper
 * setters call XKillClient() on it, which would kill dtwm.
 */
void
WallpaperSetRootProperty (WmScreenData *pSD, Pixmap pix)
{
    static Atom xa_XROOTPMAP_ID = None;

    if (xa_XROOTPMAP_ID == None)
	xa_XROOTPMAP_ID = XInternAtom (DISPLAY, "_XROOTPMAP_ID", False);

    if (pix == None)
	XDeleteProperty (DISPLAY, pSD->rootWindow, xa_XROOTPMAP_ID);
    else
	XChangeProperty (DISPLAY, pSD->rootWindow, xa_XROOTPMAP_ID,
			 XA_PIXMAP, 32, PropModeReplace,
			 (unsigned char *) &pix, 1);
}
