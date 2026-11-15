/*
 * dtswitch - Alt+Tab window switcher for dtwm.
 *
 * Lists the managed windows (_NET_CLIENT_LIST_STACKING, the topmost
 * first) in a small CDE-styled list while Alt is held: Tab moves down,
 * Shift+Tab (or "dtswitch -r") up, releasing Alt switches to the
 * selected window (_NET_ACTIVE_WINDOW), Escape cancels.  A quick
 * Alt+Tab, released before the list shows, switches to the previous
 * window at once.  The Front Panel and its drawers are left out.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>

#define MAX_WIN 256

static Display *dpy;
static Window   wins[MAX_WIN];
static int      nWins, sel;
static Widget   top, list;

static Atom
A(const char *name)
{
    return XInternAtom(dpy, name, False);
}

static unsigned char *
GetProp(Window w, Atom prop, Atom type, unsigned long *n)
{
    Atom           at;
    int            af;
    unsigned long  after;
    unsigned char *data = NULL;

    *n = 0;
    if (XGetWindowProperty(dpy, w, prop, 0, 65536, False, type, &at, &af,
                           n, &after, &data) != Success || !data)
        return NULL;
    if (at != type || (type == XA_WINDOW && af != 32))
    {
        XFree(data);
        *n = 0;
        return NULL;
    }
    return data;
}

/* The window's title: _NET_WM_NAME (UTF-8), else WM_NAME. */
static char *
Title(Window w)
{
    unsigned long n;
    unsigned char *d = GetProp(w, A("_NET_WM_NAME"), A("UTF8_STRING"), &n);
    char *t;

    if (d && n > 0)
    {
        t = XtMalloc(n + 1);
        memcpy(t, d, n);
        t[n] = '\0';
        XFree(d);
        return t;
    }
    if (d)
        XFree(d);
    if (XFetchName(dpy, w, &t) && t)
    {
        char *c = XtNewString(t);

        XFree(t);
        return c;
    }
    return XtNewString("(untitled)");
}

/* Left out: the Front Panel and drawers (class Dtwm), and windows that
 * ask to be (_NET_WM_STATE_SKIP_TASKBAR). */
static Boolean
Wanted(Window w)
{
    XClassHint ch;
    unsigned long n, i;
    Atom *st;
    Boolean ok = True;

    if (XGetClassHint(dpy, w, &ch))
    {
        if (ch.res_class && strcmp(ch.res_class, "Dtwm") == 0)
            ok = False;
        if (ch.res_name) XFree(ch.res_name);
        if (ch.res_class) XFree(ch.res_class);
    }
    if (ok && (st = (Atom *) GetProp(w, A("_NET_WM_STATE"), XA_ATOM, &n)))
    {
        for (i = 0; i < n; i++)
            if (st[i] == A("_NET_WM_STATE_SKIP_TASKBAR"))
                ok = False;
        XFree(st);
    }
    return ok;
}

static void
ReadWindows(void)
{
    unsigned long n;
    long i;
    Window *w = (Window *) GetProp(DefaultRootWindow(dpy),
                                   A("_NET_CLIENT_LIST_STACKING"),
                                   XA_WINDOW, &n);

    nWins = 0;
    if (!w)
        return;
    for (i = (long) n - 1; i >= 0 && nWins < MAX_WIN; i--)	/* top first */
        if (Wanted(w[i]))
            wins[nWins++] = w[i];
    XFree(w);
}

static void
Activate(Window w)
{
    XEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = A("_NET_ACTIVE_WINDOW");
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 2;			/* from a pager */
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, DefaultRootWindow(dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(dpy);
}

/* Is an Alt key still down? */
static Boolean
AltDown(void)
{
    char keys[32];
    KeyCode l = XKeysymToKeycode(dpy, XK_Alt_L);
    KeyCode r = XKeysymToKeycode(dpy, XK_Alt_R);

    XQueryKeymap(dpy, keys);
    return (l && (keys[l / 8] & (1 << (l % 8)))) ||
           (r && (keys[r / 8] & (1 << (r % 8))));
}

static void
Select(int i)
{
    sel = (i % nWins + nWins) % nWins;
    XmListSelectPos(list, sel + 1, False);
    XmListSetBottomPos(list, sel + 1);
}

int
main(int argc, char **argv)
{
    XtAppContext app;
    Widget  form, label;
    XmString *items, s;
    Boolean reverse = (argc > 1 && strcmp(argv[1], "-r") == 0);
    int     i, x, y, sw, sh;
    Dimension w, h;

    top = XtVaAppInitialize(&app, "Dtswitch", NULL, 0, &argc, argv, NULL,
                            XmNoverrideRedirect, True,
                            XmNmwmDecorations, 0, NULL);
    dpy = XtDisplay(top);
    ReadWindows();
    if (nWins == 0)
        return 0;
    sel = reverse ? nWins - 1 : (nWins > 1 ? 1 : 0);

    /* Alt already released (a quick tap): switch at once. */
    if (!AltDown() || nWins == 1)
    {
        Activate(wins[sel]);
        return 0;
    }

    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, top,
                                   XmNmarginWidth, 8, XmNmarginHeight, 8,
                                   XmNshadowThickness, 2,
                                   XmNshadowType, XmSHADOW_OUT, NULL);
    s = XmStringCreateLocalized("Windows (Tab: next, release Alt: switch)");
    label = XtVaCreateManagedWidget("label", xmLabelWidgetClass, form,
                                    XmNlabelString, s,
                                    XmNtopAttachment, XmATTACH_FORM,
                                    XmNleftAttachment, XmATTACH_FORM, NULL);
    XmStringFree(s);
    items = (XmString *) XtMalloc(nWins * sizeof(XmString));
    for (i = 0; i < nWins; i++)
    {
        char *t = Title(wins[i]);

        items[i] = XmStringCreateLocalized(t);
        XtFree(t);
    }
    list = XtVaCreateManagedWidget("windows", xmListWidgetClass, form,
                                   XmNitems, items, XmNitemCount, nWins,
                                   XmNvisibleItemCount, nWins < 12 ? nWins : 12,
                                   XmNselectionPolicy, XmBROWSE_SELECT,
                                   XmNtopAttachment, XmATTACH_WIDGET,
                                   XmNtopWidget, label, XmNtopOffset, 6,
                                   XmNleftAttachment, XmATTACH_FORM,
                                   XmNrightAttachment, XmATTACH_FORM,
                                   XmNbottomAttachment, XmATTACH_FORM,
                                   XmNtraversalOn, False, NULL);
    for (i = 0; i < nWins; i++)
        XmStringFree(items[i]);
    XtFree((char *) items);

    /* centered on the screen */
    XtRealizeWidget(top);
    XtVaGetValues(top, XmNwidth, &w, XmNheight, &h, NULL);
    if (w < 320)
        w = 320;
    sw = DisplayWidth(dpy, DefaultScreen(dpy));
    sh = DisplayHeight(dpy, DefaultScreen(dpy));
    x = (sw - (int) w) / 2;
    y = (sh - (int) h) / 3;
    XtVaSetValues(top, XmNx, x, XmNy, y, XmNwidth, w, NULL);
    XtMapWidget(top);
    Select(sel);

    if (XGrabKeyboard(dpy, XtWindow(top), False, GrabModeAsync,
                      GrabModeAsync, CurrentTime) != GrabSuccess)
    {
        Activate(wins[sel]);
        return 0;
    }

    for (;;)
    {
        XEvent ev;
        KeySym ks;

        XtAppNextEvent(app, &ev);
        if (ev.type == KeyPress)
        {
            ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_Tab || ks == XK_ISO_Left_Tab)
                Select((ev.xkey.state & ShiftMask) || ks == XK_ISO_Left_Tab
                       ? sel - 1 : sel + 1);
            else if (ks == XK_Down || ks == XK_Right)
                Select(sel + 1);
            else if (ks == XK_Up || ks == XK_Left)
                Select(sel - 1);
            else if (ks == XK_Escape)
                break;
            else if (ks == XK_Return || ks == XK_KP_Enter)
            {
                Activate(wins[sel]);
                break;
            }
            continue;
        }
        if (ev.type == KeyRelease)
        {
            ks = XLookupKeysym(&ev.xkey, 0);
            if ((ks == XK_Alt_L || ks == XK_Alt_R || ks == XK_Meta_L ||
                 ks == XK_Meta_R) && !(AltDown()))
            {
                Activate(wins[sel]);
                break;
            }
            continue;
        }
        XtDispatchEvent(&ev);
    }
    XUngrabKeyboard(dpy, CurrentTime);
    XFlush(dpy);
    return 0;
}
