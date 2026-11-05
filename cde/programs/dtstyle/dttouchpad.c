/*
 * dttouchpad - apply the touchpad settings chosen in Style Manager.
 *
 *   dttouchpad [-apply]   apply $HOME/.dt/touchpad to every touchpad
 *   dttouchpad -watch     apply, then stay running and re-apply when a
 *                         pointer device is added or re-enabled
 *                         (hotplug, resume from suspend)
 *   dttouchpad -probe     print what the touchpads support and their
 *                         current settings
 *
 * Started with -watch by Xsession.d/0070.dtmodern; exits with the X
 * session.  Exits quietly on servers without XInput device properties.
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
#ifdef HAVE_XI_PROPS
#include <X11/extensions/XInput.h>
#endif

#include "Touchpad.h"

static void
print_bool(const char *name, int v)
{
    printf("%s=%s\n", name, v == TP_UNSET ? "unknown" : v ? "1" : "0");
}

static int
probe(Display *dpy)
{
    TpSettings s;
    int caps = TpProbe(dpy);

    if (caps == 0) {
	printf("no touchpad\n");
	return 1;
    }
    printf("capabilities=%s%s%s%s%s\n",
	   caps & TP_CAP_TAP ? " tapping" : "",
	   caps & TP_CAP_NATURAL ? " natural_scrolling" : "",
	   caps & TP_CAP_SCROLL ? " scroll_method" : "",
	   caps & TP_CAP_DWT ? " disable_while_typing" : "",
	   caps & TP_CAP_SPEED ? " speed" : "");
    TpGetCurrent(dpy, &s);
    print_bool("tapping", s.tapping);
    print_bool("natural_scrolling", s.natural);
    printf("scroll_method=%s\n", s.scroll == TP_SCROLL_TWOFINGER ?
	   "twofinger" : s.scroll == TP_SCROLL_EDGE ? "edge" : "unknown");
    print_bool("disable_while_typing", s.dwt);
    if (s.speed != TP_UNSET)
	printf("speed=%d\n", s.speed);
    return 0;
}

static void
apply(Display *dpy)
{
    TpSettings s;

    if (TpLoad(&s))
	TpApply(dpy, &s);
}

#ifdef HAVE_XI_PROPS
static void
watch(Display *dpy)
{
    XEventClass cls;
    XEvent ev;
    int presence = -1, op, evbase, errbase;

    /* Keep running even without a touchpad: one may be plugged in. */
    if (!XQueryExtension(dpy, INAME, &op, &evbase, &errbase))
	return;
    DevicePresence(dpy, presence, cls);
    if (XSelectExtensionEvent(dpy, DefaultRootWindow(dpy), &cls, 1) != 0)
	return;
    for (;;) {
	XNextEvent(dpy, &ev);	/* exits via the I/O error handler */
	if (ev.type == presence) {
	    XDevicePresenceNotifyEvent *p = (XDevicePresenceNotifyEvent *) &ev;
#if defined(DeviceAdded) && defined(DeviceEnabled)
	    if (p->devchange != DeviceAdded && p->devchange != DeviceEnabled)
		continue;
#endif
	    apply(dpy);
	}
    }
}
#endif

int
main(int argc, char **argv)
{
    Display *dpy;
    const char *mode = argc > 1 ? argv[1] : "-apply";

    if (strcmp(mode, "-apply") != 0 && strcmp(mode, "-watch") != 0 &&
	strcmp(mode, "-probe") != 0) {
	fprintf(stderr, "usage: %s [-apply | -watch | -probe]\n", argv[0]);
	return 2;
    }
    if ((dpy = XOpenDisplay(NULL)) == NULL) {
	fprintf(stderr, "%s: cannot open display\n", argv[0]);
	return 1;
    }
    if (strcmp(mode, "-probe") == 0)
	return probe(dpy);

    apply(dpy);
#ifdef HAVE_XI_PROPS
    if (strcmp(mode, "-watch") == 0)
	watch(dpy);
#endif
    XCloseDisplay(dpy);
    return 0;
}
