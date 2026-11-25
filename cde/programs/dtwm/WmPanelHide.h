#ifndef _WmPanelHide_h
#define _WmPanelHide_h
/* Front Panel auto-hide (WmPanelHide.c). */
extern void PanelAutoHideInit(void);
extern void PanelAutoHideShow(void);
/* After a monitor change: back to the primary monitor if the panel
 * is no longer on one. */
extern void PanelKeepOnScreen(void);
#endif
