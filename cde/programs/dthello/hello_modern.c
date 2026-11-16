/*
 * hello_modern.c - the startup splash in the Motif look, for displays
 * with true color (15 bits or more).
 *
 * A night-sky gradient fills the screen; a raised, bevelled panel in
 * the middle holds the CDE logo, the desktop's name, what is
 * happening ("Starting the desktop...") and a Motif-style busy bar
 * whose block sweeps back and forth until the window manager takes
 * over.  The copyright text dthello was given goes below the panel.
 *
 * Everything is drawn with core X calls and the fontsets dthello
 * already uses, so it runs wherever dthello did; 8-bit, gray and
 * monochrome displays (and DT_HELLO_CLASSIC=1) keep the classic
 * splash.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/Xutil.h>
#include <Xm/Xm.h>
#include <Dt/Icon.h>
#include <Dt/IconFile.h>
#include "hello_modern.h"

extern Display       *dpy;
extern Window         welcome;
extern XFontSet       fontset;
extern unsigned long  textHeight;
extern unsigned int   displayWidth, displayHeight;
extern unsigned char *ppchText[];
extern int            numLines;
extern int            greetingLines;

static int noteFirst, noteLast;	/* the text lines shown (no blank ends) */

/* The colors (#RRGGBB) */
#define SKY_TOP      0x0a1428	/* night sky, top ... */
#define SKY_BOTTOM   0x34507e	/* ... to the horizon */
#define PANEL        0xb4b8c8	/* Motif gray-blue (CDE "Default") */
#define PANEL_LIGHT  0xe4e6ee	/* top/left bevel */
#define PANEL_DARK   0x5c6070	/* bottom/right bevel */
#define PANEL_TEXT   0x101420
#define TROUGH       0x8e93a6
#define BAR          0xeda870	/* CDE active-window orange */
#define BAR_LIGHT    0xf8d4b0
#define BAR_DARK     0x9a6a40
#define NOTE_TEXT    0xc8d2e6	/* copyright on the sky */

#define BEVEL        2
#define BAR_H        14
#define TICK_MS      50

static Boolean       on = False;
static GC            mgc;
static XFontSet      titleFont, smallFont;
static Pixmap        logo = None;
static unsigned int  logoW, logoH;
static int           panelX, panelY, panelW, panelH;
static int           barX, barY, barW;
static int           pos = 0, dir = 1;
static XVisualInfo   vinfo;
static const char   *title = "Clanker Desktop Environment";
static const char   *status = "Starting the desktop...";
static XtAppContext  app;

/* A pixel value straight from #RRGGBB, on a TrueColor visual. */
static unsigned long
Pix(unsigned long rgb)
{
    unsigned long r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
    unsigned long p = 0;
    unsigned long m;
    int           s;

    m = vinfo.red_mask;   for (s = 0; m && !(m & 1); s++) m >>= 1;
    p |= ((r * m / 255) << s);
    m = vinfo.green_mask; for (s = 0; m && !(m & 1); s++) m >>= 1;
    p |= ((g * m / 255) << s);
    m = vinfo.blue_mask;  for (s = 0; m && !(m & 1); s++) m >>= 1;
    p |= ((b * m / 255) << s);
    return p;
}

static unsigned long
Mix(unsigned long a, unsigned long b, int i, int n)
{
    unsigned long out = 0;
    int           k;

    for (k = 16; k >= 0; k -= 8)
    {
        long ca = (a >> k) & 0xff, cb = (b >> k) & 0xff;

        out |= (unsigned long) (ca + (cb - ca) * i / (n ? n : 1)) << k;
    }
    return out;
}

static void
Fill(unsigned long rgb, int x, int y, int w, int h)
{
    XSetForeground(dpy, mgc, Pix(rgb));
    XFillRectangle(dpy, welcome, mgc, x, y, w, h);
}

/* A Motif bevel: light top/left, dark bottom/right (swapped: sunken). */
static void
Bevel(int x, int y, int w, int h, unsigned long light, unsigned long dark)
{
    int i;

    for (i = 0; i < BEVEL; i++)
    {
        XSetForeground(dpy, mgc, Pix(light));
        XDrawLine(dpy, welcome, mgc, x + i, y + i, x + w - 1 - i, y + i);
        XDrawLine(dpy, welcome, mgc, x + i, y + i, x + i, y + h - 1 - i);
        XSetForeground(dpy, mgc, Pix(dark));
        XDrawLine(dpy, welcome, mgc, x + i, y + h - 1 - i,
                  x + w - 1 - i, y + h - 1 - i);
        XDrawLine(dpy, welcome, mgc, x + w - 1 - i, y + i,
                  x + w - 1 - i, y + h - 1 - i);
    }
}

static int
TextWidth(XFontSet fs, const char *s)
{
    return XmbTextEscapement(fs, (char *) s, strlen(s));
}

static int
Ascent(XFontSet fs)
{
    return -XExtentsOfFontSet(fs)->max_logical_extent.y;
}

static int
LineHeight(XFontSet fs)
{
    return XExtentsOfFontSet(fs)->max_logical_extent.height;
}

static XFontSet
LoadFont(const char *list)
{
    char   **missing, *def;
    int      n;
    XFontSet fs = XCreateFontSet(dpy, (char *) list, &missing, &n, &def);

    if (missing)
        XFreeStringList(missing);
    return fs;
}

static void
DrawBar(void)
{
    int inner = barW - 2 * BEVEL;
    int block = inner / 4;
    int x = barX + BEVEL + pos;

    Fill(TROUGH, barX + BEVEL, barY + BEVEL, inner, BAR_H - 2 * BEVEL);
    /* the moving block, raised */
    Fill(BAR, x, barY + BEVEL, block, BAR_H - 2 * BEVEL);
    XSetForeground(dpy, mgc, Pix(BAR_LIGHT));
    XDrawLine(dpy, welcome, mgc, x, barY + BEVEL, x + block - 1, barY + BEVEL);
    XDrawLine(dpy, welcome, mgc, x, barY + BEVEL, x, barY + BAR_H - BEVEL - 1);
    XSetForeground(dpy, mgc, Pix(BAR_DARK));
    XDrawLine(dpy, welcome, mgc, x, barY + BAR_H - BEVEL - 1,
              x + block - 1, barY + BAR_H - BEVEL - 1);
    XDrawLine(dpy, welcome, mgc, x + block - 1, barY + BEVEL,
              x + block - 1, barY + BAR_H - BEVEL - 1);
}

static void
Tick(XtPointer data, XtIntervalId *id)
{
    int inner = barW - 2 * BEVEL;
    int block = inner / 4;
    int step = inner / 60 > 2 ? inner / 60 : 2;

    pos += dir * step;
    if (pos <= 0) { pos = 0; dir = 1; }
    if (pos >= inner - block) { pos = inner - block; dir = -1; }
    DrawBar();
    XFlush(dpy);
    XtAppAddTimeOut(app, TICK_MS, Tick, NULL);
}

Boolean
ModernSplashInit(XtAppContext appContext, const char *titleText,
                 const char *statusText)
{
    XVisualInfo  tmpl, *vi;
    int          n;
    Screen      *scr = DefaultScreenOfDisplay(dpy);
    char        *e = getenv("DT_HELLO_CLASSIC");

    if (e && *e && strcmp(e, "0") != 0)
        return False;
    tmpl.visualid = XVisualIDFromVisual(DefaultVisualOfScreen(scr));
    vi = XGetVisualInfo(dpy, VisualIDMask, &tmpl, &n);
    if (!vi || n < 1)
        return False;
    vinfo = vi[0];
    XFree(vi);
    if (vinfo.class != TrueColor || vinfo.depth < 15)
        return False;

    app = appContext;
    if (titleText && *titleText)
        title = titleText;
    if (statusText && *statusText)
        status = statusText;
    mgc = XCreateGC(dpy, RootWindowOfScreen(scr), 0, NULL);

    titleFont = LoadFont(
        "-dt-interface system-bold-r-normal-xxl*-*-*-*-*-*-*-*-*,"
        "-*-helvetica-bold-r-normal--*-240-*-*-*-*-*-*,"
        "-*-*-bold-r-normal--*-240-*-*-*-*-*-*,"
        "-*-*-medium-r-normal--*-240-*-*-*-*-*-*,fixed");
    smallFont = LoadFont(
        "-dt-interface system-medium-r-normal-s*-*-*-*-*-*-*-*-*,"
        "-*-helvetica-medium-r-normal--*-120-*-*-*-*-*-*,"
        "-*-*-medium-r-normal--*-120-*-*-*-*-*-*,fixed");
    if (!titleFont)
        titleFont = fontset;
    if (!smallFont)
        smallFont = fontset;

    /* The login screen's logo, if the display is tall enough for it. */
    if (displayHeight >= 600)
    {
        char *name = _DtGetIconFileName(scr, "Dtlogo", NULL, NULL, 0);

        if (name)
        {
            Pixmap p = XmGetPixmap(scr, name, Pix(PANEL_TEXT), Pix(PANEL));

            if (p != XmUNSPECIFIED_PIXMAP)
            {
                Window       r;
                int          x, y;
                unsigned int bw, d;

                if (XGetGeometry(dpy, p, &r, &x, &y, &logoW, &logoH, &bw, &d))
                    logo = p;
            }
            XtFree(name);
        }
    }
    on = True;
    return True;
}

void
ModernSplashPaint(void)
{
    int          y, i, tw, sw, lh, maxw = 0, noteH;
    int          bands = 64;
    int          logoSide;

    if (!on)
        return;

    /* sky: horizontal bands from top to bottom */
    for (i = 0; i < bands; i++)
    {
        int y0 = (int) displayHeight * i / bands;
        int y1 = (int) displayHeight * (i + 1) / bands;

        Fill(Mix(SKY_TOP, SKY_BOTTOM, i, bands - 1), 0, y0,
             displayWidth, y1 - y0);
    }

    /* panel size: logo beside the text when there is room */
    tw = TextWidth(titleFont, title);
    sw = TextWidth(smallFont, status);
    lh = LineHeight(titleFont);
    logoSide = logo != None ? (int) logoW : 0;
    if (logoSide > (int) displayHeight / 3)
        logoSide = 0;			/* too big for this screen */
    panelW = (tw > sw ? tw : sw) + 64 + (logoSide ? logoSide + 32 : 0);
    if (panelW < 420)
        panelW = 420;
    if (panelW > (int) displayWidth - 32)
        panelW = displayWidth - 32;
    panelH = (logoSide > lh + LineHeight(smallFont) + BAR_H + 40 ?
              logoSide : lh + LineHeight(smallFont) + BAR_H + 40) + 48;

    /* the text below: without the default greeting and blank ends */
    noteFirst = greetingLines;
    noteLast = numLines - 1;
    while (noteFirst <= noteLast && ppchText[noteFirst][0] == '\0')
        noteFirst++;
    while (noteLast >= noteFirst && ppchText[noteLast][0] == '\0')
        noteLast--;
    for (i = noteFirst; i <= noteLast; i++)
    {
        int w = TextWidth(smallFont, (char *) ppchText[i]);

        if (w > maxw)
            maxw = w;
    }
    noteH = (noteLast - noteFirst + 1) * LineHeight(smallFont);
    if (maxw > (int) displayWidth - 32 ||
        panelH + 40 + noteH > (int) displayHeight)
        noteH = 0;			/* no room: leave it out */

    panelX = ((int) displayWidth - panelW) / 2;
    panelY = ((int) displayHeight - panelH - (noteH ? noteH + 40 : 0)) / 2;

    /* drop shadow, panel, bevel */
    Fill(Mix(SKY_TOP, 0x000000, 1, 2), panelX + 6, panelY + 6, panelW, panelH);
    Fill(PANEL, panelX, panelY, panelW, panelH);
    Bevel(panelX, panelY, panelW, panelH, PANEL_LIGHT, PANEL_DARK);
    /* an etched line inside, like a Motif frame */
    Bevel(panelX + 8, panelY + 8, panelW - 16, panelH - 16,
          PANEL_DARK, PANEL_LIGHT);

    {
        int textX = panelX + 32;
        int textW = panelW - 64;

        if (logoSide)
        {
            int ly = panelY + (panelH - (int) logoH) / 2;

            XCopyArea(dpy, logo, welcome, mgc, 0, 0, logoW, logoH,
                      panelX + 24, ly);
            Bevel(panelX + 22, ly - 2, logoW + 4, logoH + 4,
                  PANEL_DARK, PANEL_LIGHT);
            textX += logoSide + 24;
            textW -= logoSide + 24;
        }

        y = panelY + (panelH - (lh + LineHeight(smallFont) + 16 + BAR_H)) / 2;
        XSetForeground(dpy, mgc, Pix(PANEL_TEXT));
        XmbDrawString(dpy, welcome, titleFont, mgc, textX, y + Ascent(titleFont),
                      (char *) title, strlen(title));
        y += lh + 4;
        XSetForeground(dpy, mgc, Pix(PANEL_DARK));
        XmbDrawString(dpy, welcome, smallFont, mgc, textX,
                      y + Ascent(smallFont), (char *) status, strlen(status));
        y += LineHeight(smallFont) + 12;

        barX = textX;
        barY = y;
        barW = textW;
        if (barW > 2 * BEVEL + 40)
        {
            Bevel(barX, barY, barW, BAR_H, PANEL_DARK, PANEL_LIGHT);
            if (pos > barW)
                pos = 0;
            DrawBar();
        }
    }

    /* the copyright text, a block centered under the panel */
    if (noteH)
    {
        int x = ((int) displayWidth - maxw) / 2;

        y = panelY + panelH + 40;
        XSetForeground(dpy, mgc, Pix(NOTE_TEXT));
        for (i = noteFirst; i <= noteLast; i++)
        {
            XmbDrawString(dpy, welcome, smallFont, mgc, x, y + Ascent(smallFont),
                          (char *) ppchText[i], strlen((char *) ppchText[i]));
            y += LineHeight(smallFont);
        }
    }
    XFlush(dpy);
}

void
ModernSplashStart(void)
{
    if (on && barW > 2 * BEVEL + 40)
        XtAppAddTimeOut(app, TICK_MS, Tick, NULL);
}

Boolean
ModernSplashOn(void)
{
    return on;
}
