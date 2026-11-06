/*
 * WmPanelOnly.c - dtwm -panelonly: the Front Panel, backdrops and the
 * Workspace menu under another window manager.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 *
 * With -panelonly dtwm does not manage windows (another, EWMH window
 * manager such as xfwm4 does).  It still draws the Front Panel and the
 * workspace backdrops, answers dtsession, and keeps its workspaces in
 * step with the window manager's desktops:
 *
 *   - at start the window manager gets as many desktops as dtwm has
 *     workspaces, with the same names;
 *   - the Front Panel's workspace buttons switch desktops through
 *     _NET_CURRENT_DESKTOP;
 *   - switching desktops in the window manager (keys, pager) switches
 *     the Front Panel and the backdrop.
 *
 * The Front Panel is a dock window on every desktop, with a strut so
 * maximized windows stay clear of it.
 *
 * The backdrop is drawn on the root window and on a full-screen window
 * of type _NET_WM_WINDOW_TYPE_DESKTOP (as xfdesktop does): the window
 * manager keeps it below everything, and clicks on it post the
 * Workspace menu, since the window manager holds the root window's
 * button presses.
 */
#include "WmGlobal.h"
#include "WmPanelOnly.h"
#include "WmWrkspace.h"
#include "WmIconBox.h"
#include "WmEvent.h"

#include <stdlib.h>

#include <X11/Xatom.h>
#include <string.h>

/* Set while following the window manager, so ChangeToWorkspace does
 * not echo the switch back to it. */
static Boolean following;

static Window desktopWin;

static Atom
A(const char *name)
{
    return XInternAtom(DISPLAY, name, False);
}

static void
SendRootMessage(WmScreenData *pSD, Window w, Atom type,
                long l0, long l1, long l2, long l3, long l4)
{
    XEvent ev;

    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = type;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = l0;
    ev.xclient.data.l[1] = l1;
    ev.xclient.data.l[2] = l2;
    ev.xclient.data.l[3] = l3;
    ev.xclient.data.l[4] = l4;
    XSendEvent(DISPLAY, pSD->rootWindow, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
}

static int
WorkspaceIndex(WmWorkspaceData *pWS)
{
    WmScreenData *pSD = pWS->pSD;

    for (int i = 0; i < pSD->numWorkspaces; i++)
        if (&pSD->pWS[i] == pWS)
            return i;
    return -1;
}

/*
 * Give the window manager our workspaces: their number and names, and
 * make its current desktop ours.
 */
void
PanelOnlyInit(WmScreenData *pSD)
{
    size_t len = 0;
    char *names;

    SendRootMessage(pSD, pSD->rootWindow, A("_NET_NUMBER_OF_DESKTOPS"),
                    pSD->numWorkspaces, 0, 0, 0, 0);

    /* _NET_DESKTOP_NAMES: UTF-8 strings, each NUL-terminated.  The
     * visible names are the workspace titles ("One", ...). */
    names = NULL;
    for (int i = 0; i < pSD->numWorkspaces; i++) {
        String t = WmXmStringToString(pSD->pWS[i].title);
        const char *title = t ? t : pSD->pWS[i].name;
        size_t n = strlen(title) + 1;

        names = XtRealloc(names, len + n);
        memcpy(names + len, title, n);
        len += n;
        free(t);
    }
    XChangeProperty(DISPLAY, pSD->rootWindow, A("_NET_DESKTOP_NAMES"),
                    A("UTF8_STRING"), 8, PropModeReplace,
                    (unsigned char *) names, (int) len);
    XtFree(names);

    int cur = WorkspaceIndex(pSD->pActiveWS);
    if (cur >= 0)
        PanelOnlySetDesktop(pSD, cur);
}

/* The Front Panel switched workspace: switch the window manager. */
void
PanelOnlySetDesktop(WmScreenData *pSD, int index)
{
    if (following || index < 0)
        return;
    SendRootMessage(pSD, pSD->rootWindow, A("_NET_CURRENT_DESKTOP"),
                    index, CurrentTime, 0, 0, 0);
}

void
PanelOnlyWorkspaceChanged(WmWorkspaceData *pWS)
{
    PanelOnlySetDesktop(pWS->pSD, WorkspaceIndex(pWS));
}

/*
 * A root window property changed.  Returns True if it was the window
 * manager's current desktop (now followed).
 */
Boolean
PanelOnlyRootProperty(XPropertyEvent *ev)
{
    WmScreenData *pSD = ACTIVE_PSD;
    Atom type;
    int format;
    unsigned long n, after;
    unsigned char *data = NULL;
    long desk = -1;

    if (ev->atom != A("_NET_CURRENT_DESKTOP") || ev->state != PropertyNewValue)
        return False;

    if (XGetWindowProperty(DISPLAY, ev->window, ev->atom, 0, 1, False,
                           XA_CARDINAL, &type, &format, &n, &after,
                           &data) == Success && data) {
        if (format == 32 && n == 1)
            desk = *(long *) data;
        XFree(data);
    }
    if (desk >= 0 && desk < pSD->numWorkspaces &&
        &pSD->pWS[desk] != pSD->pActiveWS) {
        following = True;
        ChangeToWorkspace(&pSD->pWS[desk]);
        following = False;
    }
    return True;
}

/*
 * Make the Front Panel shell a dock on every desktop, reserving its
 * height at the screen edge it sits on.
 */
void
PanelOnlyDockWindow(Window w, int x, int y, int width, int height)
{
    WmScreenData *pSD = ACTIVE_PSD;
    int sh = DisplayHeight(DISPLAY, pSD->screen);
    Atom type = A("_NET_WM_WINDOW_TYPE_DOCK");
    Atom state[2] = { A("_NET_WM_STATE_STICKY"), A("_NET_WM_STATE_ABOVE") };
    long all = 0xFFFFFFFFL;
    /* left right top bottom, then start/end for each */
    long strut[12] = { 0 };

    if (y + height / 2 >= sh / 2) {     /* bottom half: reserve bottom */
        strut[3] = sh - y;
        strut[10] = x;
        strut[11] = x + width - 1;
    } else {
        strut[2] = y + height;
        strut[8] = x;
        strut[9] = x + width - 1;
    }

    XChangeProperty(DISPLAY, w, A("_NET_WM_WINDOW_TYPE"), XA_ATOM, 32,
                    PropModeReplace, (unsigned char *) &type, 1);
    XChangeProperty(DISPLAY, w, A("_NET_WM_STATE"), XA_ATOM, 32,
                    PropModeReplace, (unsigned char *) state, 2);
    XChangeProperty(DISPLAY, w, A("_NET_WM_DESKTOP"), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *) &all, 1);
    XChangeProperty(DISPLAY, w, A("_NET_WM_STRUT_PARTIAL"), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *) strut, 12);
    XChangeProperty(DISPLAY, w, A("_NET_WM_STRUT"), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *) strut, 4);
}

/* The full-screen desktop window (see above), created on first use. */
static void
CreateDesktopWindow(WmScreenData *pSD)
{
    XSetWindowAttributes xswa;
    Atom type = A("_NET_WM_WINDOW_TYPE_DESKTOP");
    Atom state[2] = { A("_NET_WM_STATE_STICKY"), A("_NET_WM_STATE_BELOW") };
    long all = 0xFFFFFFFFL;
    XWMHints hints;
    XClassHint klass = { "dtwm-desktop", "Dtwm" };

    xswa.event_mask = ButtonPressMask | ButtonReleaseMask;
    xswa.background_pixel = BlackPixel(DISPLAY, pSD->screen);
    desktopWin = XCreateWindow(DISPLAY, pSD->rootWindow, 0, 0,
                               DisplayWidth(DISPLAY, pSD->screen),
                               DisplayHeight(DISPLAY, pSD->screen), 0,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWEventMask | CWBackPixel, &xswa);

    hints.flags = InputHint;
    hints.input = False;            /* never takes the keyboard focus */
    XSetWMHints(DISPLAY, desktopWin, &hints);
    XSetClassHint(DISPLAY, desktopWin, &klass);
    XStoreName(DISPLAY, desktopWin, "Desktop");
    XChangeProperty(DISPLAY, desktopWin, A("_NET_WM_WINDOW_TYPE"), XA_ATOM,
                    32, PropModeReplace, (unsigned char *) &type, 1);
    XChangeProperty(DISPLAY, desktopWin, A("_NET_WM_STATE"), XA_ATOM, 32,
                    PropModeReplace, (unsigned char *) state, 2);
    XChangeProperty(DISPLAY, desktopWin, A("_NET_WM_DESKTOP"), XA_CARDINAL,
                    32, PropModeReplace, (unsigned char *) &all, 1);
    XMapWindow(DISPLAY, desktopWin);
}

/* The backdrop changed: show it on the desktop window too. */
void
PanelOnlySetBackdrop(WmScreenData *pSD, Pixmap pixmap, Pixel pixel,
                     Boolean usePixmap)
{
    if (!desktopWin)
        CreateDesktopWindow(pSD);
    if (usePixmap)
        XSetWindowBackgroundPixmap(DISPLAY, desktopWin, pixmap);
    else
        XSetWindowBackground(DISPLAY, desktopWin, pixel);
    XClearWindow(DISPLAY, desktopWin);
}

void
PanelOnlyScreenChanged(WmScreenData *pSD)
{
    if (desktopWin)
        XMoveResizeWindow(DISPLAY, desktopWin, 0, 0,
                          DisplayWidth(DISPLAY, pSD->screen),
                          DisplayHeight(DISPLAY, pSD->screen));
}

/*
 * Clicks on the desktop window are clicks on the desktop: hand them to
 * the root window code (the Workspace menu).  Returns True if the
 * event was for the desktop window.
 */
Boolean
PanelOnlyDesktopEvent(XEvent *event)
{
    if (!desktopWin || event->xany.window != desktopWin)
        return False;
    if (event->type == ButtonPress || event->type == ButtonRelease) {
        event->xbutton.window = ACTIVE_ROOT;
        event->xbutton.subwindow = None;
        event->xbutton.x = event->xbutton.x_root;
        event->xbutton.y = event->xbutton.y_root;
        (void) WmDispatchWsEvent(event);
    }
    return True;
}
