/*
 * SvcDbus.h - the D-Bus session bus, driven by the Xt main loop.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 *
 * CDE programs use D-Bus for desktop services that modern
 * applications expect (org.freedesktop.ScreenSaver and others), next
 * to ToolTalk.  D-Bus is optional: without it, or with no session bus
 * running, _DtSvcDbusSessionBus() returns NULL and callers carry on
 * without it.
 *
 * The connection is libdbus's shared session connection.  Callers that
 * need the libdbus API include <dbus/dbus.h> themselves.
 */
#ifndef _DT_SVC_DBUS_H
#define _DT_SVC_DBUS_H

#include <X11/Intrinsic.h>

struct DBusConnection;

/*
 * Connect to the session bus and dispatch its messages from app's
 * main loop (XtAppMainLoop, XtAppNextEvent, XtAppProcessEvent).
 * Returns NULL if D-Bus support was not built or no bus is running.
 * Repeated calls return the same connection.
 */
struct DBusConnection *_DtSvcDbusSessionBus(XtAppContext app);

/*
 * Ask for a well-known bus name.  Returns True if this process now
 * owns it; False if another process does (it is not queued).
 */
Boolean _DtSvcDbusRequestName(struct DBusConnection *bus, const char *name);

#endif /* _DT_SVC_DBUS_H */
