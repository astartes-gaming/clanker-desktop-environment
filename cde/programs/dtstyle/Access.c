/*
 * Access.c - accessibility settings, shared by dtstyle (Settings >
 * Accessibility) and dtaccess (applies them at login).
 *
 * The keyboard settings are XKB's AccessX controls, part of every X
 * server since X11R6.1: sticky keys, slow keys, bounce keys, mouse keys
 * and the keyboard shortcuts that toggle them.  The large pointer is
 * the Xcursor.size resource, used by applications started afterwards.
 * They are kept in ~/.dt/accessibility as "name=0|1" lines.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>

#include "Access.h"

const char *const AccessKeys[ACC_COUNT] = {
    "sticky", "slow", "bounce", "mousekeys", "toggles", "bigcursor",
    "keyboard"
};

static void
AccessPath(const char *home, char *buf, size_t len)
{
    snprintf(buf, len, "%s/.dt/accessibility", home ? home : ".");
}

void
AccessRead(const char *home, int on[ACC_COUNT])
{
    char  path[1024], line[256], *v;
    FILE *fp;
    int   i;

    for (i = 0; i < ACC_COUNT; i++)
        on[i] = 0;
    AccessPath(home, path, sizeof(path));
    if ((fp = fopen(path, "r")) == NULL)
        return;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || (v = strchr(line, '=')) == NULL)
            continue;
        *v++ = '\0';
        for (i = 0; i < ACC_COUNT; i++)
            if (strcmp(line, AccessKeys[i]) == 0)
                on[i] = atoi(v) != 0;
    }
    fclose(fp);
}

int
AccessWrite(const char *home, const int on[ACC_COUNT])
{
    char  path[1024], tmp[1100], dir[1024];
    FILE *fp;
    int   i, fd;

    snprintf(dir, sizeof(dir), "%s/.dt", home ? home : ".");
    (void) mkdir(dir, 0755);
    AccessPath(home, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
    if ((fd = mkstemp(tmp)) < 0)
        return 0;
    if ((fp = fdopen(fd, "w")) == NULL) {
        close(fd);
        unlink(tmp);
        return 0;
    }
    fprintf(fp, "# Accessibility settings, written by Settings > "
                "Accessibility.\n");
    for (i = 0; i < ACC_COUNT; i++)
        fprintf(fp, "%s=%d\n", AccessKeys[i], on[i] ? 1 : 0);
    fchmod(fd, 0644);
    if (fclose(fp) != 0 || rename(tmp, path) != 0) {
        unlink(tmp);
        return 0;
    }
    return 1;
}

static void
SetCursorSize(int big)
{
    /* Xcursor.size: read by applications as they start. */
    FILE *xrdb = popen("xrdb -merge -nocpp 2>/dev/null", "w");

    if (xrdb) {
        fprintf(xrdb, "Xcursor.size: %d\n", big ? 48 : 24);
        pclose(xrdb);
    }
}

void
AccessApply(Display *dpy, const int on[ACC_COUNT])
{
    int major = XkbMajorVersion, minor = XkbMinorVersion, op, ev, err;
    XkbDescPtr xkb;
    unsigned int which, mask;

    if (XkbQueryExtension(dpy, &op, &ev, &err, &major, &minor) &&
        (xkb = XkbGetMap(dpy, 0, XkbUseCoreKbd)) != NULL) {
        if (XkbGetControls(dpy, XkbAllControlsMask, xkb) == Success &&
            xkb->ctrls) {
            XkbControlsPtr c = xkb->ctrls;

            mask = XkbStickyKeysMask | XkbSlowKeysMask | XkbBounceKeysMask |
                   XkbMouseKeysMask | XkbMouseKeysAccelMask |
                   XkbAccessXKeysMask;
            c->enabled_ctrls &= ~mask;
            /* never time out: the settings stay until changed here */
            c->enabled_ctrls &= ~XkbAccessXTimeoutMask;
            if (on[ACC_STICKY])    c->enabled_ctrls |= XkbStickyKeysMask;
            if (on[ACC_SLOW])      c->enabled_ctrls |= XkbSlowKeysMask;
            if (on[ACC_BOUNCE])    c->enabled_ctrls |= XkbBounceKeysMask;
            if (on[ACC_MOUSEKEYS])
                c->enabled_ctrls |= XkbMouseKeysMask | XkbMouseKeysAccelMask;
            if (on[ACC_TOGGLES])   c->enabled_ctrls |= XkbAccessXKeysMask;

            /* Sticky keys: press a modifier twice to lock it; pressing
             * two keys at once turns sticky keys off (if toggles on). */
            c->ax_options |= XkbAX_LatchToLockMask;
            if (on[ACC_TOGGLES])
                c->ax_options |= XkbAX_TwoKeysMask;
            else
                c->ax_options &= ~XkbAX_TwoKeysMask;
            if (c->slow_keys_delay < 100)
                c->slow_keys_delay = 300;
            if (c->debounce_delay < 100)
                c->debounce_delay = 300;

            /* the enable bits, and the controls that have settings */
            which = XkbControlsEnabledMask | XkbStickyKeysMask |
                    XkbSlowKeysMask | XkbBounceKeysMask;
            XkbSetControls(dpy, which, xkb);
        }
        XkbFreeKeyboard(xkb, 0, True);
    }
    SetCursorSize(on[ACC_BIGCURSOR]);
    XFlush(dpy);
}
