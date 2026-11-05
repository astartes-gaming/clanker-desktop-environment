/*
 * WmSnap.c - window snapping and tiling for dtwm.
 *
 * Part of the Clanker Desktop Environment, a fork of the Common Desktop
 * Environment; distributed under the same terms (LGPL-2.0-or-later).
 *
 * Features (all per monitor when Xinerama is active):
 *
 *  - Magnetic snapping while moving: frame edges within snapDistance
 *    pixels of a monitor edge, or of another visible window's edge
 *    (snapToWindows), are pulled flush against it.  snapDistance 0
 *    turns it off.
 *
 *  - Edge tiling (edgeTiling): releasing a drag with the pointer on the
 *    left/right edge of a monitor tiles the window to that half; the
 *    top edge maximizes it.  Dragging a tiled window away restores its
 *    previous size.
 *
 *  - f.tile_left, f.tile_right, f.untile and f.toggle_maximize window
 *    manager functions (bound to Super+arrows and title double-click in
 *    the default sys.dtwmrc).
 *
 * Plain C89 and Xlib only, so it builds wherever dtwm does.
 */

#include <stdlib.h>
#include <Dt/DtXinerama.h>

#include "WmGlobal.h"
#include "WmSnap.h"
#include "WmWinConf.h"
#include "WmWinState.h"
#include "WmWrkspace.h"

extern DtXineramaInfo_t *DtXI;		/* WmMultiHead.c */

/* Pointer this close to a monitor edge selects a tiling zone. */
#define EDGE_ZONE_PX	2

typedef struct { int x, y, w, h; } SnapRect;

/* ------------------------------------------------------------------ */

static void
ScreenRect (ClientData *pcd, SnapRect *r)
{
    r->x = 0;
    r->y = 0;
    r->w = DisplayWidth (DISPLAY, SCREEN_FOR_CLIENT(pcd));
    r->h = DisplayHeight (DISPLAY, SCREEN_FOR_CLIENT(pcd));
}

/* Monitor containing (px, py); the whole screen without Xinerama. */
static void
HeadRect (ClientData *pcd, int px, int py, SnapRect *r)
{
    unsigned int i, w, h, ux, uy;
    int x, y;

    if (!DtXI)
	DtXI = _DtXineramaInit (DISPLAY);

    if (DtXI)
    {
	for (i = 0; _DtXineramaGetScreen (DtXI, i, &w, &h, &ux, &uy); i++)
	{
	    x = (int) ux;
	    y = (int) uy;
	    if (px >= x && px < x + (int) w && py >= y && py < y + (int) h)
	    {
		r->x = x; r->y = y; r->w = (int) w; r->h = (int) h;
		return;
	    }
	}
    }
    ScreenRect (pcd, r);
}

static void
FrameRect (ClientData *pcd, SnapRect *r)
{
    int bx = pcd->clientOffset.x;
    int by = pcd->clientOffset.y;

    if (pcd->maxConfig)
    {
	r->x = pcd->maxX - bx;
	r->y = pcd->maxY - by;
	r->w = pcd->maxWidth + 2 * bx;
	r->h = pcd->maxHeight + bx + by;
    }
    else
    {
	r->x = pcd->clientX - bx;
	r->y = pcd->clientY - by;
	r->w = pcd->clientWidth + 2 * bx;
	r->h = pcd->clientHeight + bx + by;
    }
}

static int
Abs (int v)
{
    return (v < 0) ? -v : v;
}

/*
 * Try to snap one axis.  'lo'/'hi' are the moving frame's edges along
 * the axis, 'edge' a target line.  Keeps the closest candidate in *best.
 */
static void
Consider (int lo, int hi, int edge, int dist, int *best, int *bestDelta)
{
    int d;

    d = edge - lo;			/* our leading edge onto the line */
    if (Abs (d) <= dist && Abs (d) < *best) { *best = Abs (d); *bestDelta = d; }
    d = edge - hi;			/* our trailing edge onto the line */
    if (Abs (d) <= dist && Abs (d) < *best) { *best = Abs (d); *bestDelta = d; }
}

/* True when ranges [a0,a1) and [b0,b1) overlap or nearly touch. */
static int
Overlaps (int a0, int a1, int b0, int b1, int slack)
{
    return (a0 < b1 + slack) && (b0 < a1 + slack);
}

/* ------------------------------------------------------------------ */

void
SnapFramePosition (ClientData *pcd, int *pX, int *pY,
		   unsigned int w, unsigned int h)
{
    WmScreenData    *pSD = pcd->pSD;
    int		     dist = pSD->snapDistance;
    int		     bestX, bestY, dX = 0, dY = 0;
    SnapRect	     head;
    ClientListEntry *pCLE;

    if (dist <= 0 || wmGD.movingIcon)
	return;

    bestX = bestY = dist + 1;

    /* Monitor edges of the monitor holding the frame's center. */
    HeadRect (pcd, *pX + (int) w / 2, *pY + (int) h / 2, &head);
    Consider (*pX, *pX + (int) w, head.x, dist, &bestX, &dX);
    Consider (*pX, *pX + (int) w, head.x + head.w, dist, &bestX, &dX);
    Consider (*pY, *pY + (int) h, head.y, dist, &bestY, &dY);
    Consider (*pY, *pY + (int) h, head.y + head.h, dist, &bestY, &dY);

    /* Edges of other visible windows in this workspace. */
    if (pSD->snapToWindows)
    {
	for (pCLE = pSD->clientList; pCLE; pCLE = pCLE->nextSibling)
	{
	    ClientData *o = pCLE->pCD;
	    SnapRect    r;

	    if (!o || o == pcd || pCLE->type != NORMAL_STATE)
		continue;
	    if (o->clientState != NORMAL_STATE &&
		o->clientState != MAXIMIZED_STATE)
		continue;
	    if (!ClientInWorkspace (pSD->pActiveWS, o))
		continue;

	    FrameRect (o, &r);

	    /* Only snap side by side when the frames face each other. */
	    if (Overlaps (*pY, *pY + (int) h, r.y, r.y + r.h, dist))
	    {
		Consider (*pX, *pX + (int) w, r.x, dist, &bestX, &dX);
		Consider (*pX, *pX + (int) w, r.x + r.w, dist, &bestX, &dX);
	    }
	    if (Overlaps (*pX, *pX + (int) w, r.x, r.x + r.w, dist))
	    {
		Consider (*pY, *pY + (int) h, r.y, dist, &bestY, &dY);
		Consider (*pY, *pY + (int) h, r.y + r.h, dist, &bestY, &dY);
	    }
	}
    }

    *pX += dX;
    *pY += dY;
}

int
SnapEdgeZone (ClientData *pcd, int ptrX, int ptrY)
{
    SnapRect head;

    if (!pcd->pSD->edgeTiling || wmGD.movingIcon)
	return SNAP_ZONE_NONE;

    HeadRect (pcd, ptrX, ptrY, &head);

    if (ptrY < head.y + EDGE_ZONE_PX &&
	(pcd->clientFunctions & MWM_FUNC_MAXIMIZE))
	return SNAP_ZONE_TOP;
    if (!(pcd->clientFunctions & MWM_FUNC_RESIZE))
	return SNAP_ZONE_NONE;
    if (ptrX < head.x + EDGE_ZONE_PX)
	return SNAP_ZONE_LEFT;
    if (ptrX >= head.x + head.w - EDGE_ZONE_PX)
	return SNAP_ZONE_RIGHT;
    return SNAP_ZONE_NONE;
}

void
SnapZoneRect (ClientData *pcd, int zone, int ptrX, int ptrY,
	      int *pX, int *pY, unsigned int *pW, unsigned int *pH)
{
    SnapRect head;
    int      half;

    HeadRect (pcd, ptrX, ptrY, &head);
    half = head.w / 2;

    *pY = head.y;
    *pH = (unsigned int) head.h;
    switch (zone)
    {
	case SNAP_ZONE_LEFT:
	    *pX = head.x;
	    *pW = (unsigned int) half;
	    break;
	case SNAP_ZONE_RIGHT:
	    *pX = head.x + half;
	    *pW = (unsigned int) (head.w - half);
	    break;
	default:		/* SNAP_ZONE_TOP: whole monitor */
	    *pX = head.x;
	    *pW = (unsigned int) head.w;
	    break;
    }
}

void
SnapTile (ClientData *pcd, int zone, int ptrX, int ptrY, Time time)
{
    int		 x, y, bx, by;
    unsigned int w, h;
    SnapRect	 cur;

    if (!pcd || zone == SNAP_ZONE_NONE)
	return;

    /* Keyboard invocation: use the monitor the window is on. */
    if (ptrX < 0 || ptrY < 0)
    {
	FrameRect (pcd, &cur);
	ptrX = cur.x + cur.w / 2;
	ptrY = cur.y + cur.h / 2;
    }

    if (zone == SNAP_ZONE_TOP)
    {
	if (pcd->clientFunctions & MWM_FUNC_MAXIMIZE)
	    SetClientState (pcd, MAXIMIZED_STATE, time);
	return;
    }
    if (!(pcd->clientFunctions & MWM_FUNC_RESIZE))
	return;

    if (pcd->clientState == MAXIMIZED_STATE)
	SetClientState (pcd, NORMAL_STATE, time);

    if (!pcd->snapTiled)
    {
	pcd->snapRestoreWidth = pcd->clientWidth;
	pcd->snapRestoreHeight = pcd->clientHeight;
    }

    SnapZoneRect (pcd, zone, ptrX, ptrY, &x, &y, &w, &h);
    bx = pcd->clientOffset.x;
    by = pcd->clientOffset.y;
    if ((int) w <= 2 * bx || (int) h <= bx + by)
	return;

    /* Frame position, client size (as an interactive configure does). */
    ProcessNewConfiguration (pcd, x, y, w - 2 * bx, h - bx - by, False);
    pcd->snapTiled = True;
    pcd->snapZone = zone;
}

void
SnapUntile (ClientData *pcd, Time time)
{
    SnapRect frame, head;
    int      x, y, w, h;

    if (!pcd)
	return;

    if (pcd->clientState == MAXIMIZED_STATE)
    {
	SetClientState (pcd, NORMAL_STATE, time);
	return;
    }
    if (!pcd->snapTiled)
	return;

    FrameRect (pcd, &frame);
    HeadRect (pcd, frame.x + frame.w / 2, frame.y + frame.h / 2, &head);

    w = pcd->snapRestoreWidth;
    h = pcd->snapRestoreHeight;
    /* Center the restored window on the half it occupied, on screen. */
    x = frame.x + (frame.w - (w + 2 * pcd->clientOffset.x)) / 2;
    y = frame.y + (frame.h - (h + pcd->clientOffset.x + pcd->clientOffset.y)) / 2;
    if (x < head.x) x = head.x;
    if (y < head.y) y = head.y;

    pcd->snapTiled = False;
    ProcessNewConfiguration (pcd, x, y, (unsigned int) w, (unsigned int) h,
			     False);
}

void
SnapPrepareDrag (ClientData *pcd, int ptrX, int ptrY)
{
    SnapRect frame;
    int      newFrameW, x, y;

    if (!pcd || !pcd->snapTiled || pcd->clientState != NORMAL_STATE)
	return;

    FrameRect (pcd, &frame);
    newFrameW = pcd->snapRestoreWidth + 2 * pcd->clientOffset.x;

    /* Keep the grab point at the same fraction of the title bar. */
    if (frame.w > 0)
	x = ptrX - (int) ((long) (ptrX - frame.x) * newFrameW / frame.w);
    else
	x = frame.x;
    y = frame.y;

    pcd->snapTiled = False;
    ProcessNewConfiguration (pcd, x, y,
			     (unsigned int) pcd->snapRestoreWidth,
			     (unsigned int) pcd->snapRestoreHeight, False);
}

/* ------------------------------------------------------------------ */

/*
 * Keep 'v..v+len' anchored when the screen grows or shrinks along one
 * axis: windows touching the far edge stay there, centered ones stay
 * centered (the front panel, for one), the rest keep their position.
 */
static int
Reanchor (int v, int len, int oldSize, int newSize)
{
    if (oldSize == newSize || oldSize <= 0)
	return v;
    /* Centered (give or take a border) wins over touching an edge,
     * so a panel that spans the screen stays centered. */
    if (Abs (v + len / 2 - oldSize / 2) <= 8 + oldSize / 100)
	return (newSize - len) / 2;
    if (v > 0 && Abs (v + len - oldSize) <= 1)
	return newSize - len;
    return v;
}

/* Squared distance from (px, py) to the nearest point of r. */
static long
DistanceTo (SnapRect *r, int px, int py)
{
    long dx = 0, dy = 0;

    if (px < r->x) dx = r->x - px;
    else if (px >= r->x + r->w) dx = px - (r->x + r->w - 1);
    if (py < r->y) dy = r->y - py;
    else if (py >= r->y + r->h) dy = py - (r->y + r->h - 1);
    return dx * dx + dy * dy;
}

/* The monitor nearest to (px, py); the whole screen without Xinerama. */
static void
NearestHeadRect (ClientData *pcd, int px, int py, SnapRect *r)
{
    unsigned int i, w, h, ux, uy;
    SnapRect	 cand;
    long	 d, best = -1;

    ScreenRect (pcd, r);
    if (!DtXI)
	DtXI = _DtXineramaInit (DISPLAY);
    if (!DtXI)
	return;
    for (i = 0; _DtXineramaGetScreen (DtXI, i, &w, &h, &ux, &uy); i++)
    {
	cand.x = (int) ux; cand.y = (int) uy;
	cand.w = (int) w;  cand.h = (int) h;
	d = DistanceTo (&cand, px, py);
	if (best < 0 || d < best)
	{
	    best = d;
	    *r = cand;
	}
    }
}

/*
 * The screen size or monitor layout changed (RandR: a monitor was
 * plugged in or removed, the resolution or rotation changed).  Keep
 * the window reachable: re-anchor it, fit it on its monitor (the
 * nearest one if its monitor is gone), and refit maximized and tiled
 * windows to their monitor.  oldW/oldH are the screen size before the change.
 */
void
SnapScreenChanged (ClientData *pcd, int oldW, int oldH, Time time)
{
    SnapRect	 f, head;
    int		 bx, by, x, y, cx, cy;
    Boolean	 maximized;

    if (!pcd || pcd->clientState == MINIMIZED_STATE ||
	pcd->clientState == WITHDRAWN_STATE)
	return;

    maximized = (pcd->clientState == MAXIMIZED_STATE);
    bx = pcd->clientOffset.x;
    by = pcd->clientOffset.y;

    /* Normal (unmaximized) frame geometry. */
    f.x = pcd->clientX - bx;
    f.y = pcd->clientY - by;
    f.w = pcd->clientWidth + 2 * bx;
    f.h = pcd->clientHeight + bx + by;

    ScreenRect (pcd, &head);
    x = Reanchor (f.x, f.w, oldW, head.w);
    y = Reanchor (f.y, f.h, oldH, head.h);

    /*
     * Fit it on the monitor holding its center (or the nearest one if
     * that monitor is gone); the title bar wins if it is too big.
     */
    cx = x + f.w / 2;
    cy = y + f.h / 2;
    NearestHeadRect (pcd, cx, cy, &head);
    if (x + f.w > head.x + head.w) x = head.x + head.w - f.w;
    if (y + f.h > head.y + head.h) y = head.y + head.h - f.h;
    if (x < head.x) x = head.x;
    if (y < head.y) y = head.y;

    if (maximized)
    {
	/* Re-maximize on the monitor the window now belongs to. */
	pcd->clientX = x + bx;
	pcd->clientY = y + by;
	SetClientState (pcd, NORMAL_STATE, time);
	SetClientState (pcd, MAXIMIZED_STATE, time);
	return;
    }

    if (x != f.x || y != f.y)
	ProcessNewConfiguration (pcd, x, y, (unsigned int) pcd->clientWidth,
				 (unsigned int) pcd->clientHeight, False);

    if (pcd->snapTiled && pcd->snapZone != SNAP_ZONE_NONE)
	SnapTile (pcd, pcd->snapZone, -1, -1, time);
}
