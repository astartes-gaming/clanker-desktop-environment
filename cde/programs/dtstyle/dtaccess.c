/*
 * dtaccess - apply the accessibility settings at login.
 *
 * Reads ~/.dt/accessibility (written by Settings > Accessibility) and
 * sets the XKB AccessX controls and the pointer size.  With -keyboard,
 * exits 0 only if the on-screen keyboard is to be shown at login.
 * Run by Xsession.d/0070.dtmodern.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>

#include "Access.h"

int
main(int argc, char **argv)
{
    int      on[ACC_COUNT];
    Display *dpy;

    AccessRead(getenv("HOME"), on);
    if (argc > 1 && strcmp(argv[1], "-keyboard") == 0)
        return on[ACC_KEYBOARD] ? 0 : 1;
    if ((dpy = XOpenDisplay(NULL)) == NULL) {
        fprintf(stderr, "dtaccess: cannot open the display\n");
        return 1;
    }
    AccessApply(dpy, on);
    XCloseDisplay(dpy);
    return 0;
}
