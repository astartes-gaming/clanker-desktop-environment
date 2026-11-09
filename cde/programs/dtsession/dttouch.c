/*
 * dttouch - touchscreen gestures for CDE and other X11 applications.
 *
 * CDE's Motif applications know only the mouse.  On a touchscreen the
 * X server turns the first finger into the pointer, so taps click and
 * one-finger drags drag, but there is no right button (CDE's context
 * menus) and no wheel.  dttouch adds them:
 *
 *   long press (0.6 s, finger still)   right click where the finger is
 *   two-finger drag                    scroll (wheel buttons 4-7)
 *
 * It holds a touch grab (XInput 2.2) on the root window, so it sees each
 * touch before any application.  A touch that turns out to be a tap or
 * a one-finger drag is rejected and the server replays it to the
 * application as usual (pointer emulation); a long press or a
 * two-finger gesture is accepted (the application never sees it) and
 * dttouch sends the right click or the wheel clicks with XTest.
 *
 * Environment: DT_TOUCH_HOLD_MS (long-press time, default 600),
 * DT_TOUCH_SCROLL_PX (finger travel per wheel click, default 40).
 * Exits quietly when the X server has no XInput 2.2 or another program
 * already grabs touches.  With -probe it only says whether a
 * touchscreen is present (exit status 0 if so).
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>

#define MAX_TOUCHES 10

typedef struct {
    int    used;
    unsigned int id;	/* touch id */
    int    dev;		/* device the touch came from */
    double x0, y0, x, y;	/* root coordinates: start, now */
    long   t0;		/* start, in ms */
} Touch;

static Display *dpy;
static Window   root;
static int      xi_opcode;
static Touch    touches[MAX_TOUCHES];
static int      hold_ms = 600, scroll_px = 40, slop = 12;
static int      scrolling;		/* two-finger mode */
static double   sx, sy;			/* scroll: last centroid */

static long
now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000L + tv.tv_usec / 1000;
}

static Touch *
find(unsigned int id)
{
    int i;

    for (i = 0; i < MAX_TOUCHES; i++)
        if (touches[i].used && touches[i].id == id)
            return &touches[i];
    return NULL;
}

static int
count(void)
{
    int i, n = 0;

    for (i = 0; i < MAX_TOUCHES; i++)
        n += touches[i].used;
    return n;
}

/* Give the touch back to the applications (replayed as pointer
 * events), or keep it; either way dttouch is done with it. */
static void
decide(Touch *t, int accept)
{
    XIAllowTouchEvents(dpy, t->dev, t->id, root,
                       accept ? XIAcceptTouch : XIRejectTouch);
    t->used = 0;
}

static void
click(int button, double x, double y)
{
    XTestFakeMotionEvent(dpy, -1, (int) x, (int) y, CurrentTime);
    XTestFakeButtonEvent(dpy, button, True, CurrentTime);
    XTestFakeButtonEvent(dpy, button, False, CurrentTime);
    XFlush(dpy);
}

static void
centroid(double *cx, double *cy)
{
    int i, n = 0;

    *cx = *cy = 0;
    for (i = 0; i < MAX_TOUCHES; i++)
        if (touches[i].used) {
            *cx += touches[i].x;
            *cy += touches[i].y;
            n++;
        }
    if (n) {
        *cx /= n;
        *cy /= n;
    }
}

/* Wheel clicks for the centroid's travel since the last ones. */
static void
scroll(void)
{
    double cx, cy;

    centroid(&cx, &cy);
    XTestFakeMotionEvent(dpy, -1, (int) cx, (int) cy, CurrentTime);
    /* Content follows the fingers: moving them up scrolls down. */
    while (sy - cy >= scroll_px) { click(5, cx, cy); sy -= scroll_px; }
    while (cy - sy >= scroll_px) { click(4, cx, cy); sy += scroll_px; }
    while (sx - cx >= scroll_px) { click(7, cx, cy); sx -= scroll_px; }
    while (cx - sx >= scroll_px) { click(6, cx, cy); sx += scroll_px; }
}

static void
touch_event(XIDeviceEvent *ev)
{
    Touch *t = find(ev->detail);
    int    i;

    switch (ev->evtype) {
    case XI_TouchBegin:
        if (t == NULL) {
            for (i = 0; i < MAX_TOUCHES && touches[i].used; i++)
                ;
            if (i == MAX_TOUCHES) {	/* too many fingers: pass it on */
                XIAllowTouchEvents(dpy, ev->deviceid, ev->detail, root,
                                   XIRejectTouch);
                return;
            }
            t = &touches[i];
            t->used = 1;
            t->id = ev->detail;
            t->dev = ev->deviceid;
            t->x0 = t->x = ev->root_x;
            t->y0 = t->y = ev->root_y;
            t->t0 = now_ms();
        }
        if (count() >= 2 && !scrolling) {
            /* Two fingers: a scroll gesture; keep all its touches. */
            scrolling = 1;
            for (i = 0; i < MAX_TOUCHES; i++)
                if (touches[i].used)
                    XIAllowTouchEvents(dpy, touches[i].dev, touches[i].id,
                                       root, XIAcceptTouch);
            centroid(&sx, &sy);
        } else if (scrolling) {
            XIAllowTouchEvents(dpy, t->dev, t->id, root, XIAcceptTouch);
        }
        break;

    case XI_TouchUpdate:
        if (t == NULL)
            break;
        t->x = ev->root_x;
        t->y = ev->root_y;
        if (scrolling)
            scroll();
        else if ((t->x - t->x0) * (t->x - t->x0) +
                 (t->y - t->y0) * (t->y - t->y0) > slop * slop)
            decide(t, 0);		/* a drag: the application's */
        break;

    case XI_TouchEnd:
        if (t == NULL)
            break;
        if (scrolling) {
            t->used = 0;		/* already accepted */
            if (count() == 0)
                scrolling = 0;
            else
                centroid(&sx, &sy);
        } else
            decide(t, 0);		/* a tap: the application's */
        break;
    }
}

/* A single finger held still long enough: right click. */
static void
check_hold(void)
{
    int i;

    if (scrolling)
        return;
    for (i = 0; i < MAX_TOUCHES; i++) {
        Touch *t = &touches[i];

        if (t->used && now_ms() - t->t0 >= hold_ms) {
            double x = t->x, y = t->y;

            decide(t, 1);
            click(3, x, y);
        }
    }
}

/* Time until the next long press is due, or -1. */
static long
next_timeout(void)
{
    long best = -1, left;
    int  i;

    if (scrolling)
        return -1;
    for (i = 0; i < MAX_TOUCHES; i++)
        if (touches[i].used) {
            left = touches[i].t0 + hold_ms - now_ms();
            if (left < 0)
                left = 0;
            if (best < 0 || left < best)
                best = left;
        }
    return best;
}

/* Is there a direct-touch device (a touchscreen)? */
static int
have_touchscreen(void)
{
    XIDeviceInfo *info;
    int n, i, j, found = 0;

    info = XIQueryDevice(dpy, XIAllDevices, &n);
    for (i = 0; i < n && !found; i++)
        for (j = 0; j < info[i].num_classes; j++) {
            XITouchClassInfo *tc = (XITouchClassInfo *) info[i].classes[j];

            if (tc->type == XITouchClass && tc->mode == XIDirectTouch) {
                found = 1;
                break;
            }
        }
    XIFreeDeviceInfo(info);
    return found;
}

static int grab_failed;

static int
on_error(Display *d, XErrorEvent *e)
{
    grab_failed = 1;
    return 0;
}

int
main(int argc, char **argv)
{
    int major = 2, minor = 2, ev, err, xt_ev, xt_err, xt_maj, xt_min;
    unsigned char bits[XIMaskLen(XI_LASTEVENT)] = { 0 };
    XIEventMask mask;
    XIGrabModifiers mods;
    const char *s;
    int probe = argc > 1 && strcmp(argv[1], "-probe") == 0;

    if ((dpy = XOpenDisplay(NULL)) == NULL)
        return 1;
    root = DefaultRootWindow(dpy);
    if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &ev, &err) ||
        XIQueryVersion(dpy, &major, &minor) != Success ||
        major * 100 + minor < 202)
        return probe ? 1 : 0;		/* no touch support in this server */
    if (probe) {
        int r = have_touchscreen();

        printf("touchscreen: %s\n", r ? "yes" : "no");
        return r ? 0 : 1;
    }
    if (!XTestQueryExtension(dpy, &xt_ev, &xt_err, &xt_maj, &xt_min))
        return 0;

    if ((s = getenv("DT_TOUCH_HOLD_MS")) && atoi(s) > 100)
        hold_ms = atoi(s);
    if ((s = getenv("DT_TOUCH_SCROLL_PX")) && atoi(s) > 4)
        scroll_px = atoi(s);

    XISetMask(bits, XI_TouchBegin);
    XISetMask(bits, XI_TouchUpdate);
    XISetMask(bits, XI_TouchEnd);
    mask.deviceid = XIAllMasterDevices;
    mask.mask_len = sizeof(bits);
    mask.mask = bits;
    mods.modifiers = XIAnyModifier;

    XSetErrorHandler(on_error);
    if (XIGrabTouchBegin(dpy, XIAllMasterDevices, root, False, &mask,
                         1, &mods) != 0)
        grab_failed = 1;
    XSync(dpy, False);
    if (grab_failed) {
        fprintf(stderr, "dttouch: touches are grabbed by another program\n");
        return 0;
    }
    XSetErrorHandler(NULL);

    for (;;) {
        long   to;
        fd_set fds;
        struct timeval tv;

        while (XPending(dpy)) {
            XEvent e;

            XNextEvent(dpy, &e);
            if (e.type == GenericEvent && e.xcookie.extension == xi_opcode &&
                XGetEventData(dpy, &e.xcookie)) {
                touch_event((XIDeviceEvent *) e.xcookie.data);
                XFreeEventData(dpy, &e.xcookie);
            }
        }
        check_hold();
        XFlush(dpy);

        to = next_timeout();
        FD_ZERO(&fds);
        FD_SET(ConnectionNumber(dpy), &fds);
        tv.tv_sec = to / 1000;
        tv.tv_usec = (to % 1000) * 1000;
        (void) select(ConnectionNumber(dpy) + 1, &fds, NULL, NULL,
                      to < 0 ? NULL : &tv);
    }
}
