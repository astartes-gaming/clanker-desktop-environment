/*
 * LivePalette.c - running CDE applications follow palette changes.
 *
 * CDE's color server changes the palette of running applications by
 * rewriting shared colormap cells, which works only on PseudoColor
 * displays.  On today's TrueColor displays a pixel is its color, so
 * applications kept the old palette until restarted.
 *
 * Settings > Color records the new palette as "*<screen>*ColorPalette"
 * in RESOURCE_MANAGER.  Every application that calls DtInitialize()
 * watches that property; when the palette changes it reads the palette
 * file, computes the color sets as the color server does (Motif's
 * color calculation), maps each old pixel of every color set to the new
 * one, and updates the color resources of all its widgets.  Pixmaps
 * drawn in the old colors (some icons) keep them.
 *
 * Does nothing on PseudoColor/GrayScale (the color server already
 * does it), without Motif 2's color object data, or with
 * DT_NO_LIVE_PALETTE set.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/IntrinsicP.h>
#include <X11/CoreP.h>
#include <Xm/XmP.h>
#include <Xm/ColorObjP.h>

#include "DtSvcLock.h"

#ifndef CDE_DATA_TOP
#define CDE_DATA_TOP "/usr/dt"
#endif
#ifndef CDE_CONFIGURATION_TOP
#define CDE_CONFIGURATION_TOP "/etc/dt"
#endif

#define NSETS   XmCO_MAX_NUM_COLORS	/* 8 color sets */
#define NCOLORS (NSETS * 5)		/* bg fg ts bs sc */

static Display     *lp_dpy;
static int          lp_screen;
static char         lp_name[256];	/* current palette, "Name.dp" */
static Pixel        lp_old[NCOLORS];	/* the colors in use now */
static int          lp_have_old;
static XtEventDispatchProc lp_prev;
static int          lp_debug;		/* DT_LIVE_PALETTE_DEBUG */
static void       (*lp_hook)(const char *);	/* dtsession's color server */

/* The session manager's color server is told of a new palette first,
 * so that applications started afterwards get its colors. */
void
_DtLivePaletteSetHook(void (*hook)(const char *palette))
{
    lp_hook = hook;
}

/* The resources of a widget that hold palette colors. */
static const char *const lp_resources[] = {
    XmNbackground, XmNforeground, XmNtopShadowColor, XmNbottomShadowColor,
    XmNselectColor, XmNarmColor, XmNtroughColor, XmNhighlightColor,
    XmNborderColor, "pixmapBackground", "pixmapForeground", NULL
};

/* "*0*ColorPalette:\tName.dp" from the resource database string. */
static int
PaletteName(char *db, char *out, size_t len)
{
    char key[32], *p, *e;

    snprintf(key, sizeof(key), "*%d*ColorPalette:", lp_screen);
    if (db == NULL || (p = strstr(db, key)) == NULL)
        return 0;
    p += strlen(key);
    p += strspn(p, " \t");
    e = p + strcspn(p, "\n");
    if (e == p || (size_t) (e - p) >= len)
        return 0;
    memcpy(out, p, e - p);
    out[e - p] = '\0';
    return 1;
}

static char *
ResourceString(void)
{
    Atom          type;
    int           format;
    unsigned long n, after;
    unsigned char *data = NULL;

    if (XGetWindowProperty(lp_dpy, RootWindow(lp_dpy, 0), XA_RESOURCE_MANAGER,
                           0, 1L << 20, False, XA_STRING, &type, &format,
                           &n, &after, &data) != Success)
        return NULL;
    return (char *) data;
}

/* The background colors of a palette file, as the color server reads
 * them (one color per line, up to NSETS). */
static int
ReadPalette(const char *name, XColor bg[NSETS])
{
    const char *dirs[3];
    char        path[1024], line[256], *home = getenv("HOME");
    FILE       *fp = NULL;
    int         i, n = 0;

    if (strchr(name, '/'))
        return 0;
    dirs[0] = home;
    dirs[1] = CDE_CONFIGURATION_TOP;
    dirs[2] = CDE_DATA_TOP;
    for (i = 0; i < 3 && fp == NULL; i++) {
        if (dirs[i] == NULL)
            continue;
        snprintf(path, sizeof(path), "%s%s/palettes/%s", dirs[i],
                 i == 0 ? "/.dt" : "", name);
        fp = fopen(path, "r");
    }
    if (fp == NULL)
        return 0;
    while (n < NSETS && fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '!')
            break;
        if (line[0] && XParseColor(lp_dpy, DefaultColormap(lp_dpy, lp_screen),
                                   line, &bg[n]))
            n++;
    }
    fclose(fp);
    return n;
}

static Pixel
Alloc(XColor *c)
{
    XColor x = *c;

    x.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(lp_dpy, DefaultColormap(lp_dpy, lp_screen), &x))
        return BlackPixel(lp_dpy, lp_screen);
    return x.pixel;
}

/* New pixels for the palette, following the old sets' conventions:
 * black or white foregrounds and shadows stay so (dtsession's
 * foregroundColor and shadowPixmaps settings). */
static int
NewPixels(const char *name, Pixel out[NCOLORS])
{
    XColor bg[NSETS], fg, sc, ts, bs;
    XmColorProc calc = XmGetColorCalculation();
    Pixel black = BlackPixel(lp_dpy, lp_screen),
          white = WhitePixel(lp_dpy, lp_screen);
    int n, i;

    if ((n = ReadPalette(name, bg)) == 0)
        return 0;
    for (i = 0; i < NSETS; i++) {
        XColor *b = &bg[i < n ? i : n - 1];
        Pixel  *o = &lp_old[i * 5], *p = &out[i * 5];

        (*calc)(b, &fg, &sc, &ts, &bs);
        p[0] = Alloc(b);
        p[1] = (o[1] == black || o[1] == white) ? o[1] : Alloc(&fg);
        p[2] = (o[2] == white && o[3] == black) ? o[2] : Alloc(&ts);
        p[3] = (o[2] == white && o[3] == black) ? o[3] : Alloc(&bs);
        p[4] = Alloc(&sc);
    }
    return 1;
}

static void
Recolor(Widget w, const Pixel *from, const Pixel *to)
{
    Arg      args[16];
    Pixel    val[16];
    int      i, j, n = 0;
    Cardinal k;

    if (w == NULL || w->core.being_destroyed)
        return;
    for (i = 0; lp_resources[i] && i < 16; i++) {
        val[i] = (Pixel) ~0UL;
        XtSetArg(args[i], (String) lp_resources[i], &val[i]);
    }
    XtGetValues(w, args, i);
    for (j = 0; j < i; j++) {
        if (val[j] == (Pixel) ~0UL)
            continue;			/* the widget has no such resource */
        for (k = 0; k < NCOLORS; k++)
            if (from[k] == val[j]) {
                if (to[k] != val[j]) {
                    XtSetArg(args[n], (String) lp_resources[j], to[k]);
                    n++;
                    if (lp_debug)
                        fprintf(stderr, "livepalette: %s %s %06lx -> %06lx\n",
                                XtName(w), lp_resources[j],
                                (unsigned long) val[j], (unsigned long) to[k]);
                }
                break;
            }
        if (lp_debug && k == NCOLORS)
            fprintf(stderr, "livepalette: %s %s=%06lx not in the palette\n",
                    XtName(w), lp_resources[j], (unsigned long) val[j]);
    }
    if (n) {
        XtSetValues(w, args, n);
        /* Some widgets (CDE's icon gadgets without a shadow) redraw only
         * part of themselves on a color change: repaint all of it. */
        if (XtIsRealized(XtIsWidget(w) ? w : XtParent(w))) {
            if (XtIsWidget(w))
                XClearArea(lp_dpy, XtWindow(w), 0, 0, 0, 0, True);
            else if (XtIsRectObj(w))
                XClearArea(lp_dpy, XtWindow(XtParent(w)), w->core.x,
                           w->core.y, w->core.width, w->core.height, True);
        }
    }

    if (XtIsComposite(w)) {
        CompositeWidget c = (CompositeWidget) w;

        for (k = 0; k < c->composite.num_children; k++)
            Recolor(c->composite.children[k], from, to);
    }
    if (XtIsWidget(w))
        for (k = 0; k < w->core.num_popups; k++)
            Recolor(w->core.popup_list[k], from, to);
}

/* Every top-level widget of this client: the windows of the X tree
 * that Xt knows (frames of the window manager are skipped).  Each
 * widget tree is recolored once, however many windows it has. */
#define LP_MAX_ROOTS 32
static Widget lp_done[LP_MAX_ROOTS];
static int    lp_ndone;

static void
RecolorWindows(Window win, const Pixel *from, const Pixel *to, int depth)
{
    Window   root, parent, *kids = NULL;
    unsigned n, i;
    Widget   w = XtWindowToWidget(lp_dpy, win);
    int      k;

    if (w) {
        while (XtParent(w))
            w = XtParent(w);
        for (k = 0; k < lp_ndone; k++)
            if (lp_done[k] == w)
                return;
        if (lp_ndone < LP_MAX_ROOTS)
            lp_done[lp_ndone++] = w;
        Recolor(w, from, to);
        return;
    }
    if (depth > 3 || !XQueryTree(lp_dpy, win, &root, &parent, &kids, &n))
        return;
    for (i = 0; i < n; i++)
        RecolorWindows(kids[i], from, to, depth + 1);
    if (kids)
        XFree(kids);
}

/* Windows may vanish while we look at them: ignore those errors
 * (the default handler would end the application). */
static int lp_errors;

static int
IgnoreErrors(Display *d, XErrorEvent *e)
{
    lp_errors++;
    return 0;
}

static void
PaletteChanged(void)
{
    char *db = ResourceString(), name[256];
    Pixel to[NCOLORS];

    if (!PaletteName(db, name, sizeof(name))) {
        if (db) XFree(db);
        return;
    }
    if (db)
        XFree(db);
    if (strcmp(name, lp_name) == 0)
        return;
    if (lp_hook)
        (*lp_hook)(name);
    if (lp_have_old && NewPixels(name, to)) {
        XErrorHandler old;

        XSync(lp_dpy, False);
        old = XSetErrorHandler(IgnoreErrors);
        lp_ndone = 0;
        RecolorWindows(RootWindow(lp_dpy, lp_screen), lp_old, to, 0);
        XSync(lp_dpy, False);
        XSetErrorHandler(old);
        memcpy(lp_old, to, sizeof(lp_old));
    }
    snprintf(lp_name, sizeof(lp_name), "%s", name);
}

/* For applications that handle root window events themselves (dtwm):
 * pass every event here; palette changes are acted on. */
void
_DtLivePaletteEvent(XEvent *ev)
{
    if (lp_dpy && ev->type == PropertyNotify &&
        ev->xproperty.window == RootWindow(lp_dpy, 0) &&
        ev->xproperty.atom == XA_RESOURCE_MANAGER)
        PaletteChanged();
}

static Boolean
Dispatch(XEvent *ev)
{
    _DtLivePaletteEvent(ev);
    return lp_prev ? (*lp_prev)(ev) : False;
}

/* Called by DtInitialize. */
void
_DtLivePaletteInitialize(Widget w)
{
    XmPixelSet sets[XmCO_NUM_COLORS];
    short      a, i, p, s, t;
    int        use, k;
    Visual    *v;
    XWindowAttributes wa;
    char      *db;

    if (w == NULL || getenv("DT_NO_LIVE_PALETTE"))
        return;
    lp_dpy = XtDisplayOfObject(w);
    lp_screen = XScreenNumberOfScreen(XtScreenOfObject(w));
    v = DefaultVisual(lp_dpy, lp_screen);
    if (v->class == PseudoColor || v->class == GrayScale)
        return;				/* the color server does it */

    if (XmeGetColorObjData(XtScreenOfObject(w), &use, sets,
                           XmCO_NUM_COLORS, &a, &i, &p, &s, &t) &&
        use != XmCO_BLACK_WHITE) {
        for (k = 0; k < NSETS; k++) {
            lp_old[k * 5 + 0] = sets[k].bg;
            lp_old[k * 5 + 1] = sets[k].fg;
            lp_old[k * 5 + 2] = sets[k].ts;
            lp_old[k * 5 + 3] = sets[k].bs;
            lp_old[k * 5 + 4] = sets[k].sc;
        }
        lp_have_old = 1;
    }
    /* Without color object data (the color server itself) the palette
     * is still watched, for the hook. */
    lp_debug = getenv("DT_LIVE_PALETTE_DEBUG") != NULL;
    if (lp_debug && lp_have_old)
        for (k = 0; k < NSETS; k++)
            fprintf(stderr, "livepalette: set %d bg %06lx fg %06lx ts %06lx "
                    "bs %06lx sc %06lx\n", k + 1, sets[k].bg, sets[k].fg,
                    sets[k].ts, sets[k].bs, sets[k].sc);

    db = ResourceString();
    if (!PaletteName(db, lp_name, sizeof(lp_name)))
        lp_name[0] = '\0';
    if (db)
        XFree(db);

    /* Our PropertyNotify events on the root window go through Xt. */
    XGetWindowAttributes(lp_dpy, RootWindow(lp_dpy, 0), &wa);
    XSelectInput(lp_dpy, RootWindow(lp_dpy, 0),
                 wa.your_event_mask | PropertyChangeMask);
    lp_prev = XtSetEventDispatcher(lp_dpy, PropertyNotify, Dispatch);
}
