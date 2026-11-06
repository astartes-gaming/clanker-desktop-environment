/*
 * Picture.c - load backdrop pictures; see <Dt/PictureP.h>.
 *
 * PNG files are decoded with libpng (HAVE_LIBPNG) into an XImage of
 * the screen's TrueColor visual and uploaded once.  Anything else, or
 * PNG on a non-TrueColor visual or without libpng, goes to Motif.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifdef HAVE_CONFIG_H
#include <cde_config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <Xm/Xm.h>
#include <Xm/IconFile.h>
#include <Dt/PictureP.h>

#ifdef HAVE_LIBPNG
#include <png.h>

#define MAX_PIXELS (64L * 1024L * 1024L)	/* refuse absurd images */

static Boolean
HasPngSuffix(const char *name)
{
    size_t len = strlen(name);
    const char *s;

    if (len < 5)
        return False;
    s = name + len - 4;
    return (s[0] == '.' &&
            (s[1] == 'p' || s[1] == 'P') &&
            (s[2] == 'n' || s[2] == 'N') &&
            (s[3] == 'g' || s[3] == 'G'));
}

/* Lowest set bit and width of a visual's colour mask. */
static void
MaskInfo(unsigned long mask, int *shift, int *bits)
{
    *shift = 0;
    *bits = 0;
    if (!mask)
        return;
    while (!(mask & 1)) { mask >>= 1; (*shift)++; }
    while (mask & 1)    { mask >>= 1; (*bits)++; }
}

static Pixmap
LoadPng(Screen *screen, const char *path)
{
    Display        *dpy = DisplayOfScreen(screen);
    Visual         *vis = DefaultVisualOfScreen(screen);
    int             depth = DefaultDepthOfScreen(screen);
    FILE           *fp;
    png_structp     png;
    png_infop       info;
    png_uint_32     w, h, x, y;
    int             bitDepth, colorType;
    unsigned char   sig[8];
    unsigned char * volatile data = NULL;
    png_bytep     * volatile rows = NULL;
    XImage         *img;
    Pixmap          pix;
    GC              gc;
    int             rs, rb, gs, gb, bs, bb;
    size_t          rowbytes;
    unsigned char  *p;
    unsigned long   r, g, b;

#if defined(__cplusplus) || defined(c_plusplus)
    if (vis->c_class != TrueColor && vis->c_class != DirectColor)
#else
    if (vis->class != TrueColor && vis->class != DirectColor)
#endif
        return None;

    if ((fp = fopen(path, "rb")) == NULL)
        return None;
    if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8) != 0)
    {
        fclose(fp);
        return None;
    }
    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info)
    {
        if (png)
            png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return None;
    }
    if (setjmp(png_jmpbuf(png)))
    {
        /* libpng error: clean up whatever was allocated */
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        free(rows);
        free(data);
        return None;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);
    png_get_IHDR(png, info, &w, &h, &bitDepth, &colorType, NULL, NULL, NULL);
    if (w == 0 || h == 0 || (long) w * (long) h > MAX_PIXELS)
        png_error(png, "image too large");

    /* Whatever the file holds, read 8-bit RGB. */
    png_set_expand(png);
    png_set_strip_16(png);
    png_set_strip_alpha(png);
    png_set_gray_to_rgb(png);
    (void) png_set_interlace_handling(png);
    png_read_update_info(png, info);

    rowbytes = png_get_rowbytes(png, info);
    if (rowbytes < (size_t) w * 3)
        png_error(png, "unexpected row size");
    data = (unsigned char *) malloc(rowbytes * h);
    rows = (png_bytep *) malloc(h * sizeof(png_bytep));
    if (!data || !rows)
        png_error(png, "out of memory");
    for (y = 0; y < h; y++)
        rows[y] = data + y * rowbytes;
    png_read_image(png, rows);
    png_read_end(png, NULL);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    /* Convert to the visual's pixel layout. */
    pix = None;
    img = XCreateImage(dpy, vis, depth, ZPixmap, 0, NULL, w, h, 32, 0);
    if (img)
        img->data = malloc((size_t) img->bytes_per_line * h);
    if (img && img->data)
    {
        MaskInfo(vis->red_mask, &rs, &rb);
        MaskInfo(vis->green_mask, &gs, &gb);
        MaskInfo(vis->blue_mask, &bs, &bb);
        for (y = 0; y < h; y++)
        {
            p = rows[y];
            for (x = 0; x < w; x++, p += 3)
            {
                r = ((unsigned long) p[0] * ((1UL << rb) - 1) + 127) / 255;
                g = ((unsigned long) p[1] * ((1UL << gb) - 1) + 127) / 255;
                b = ((unsigned long) p[2] * ((1UL << bb) - 1) + 127) / 255;
                XPutPixel(img, (int) x, (int) y,
                          (r << rs) | (g << gs) | (b << bs));
            }
        }
        pix = XCreatePixmap(dpy, RootWindowOfScreen(screen), w, h,
                            (unsigned int) depth);
        gc = XCreateGC(dpy, pix, 0, NULL);
        XPutImage(dpy, pix, gc, img, 0, 0, 0, 0, w, h);
        XFreeGC(dpy, gc);
    }
    if (img)
        XDestroyImage(img);		/* frees img->data */
    free(rows);
    free(data);
    return pix;
}
#endif /* HAVE_LIBPNG */

Pixmap
_DtGetPicturePixmap(Screen *screen, char *name, Pixel fg, Pixel bg)
{
#ifdef HAVE_LIBPNG
    Pixmap pix;
    String path;

    if (name && HasPngSuffix(name))
    {
        if (strchr(name, '/'))
            path = XtNewString(name);
        else
            path = XmGetIconFileName(screen, NULL, name, NULL,
                                     XmUNSPECIFIED_ICON_SIZE);
        if (path)
        {
            pix = LoadPng(screen, path);
            XtFree(path);
            if (pix != None)
                return pix;
        }
    }
#endif
    return XmGetPixmap(screen, name, fg, bg);
}

void
_DtDestroyPicturePixmap(Screen *screen, Pixmap pixmap)
{
    if (pixmap == None || pixmap == XmUNSPECIFIED_PIXMAP)
        return;
    /* Motif's own pixmaps live in its cache; ours don't. */
    if (!XmDestroyPixmap(screen, pixmap))
        XFreePixmap(DisplayOfScreen(screen), pixmap);
}
