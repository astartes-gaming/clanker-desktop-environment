/*
 * applist.h - an icon list for dtappfind: rows of a small icon and a
 * label, drawn directly (Motif's XmList shows no pictures).
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifndef APPLIST_H
#define APPLIST_H

#include <X11/Intrinsic.h>
#include <Xm/Xm.h>

#define APPLIST_ICON 20		/* icon size, pixels */

typedef struct _AppList AppList;

/* What a row shows; the icon names are those of .desktop files (a
 * theme name, a path, or a CDE icon name), NULL for none. */
typedef const char *(*AppListText)(int row);
typedef const char *(*AppListIcon)(int row);
typedef void        (*AppListRowCB)(int row);
typedef void        (*AppListDragCB)(int row, XEvent *ev);

AppList *AppListCreate(Widget parent, const char *name, Arg *args, int n,
                       Widget colors, AppListText text, AppListIcon icon,
                       AppListRowCB select, AppListRowCB activate,
                       AppListDragCB drag);
Widget   AppListWidget(AppList *l);	/* the scrolled window, to attach */
void     AppListSetCount(AppList *l, int n);	/* rows changed; row 0 selected */
void     AppListSelect(AppList *l, int row);	/* and scroll to it */
int      AppListSelected(AppList *l);	/* -1: none */
int      AppListVisibleRows(AppList *l);
void     AppListForgetIcons(AppList *l);	/* the rows mean other items */
void     AppListAutoHideScrollBar(AppList *l);	/* none when all rows fit */

#endif
