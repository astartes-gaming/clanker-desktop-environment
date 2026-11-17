/*
 * dtsuperkey - the Windows (Super) key on its own opens the application
 * menu, as on other desktops.
 *
 * Usage: dtsuperkey [-command "shell command"]
 *
 * A tap of Super_L or Super_R - pressed and released within a moment,
 * with no other key or mouse button in between - runs the command
 * (default "dtappfind -menu", which also closes an open menu).  Held
 * as a modifier (Super+E, Super+arrows, Super+drag) it does nothing,
 * so window-manager shortcuts keep working.
 *
 * It watches XInput 2 raw events on the root window and grabs no key,
 * so it works next to any window manager.  One per display; it exits
 * quietly when the server lacks XInput 2 or another one runs.
 * DT_NO_SUPERKEY=1 (Xsession.d/0070.dtmodern) turns it off.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>

#define TAP_MS 700	/* longer than this: a held modifier, not a tap */

static void
Run(const char *cmd)
{
    pid_t pid = fork();

    if (pid == 0)
    {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *) NULL);
        _exit(127);
    }
}

int
main(int argc, char **argv)
{
    Display          *dpy;
    Window            root, owner;
    XIEventMask       mask;
    unsigned char     bits[XIMaskLen(XI_LASTEVENT)] = { 0 };
    int               opcode, ev, err, major = 2, minor = 0;
    KeyCode           superL, superR;
    const char       *cmd = "dtappfind -menu";
    Atom              sel;
    char              name[64];
    Bool           down = False, other = False;
    Time              pressed = 0;
    int               i;

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-command") == 0 && i + 1 < argc)
            cmd = argv[++i];
        else
        {
            fprintf(stderr, "usage: %s [-command \"shell command\"]\n",
                    argv[0]);
            return 2;
        }
    }
    signal(SIGCHLD, SIG_IGN);		/* nobody waits for the menu */

    if ((dpy = XOpenDisplay(NULL)) == NULL)
        return 1;
    if (!XQueryExtension(dpy, "XInputExtension", &opcode, &ev, &err) ||
        XIQueryVersion(dpy, &major, &minor) != Success)
        return 0;			/* no XInput 2: nothing to do */
    root = DefaultRootWindow(dpy);

    /* one per display */
    snprintf(name, sizeof(name), "_CLANKER_SUPERKEY_S%d", DefaultScreen(dpy));
    sel = XInternAtom(dpy, name, False);
    if (XGetSelectionOwner(dpy, sel) != None)
        return 0;
    owner = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);
    XSetSelectionOwner(dpy, sel, owner, CurrentTime);
    if (XGetSelectionOwner(dpy, sel) != owner)
        return 0;

    superL = XKeysymToKeycode(dpy, XK_Super_L);
    superR = XKeysymToKeycode(dpy, XK_Super_R);
    if (!superL && !superR)
        return 0;

    mask.deviceid = XIAllMasterDevices;
    mask.mask_len = sizeof(bits);
    mask.mask = bits;
    XISetMask(bits, XI_RawKeyPress);
    XISetMask(bits, XI_RawKeyRelease);
    XISetMask(bits, XI_RawButtonPress);
    XISelectEvents(dpy, root, &mask, 1);
    XFlush(dpy);

    for (;;)
    {
        XEvent              e;
        XGenericEventCookie *c = &e.xcookie;

        XNextEvent(dpy, &e);
        if (e.type == SelectionClear)
            return 0;			/* a newer one took over */
        if (c->type != GenericEvent || c->extension != opcode ||
            !XGetEventData(dpy, c))
            continue;
        {
            XIRawEvent *r = (XIRawEvent *) c->data;
            Bool     isSuper = r->detail != 0 &&
                ((KeyCode) r->detail == superL || (KeyCode) r->detail == superR);

            switch (r->evtype)
            {
            case XI_RawKeyPress:
                if (isSuper)
                {
                    if (!down)		/* not an autorepeat */
                    {
                        down = True;
                        other = False;
                        pressed = r->time;
                    }
                }
                else if (down)
                    other = True;	/* Super+something */
                break;
            case XI_RawButtonPress:
                if (down)
                    other = True;	/* Super+click or scroll */
                break;
            case XI_RawKeyRelease:
                if (isSuper && down)
                {
                    if (!other && r->time - pressed <= TAP_MS)
                        Run(cmd);
                    down = False;
                }
                break;
            }
        }
        XFreeEventData(dpy, c);
    }
}
