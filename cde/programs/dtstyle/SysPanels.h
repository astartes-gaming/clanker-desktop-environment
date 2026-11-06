/*
 * SysPanels.h - Style Manager panels for Network, Bluetooth, software
 * Updates, Autostart, the Front Panel buttons and the session options
 * (~/.dtprofile).
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifndef _SysPanels_h
#define _SysPanels_h

#include <X11/Intrinsic.h>

#define NETWORK   "Network"
#define BLUETOOTH "Bluetooth"
#define UPDATES   "Updates"
#define AUTOSTART "Autostart"
#define FRONTPANEL "FrontPanel"
#define SESSION   "Session"

#define NETWORK_ICON   "DtNetwk"
#define BLUETOOTH_ICON "DtBlue"
#define UPDATES_ICON   "DtUpdat"
#define AUTOSTART_ICON "DtStart"
#define FRONTPANEL_ICON "Fpapps.l"
#define SESSION_ICON    "DtSystem.l"

extern void popup_networkBB(Widget shell);
extern void popup_bluetoothBB(Widget shell);
extern void popup_updatesBB(Widget shell);
extern void popup_autostartBB(Widget shell);
extern void popup_frontPanelBB(Widget shell);
extern void popup_sessionBB(Widget shell);

#endif /* _SysPanels_h */
