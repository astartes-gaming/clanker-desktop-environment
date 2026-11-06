/*
 * SmDbus.c - dtsession's services on the D-Bus session bus.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 *
 * dtsession owns org.freedesktop.ScreenSaver, the interface browsers,
 * video players and presentation tools call to keep the screen from
 * blanking and locking while they play (Inhibit/UnInhibit), and that
 * desktop tools use to lock the screen.  The same object is exported at
 * /org/freedesktop/ScreenSaver and /ScreenSaver, as other desktops do.
 *
 * While any application holds an inhibit, the X screen saver (and with
 * it DPMS) is suspended with XScreenSaverSuspend, or, on servers
 * without it, the saver timer is reset every 30 seconds.  An inhibit
 * ends when the application calls UnInhibit or leaves the bus.
 */
#include <cde_config.h>

#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include "Sm.h"
#include "SmGlobals.h"
#include "SmLock.h"
#include "SmDbus.h"

#ifdef HAVE_DBUS

#include <Dt/SvcDbus.h>
#include <dbus/dbus.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined (USE_X11SSEXT)
#include <X11/extensions/scrnsaver.h>
#endif

#define SS_NAME   "org.freedesktop.ScreenSaver"
#define SS_IFACE  "org.freedesktop.ScreenSaver"
#define SS_PATH   "/org/freedesktop/ScreenSaver"
#define SS_PATH2  "/ScreenSaver"

enum {
    MAX_INHIBITORS = 1024,      /* per session; a misbehaving client cannot
                                 * grow dtsession without bound */
    HEARTBEAT_MS = 30 * 1000    /* saver reset interval without Suspend */
};

static const char introspectXml[] =
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE
    "<node>\n"
    " <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
    "  <method name=\"Introspect\">\n"
    "   <arg name=\"xml\" type=\"s\" direction=\"out\"/>\n"
    "  </method>\n"
    " </interface>\n"
    " <interface name=\"" SS_IFACE "\">\n"
    "  <method name=\"Lock\"/>\n"
    "  <method name=\"SimulateUserActivity\"/>\n"
    "  <method name=\"GetActive\">\n"
    "   <arg type=\"b\" direction=\"out\"/>\n"
    "  </method>\n"
    "  <method name=\"SetActive\">\n"
    "   <arg name=\"e\" type=\"b\" direction=\"in\"/>\n"
    "   <arg type=\"b\" direction=\"out\"/>\n"
    "  </method>\n"
    "  <method name=\"GetActiveTime\">\n"
    "   <arg name=\"seconds\" type=\"u\" direction=\"out\"/>\n"
    "  </method>\n"
    "  <method name=\"GetSessionIdleTime\">\n"
    "   <arg name=\"seconds\" type=\"u\" direction=\"out\"/>\n"
    "  </method>\n"
    "  <method name=\"Inhibit\">\n"
    "   <arg name=\"application_name\" type=\"s\" direction=\"in\"/>\n"
    "   <arg name=\"reason_for_inhibit\" type=\"s\" direction=\"in\"/>\n"
    "   <arg name=\"cookie\" type=\"u\" direction=\"out\"/>\n"
    "  </method>\n"
    "  <method name=\"UnInhibit\">\n"
    "   <arg name=\"cookie\" type=\"u\" direction=\"in\"/>\n"
    "  </method>\n"
    "  <signal name=\"ActiveChanged\">\n"
    "   <arg type=\"b\"/>\n"
    "  </signal>\n"
    " </interface>\n"
    "</node>\n";

typedef struct Inhibitor {
    struct Inhibitor *next;
    uint32_t cookie;
    char *owner;                /* unique bus name of the application */
} Inhibitor;

static DBusConnection *bus;
static bool ownsName;
static Inhibitor *inhibitors;
static int inhibitorCount;
static uint32_t nextCookie = 1;
static bool suspended;          /* saver held off for inhibitors */
static bool canSuspend;         /* server has MIT-SCREEN-SAVER 1.1 */
static XtIntervalId heartbeat;
static bool screenActive;
static time_t activeSince;

/* ---- holding off the screen saver ---------------------------------- */

static void
Heartbeat(XtPointer data, XtIntervalId *id)
{
    (void) data; (void) id;
    XResetScreenSaver(smGD.display);
    XFlush(smGD.display);
    heartbeat = XtAppAddTimeOut(smGD.appCon, HEARTBEAT_MS, Heartbeat, NULL);
}

static void
UpdateSuspend(void)
{
    bool want = inhibitors != NULL;

    if (want == suspended)
        return;
    suspended = want;

#if defined (USE_X11SSEXT) && defined (HAVE_XSCREENSAVERSUSPEND)
    if (canSuspend) {
        XScreenSaverSuspend(smGD.display, want ? True : False);
        XFlush(smGD.display);
        return;
    }
#endif
    if (want) {
        XResetScreenSaver(smGD.display);
        XFlush(smGD.display);
        heartbeat = XtAppAddTimeOut(smGD.appCon, HEARTBEAT_MS, Heartbeat, NULL);
    } else if (heartbeat) {
        XtRemoveTimeOut(heartbeat);
        heartbeat = 0;
    }
}

static uint32_t
AddInhibitor(const char *owner)
{
    if (inhibitorCount >= MAX_INHIBITORS)
        return 0;

    Inhibitor *in = calloc(1, sizeof *in);
    if (!in || !(in->owner = strdup(owner))) {
        free(in);
        return 0;
    }
    do {                        /* 0 is never a valid cookie */
        in->cookie = nextCookie++;
    } while (in->cookie == 0);
    in->next = inhibitors;
    inhibitors = in;
    inhibitorCount++;
    UpdateSuspend();
    return in->cookie;
}

/* Remove inhibitors matching cookie (if non-zero) or owner (if non-NULL). */
static void
RemoveInhibitors(uint32_t cookie, const char *owner)
{
    Inhibitor **pp = &inhibitors;

    while (*pp) {
        Inhibitor *in = *pp;
        if ((cookie && in->cookie == cookie) ||
            (owner && strcmp(in->owner, owner) == 0)) {
            *pp = in->next;
            free(in->owner);
            free(in);
            inhibitorCount--;
        } else {
            pp = &in->next;
        }
    }
    UpdateSuspend();
}

/* ---- method calls -------------------------------------------------- */

static void
Reply(DBusMessage *call, int firstType, ...)
{
    DBusMessage *reply = dbus_message_new_method_return(call);
    va_list ap;

    if (!reply)
        return;
    va_start(ap, firstType);
    if (firstType == DBUS_TYPE_INVALID ||
        dbus_message_append_args_valist(reply, firstType, ap))
        dbus_connection_send(bus, reply, NULL);
    va_end(ap);
    dbus_message_unref(reply);
}

static void
ReplyError(DBusMessage *call, const char *name, const char *text)
{
    DBusMessage *reply = dbus_message_new_error(call, name, text);

    if (reply) {
        dbus_connection_send(bus, reply, NULL);
        dbus_message_unref(reply);
    }
}

static void
LockNow(void)
{
    if (smGD.smState == READY) {
        smGD.smState = IN_PROCESS;
        LockDisplay(True);
    }
}

static dbus_uint32_t
SessionIdleSeconds(void)
{
#if defined (USE_X11SSEXT)
    XScreenSaverInfo *info = XScreenSaverAllocInfo();
    dbus_uint32_t secs = 0;

    if (info) {
        if (smGD.ssEventType != None &&
            XScreenSaverQueryInfo(smGD.display,
                                  DefaultRootWindow(smGD.display), info))
            secs = (dbus_uint32_t) (info->idle / 1000);
        XFree(info);
    }
    return secs;
#else
    return 0;
#endif
}

static DBusHandlerResult
HandleScreenSaver(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char *member = dbus_message_get_member(msg);
    DBusError err;

    (void) c; (void) data;
    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL || !member)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (dbus_message_is_method_call(msg, DBUS_INTERFACE_INTROSPECTABLE,
                                    "Introspect")) {
        const char *xml = introspectXml;
        Reply(msg, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* Calls without an interface are allowed by the D-Bus spec. */
    const char *iface = dbus_message_get_interface(msg);
    if (iface && strcmp(iface, SS_IFACE) != 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    dbus_error_init(&err);

    if (strcmp(member, "Inhibit") == 0) {
        const char *app, *reason;
        const char *sender = dbus_message_get_sender(msg);
        if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &app,
                                   DBUS_TYPE_STRING, &reason,
                                   DBUS_TYPE_INVALID) || !sender) {
            ReplyError(msg, DBUS_ERROR_INVALID_ARGS, "expected (ss)");
            dbus_error_free(&err);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        dbus_uint32_t cookie = AddInhibitor(sender);
        if (!cookie)
            ReplyError(msg, DBUS_ERROR_LIMITS_EXCEEDED, "too many inhibitors");
        else
            Reply(msg, DBUS_TYPE_UINT32, &cookie, DBUS_TYPE_INVALID);
    } else if (strcmp(member, "UnInhibit") == 0) {
        dbus_uint32_t cookie;
        if (!dbus_message_get_args(msg, &err, DBUS_TYPE_UINT32, &cookie,
                                   DBUS_TYPE_INVALID)) {
            ReplyError(msg, DBUS_ERROR_INVALID_ARGS, "expected (u)");
            dbus_error_free(&err);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (cookie)
            RemoveInhibitors(cookie, NULL);
        Reply(msg, DBUS_TYPE_INVALID);
    } else if (strcmp(member, "Lock") == 0) {
        LockNow();
        Reply(msg, DBUS_TYPE_INVALID);
    } else if (strcmp(member, "SimulateUserActivity") == 0) {
        if (!screenActive) {
            XResetScreenSaver(smGD.display);
            XFlush(smGD.display);
        }
        Reply(msg, DBUS_TYPE_INVALID);
    } else if (strcmp(member, "GetActive") == 0) {
        dbus_bool_t on = screenActive;
        Reply(msg, DBUS_TYPE_BOOLEAN, &on, DBUS_TYPE_INVALID);
    } else if (strcmp(member, "SetActive") == 0) {
        dbus_bool_t want, done = FALSE;
        if (!dbus_message_get_args(msg, &err, DBUS_TYPE_BOOLEAN, &want,
                                   DBUS_TYPE_INVALID)) {
            ReplyError(msg, DBUS_ERROR_INVALID_ARGS, "expected (b)");
            dbus_error_free(&err);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        /* Turning the saver on goes through the normal saver path, with
         * its lock delay.  Only the user can turn it off. */
#if defined (USE_X11SSEXT)
        if (want && !screenActive && smGD.ssEventType != None) {
            XForceScreenSaver(smGD.display, ScreenSaverActive);
            XFlush(smGD.display);
            done = TRUE;
        }
#endif
        Reply(msg, DBUS_TYPE_BOOLEAN, &done, DBUS_TYPE_INVALID);
    } else if (strcmp(member, "GetActiveTime") == 0) {
        dbus_uint32_t secs = 0;
        if (screenActive) {
            time_t now = time(NULL);
            if (now > activeSince)
                secs = (dbus_uint32_t) (now - activeSince);
        }
        Reply(msg, DBUS_TYPE_UINT32, &secs, DBUS_TYPE_INVALID);
    } else if (strcmp(member, "GetSessionIdleTime") == 0) {
        dbus_uint32_t secs = SessionIdleSeconds();
        Reply(msg, DBUS_TYPE_UINT32, &secs, DBUS_TYPE_INVALID);
    } else {
        ReplyError(msg, DBUS_ERROR_UNKNOWN_METHOD, member);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

/* An application left the bus: drop its inhibitors. */
static DBusHandlerResult
BusFilter(DBusConnection *c, DBusMessage *msg, void *data)
{
    const char *name, *oldOwner, *newOwner;

    (void) c; (void) data;
    if (inhibitors &&
        dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameOwnerChanged") &&
        dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &name,
                              DBUS_TYPE_STRING, &oldOwner,
                              DBUS_TYPE_STRING, &newOwner,
                              DBUS_TYPE_INVALID) &&
        *newOwner == '\0')
        RemoveInhibitors(0, name);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---- public -------------------------------------------------------- */

void
SmDbusInit(void)
{
    static const DBusObjectPathVTable vtable = {
        .message_function = HandleScreenSaver
    };

    bus = _DtSvcDbusSessionBus(smGD.appCon);
    if (!bus)
        return;

#if defined (USE_X11SSEXT) && defined (HAVE_XSCREENSAVERSUSPEND)
    {
        int ev, er, major = 0, minor = 0;
        canSuspend = XScreenSaverQueryExtension(smGD.display, &ev, &er) &&
                     XScreenSaverQueryVersion(smGD.display, &major, &minor) &&
                     (major > 1 || (major == 1 && minor >= 1));
    }
#endif

    /* Another screen saver (xscreensaver, light-locker...) may own the
     * name already; then it keeps answering, and we stay out of its way. */
    ownsName = _DtSvcDbusRequestName(bus, SS_NAME);
    if (!ownsName)
        return;

    if (!dbus_connection_register_object_path(bus, SS_PATH, &vtable, NULL) ||
        !dbus_connection_register_object_path(bus, SS_PATH2, &vtable, NULL))
        return;

    /* Watch for applications leaving the bus (sent without waiting). */
    dbus_bus_add_match(bus,
                       "type='signal',sender='" DBUS_SERVICE_DBUS "',"
                       "interface='" DBUS_INTERFACE_DBUS "',"
                       "member='NameOwnerChanged',arg2=''", NULL);
    dbus_connection_add_filter(bus, BusFilter, NULL, NULL);
}

void
SmDbusScreenActive(Boolean active)
{
    bool on = active ? true : false;

    if (on == screenActive)
        return;
    screenActive = on;
    if (on)
        activeSince = time(NULL);

    if (!bus || !ownsName)
        return;
    dbus_bool_t value = on;
    const char *paths[] = { SS_PATH, SS_PATH2 };
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        DBusMessage *sig = dbus_message_new_signal(paths[i], SS_IFACE,
                                                   "ActiveChanged");
        if (!sig)
            continue;
        if (dbus_message_append_args(sig, DBUS_TYPE_BOOLEAN, &value,
                                     DBUS_TYPE_INVALID))
            dbus_connection_send(bus, sig, NULL);
        dbus_message_unref(sig);
    }
}

#else /* !HAVE_DBUS */

void
SmDbusInit(void)
{
}

void
SmDbusScreenActive(Boolean active)
{
    (void) active;
}

#endif /* HAVE_DBUS */
