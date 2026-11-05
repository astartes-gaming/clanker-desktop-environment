/*
 * Touchpad.c - touchpad settings through XInput device properties.
 *
 * See Touchpad.h.  Handles the X.Org libinput driver ("libinput ..."
 * properties) and the older synaptics driver ("Synaptics ..."
 * properties).  Uses only the XInput 1.5 property requests, so it works
 * with any X.Org server since 1.6 and degrades to nothing elsewhere.
 * When built without libXi (no HAVE_XI_PROPS) only the settings file
 * functions do anything.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifdef HAVE_CONFIG_H
#include <cde_config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>		/* MAXPATHLEN */
#include <X11/Xlib.h>
#include <X11/Xmd.h>		/* CARD32 */
#ifdef HAVE_XI_PROPS
#include <X11/extensions/XInput.h>
#endif

#include "Touchpad.h"

void
TpClear(TpSettings *s)
{
    s->tapping = TP_UNSET;
    s->natural = TP_UNSET;
    s->scroll  = TP_UNSET;
    s->dwt     = TP_UNSET;
    s->speed   = TP_UNSET;
}

/*
 * Settings file: $HOME/.dt/touchpad, "key=value" lines.  Unknown keys
 * and out-of-range values are ignored; keys that are missing leave the
 * driver's setting alone.
 */
static int
tp_path(char *buf, size_t len)
{
    char *home = getenv("HOME");

    if (home == NULL || *home == '\0' || strlen(home) + 16 > len)
	return 0;
    sprintf(buf, "%s/.dt/touchpad", home);
    return 1;
}

static int
tp_bool(const char *val)
{
    if (strcmp(val, "1") == 0 || strcmp(val, "true") == 0)
	return 1;
    if (strcmp(val, "0") == 0 || strcmp(val, "false") == 0)
	return 0;
    return TP_UNSET;
}

int
TpLoad(TpSettings *s)
{
    char path[MAXPATHLEN], line[128], key[32], val[32];
    FILE *fp;
    int v;

    TpClear(s);
    if (!tp_path(path, sizeof(path)) || (fp = fopen(path, "r")) == NULL)
	return 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
	if (line[0] == '#')
	    continue;
	if (sscanf(line, " %31[a-z_] = %31s", key, val) != 2)
	    continue;
	if (strcmp(key, "tapping") == 0)
	    s->tapping = tp_bool(val);
	else if (strcmp(key, "natural_scrolling") == 0)
	    s->natural = tp_bool(val);
	else if (strcmp(key, "disable_while_typing") == 0)
	    s->dwt = tp_bool(val);
	else if (strcmp(key, "scroll_method") == 0) {
	    if (strcmp(val, "twofinger") == 0)
		s->scroll = TP_SCROLL_TWOFINGER;
	    else if (strcmp(val, "edge") == 0)
		s->scroll = TP_SCROLL_EDGE;
	} else if (strcmp(key, "speed") == 0) {
	    v = atoi(val);
	    if (v >= -100 && v <= 100)
		s->speed = v;
	}
    }
    fclose(fp);
    return 1;
}

int
TpSave(const TpSettings *s)
{
    char path[MAXPATHLEN], tmp[MAXPATHLEN + 8];
    FILE *fp;
    int fd;

    if (!tp_path(path, sizeof(path)))
	return 0;
    sprintf(tmp, "%s.XXXXXX", path);
    if ((fd = mkstemp(tmp)) < 0)
	return 0;
    if ((fp = fdopen(fd, "w")) == NULL) {
	close(fd);
	unlink(tmp);
	return 0;
    }
    fputs("# Touchpad settings written by Style Manager > Mouse.\n", fp);
    fputs("# Applied at login by dttouchpad.\n", fp);
    if (s->tapping != TP_UNSET)
	fprintf(fp, "tapping=%d\n", s->tapping);
    if (s->natural != TP_UNSET)
	fprintf(fp, "natural_scrolling=%d\n", s->natural);
    if (s->scroll == TP_SCROLL_TWOFINGER)
	fputs("scroll_method=twofinger\n", fp);
    else if (s->scroll == TP_SCROLL_EDGE)
	fputs("scroll_method=edge\n", fp);
    if (s->dwt != TP_UNSET)
	fprintf(fp, "disable_while_typing=%d\n", s->dwt);
    if (s->speed != TP_UNSET)
	fprintf(fp, "speed=%d\n", s->speed);
    if (fclose(fp) != 0 || rename(tmp, path) != 0) {
	unlink(tmp);
	return 0;
    }
    return 1;
}

#ifndef HAVE_XI_PROPS

int  TpProbe(Display *dpy) { return 0; }
void TpGetCurrent(Display *dpy, TpSettings *s) { TpClear(s); }
void TpGetDefaults(Display *dpy, TpSettings *s) { TpClear(s); }
void TpApply(Display *dpy, const TpSettings *s) { }

#else /* HAVE_XI_PROPS */

#define DRV_LIBINPUT  1
#define DRV_SYNAPTICS 2

/* libinput driver properties */
#define LI_TAP        "libinput Tapping Enabled"
#define LI_NATURAL    "libinput Natural Scrolling Enabled"
#define LI_SCROLL_AV  "libinput Scroll Methods Available"
#define LI_SCROLL     "libinput Scroll Method Enabled"
#define LI_DWT        "libinput Disable While Typing Enabled"
#define LI_SPEED      "libinput Accel Speed"
/* synaptics driver properties */
#define SY_TAP        "Synaptics Tap Action"
#define SY_DISTANCE   "Synaptics Scrolling Distance"
#define SY_TWOFINGER  "Synaptics Two-Finger Scrolling"
#define SY_EDGE       "Synaptics Edge Scrolling"

#define MAX_ITEMS 16

static int tp_xerror;

static int
tp_error_handler(Display *dpy, XErrorEvent *ev)
{
    tp_xerror = 1;
    return 0;
}

/* XInput 1.5 or later (device properties) present on this display? */
static int
tp_have_xi(Display *dpy)
{
    XExtensionVersion *v;
    int op, ev, err, ok;

    if (!XQueryExtension(dpy, INAME, &op, &ev, &err))
	return 0;
    v = XGetExtensionVersion(dpy, INAME);
    if (v == NULL || v == (XExtensionVersion *) NoSuchExtension)
	return 0;
    ok = v->present &&
	 (v->major_version > 1 ||
	  (v->major_version == 1 && v->minor_version >= 5));
    XFree(v);
    return ok;
}

/*
 * Read up to MAX_ITEMS items of a device property as longs (format 8,
 * 16 or 32).  For FLOAT properties the items hold the raw 32-bit
 * pattern.  Returns the item count, 0 if the property is missing.
 */
static int
tp_get(Display *dpy, XDevice *dev, const char *name,
       long *items, Atom *type_ret, int *format_ret)
{
    Atom prop, type;
    int format, i;
    unsigned long n, after;
    unsigned char *data = NULL;

    if ((prop = XInternAtom(dpy, name, True)) == None)
	return 0;
    if (XGetDeviceProperty(dpy, dev, prop, 0, MAX_ITEMS, False,
			   AnyPropertyType, &type, &format, &n, &after,
			   &data) != Success || type == None || n == 0) {
	if (data)
	    XFree(data);
	return 0;
    }
    if (n > MAX_ITEMS)
	n = MAX_ITEMS;
    for (i = 0; i < (int) n; i++) {
	if (format == 8)
	    items[i] = ((unsigned char *) data)[i];
	else if (format == 16)
	    items[i] = ((short *) data)[i];
	else
	    items[i] = ((long *) data)[i];
    }
    XFree(data);
    if (type_ret)
	*type_ret = type;
    if (format_ret)
	*format_ret = format;
    return (int) n;
}

/* Write n items back with the property's own type and format. */
static void
tp_put(Display *dpy, XDevice *dev, const char *name,
       const long *items, int n, Atom type, int format)
{
    Atom prop;
    unsigned char b8[MAX_ITEMS];
    short b16[MAX_ITEMS];
    int i;

    if ((prop = XInternAtom(dpy, name, True)) == None)
	return;
    if (format == 8) {
	for (i = 0; i < n; i++)
	    b8[i] = (unsigned char) items[i];
	XChangeDeviceProperty(dpy, dev, prop, type, 8, PropModeReplace,
			      b8, n);
    } else if (format == 16) {
	for (i = 0; i < n; i++)
	    b16[i] = (short) items[i];
	XChangeDeviceProperty(dpy, dev, prop, type, 16, PropModeReplace,
			      (unsigned char *) b16, n);
    } else {
	XChangeDeviceProperty(dpy, dev, prop, type, 32, PropModeReplace,
			      (unsigned char *) items, n);
    }
}

/* Set items[first..first+count-1] of a property, keeping the rest. */
static void
tp_set(Display *dpy, XDevice *dev, const char *name,
       int first, const long *vals, int count)
{
    long items[MAX_ITEMS];
    Atom type;
    int format, n, i;

    n = tp_get(dpy, dev, name, items, &type, &format);
    if (n < first + count)
	return;
    for (i = 0; i < count; i++)
	items[first + i] = vals[i];
    tp_put(dpy, dev, name, items, n, type, format);
}

/* FLOAT properties carry IEEE floats in 32-bit items. */
static float
tp_long_to_float(long l)
{
    union { float f; CARD32 u; } c;

    c.u = (CARD32) l;
    return c.f;
}

static long
tp_float_to_long(float f)
{
    union { float f; CARD32 u; } c;

    c.f = f;
    return (long) c.u;
}

static int
tp_get1(Display *dpy, XDevice *dev, const char *name, long *val)
{
    long items[MAX_ITEMS];

    if (tp_get(dpy, dev, name, items, NULL, NULL) < 1)
	return 0;
    *val = items[0];
    return 1;
}

static int
tp_driver(Display *dpy, XDevice *dev)
{
    long v;

    if (tp_get1(dpy, dev, LI_TAP, &v))
	return DRV_LIBINPUT;	/* only touchpads can tap */
    if (tp_get1(dpy, dev, SY_TAP, &v))
	return DRV_SYNAPTICS;
    return 0;
}

static int
tp_caps(Display *dpy, XDevice *dev, int driver)
{
    long items[MAX_ITEMS];
    int caps = TP_CAP_TAP;

    if (driver == DRV_LIBINPUT) {
	if (tp_get(dpy, dev, LI_NATURAL, items, NULL, NULL) > 0)
	    caps |= TP_CAP_NATURAL;
	if (tp_get(dpy, dev, LI_SCROLL_AV, items, NULL, NULL) >= 2 &&
	    items[0] && items[1])
	    caps |= TP_CAP_SCROLL;
	if (tp_get(dpy, dev, LI_DWT, items, NULL, NULL) > 0)
	    caps |= TP_CAP_DWT;
	if (tp_get(dpy, dev, LI_SPEED, items, NULL, NULL) > 0)
	    caps |= TP_CAP_SPEED;
    } else {
	if (tp_get(dpy, dev, SY_DISTANCE, items, NULL, NULL) >= 2)
	    caps |= TP_CAP_NATURAL;
	if (tp_get(dpy, dev, SY_TWOFINGER, items, NULL, NULL) >= 2 &&
	    tp_get(dpy, dev, SY_EDGE, items, NULL, NULL) >= 2)
	    caps |= TP_CAP_SCROLL;
    }
    return caps;
}

/* Read the settings of one device; "suffix" selects " Default" props. */
static void
tp_read(Display *dpy, XDevice *dev, int driver, int defaults,
	TpSettings *s)
{
    char name[96];
    long items[MAX_ITEMS], v;
    const char *suf = defaults ? " Default" : "";

    TpClear(s);
    if (driver == DRV_LIBINPUT) {
	sprintf(name, "%s%s", LI_TAP, suf);
	if (tp_get1(dpy, dev, name, &v))
	    s->tapping = v != 0;
	sprintf(name, "%s%s", LI_NATURAL, suf);
	if (tp_get1(dpy, dev, name, &v))
	    s->natural = v != 0;
	sprintf(name, "%s%s", LI_DWT, suf);
	if (tp_get1(dpy, dev, name, &v))
	    s->dwt = v != 0;
	sprintf(name, "%s%s", LI_SCROLL, suf);
	if (tp_get(dpy, dev, name, items, NULL, NULL) >= 2) {
	    if (items[0])
		s->scroll = TP_SCROLL_TWOFINGER;
	    else if (items[1])
		s->scroll = TP_SCROLL_EDGE;
	}
	sprintf(name, "%s%s", LI_SPEED, suf);
	if (tp_get1(dpy, dev, name, &v)) {
	    float f = tp_long_to_float(v) * 100.0f;
	    s->speed = (int) (f < 0 ? f - 0.5f : f + 0.5f);
	}
    } else if (!defaults) {	/* synaptics has no "Default" props */
	if (tp_get(dpy, dev, SY_TAP, items, NULL, NULL) >= 5)
	    s->tapping = items[4] != 0;
	if (tp_get1(dpy, dev, SY_DISTANCE, &v))
	    s->natural = v < 0;
	if (tp_get1(dpy, dev, SY_TWOFINGER, &v) && v)
	    s->scroll = TP_SCROLL_TWOFINGER;
	else if (tp_get1(dpy, dev, SY_EDGE, &v) && v)
	    s->scroll = TP_SCROLL_EDGE;
    }
}

static void
tp_write(Display *dpy, XDevice *dev, int driver, const TpSettings *s)
{
    long v[3], items[MAX_ITEMS];
    int n;

    if (driver == DRV_LIBINPUT) {
	if (s->tapping != TP_UNSET) {
	    v[0] = s->tapping;
	    tp_set(dpy, dev, LI_TAP, 0, v, 1);
	}
	if (s->natural != TP_UNSET) {
	    v[0] = s->natural;
	    tp_set(dpy, dev, LI_NATURAL, 0, v, 1);
	}
	if (s->dwt != TP_UNSET) {
	    v[0] = s->dwt;
	    tp_set(dpy, dev, LI_DWT, 0, v, 1);
	}
	if (s->scroll != TP_UNSET &&
	    tp_get(dpy, dev, LI_SCROLL_AV, items, NULL, NULL) >= 2) {
	    v[0] = s->scroll == TP_SCROLL_TWOFINGER;
	    v[1] = s->scroll == TP_SCROLL_EDGE;
	    v[2] = 0;
	    if ((v[0] && items[0]) || (v[1] && items[1]))
		tp_set(dpy, dev, LI_SCROLL, 0, v, 3);
	}
	if (s->speed != TP_UNSET) {
	    v[0] = tp_float_to_long((float) s->speed / 100.0f);
	    tp_set(dpy, dev, LI_SPEED, 0, v, 1);
	}
	return;
    }

    /* synaptics */
    if (s->tapping != TP_UNSET) {
	/* one/two/three-finger taps -> buttons 1/3/2 */
	v[0] = s->tapping ? 1 : 0;
	v[1] = s->tapping ? 3 : 0;
	v[2] = s->tapping ? 2 : 0;
	tp_set(dpy, dev, SY_TAP, 4, v, 3);
    }
    if (s->natural != TP_UNSET &&
	(n = tp_get(dpy, dev, SY_DISTANCE, items, NULL, NULL)) >= 2) {
	v[0] = items[0] < 0 ? -items[0] : items[0];
	v[1] = items[1] < 0 ? -items[1] : items[1];
	if (s->natural) {
	    v[0] = -v[0];
	    v[1] = -v[1];
	}
	tp_set(dpy, dev, SY_DISTANCE, 0, v, 2);
    }
    if (s->scroll != TP_UNSET) {
	v[0] = v[1] = s->scroll == TP_SCROLL_TWOFINGER;
	tp_set(dpy, dev, SY_TWOFINGER, 0, v, 2);
	v[0] = v[1] = s->scroll == TP_SCROLL_EDGE;
	tp_set(dpy, dev, SY_EDGE, 0, v, 2);
    }
}

#define OP_PROBE    0
#define OP_CURRENT  1
#define OP_DEFAULTS 2
#define OP_APPLY    3

/*
 * Run one operation over every touchpad.  PROBE ORs the capabilities
 * of all touchpads, CURRENT/DEFAULTS read the first one, APPLY writes
 * all of them.  X errors (no extension, a device that went away, a
 * value the driver refuses) are ignored.
 */
static int
tp_foreach(Display *dpy, int op, TpSettings *s)
{
    XErrorHandler old;
    XDeviceInfo *list;
    XDevice *dev;
    int i, n = 0, driver, result = 0, done = 0;

    if (op != OP_APPLY)
	TpClear(s);
    if (!tp_have_xi(dpy))
	return 0;

    XSync(dpy, False);
    tp_xerror = 0;
    old = XSetErrorHandler(tp_error_handler);

    list = XListInputDevices(dpy, &n);
    for (i = 0; list != NULL && i < n && !done; i++) {
	if (list[i].use != IsXExtensionPointer && list[i].use != IsXPointer)
	    continue;
	tp_xerror = 0;
	dev = XOpenDevice(dpy, list[i].id);
	if (dev == NULL)
	    continue;	/* e.g. a master device on an XI2 server */
	if ((driver = tp_driver(dpy, dev)) != 0) {
	    switch (op) {
	    case OP_PROBE:
		result |= tp_caps(dpy, dev, driver);
		break;
	    case OP_CURRENT:
	    case OP_DEFAULTS:
		tp_read(dpy, dev, driver, op == OP_DEFAULTS, s);
		result = done = 1;
		break;
	    case OP_APPLY:
		tp_write(dpy, dev, driver, s);
		result++;
		break;
	    }
	}
	XCloseDevice(dpy, dev);
    }
    if (list)
	XFreeDeviceList(list);

    XSync(dpy, False);
    XSetErrorHandler(old);
    return result;
}

int
TpProbe(Display *dpy)
{
    TpSettings s;

    return tp_foreach(dpy, OP_PROBE, &s);
}

void
TpGetCurrent(Display *dpy, TpSettings *s)
{
    tp_foreach(dpy, OP_CURRENT, s);
}

void
TpGetDefaults(Display *dpy, TpSettings *s)
{
    tp_foreach(dpy, OP_DEFAULTS, s);
}

void
TpApply(Display *dpy, const TpSettings *s)
{
    TpSettings copy;

    copy = *s;
    tp_foreach(dpy, OP_APPLY, &copy);
}

#endif /* HAVE_XI_PROPS */
