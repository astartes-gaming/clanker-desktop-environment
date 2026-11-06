/*
 * wsmbackdrop - test helper for the VM self-test (tools/vm/selftest.sh).
 *
 *   wsmbackdrop set PATH TYPE   ask dtwm to show PATH as the current
 *                               workspace's backdrop (TYPE 0 tiled,
 *                               1 center, 2 fit, 3 fill, 4 stretch)
 *   wsmbackdrop info            print the _XROOTPMAP_ID pixmap's size
 *                               and the RGB of its centre pixel
 *
 * Built on the host against the CDE tree; see selftest.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <Dt/Wsm.h>
#include <Dt/WsmP.h>

int
main(int argc, char **argv)
{
    Display *dpy = XOpenDisplay(NULL);
    Window root;

    if (!dpy || argc < 2)
        return 2;
    root = DefaultRootWindow(dpy);

    if (argv[1][0] == 's' && argc == 4) {
        _DtWsmChangeBackdrop(dpy, root, argv[2], None,
                             (DtWsmBackdropImageType) atoi(argv[3]));
        XSync(dpy, False);
        return 0;
    }
    if (argv[1][0] == 'i') {
        Atom a = XInternAtom(dpy, "_XROOTPMAP_ID", True), type;
        int fmt, x, y;
        unsigned long n, after;
        unsigned char *data = NULL;
        unsigned int w, h, bw, d;
        Window r;
        Pixmap pix;
        XImage *img;
        unsigned long px;
        Visual *v = DefaultVisual(dpy, DefaultScreen(dpy));

        if (a == None || XGetWindowProperty(dpy, root, a, 0, 1, False,
                XA_PIXMAP, &type, &fmt, &n, &after, &data) != Success ||
            n != 1) {
            printf("none\n");
            return 1;
        }
        pix = *(Pixmap *) data;
        if (!XGetGeometry(dpy, pix, &r, &x, &y, &w, &h, &bw, &d))
            return 1;
        img = XGetImage(dpy, pix, w / 2, h / 2, 1, 1, AllPlanes, ZPixmap);
        px = XGetPixel(img, 0, 0);
        printf("%ux%u %lu,%lu,%lu\n", w, h,
               (px & v->red_mask) >> 16, (px & v->green_mask) >> 8,
               px & v->blue_mask);
        return 0;
    }
    return 2;
}
