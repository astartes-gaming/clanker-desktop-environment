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
#define ACCESS    "Accessibility"
#define SHORTCUTS "Shortcuts"
#define SEARCHP   "Search"
#define DISPLAYS  "Displays"
#define SOUND     "Sound"
#define CONTROLLERS "Controllers"
#define USERS     "Users"
#define REGION    "Region"
#define INPUT     "Input"
#define MEDIA     "Media"
#define BORDERS   "Borders"

#define NETWORK_ICON   "DtNetwk"
#define BLUETOOTH_ICON "DtBlue"
#define UPDATES_ICON   "DtUpdat"
#define AUTOSTART_ICON "DtStart"
#define FRONTPANEL_ICON "Fpapps.l"
#define SESSION_ICON    "DtSystem.l"
#define ACCESS_ICON     "DtAccess"
#define SHORTCUTS_ICON  "DtKeybd"
#define SEARCHP_ICON    "DtSearch"
#define DISPLAYS_ICON   "DtScrn"
#define SOUND_ICON      "Dthvol.m"
#define CONTROLLERS_ICON "DtGamesCards.l"
#define USERS_ICON      "Dthome.l"
#define REGION_ICON     "DtI18N"
#define INPUT_ICON      "DtKeybd"
#define MEDIA_ICON      "DtSystem.l"
#define BORDERS_ICON    "DtDtwm"

extern void popup_networkBB(Widget shell);
extern void popup_shortcutsBB(Widget shell);
extern void popup_bluetoothBB(Widget shell);
extern void popup_updatesBB(Widget shell);
extern void popup_autostartBB(Widget shell);
extern void popup_frontPanelBB(Widget shell);
extern void popup_sessionBB(Widget shell);
extern void popup_accessBB(Widget shell);
/* SysPanelsMore.c */
extern void popup_searchBB(Widget shell);
extern void popup_displaysBB(Widget shell);
extern void popup_soundBB(Widget shell);
extern void popup_controllersBB(Widget shell);
extern void popup_usersBB(Widget shell);
extern void popup_regionBB(Widget shell);
extern void popup_inputBB(Widget shell);
extern void popup_mediaBB(Widget shell);
extern void popup_bordersBB(Widget shell);

#endif /* _SysPanels_h */
