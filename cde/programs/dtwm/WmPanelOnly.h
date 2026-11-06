/*
 * WmPanelOnly.h - dtwm -panelonly (Front Panel under another window
 * manager).  Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifndef _WmPanelOnly_h
#define _WmPanelOnly_h

extern void PanelOnlyInit(WmScreenData *pSD);
extern void PanelOnlySetDesktop(WmScreenData *pSD, int index);
extern void PanelOnlyWorkspaceChanged(WmWorkspaceData *pWS);
extern Boolean PanelOnlyRootProperty(XPropertyEvent *ev);
extern void PanelOnlyDockWindow(Window w, int x, int y, int width, int height);
extern void PanelOnlySetBackdrop(WmScreenData *pSD, Pixmap pixmap, Pixel pixel,
                                 Boolean usePixmap);
extern void PanelOnlyScreenChanged(WmScreenData *pSD);
extern Boolean PanelOnlyDesktopEvent(XEvent *event);
extern void PanelOnlyMovePanel(Widget shell, XEvent *event);

#endif /* _WmPanelOnly_h */
