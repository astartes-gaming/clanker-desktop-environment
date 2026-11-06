/*
 * WmSysTray.c - a system tray in the Front Panel.
 *
 * A Front Panel control of TYPE systray becomes a freedesktop.org
 * system tray (System Tray Protocol 0.3 with XEMBED): it owns the
 * _NET_SYSTEM_TRAY_S<screen> selection, applications ask it to dock
 * their icon windows, and it reparents them into the control, laid
 * out in rows that fit the panel's height.  The control grows to the
 * right as icons arrive.
 *
 * Front Panel controls are gadgets, with no window of their own, so
 * the tray is a small window of ours laid over the control, inside
 * the box's window.  Its events, and those of the icons and of the
 * selection owner, are routed to the box widget (XtRegisterDrawable)
 * and handled by raw event handlers, which leave the box's own event
 * mask alone.
 *
 * Only core Xlib and Xt (X11R6 XtRegisterDrawable) are used, so this
 * builds wherever dtwm does.  If another program already owns the
 * tray selection, the control stays empty and that tray keeps it.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>

#include "WmSysTray.h"

#define SYSTEM_TRAY_REQUEST_DOCK    0
#define SYSTEM_TRAY_BEGIN_MESSAGE   1
#define SYSTEM_TRAY_CANCEL_MESSAGE  2

#define XEMBED_EMBEDDED_NOTIFY      0
#define XEMBED_MAPPED               (1 << 0)

#define TRAY_PAD    3       /* space around and between icons */

typedef struct {
    Window  win;
    Boolean mapped;         /* the icon's XEMBED_MAPPED flag */
} TrayIcon;

static Widget     tray_w;           /* the Front Panel control (gadget) */
static Widget     tray_parent;      /* the box holding it */
static Window     tray_win;         /* our window over the control */
static Window     tray_owner;       /* owns the selection */
static Atom       a_selection, a_opcode, a_manager, a_xembed,
                  a_xembed_info, a_orientation;
static TrayIcon  *icons;
static int        n_icons, max_icons;
static Dimension  base_width;       /* the control's width when empty */
static Boolean    tray_active;


/* Lay our window over the control (which may have moved). */
static void
PlaceWindow (void)
{
    Position  x, y;
    Dimension w, h;

    XtVaGetValues (tray_w, XtNx, &x, XtNy, &y,
                   XtNwidth, &w, XtNheight, &h, NULL);
    if (w > 0 && h > 0)
        XMoveResizeWindow (XtDisplay (tray_parent), tray_win, x, y, w, h);
}


/* Icon size: what fits two rows in the panel, 16..24 pixels. */
static int
IconSize (int height)
{
    int size = (height - 3 * TRAY_PAD) / 2;

    if (size > 24) size = 24;
    if (size < 16) size = (height - 2 * TRAY_PAD < 16) ?
                          height - 2 * TRAY_PAD : 16;
    return size > 0 ? size : 1;
}


static void
Layout (void)
{
    Display  *dpy = XtDisplay (tray_parent);
    Dimension width, height;
    int       size, rows, cols, visible, i, k;

    XtVaGetValues (tray_w, XtNheight, &height, NULL);
    size = IconSize (height);
    rows = (height - TRAY_PAD) / (size + TRAY_PAD);
    if (rows < 1) rows = 1;

    for (i = visible = 0; i < n_icons; i++)
        if (icons[i].mapped)
            visible++;
    cols = (visible + rows - 1) / rows;

    /* Center the block of icons vertically. */
    {
        int used = rows * size + (rows - 1) * TRAY_PAD;
        int y0 = ((int) height - used) / 2;
        int x0 = TRAY_PAD;

        for (i = k = 0; i < n_icons; i++) {
            if (!icons[i].mapped) {
                XUnmapWindow (dpy, icons[i].win);
                continue;
            }
            XMoveResizeWindow (dpy, icons[i].win,
                               x0 + (k / rows) * (size + TRAY_PAD),
                               y0 + (k % rows) * (size + TRAY_PAD),
                               size, size);
            XMapRaised (dpy, icons[i].win);
            k++;
        }
    }

    width = cols * (size + TRAY_PAD) + TRAY_PAD;
    if (width < base_width)
        width = base_width;
    XtVaSetValues (tray_w, XtNwidth, width, NULL);
    PlaceWindow ();
}


static int
FindIcon (Window w)
{
    int i;

    for (i = 0; i < n_icons; i++)
        if (icons[i].win == w)
            return i;
    return -1;
}


/* The XEMBED_MAPPED flag from the icon's _XEMBED_INFO (default on). */
static Boolean
IconWantsMap (Display *dpy, Window w)
{
    Atom           type;
    int            format;
    unsigned long  n, after;
    unsigned char *data = NULL;
    Boolean        mapped = True;

    if (XGetWindowProperty (dpy, w, a_xembed_info, 0, 2, False,
                            a_xembed_info, &type, &format, &n, &after,
                            &data) == Success && data != NULL) {
        if (type == a_xembed_info && format == 32 && n >= 2)
            mapped = (((unsigned long *) data)[1] & XEMBED_MAPPED) != 0;
        XFree (data);
    }
    return mapped;
}


static void
RemoveIcon (int i, Boolean give_back)
{
    Display *dpy = XtDisplay (tray_parent);
    Window   w = icons[i].win;

    XtUnregisterDrawable (dpy, w);
    if (give_back) {
        XSelectInput (dpy, w, NoEventMask);
        XUnmapWindow (dpy, w);
        XReparentWindow (dpy, w, RootWindowOfScreen (XtScreen (tray_parent)),
                         0, 0);
        XRemoveFromSaveSet (dpy, w);
    }
    memmove (&icons[i], &icons[i + 1],
             (n_icons - i - 1) * sizeof (TrayIcon));
    n_icons--;
}


static void
Dock (Window w)
{
    Display *dpy = XtDisplay (tray_parent);
    XEvent   ev;

    if (w == None || FindIcon (w) >= 0)
        return;

    if (n_icons == max_icons) {
        int       n = max_icons ? max_icons * 2 : 8;
        TrayIcon *p = (TrayIcon *) realloc (icons, n * sizeof (TrayIcon));

        if (p == NULL)
            return;
        icons = p;
        max_icons = n;
    }

    /* The icon window belongs to another client: if it vanishes before
     * we get here, the X errors are ignored by dtwm's error handler. */
    XSelectInput (dpy, w, StructureNotifyMask | PropertyChangeMask);
    XtRegisterDrawable (dpy, w, tray_parent);
    XAddToSaveSet (dpy, w);         /* survive a dtwm restart */
    XReparentWindow (dpy, w, tray_win, 0, 0);

    icons[n_icons].win = w;
    icons[n_icons].mapped = IconWantsMap (dpy, w);
    n_icons++;

    memset (&ev, 0, sizeof (ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = a_xembed;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = CurrentTime;
    ev.xclient.data.l[1] = XEMBED_EMBEDDED_NOTIFY;
    ev.xclient.data.l[3] = (long) tray_win;
    ev.xclient.data.l[4] = 0;       /* XEMBED protocol version */
    XSendEvent (dpy, w, False, NoEventMask, &ev);

    Layout ();
}


static void
TrayEvent (Widget w, XtPointer client_data, XEvent *ev, Boolean *cont)
{
    int i;

    switch (ev->type) {
    case ClientMessage:
        if (ev->xclient.window != tray_owner)
            break;
        if (ev->xclient.message_type == a_opcode &&
            ev->xclient.data.l[1] == SYSTEM_TRAY_REQUEST_DOCK)
            Dock ((Window) ev->xclient.data.l[2]);
        break;

    case DestroyNotify:
        if ((i = FindIcon (ev->xdestroywindow.window)) >= 0) {
            RemoveIcon (i, False);
            Layout ();
        }
        break;

    case ReparentNotify:
        /* Taken away from us (by the application or another tray). */
        if ((i = FindIcon (ev->xreparent.window)) >= 0 &&
            ev->xreparent.parent != tray_win) {
            RemoveIcon (i, False);
            Layout ();
        }
        break;

    case PropertyNotify:
        if (ev->xproperty.atom == a_xembed_info &&
            (i = FindIcon (ev->xproperty.window)) >= 0) {
            icons[i].mapped = IconWantsMap (XtDisplay (tray_parent),
                                            icons[i].win);
            Layout ();
        }
        break;

    case SelectionClear:
        /* Another tray took over: hand it our icons. */
        if (ev->xselectionclear.window == tray_owner &&
            ev->xselectionclear.selection == a_selection) {
            tray_active = False;
            while (n_icons > 0)
                RemoveIcon (n_icons - 1, True);
            Layout ();
        }
        break;
    }
}


static void
Start (void)
{
    Display *dpy = XtDisplay (tray_parent);
    Screen  *scr = XtScreen (tray_parent);
    char     name[64];
    long     orientation = 0;       /* _NET_SYSTEM_TRAY_ORIENTATION_HORZ */
    XEvent   ev;

    sprintf (name, "_NET_SYSTEM_TRAY_S%d", XScreenNumberOfScreen (scr));
    a_selection   = XInternAtom (dpy, name, False);
    a_opcode      = XInternAtom (dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
    a_manager     = XInternAtom (dpy, "MANAGER", False);
    a_xembed      = XInternAtom (dpy, "_XEMBED", False);
    a_xembed_info = XInternAtom (dpy, "_XEMBED_INFO", False);
    a_orientation = XInternAtom (dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);

    if (XGetSelectionOwner (dpy, a_selection) != None)
        return;                     /* another tray is running */

    tray_owner = XCreateSimpleWindow (dpy, RootWindowOfScreen (scr),
                                      -1, -1, 1, 1, 0, 0, 0);
    XChangeProperty (dpy, tray_owner, a_orientation, XA_CARDINAL, 32,
                     PropModeReplace, (unsigned char *) &orientation, 1);
    XtRegisterDrawable (dpy, tray_owner, tray_parent);

    XSetSelectionOwner (dpy, a_selection, tray_owner, CurrentTime);
    if (XGetSelectionOwner (dpy, a_selection) != tray_owner)
        return;
    tray_active = True;

    /* Our window over the control holds the icons; the panel's
     * background shows through it. */
    {
        Position  x, y;
        Dimension w, h;

        XtVaGetValues (tray_w, XtNx, &x, XtNy, &y,
                       XtNwidth, &w, XtNheight, &h, NULL);
        tray_win = XCreateSimpleWindow (dpy, XtWindow (tray_parent),
                                        x, y, w ? w : 1, h ? h : 1, 0, 0, 0);
        XSetWindowBackgroundPixmap (dpy, tray_win, ParentRelative);
        XSelectInput (dpy, tray_win, SubstructureNotifyMask);
        XtRegisterDrawable (dpy, tray_win, tray_parent);
        XMapRaised (dpy, tray_win);
    }
    XtAddRawEventHandler (tray_parent,
                          SubstructureNotifyMask | StructureNotifyMask |
                          PropertyChangeMask, True, TrayEvent, NULL);

    /* Tell waiting applications that a tray is here. */
    memset (&ev, 0, sizeof (ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = RootWindowOfScreen (scr);
    ev.xclient.message_type = a_manager;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = CurrentTime;
    ev.xclient.data.l[1] = (long) a_selection;
    ev.xclient.data.l[2] = (long) tray_owner;
    XSendEvent (dpy, RootWindowOfScreen (scr), False,
                StructureNotifyMask, &ev);
}


/* Wait until the box is on screen before taking the tray selection. */
static void
StartWhenShown (XtPointer client_data, XtIntervalId *id)
{
    XWindowAttributes wa;

    if (XtIsRealized (tray_parent) &&
        XGetWindowAttributes (XtDisplay (tray_parent),
                              XtWindow (tray_parent), &wa) &&
        wa.map_state == IsViewable)
        Start ();
    else
        XtAppAddTimeOut (XtWidgetToApplicationContext (tray_parent), 500,
                         StartWhenShown, NULL);
}


void
SysTrayCreate (Widget control)
{
    if (tray_w != NULL || getenv ("DT_NO_SYSTRAY") != NULL)
        return;                     /* one tray per panel */
    tray_w = control;
    tray_parent = XtIsWidget (control) ? control : XtParent (control);
    XtVaGetValues (control, XtNwidth, &base_width, NULL);

    XtAppAddTimeOut (XtWidgetToApplicationContext (tray_parent), 500,
                     StartWhenShown, NULL);
}


/* dtwm is going away: give the icons back to the root window, so the
 * applications live on and dock again with the next tray. */
void
SysTrayShutdown (void)
{
    if (tray_w == NULL || !tray_active)
        return;
    while (n_icons > 0)
        RemoveIcon (n_icons - 1, True);
    XFlush (XtDisplay (tray_parent));
}
