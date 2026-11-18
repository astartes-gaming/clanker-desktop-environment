/*
 * applist.c - an icon list for dtappfind; see applist.h.
 *
 * Rows of a small icon and a label in a scrolled drawing area, in the
 * colors of a Motif list (those of a given text widget; the selected
 * row reversed).  Icons are found as other desktops find them - the
 * icon themes' PNGs (hicolor, then common themes), /usr/share/pixmaps,
 * a full path - or as CDE icons (Dtcalc), scaled to APPLIST_ICON, and
 * loaded only when a row first shows, once per name.
 *
 * PNG needs libpng (HAVE_LIBPNG) and a TrueColor visual; XPM and CDE
 * icons go through Motif on any visual.  Rows without a picture show a
 * generic one, or none.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifdef HAVE_CONFIG_H
#include <cde_config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <Xm/Xm.h>
#include <Xm/ScrolledW.h>
#include <Xm/ScrollBar.h>
#include <Xm/DrawingA.h>
#ifdef HAVE_LIBPNG
#include <png.h>
#endif
#include "applist.h"

#ifndef CDE_INSTALLATION_TOP
#define CDE_INSTALLATION_TOP "/usr/dt"
#endif

struct _AppList {
    Widget         sw, da, sb;
    int            n, sel, top, rowH;
    AppListText    text;
    AppListIcon    icon;
    AppListRowCB   select, activate;
    AppListDragCB  drag;
    GC             gc;
    XmRenderTable  rt;
    Pixel          bg, fg;
    Time           lastTime;
    int            lastRow;
    Boolean        pressed;
    Boolean        autoHide;		/* no scroll bar when all rows fit */
    int            pressX, pressY, pressRow;
};

/* ------------------------------------------------------------------ */
/* Finding icon files                                                 */
/* ------------------------------------------------------------------ */

enum { ICON_NONE, ICON_PNG, ICON_XPM };

static int
Readable(const char *p)
{
    return access(p, R_OK) == 0;
}

static int
TypeOf(const char *p)
{
    size_t n = strlen(p);

    if (n > 4 && strcmp(p + n - 4, ".png") == 0)
        return ICON_PNG;
    if (n > 4 && (strcmp(p + n - 4, ".xpm") == 0 || strcmp(p + n - 3, ".pm") == 0))
        return ICON_XPM;
    return ICON_NONE;			/* .svg and others: not here */
}

/* The icon theme folders: $XDG_DATA_HOME/icons, each $XDG_DATA_DIRS/icons,
 * ~/.icons (the spec's order). */
static int
IconBases(char bases[][1024], int max)
{
    const char *v;
    char       *dirs, *t, *save = NULL;
    int         n = 0;

    v = getenv("HOME");
    if (v && n < max)
        snprintf(bases[n++], 1024, "%s/.icons", v);
    v = getenv("XDG_DATA_HOME");
    if (v && *v && n < max)
        snprintf(bases[n++], 1024, "%s/icons", v);
    else if (getenv("HOME") && n < max)
        snprintf(bases[n++], 1024, "%s/.local/share/icons", getenv("HOME"));
    v = getenv("XDG_DATA_DIRS");
    dirs = XtNewString((v && *v) ? v : "/usr/local/share:/usr/share");
    for (t = strtok_r(dirs, ":", &save); t && n < max;
         t = strtok_r(NULL, ":", &save))
        snprintf(bases[n++], 1024, "%s/icons", t);
    XtFree(dirs);
    return n;
}

/* Run argv (no shell), give it a few seconds; True if it succeeded. */
static Boolean
RunQuietly(char *const argv[])
{
    pid_t pid = fork();
    int   status, i;

    if (pid < 0)
        return False;
    if (pid == 0)
    {
        int fd;

        for (fd = 0; fd < 3; fd++)
            close(fd);
        execvp(argv[0], argv);
        _exit(127);
    }
    for (i = 0; i < 50; i++)		/* 5 seconds at most */
    {
        if (waitpid(pid, &status, WNOHANG) == pid)
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        usleep(100000);
    }
    kill(pid, 9);
    waitpid(pid, &status, 0);
    return False;
}

/* A PNG of an SVG icon in $XDG_CACHE_HOME/clanker/icons, made once
 * with rsvg-convert or ImageMagick, whichever is installed. */
static Boolean
SvgToPng(const char *svg, const char *base, char *out, size_t len)
{
    const char *cache = getenv("XDG_CACHE_HOME");
    char        dir[1024], tmp[1100];
    struct stat ss, cs;

    if (cache && *cache)
        snprintf(dir, sizeof(dir), "%s/clanker", cache);
    else if (getenv("HOME"))
        snprintf(dir, sizeof(dir), "%s/.cache/clanker", getenv("HOME"));
    else
        return False;
    (void) mkdir(dir, 0700);
    strncat(dir, "/icons", sizeof(dir) - strlen(dir) - 1);
    (void) mkdir(dir, 0700);
    snprintf(out, len, "%s/%s.png", dir, base);
    if (stat(out, &cs) == 0 && stat(svg, &ss) == 0 &&
        cs.st_mtime >= ss.st_mtime)
        return True;
    snprintf(tmp, sizeof(tmp), "%s.tmp.png", out);
    {
        char *rsvg[] = { "rsvg-convert", "-w", "40", "-h", "40", "-o", tmp,
                         (char *) svg, NULL };
        char *im[] = { "convert", "-background", "none", "-density", "300",
                       (char *) svg, "-resize", "40x40", tmp, NULL };

        if ((RunQuietly(rsvg) || RunQuietly(im)) && rename(tmp, out) == 0)
            return True;
    }
    unlink(tmp);
    return False;
}

static int
FindIcon(const char *name, char *out, size_t len)
{
    static const char *themes[] = {
        "hicolor", "elementary-xfce", "gnome", "Adwaita", "oxygen",
        "Tango", "locolor", NULL
    };
    static const int   sizes[] = { 24, 22, 32, 48, 16, 64, 128, 256, 0 };
    static const char *contexts[] = {
        "apps", "categories", "legacy", "places", "devices", "mimetypes",
        NULL
    };
    static char  bases[16][1024];
    static int   nBases = -1;
    char         base[256];
    const char  *dot;
    int          b, t, s, c;

    if (!name || !*name)
        return ICON_NONE;
    if (name[0] == '/')			/* a full path */
    {
        snprintf(out, len, "%s", name);
        return Readable(out) ? TypeOf(out) : ICON_NONE;
    }
    /* "foo.png" names the icon foo */
    snprintf(base, sizeof(base), "%s", name);
    if ((dot = strrchr(base, '.')) != NULL &&
        (strcmp(dot, ".png") == 0 || strcmp(dot, ".xpm") == 0 ||
         strcmp(dot, ".svg") == 0))
        base[dot - base] = '\0';
    if (strchr(base, '/'))
        return ICON_NONE;

    if (nBases < 0)
        nBases = IconBases(bases, 16);
    for (t = 0; themes[t]; t++)
        for (s = 0; sizes[s]; s++)
            for (b = 0; b < nBases; b++)
                for (c = 0; contexts[c]; c++)
                {
                    /* theme/24x24/apps/x.png, theme/apps/24/x.png */
                    snprintf(out, len, "%s/%s/%dx%d/%s/%s.png", bases[b],
                             themes[t], sizes[s], sizes[s], contexts[c], base);
                    if (Readable(out))
                        return ICON_PNG;
                    snprintf(out, len, "%s/%s/%s/%d/%s.png", bases[b],
                             themes[t], contexts[c], sizes[s], base);
                    if (Readable(out))
                        return ICON_PNG;
                }
    snprintf(out, len, "/usr/share/pixmaps/%s.png", base);
    if (Readable(out))
        return ICON_PNG;
    /* only an SVG (many current applications): converted once */
    for (t = 0; themes[t]; t++)
        for (b = 0; b < nBases; b++)
            for (c = 0; contexts[c]; c++)
            {
                char svg[1200];

                snprintf(svg, sizeof(svg), "%s/%s/scalable/%s/%s.svg",
                         bases[b], themes[t], contexts[c], base);
                if (Readable(svg) && SvgToPng(svg, base, out, len))
                    return ICON_PNG;
            }
    snprintf(out, len, "/usr/share/pixmaps/%s.svg", base);
    if (Readable(out))
    {
        char svg[1200];

        snprintf(svg, sizeof(svg), "%s", out);
        if (SvgToPng(svg, base, out, len))
            return ICON_PNG;
    }
    snprintf(out, len, "/usr/share/pixmaps/%s.xpm", base);
    if (Readable(out))
        return ICON_XPM;
    snprintf(out, len, "/usr/share/pixmaps/%s_32x32.xpm", base);
    if (Readable(out))
        return ICON_XPM;
    /* CDE's icons: tiny (16), then medium (32) */
    snprintf(out, len, CDE_INSTALLATION_TOP "/appconfig/icons/C/%s.t.pm", base);
    if (Readable(out))
        return ICON_XPM;
    snprintf(out, len, CDE_INSTALLATION_TOP "/appconfig/icons/C/%s.m.pm", base);
    if (Readable(out))
        return ICON_XPM;
    return ICON_NONE;
}

/* ------------------------------------------------------------------ */
/* Loading and scaling                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    char   *name;
    Pixmap  pm[2];			/* on the row color, on the selection */
} CachedIcon;

static CachedIcon cache[2048];
static int        nCache;

static int
Shift(unsigned long mask)
{
    int s = 0;

    while (mask && !(mask & 1))
    {
        mask >>= 1;
        s++;
    }
    return s;
}

static unsigned long
Bits(unsigned long mask)
{
    while (mask && !(mask & 1))
        mask >>= 1;
    return mask;
}

#ifdef HAVE_LIBPNG
/* An RGBA picture of the file (w*h*4 bytes), or NULL. */
static unsigned char *
ReadPng(const char *path, int *w, int *h)
{
    FILE          *fp = fopen(path, "rb");
    png_structp    png;
    png_infop      info;
    unsigned char *data = NULL;
    png_bytep     *rows = NULL;
    int            y;

    if (!fp)
        return NULL;
    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info)
    {
        if (png)
            png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return NULL;
    }
    if (setjmp(png_jmpbuf(png)))
    {
        png_destroy_read_struct(&png, &info, NULL);
        free(data);
        free(rows);
        fclose(fp);
        return NULL;
    }
    png_init_io(png, fp);
    png_read_info(png, info);
    *w = png_get_image_width(png, info);
    *h = png_get_image_height(png, info);
    if (*w <= 0 || *h <= 0 || *w > 1024 || *h > 1024)
        png_error(png, "size");
    png_set_expand(png);		/* palette, gray < 8 bit, tRNS */
    png_set_strip_16(png);
    png_set_gray_to_rgb(png);
    png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
    png_read_update_info(png, info);
    data = malloc((size_t) *w * *h * 4);
    rows = malloc(sizeof(png_bytep) * *h);
    if (!data || !rows)
        png_error(png, "memory");
    for (y = 0; y < *h; y++)
        rows[y] = data + (size_t) y * *w * 4;
    png_read_image(png, rows);
    png_read_end(png, NULL);
    png_destroy_read_struct(&png, &info, NULL);
    free(rows);
    fclose(fp);
    return data;
}
#endif

/* RGBA (w x h) scaled into APPLIST_ICON square (aspect kept, centered),
 * blended over color bg, as a pixmap; TrueColor only. */
static Pixmap
RgbaToPixmap(Widget wid, unsigned char *rgba, int w, int h, Pixel bg)
{
    Display      *dpy = XtDisplay(wid);
    Screen       *scr = XtScreen(wid);
    Visual       *vis = DefaultVisualOfScreen(scr);
    int           depth = DefaultDepthOfScreen(scr);
    int           S = APPLIST_ICON, x, y, tw, th, ox, oy;
    XColor        bc;
    XImage       *img;
    Pixmap        pm;
    GC            gc;
    unsigned long rm = vis->red_mask, gm = vis->green_mask, bm = vis->blue_mask;

    if (vis->class != TrueColor || depth < 15)
        return None;
    bc.pixel = bg;
    XQueryColor(dpy, DefaultColormapOfScreen(scr), &bc);
    img = XCreateImage(dpy, vis, depth, ZPixmap, 0, NULL, S, S, 32, 0);
    if (!img)
        return None;
    img->data = malloc((size_t) img->bytes_per_line * S);
    if (!img->data)
    {
        XDestroyImage(img);
        return None;
    }
    if (w >= h) { tw = S; th = (h * S + w / 2) / w; }
    else        { th = S; tw = (w * S + h / 2) / h; }
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    ox = (S - tw) / 2;
    oy = (S - th) / 2;
    for (y = 0; y < S; y++)
        for (x = 0; x < S; x++)
        {
            double r = bc.red / 257.0, g = bc.green / 257.0, b = bc.blue / 257.0;

            if (x >= ox && x < ox + tw && y >= oy && y < oy + th)
            {
                /* box filter over the source pixels of this one */
                int    sx0 = (x - ox) * w / tw, sx1 = (x - ox + 1) * w / tw;
                int    sy0 = (y - oy) * h / th, sy1 = (y - oy + 1) * h / th;
                double ar = 0, ag = 0, ab = 0, aa = 0, cnt = 0;
                int    sx, sy;

                if (sx1 <= sx0) sx1 = sx0 + 1;
                if (sy1 <= sy0) sy1 = sy0 + 1;
                for (sy = sy0; sy < sy1 && sy < h; sy++)
                    for (sx = sx0; sx < sx1 && sx < w; sx++)
                    {
                        unsigned char *p = rgba + ((size_t) sy * w + sx) * 4;
                        double         a = p[3] / 255.0;

                        ar += p[0] * a; ag += p[1] * a; ab += p[2] * a;
                        aa += a; cnt++;
                    }
                if (cnt > 0)
                {
                    r = (ar + r * (cnt - aa)) / cnt;
                    g = (ag + g * (cnt - aa)) / cnt;
                    b = (ab + b * (cnt - aa)) / cnt;
                }
            }
            XPutPixel(img, x, y,
                      ((unsigned long) (r * Bits(rm) / 255 + 0.5) << Shift(rm)) |
                      ((unsigned long) (g * Bits(gm) / 255 + 0.5) << Shift(gm)) |
                      ((unsigned long) (b * Bits(bm) / 255 + 0.5) << Shift(bm)));
        }
    pm = XCreatePixmap(dpy, RootWindowOfScreen(scr), S, S, depth);
    gc = XCreateGC(dpy, pm, 0, NULL);
    XPutImage(dpy, pm, gc, img, 0, 0, 0, 0, S, S);
    XFreeGC(dpy, gc);
    XDestroyImage(img);
    return pm;
}

/* An XPM (or CDE .pm) on bg, scaled to APPLIST_ICON (any visual). */
static Pixmap
XpmToPixmap(Widget wid, const char *path, Pixel fg, Pixel bg)
{
    Display     *dpy = XtDisplay(wid);
    Screen      *scr = XtScreen(wid);
    Pixmap       src = XmGetPixmap(scr, (char *) path, fg, bg);
    Pixmap       pm;
    Window       root;
    int          x, y, S = APPLIST_ICON, tw, th, ox, oy;
    unsigned int w, h, bw, d;
    XImage      *in, *out;
    GC           gc;

    if (src == XmUNSPECIFIED_PIXMAP)
        return None;
    if (!XGetGeometry(dpy, src, &root, &x, &y, &w, &h, &bw, &d) ||
        (in = XGetImage(dpy, src, 0, 0, w, h, AllPlanes, ZPixmap)) == NULL)
    {
        XmDestroyPixmap(scr, src);
        return None;
    }
    pm = XCreatePixmap(dpy, RootWindowOfScreen(scr), S, S, d);
    gc = XCreateGC(dpy, pm, 0, NULL);
    XSetForeground(dpy, gc, bg);
    XFillRectangle(dpy, pm, gc, 0, 0, S, S);
    if (w >= h) { tw = (int) w > S ? S : (int) w; th = h * tw / w; }
    else        { th = (int) h > S ? S : (int) h; tw = w * th / h; }
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    ox = (S - tw) / 2;
    oy = (S - th) / 2;
    out = XGetImage(dpy, pm, 0, 0, S, S, AllPlanes, ZPixmap);
    if (out)
    {
        for (y = 0; y < th; y++)		/* nearest neighbour */
            for (x = 0; x < tw; x++)
                XPutPixel(out, ox + x, oy + y,
                          XGetPixel(in, x * w / tw, y * h / th));
        XPutImage(dpy, pm, gc, out, 0, 0, 0, 0, S, S);
        XDestroyImage(out);
    }
    XFreeGC(dpy, gc);
    XDestroyImage(in);
    XmDestroyPixmap(scr, src);
    return pm;
}

static void IconFor1(AppList *l, const char *name, Pixmap pm[2]);

/* The pixmaps of icon "name" on bg (0) and on fg (1), or None; "name"
 * may list alternatives, "a|b": the first found. */
static void
IconFor(AppList *l, const char *name, Pixmap pm[2])
{
    char        one[512];
    const char *p = name;

    pm[0] = pm[1] = None;
    while (p && *p && pm[0] == None)
    {
        size_t n = strcspn(p, "|");

        snprintf(one, sizeof(one), "%.*s", (int) n, p);
        IconFor1(l, one, pm);
        p += n;
        if (*p == '|')
            p++;
    }
}

static void
IconFor1(AppList *l, const char *name, Pixmap pm[2])
{
    char path[2048];
    int  i, type;

    pm[0] = pm[1] = None;
    if (!name || !*name)
        return;
    for (i = 0; i < nCache; i++)
        if (strcmp(cache[i].name, name) == 0)
        {
            pm[0] = cache[i].pm[0];
            pm[1] = cache[i].pm[1];
            return;
        }
    type = FindIcon(name, path, sizeof(path));
#ifdef HAVE_LIBPNG
    if (type == ICON_PNG)
    {
        int            w, h;
        unsigned char *rgba = ReadPng(path, &w, &h);

        if (rgba)
        {
            pm[0] = RgbaToPixmap(l->da, rgba, w, h, l->bg);
            pm[1] = RgbaToPixmap(l->da, rgba, w, h, l->fg);
            free(rgba);
        }
    }
#endif
    if (type == ICON_XPM)
    {
        pm[0] = XpmToPixmap(l->da, path, l->fg, l->bg);
        pm[1] = XpmToPixmap(l->da, path, l->bg, l->fg);
    }
    if (nCache < (int) (sizeof(cache) / sizeof(cache[0])))
    {
        cache[nCache].name = XtNewString(name);
        cache[nCache].pm[0] = pm[0];
        cache[nCache].pm[1] = pm[1];
        nCache++;
    }
}

/* ------------------------------------------------------------------ */
/* The list                                                           */
/* ------------------------------------------------------------------ */

static int
Height(AppList *l)
{
    Dimension h = 0;

    XtVaGetValues(l->da, XmNheight, &h, NULL);
    return h;
}

static void
UpdateScrollBar(AppList *l)
{
    int total = l->n * l->rowH, view = Height(l);

    if (view < 1)
        view = 1;
    if (total < view)
        total = view;
    if (l->top > total - view)
        l->top = total - view;
    if (l->top < 0)
        l->top = 0;
    if (l->autoHide)
    {
        if (total <= view && XtIsManaged(l->sb))
            XtUnmanageChild(l->sb);
        else if (total > view && !XtIsManaged(l->sb))
            XtManageChild(l->sb);
    }
    XtVaSetValues(l->sb, XmNminimum, 0, XmNmaximum, total,
                  XmNsliderSize, view, XmNvalue, l->top,
                  XmNincrement, l->rowH, XmNpageIncrement,
                  view > l->rowH ? view - l->rowH : l->rowH, NULL);
}

static void
Draw(AppList *l)
{
    Display  *dpy = XtDisplay(l->da);
    Window    win = XtWindow(l->da);
    Dimension w = 0;
    int       h = Height(l), row, y;

    if (!XtIsRealized(l->da))
        return;
    XtVaGetValues(l->da, XmNwidth, &w, NULL);
    if (!l->gc)
        l->gc = XCreateGC(dpy, win, 0, NULL);
    XSetForeground(dpy, l->gc, l->bg);
    XFillRectangle(dpy, win, l->gc, 0, 0, w, h);
    for (row = l->top / l->rowH; row < l->n && row * l->rowH - l->top < h; row++)
    {
        Boolean     sel = (row == l->sel);
        const char *label = l->text(row);
        XmString    xs;
        Pixmap      pm[2];
        Dimension   th;

        y = row * l->rowH - l->top;
        if (sel)
        {
            XSetForeground(dpy, l->gc, l->fg);
            XFillRectangle(dpy, win, l->gc, 0, y, w, l->rowH);
        }
        IconFor(l, l->icon ? l->icon(row) : NULL, pm);
        if (pm[0] == None && l->icon)	/* a generic one */
            IconFor(l, "application-x-executable", pm);
        if (pm[0] == None && l->icon)
            IconFor(l, "Dtactn", pm);
        if (pm[sel ? 1 : 0] != None)
            XCopyArea(dpy, pm[sel ? 1 : 0], win, l->gc, 0, 0,
                      APPLIST_ICON, APPLIST_ICON, 4,
                      y + (l->rowH - APPLIST_ICON) / 2);
        xs = XmStringCreateLocalized((char *) (label ? label : ""));
        th = XmStringHeight(l->rt, xs);
        XSetForeground(dpy, l->gc, sel ? l->bg : l->fg);
        XSetBackground(dpy, l->gc, sel ? l->fg : l->bg);
        XmStringDraw(dpy, win, l->rt, xs, l->gc, 4 + APPLIST_ICON + 6,
                     y + (l->rowH - th) / 2, w - APPLIST_ICON - 12,
                     XmALIGNMENT_BEGINNING, XmSTRING_DIRECTION_L_TO_R, NULL);
        XmStringFree(xs);
    }
}

static void
ExposeCB(Widget w, XtPointer client, XtPointer call)
{
    XmDrawingAreaCallbackStruct *cb = (XmDrawingAreaCallbackStruct *) call;

    if (cb->event && cb->event->type == Expose && cb->event->xexpose.count > 0)
        return;
    Draw((AppList *) client);
}

static void
ResizeCB(Widget w, XtPointer client, XtPointer call)
{
    UpdateScrollBar((AppList *) client);
    Draw((AppList *) client);
}

static void
ScrollCB(Widget w, XtPointer client, XtPointer call)
{
    AppList *l = (AppList *) client;

    l->top = ((XmScrollBarCallbackStruct *) call)->value;
    Draw(l);
}

static int
RowAt(AppList *l, int y)
{
    int row = (y + l->top) / l->rowH;

    return (row >= 0 && row < l->n) ? row : -1;
}

static void
InputEH(Widget w, XtPointer client, XEvent *ev, Boolean *cont)
{
    AppList *l = (AppList *) client;
    int      row;

    switch (ev->type)
    {
    case ButtonPress:
        if (ev->xbutton.button == Button4 || ev->xbutton.button == Button5)
        {
            l->top += (ev->xbutton.button == Button4 ? -3 : 3) * l->rowH;
            UpdateScrollBar(l);
            Draw(l);
            return;
        }
        if (ev->xbutton.button != Button1)
            return;
        row = RowAt(l, ev->xbutton.y);
        if (row < 0)
            return;
        l->pressed = True;
        l->pressX = ev->xbutton.x;
        l->pressY = ev->xbutton.y;
        l->pressRow = row;
        AppListSelect(l, row);
        if (l->select)
            l->select(row);
        if (row == l->lastRow &&
            ev->xbutton.time - l->lastTime <=
                (Time) XtGetMultiClickTime(XtDisplay(w)))
        {
            l->pressed = False;
            l->lastTime = 0;
            if (l->activate)
                l->activate(row);
            return;
        }
        l->lastRow = row;
        l->lastTime = ev->xbutton.time;
        break;
    case ButtonRelease:
        l->pressed = False;
        break;
    case MotionNotify:
        if (l->pressed && l->drag &&
            (abs(ev->xmotion.x - l->pressX) > 8 ||
             abs(ev->xmotion.y - l->pressY) > 8))
        {
            l->pressed = False;
            l->lastTime = 0;
            l->drag(l->pressRow, ev);
        }
        break;
    }
}

AppList *
AppListCreate(Widget parent, const char *name, Arg *args, int n,
              Widget colors, AppListText text, AppListIcon icon,
              AppListRowCB select, AppListRowCB activate, AppListDragCB drag)
{
    AppList  *l = (AppList *) XtCalloc(1, sizeof(AppList));
    Arg       a[24];
    int       i, k = 0;
    XmString  xs;
    Dimension th;

    l->text = text;
    l->icon = icon;
    l->select = select;
    l->activate = activate;
    l->drag = drag;
    l->lastRow = -1;
    XtVaGetValues(colors, XmNbackground, &l->bg, XmNforeground, &l->fg,
                  XmNrenderTable, &l->rt, NULL);

    for (i = 0; i < n && k < 18; i++)
        a[k++] = args[i];
    XtSetArg(a[k], XmNscrollingPolicy, XmAPPLICATION_DEFINED); k++;
    XtSetArg(a[k], XmNscrollBarDisplayPolicy, XmSTATIC); k++;
    XtSetArg(a[k], XmNvisualPolicy, XmVARIABLE); k++;
    XtSetArg(a[k], XmNshadowThickness, 2); k++;
    l->sw = XmCreateScrolledWindow(parent, (char *) name, a, k);
    xs = XmStringCreateLocalized("Ag");
    th = XmStringHeight(l->rt, xs);
    XmStringFree(xs);
    l->rowH = (th + 4 > APPLIST_ICON + 4) ? th + 4 : APPLIST_ICON + 4;
    l->da = XtVaCreateManagedWidget("rows", xmDrawingAreaWidgetClass, l->sw,
                                    XmNbackground, l->bg,
                                    XmNtraversalOn, False, NULL);
    l->sb = XtVaCreateManagedWidget("scroll", xmScrollBarWidgetClass, l->sw,
                                    XmNorientation, XmVERTICAL, NULL);
    XmScrolledWindowSetAreas(l->sw, NULL, l->sb, l->da);
    XtAddCallback(l->da, XmNexposeCallback, ExposeCB, l);
    XtAddCallback(l->da, XmNresizeCallback, ResizeCB, l);
    XtAddCallback(l->sb, XmNvalueChangedCallback, ScrollCB, l);
    XtAddCallback(l->sb, XmNdragCallback, ScrollCB, l);
    XtAddEventHandler(l->da, ButtonPressMask | ButtonReleaseMask |
                      Button1MotionMask, False, InputEH, l);
    XtManageChild(l->sw);
    return l;
}

Widget
AppListWidget(AppList *l)
{
    return l->sw;
}

int
AppListVisibleRows(AppList *l)
{
    int v = Height(l) / l->rowH;

    return v > 0 ? v : 1;
}

void
AppListSetCount(AppList *l, int n)
{
    l->n = n;
    l->sel = n > 0 ? 0 : -1;
    l->top = 0;
    UpdateScrollBar(l);
    Draw(l);
}

void
AppListSelect(AppList *l, int row)
{
    int view = Height(l);

    if (row < 0 || row >= l->n)
        return;
    l->sel = row;
    if (row * l->rowH < l->top)
        l->top = row * l->rowH;
    else if ((row + 1) * l->rowH > l->top + view)
        l->top = (row + 1) * l->rowH - view;
    UpdateScrollBar(l);
    Draw(l);
}

int
AppListSelected(AppList *l)
{
    return l->sel;
}

void
AppListForgetIcons(AppList *l)
{
    (void) l;				/* icons are cached by name */
}

void
AppListAutoHideScrollBar(AppList *l)
{
    l->autoHide = True;
    UpdateScrollBar(l);
}
