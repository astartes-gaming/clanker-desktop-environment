/*
 * SysPanels.h - Style Manager panels for Network, Bluetooth and
 * software Updates.
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

#define NETWORK_ICON   "DtNetwk"
#define BLUETOOTH_ICON "DtBlue"
#define UPDATES_ICON   "DtUpdat"
#define AUTOSTART_ICON "DtStart"

extern void popup_networkBB(Widget shell);
extern void popup_bluetoothBB(Widget shell);
extern void popup_updatesBB(Widget shell);
extern void popup_autostartBB(Widget shell);

#endif /* _SysPanels_h */
