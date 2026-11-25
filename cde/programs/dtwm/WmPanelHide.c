/*
 * WmPanelHide.c - Front Panel auto-hide (Settings > Front Panel >
 * "Hide automatically", the file ~/.dt/frontpanel-autohide).
 *
 * When the pointer has left the panel for a moment, and no drawer or
 * menu is open, the panel slides off its screen edge, leaving a strip
 * of HIDE_STRIP pixels; the pointer touching that strip brings it back.
 * Only a panel sitting on the top or bottom edge hides.  Under another
 * window manager (dtwm -panelonly) the panel's reserved edge shrinks to
 * the strip while hidden, so maximized windows can use the screen.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */

#include "WmGlobal.h"
#include "WmPanelOnly.h"
#include "WmWinConf.h"
#include "WmPanelHide.h"
#include "WmMultiHead.h"
#include "DataBaseLoad.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define HIDE_STRIP	2	/* pixels left on screen */
#define HIDE_DELAY	900	/* ms after the pointer leaves */


static Boolean      enabled = False;
static Boolean      hidden = False;
static int          shownX, shownY;	/* where the panel shows */
static XtIntervalId timer = 0;

/* The panel's top-left corner and size, in root coordinates. */
static Boolean
PanelGeometry(int *x, int *y, int *w, int *h)
{
    Window       child;
    Window       root;
    int          gx, gy;
    unsigned int gw, gh, bw, depth;

    if (!panel.shell || !XtIsRealized(panel.shell))
        return False;
    if (!XGetGeometry(DISPLAY, XtWindow(panel.shell), &root, &gx, &gy,
                      &gw, &gh, &bw, &depth) ||
        !XTranslateCoordinates(DISPLAY, XtWindow(panel.shell), root, 0, 0,
                               x, y, &child))
        return False;
    *w = (int) gw;
    *h = (int) gh;
    return True;
}

/* Move the panel so that its window's top-left corner is at (x, y). */
static void
PanelMove(int x, int y)
{
    ClientData *pCD = NULL;
    int         cx, cy, w, h;

    if (!PanelGeometry(&cx, &cy, &w, &h))
        return;
    if (wmGD.panelOnly)
    {
        /* the other window manager moves its dock window */
        XMoveWindow(DISPLAY, XtWindow(panel.shell), x, y);
        PanelOnlyDockWindow(XtWindow(panel.shell), x, y, w, h);
    }
    else if (!XFindContext(DISPLAY, XtWindow(panel.shell),
                           wmGD.windowContextType, (caddr_t *) &pCD) && pCD)
    {
        int ax, ay, aw, ah, i;
        int px = pCD->clientX + (x - cx), py = pCD->clientY + (y - cy);

        /* dtwm places by its own reckoning (frame border): move, see
         * where the window landed, and correct what we ask for */
        for (i = 0; i < 3; i++)
        {
            ProcessNewConfiguration(pCD, px, py, pCD->clientWidth,
                                    pCD->clientHeight, False);
            XSync(DISPLAY, False);
            if (!PanelGeometry(&ax, &ay, &aw, &ah) ||
                (ax == x && ay == y))
                break;
            px -= ax - x;
            py -= ay - y;
        }
    }
    XFlush(DISPLAY);
}

static Boolean
DrawerOpen(void)
{
    int i, j;

    for (i = 0; i < panel.box_data_count; i++)
        for (j = 0; j < panel.box_data[i]->control_data_count; j++)
        {
            SubpanelData *s = panel.box_data[i]->control_data[j]->subpanel_data;

            if (s && s->shell && XtIsManaged(s->shell))
                return True;
        }
    return False;
}

static void HideTimeout(XtPointer data, XtIntervalId *id);

static void
StartTimer(void)
{
    if (timer)
        XtRemoveTimeOut(timer);
    timer = XtAppAddTimeOut(XtWidgetToApplicationContext(panel.shell),
                            HIDE_DELAY, HideTimeout, NULL);
}

static void
Hide(void)
{
    int x, y, w, h, sh;

    if (hidden || !PanelGeometry(&x, &y, &w, &h))
        return;
    sh = DisplayHeight(DISPLAY, DefaultScreen(DISPLAY));
    shownX = x;
    shownY = y;
    if (y + h >= sh - 4)			/* on the bottom edge */
        PanelMove(x, sh - HIDE_STRIP);
    else if (y <= 4)				/* on the top edge */
        PanelMove(x, HIDE_STRIP - h);
    else
        return;					/* in the middle: stays */
    hidden = True;
}

static void
Show(void)
{
    if (!hidden)
        return;
    hidden = False;
    PanelMove(shownX, shownY);
}

static void
HideTimeout(XtPointer data, XtIntervalId *id)
{
    Window       root, child;
    int          rx, ry, wx, wy, x, y, w, h;
    unsigned int mask;

    timer = 0;
    if (!enabled || hidden || !PanelGeometry(&x, &y, &w, &h))
        return;
    /* still over the panel, a drawer or menu open, a button held:
     * try again later */
    if ((XQueryPointer(DISPLAY, XtWindow(panel.shell), &root, &child,
                       &rx, &ry, &wx, &wy, &mask) &&
         rx >= x && rx < x + w && ry >= y && ry < y + h) ||
        DrawerOpen() || wmGD.menuActive ||
        (mask & (Button1Mask | Button2Mask | Button3Mask)))
    {
        StartTimer();
        return;
    }
    Hide();
}

static void
CrossingEH(Widget w, XtPointer client, XEvent *ev, Boolean *cont)
{
    if (!enabled)
        return;
    if (ev->type == EnterNotify)
    {
        if (timer)
        {
            XtRemoveTimeOut(timer);
            timer = 0;
        }
        Show();
    }
    else if (ev->type == LeaveNotify &&
             ev->xcrossing.detail != NotifyInferior)
        StartTimer();
}

/* After the panel is created (WmFP.c). */
void
PanelAutoHideInit(void)
{
    char  path[4096];
    char *home = getenv("HOME");

    if (!home || !panel.shell)
        return;
    snprintf(path, sizeof(path), "%s/.dt/frontpanel-autohide", home);
    if (access(path, F_OK) != 0)
        return;
    enabled = True;
    XtAddEventHandler(panel.shell, EnterWindowMask | LeaveWindowMask,
                      False, CrossingEH, NULL);
    StartTimer();			/* hide shortly after login */
}

/* Show the panel now (e.g. f.toggle_frontpanel, a drag); it hides
 * again once the pointer has left it. */
void
PanelAutoHideShow(void)
{
    if (!enabled)
        return;
    Show();
    StartTimer();
}

/*
 * The monitor the panel was on was unplugged or moved (RandR change,
 * WmMultiHead.c): put the panel at the bottom middle of the primary
 * monitor, where a new session would have it.  A panel still on a
 * monitor stays where the user put it.
 */
void
PanelKeepOnScreen(void)
{
    int x, y, w, h, hx, hy, hw, hh, scr = DefaultScreen(DISPLAY);

    if (!PanelGeometry(&x, &y, &w, &h))
        return;
    if (hidden)
    {
        x = shownX;
        y = shownY;
    }
    if (RectOnAHead(DISPLAY, scr, x, y, w, h))
        return;
    GetPrimaryHead(DISPLAY, scr, &hx, &hy, &hw, &hh);
    x = hx + (hw > w ? (hw - w) / 2 : 0);
    y = hy + hh - h;
    if (hidden)
    {
        hidden = False;
        shownX = x;
        shownY = y;
    }
    PanelMove(x, y);
    if (enabled)
        StartTimer();
}
