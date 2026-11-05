/*
 * Copyright (c) 2016 Matthew R. Trower
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <cde_config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

/*
 * Included Files:
 */
#include <Dt/DtXinerama.h>

#include "WmMultiHead.h"

/*
 * Global Variables
 */
DtXineramaInfo_t *DtXI = NULL;/* Xinerama data; refreshed on RandR changes */


/*************************************<->*************************************
 *
 *  GetHeadInfo (pcd)
 *
 *
 *  Description:
 *  -----------
 *  Search for the head containing target client.
 *
 *
 *  Inputs:
 *  ------
 *
 *
 *  Outputs:
 *  -------
 *  Return = head metrics on success, NULL on failure.
 *           menuWidget, and menuButtons members.
 *
 *
 *  Comments:
 *  --------
 *
 *  Can fail if:
 *
 *  - MultiHead(eg. Xinerama) is not active
 *  - Client does not fall within any existing head
 *  - malloc error
 *  - pcd is NULL
 *
 *************************************<->***********************************/
WmHeadInfo_t *GetHeadInfo(const ClientData *pcd) {
    WmHeadInfo_t *WmHI = NULL;
    int idx = 0;

    if (!DtXI)
        DtXI = _DtXineramaInit(DISPLAY);

    if (!pcd || !DtXI)
        return NULL;

    if (!(WmHI = (WmHeadInfo_t *)malloc(sizeof(WmHeadInfo_t)))) {
#ifdef DEBUG
        fprintf(stderr, "(dtwm) _GetScreenInfo: malloc failed\n");
#endif

        return NULL;
    }

    /*
     * TODO
     *
     * DtXineramaInfo_t uses unsigned ints
     * XineramaScreenInfo uses shorts(?)
     * ClientData uses ints
     * FrameToClient and friends use a mixture (!)
     *
     * Explicit casting would shut the compiler up, but wouldn't change the
     * fundamental fact that we can't agree on coordinate types.
     */
    while (_DtXineramaGetScreen(DtXI, idx++,
            &WmHI->width, &WmHI->height, &WmHI->x_org, &WmHI->y_org)) {

        if (pcd->clientX >= WmHI->x_org &&
            pcd->clientY >= WmHI->y_org &&
            pcd->clientX <= WmHI->x_org + WmHI->width &&
            pcd->clientY <= WmHI->y_org + WmHI->height)

            return WmHI;
    }

    free(WmHI);

    /* No valid screen */
    return NULL;
}


/*************************************<->*************************************
 *
 *  GetHeadInfoById (id)
 *
 *
 *  Description:
 *  -----------
 *  Search for the head by ID.
 *
 *
 *  Inputs:
 *  ------
 *
 *
 *  Outputs:
 *  -------
 *  Return = head metrics on success, NULL on failure.
 *
 *
 *  Comments:
 *  --------
 *
 *  Can fail if:
 *
 *  - MultiHead(eg. Xinerama) is not active
 *  - malloc error
 *  - id is less than 0
 *
 *************************************<->***********************************/
WmHeadInfo_t *GetHeadInfoById(int id) {
    WmHeadInfo_t *WmHI = NULL;

    if (!DtXI)
        DtXI = _DtXineramaInit(DISPLAY);

    if (id < 0 || !DtXI)
        return NULL;

    if (!(WmHI = (WmHeadInfo_t *)malloc(sizeof(WmHeadInfo_t)))) {
#ifdef DEBUG
        fprintf(stderr, "(dtwm) _GetHeadInfoById: malloc failed\n");
#endif

        return NULL;
    }

    if (_DtXineramaGetScreen(DtXI, id,
            &WmHI->width, &WmHI->height, &WmHI->x_org, &WmHI->y_org))
        return WmHI;

    free(WmHI);
    return NULL;
}


/*************************************<->*************************************
 *
 *  Monitor hotplug and resolution changes.
 *
 *  With the RandR extension (optional at build and run time) dtwm hears
 *  about monitors being connected or removed and about resolution or
 *  rotation changes.  It then updates Xlib's idea of the screen size,
 *  re-reads the monitor layout, resizes the workspace backdrops and
 *  moves windows that would otherwise be lost off screen.  Without
 *  RandR nothing changes.
 *
 *************************************<->***********************************/

#ifdef HAVE_XRANDR
#include <X11/extensions/Xrandr.h>
#include "WmFunction.h"
#include "WmSnap.h"

static int randrEventBase = -1;

static void RefreshHeadInfo(void)
{
    if (DtXI) {
        if (DtXI->ScreenInfo)
            XFree(DtXI->ScreenInfo);
        free(DtXI);
        DtXI = NULL;
    }
}

static void ScreenChanged(WmScreenData *pSD, int oldW, int oldH, Time time)
{
    ClientListEntry *pCLE;
    int ws;
    int w = DisplayWidth(DISPLAY, pSD->screen);
    int h = DisplayHeight(DISPLAY, pSD->screen);

    for (ws = 0; ws < pSD->numWorkspaces; ws++) {
        WmWorkspaceData *pWS = &pSD->pWS[ws];

        if (pWS->backdrop.window)
            XResizeWindow(DISPLAY, pWS->backdrop.window,
                          (unsigned int) w, (unsigned int) h);
    }

    for (pCLE = pSD->clientList; pCLE; pCLE = pCLE->nextSibling) {
        if (pCLE->type == NORMAL_STATE)
            SnapScreenChanged(pCLE->pCD, oldW, oldH, time);
    }

    /*
     * Some drivers lose window contents on a mode change without
     * sending Expose; repaint everything, as f.refresh does.
     */
    F_Refresh(NULL, NULL, NULL);
}
#endif /* HAVE_XRANDR */

void InitScreenChangeEvents(void)
{
#ifdef HAVE_XRANDR
    int errorBase, i;
    unsigned long mask = RRScreenChangeNotifyMask;

    if (!XRRQueryExtension(DISPLAY, &randrEventBase, &errorBase)) {
        randrEventBase = -1;
        return;
    }
#ifdef RRCrtcChangeNotifyMask
    mask |= RRCrtcChangeNotifyMask;	/* monitors moved, same screen size */
#endif
    for (i = 0; i < wmGD.numScreens; i++) {
        if (wmGD.Screens[i].managed)
            XRRSelectInput(DISPLAY, wmGD.Screens[i].rootWindow, mask);
    }
#endif
}

Boolean HandleScreenChangeEvent(XEvent *event)
{
#ifdef HAVE_XRANDR
    WmScreenData *pSD = NULL;
    Window root;
    Time time = CurrentTime;
    int i, oldW, oldH;

    if (randrEventBase < 0)
        return False;

    if (event->type == randrEventBase + RRScreenChangeNotify) {
        XRRScreenChangeNotifyEvent *sce = (XRRScreenChangeNotifyEvent *) event;

        root = sce->root;
        time = sce->timestamp;
    }
#ifdef RRNotify_CrtcChange
    else if (event->type == randrEventBase + RRNotify &&
             ((XRRNotifyEvent *) event)->subtype == RRNotify_CrtcChange) {
        root = event->xany.window;
    }
#endif
    else
        return False;

    for (i = 0; i < wmGD.numScreens; i++) {
        if (wmGD.Screens[i].managed && wmGD.Screens[i].rootWindow == root) {
            pSD = &wmGD.Screens[i];
            break;
        }
    }

    oldW = DisplayWidth(DISPLAY, pSD ? pSD->screen : DefaultScreen(DISPLAY));
    oldH = DisplayHeight(DISPLAY, pSD ? pSD->screen : DefaultScreen(DISPLAY));
    XRRUpdateConfiguration(event);	/* updates DisplayWidth/Height */
    RefreshHeadInfo();

    if (pSD)
        ScreenChanged(pSD, oldW, oldH, time);
    return True;
#else
    return False;
#endif
}
