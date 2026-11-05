/*
 * WmSnap.h - window snapping and tiling for dtwm.
 *
 * Part of the Clanker Desktop Environment, a fork of the Common Desktop
 * Environment; distributed under the same terms (LGPL-2.0-or-later).
 */
#ifndef _WmSnap_h
#define _WmSnap_h

#include "WmGlobal.h"

#define SNAP_ZONE_NONE	0
#define SNAP_ZONE_LEFT	1
#define SNAP_ZONE_RIGHT	2
#define SNAP_ZONE_TOP	3

/* Magnetic snapping: pull the frame rectangle (*pX, *pY, w, h) to monitor
 * edges and, if enabled, to edges of other windows within snapDistance. */
extern void SnapFramePosition (ClientData *pcd, int *pX, int *pY,
			       unsigned int w, unsigned int h);

/* Edge tiling zone under the pointer (pointer touching a monitor edge). */
extern int SnapEdgeZone (ClientData *pcd, int ptrX, int ptrY);

/* Frame rectangle a zone would give, for the drag preview outline. */
extern void SnapZoneRect (ClientData *pcd, int zone, int ptrX, int ptrY,
			  int *pX, int *pY, unsigned int *pW, unsigned int *pH);

/* Tile pcd into zone on the monitor containing (ptrX, ptrY). */
extern void SnapTile (ClientData *pcd, int zone, int ptrX, int ptrY,
		      Time time);

/* Undo tiling: restore the pre-tile size, keeping the window on screen. */
extern void SnapUntile (ClientData *pcd, Time time);

/* A drag is starting on a tiled window: restore its size so that the
 * pointer keeps its relative position along the title bar. */
extern void SnapPrepareDrag (ClientData *pcd, int ptrX, int ptrY);

/* The screen size or monitor layout changed (RandR); oldW/oldH is the
 * previous screen size.  Keeps the window on a monitor and refits
 * maximized and tiled windows. */
extern void SnapScreenChanged (ClientData *pcd, int oldW, int oldH,
			       Time time);

#endif /* _WmSnap_h */
