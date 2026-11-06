/*
 * SvcDbus.c - the D-Bus session bus, driven by the Xt main loop.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 *
 * libdbus tells us which file descriptors to watch and which timers to
 * run; we hand those to Xt (XtAppAddInput, XtAppAddTimeOut), so any Xt
 * main loop also services the bus.  Incoming messages are dispatched
 * from a zero-length Xt timer, a batch at a time, so a flood of D-Bus
 * traffic cannot starve X event handling.
 */
#include <cde_config.h>

#include <Dt/SvcDbus.h>

#ifdef HAVE_DBUS

#include <dbus/dbus.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    DISPATCH_BATCH = 64,
    REENTRY_DELAY_MS = 10
};

static XtAppContext busApp;
static DBusConnection *bus;
static bool dispatchQueued;
static bool dispatching;

static void
Dispatch(XtPointer data, XtIntervalId *id)
{
    (void) data; (void) id;

    /* A message handler that runs a nested Xt loop (a modal dialog)
     * must not re-enter dbus_connection_dispatch: libdbus would wait
     * for itself.  Try again once the handler has returned. */
    if (dispatching) {
        XtAppAddTimeOut(busApp, REENTRY_DELAY_MS, Dispatch, NULL);
        return;
    }
    dispatchQueued = false;
    dispatching = true;
    DBusDispatchStatus status = DBUS_DISPATCH_COMPLETE;
    for (int n = 0; n < DISPATCH_BATCH; n++) {
        status = dbus_connection_dispatch(bus);
        if (status != DBUS_DISPATCH_DATA_REMAINS)
            break;
    }
    dispatching = false;
    if (status == DBUS_DISPATCH_DATA_REMAINS && !dispatchQueued) {
        dispatchQueued = true;  /* more left: let X events in first */
        XtAppAddTimeOut(busApp, 0, Dispatch, NULL);
    }
}

static void
QueueDispatch(void)
{
    if (!dispatchQueued) {
        dispatchQueued = true;
        XtAppAddTimeOut(busApp, 0, Dispatch, NULL);
    }
}

static void
DispatchStatusChanged(DBusConnection *c, DBusDispatchStatus status, void *data)
{
    (void) c; (void) data;
    if (status == DBUS_DISPATCH_DATA_REMAINS)
        QueueDispatch();
}

/* ---- watches: file descriptors ------------------------------------ */

typedef struct {
    XtInputId read;             /* 0 when not registered with Xt */
    XtInputId write;
} WatchInputs;

static void
WatchReady(XtPointer data, int *fd, XtInputId *id)
{
    DBusWatch *watch = data;
    WatchInputs *in = dbus_watch_get_data(watch);
    unsigned int flags = (in && *id == in->write)
        ? DBUS_WATCH_WRITABLE : DBUS_WATCH_READABLE;

    (void) fd;
    /* May toggle or remove this watch; do not touch `in` afterwards. */
    dbus_watch_handle(watch, flags);
    if (dbus_connection_get_dispatch_status(bus) == DBUS_DISPATCH_DATA_REMAINS)
        QueueDispatch();
}

static void
WatchUnregister(WatchInputs *in)
{
    if (in->read) {
        XtRemoveInput(in->read);
        in->read = 0;
    }
    if (in->write) {
        XtRemoveInput(in->write);
        in->write = 0;
    }
}

static void
WatchSync(DBusWatch *watch)
{
    WatchInputs *in = dbus_watch_get_data(watch);

    if (!in)
        return;
    WatchUnregister(in);
    if (!dbus_watch_get_enabled(watch))
        return;

    int fd = dbus_watch_get_unix_fd(watch);
    unsigned int flags = dbus_watch_get_flags(watch);

    if (flags & DBUS_WATCH_READABLE)
        in->read = XtAppAddInput(busApp, fd, (XtPointer) XtInputReadMask,
                                 WatchReady, watch);
    if (flags & DBUS_WATCH_WRITABLE)
        in->write = XtAppAddInput(busApp, fd, (XtPointer) XtInputWriteMask,
                                  WatchReady, watch);
}

static dbus_bool_t
AddWatch(DBusWatch *watch, void *data)
{
    WatchInputs *in = calloc(1, sizeof *in);

    (void) data;
    if (!in)
        return FALSE;
    dbus_watch_set_data(watch, in, free);
    WatchSync(watch);
    return TRUE;
}

static void
RemoveWatch(DBusWatch *watch, void *data)
{
    WatchInputs *in = dbus_watch_get_data(watch);

    (void) data;
    if (in)
        WatchUnregister(in);
}

static void
ToggleWatch(DBusWatch *watch, void *data)
{
    (void) data;
    WatchSync(watch);
}

/* ---- timeouts ------------------------------------------------------ */

typedef struct {
    DBusTimeout *timeout;
    XtIntervalId id;            /* 0 when not scheduled */
} Timer;

static void TimerFired(XtPointer data, XtIntervalId *id);

static void
TimerSync(Timer *t)
{
    if (t->id) {
        XtRemoveTimeOut(t->id);
        t->id = 0;
    }
    if (dbus_timeout_get_enabled(t->timeout))
        t->id = XtAppAddTimeOut(busApp,
                                (unsigned long) dbus_timeout_get_interval(t->timeout),
                                TimerFired, t);
}

static void
TimerFired(XtPointer data, XtIntervalId *id)
{
    Timer *t = data;
    DBusTimeout *timeout = t->timeout;

    (void) id;
    t->id = 0;
    TimerSync(t);               /* D-Bus timeouts repeat until removed */
    /* May remove the timeout and free `t`; do not touch it afterwards. */
    dbus_timeout_handle(timeout);
    if (dbus_connection_get_dispatch_status(bus) == DBUS_DISPATCH_DATA_REMAINS)
        QueueDispatch();
}

static dbus_bool_t
AddTimeout(DBusTimeout *timeout, void *data)
{
    Timer *t = calloc(1, sizeof *t);

    (void) data;
    if (!t)
        return FALSE;
    t->timeout = timeout;
    dbus_timeout_set_data(timeout, t, free);
    TimerSync(t);
    return TRUE;
}

static void
RemoveTimeout(DBusTimeout *timeout, void *data)
{
    Timer *t = dbus_timeout_get_data(timeout);

    (void) data;
    if (t && t->id) {
        XtRemoveTimeOut(t->id);
        t->id = 0;
    }
}

static void
ToggleTimeout(DBusTimeout *timeout, void *data)
{
    Timer *t = dbus_timeout_get_data(timeout);

    (void) data;
    if (t)
        TimerSync(t);
}

/* ---- public -------------------------------------------------------- */

/*
 * Is a session bus there to connect to?  Without this check libdbus
 * would "autolaunch" a private bus through dbus-launch, which would
 * outlive the session.
 */
static bool
SessionBusAvailable(void)
{
    const char *addr = getenv("DBUS_SESSION_BUS_ADDRESS");
    const char *rundir = getenv("XDG_RUNTIME_DIR");

    if (addr && *addr)
        return true;
    if (rundir && *rundir) {
        char path[1024];
        int n = snprintf(path, sizeof path, "%s/bus", rundir);
        if (n > 0 && (size_t) n < sizeof path && access(path, F_OK) == 0)
            return true;
    }
    return false;
}

struct DBusConnection *
_DtSvcDbusSessionBus(XtAppContext app)
{
    static bool tried;
    DBusError err;

    if (bus || tried)
        return bus;
    tried = true;

    if (!SessionBusAvailable())
        return NULL;

    dbus_error_init(&err);
    DBusConnection *c = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!c) {
        dbus_error_free(&err);
        return NULL;
    }
    /* libdbus would _exit() the whole program if the bus went away. */
    dbus_connection_set_exit_on_disconnect(c, FALSE);

    busApp = app;
    bus = c;
    if (!dbus_connection_set_watch_functions(c, AddWatch, RemoveWatch,
                                             ToggleWatch, NULL, NULL) ||
        !dbus_connection_set_timeout_functions(c, AddTimeout, RemoveTimeout,
                                               ToggleTimeout, NULL, NULL)) {
        dbus_connection_set_watch_functions(c, NULL, NULL, NULL, NULL, NULL);
        dbus_connection_set_timeout_functions(c, NULL, NULL, NULL, NULL, NULL);
        dbus_connection_unref(c);
        bus = NULL;
        return NULL;
    }
    dbus_connection_set_dispatch_status_function(c, DispatchStatusChanged,
                                                 NULL, NULL);
    QueueDispatch();            /* anything that arrived while connecting */
    return bus;
}

Boolean
_DtSvcDbusRequestName(struct DBusConnection *b, const char *name)
{
    DBusError err;

    if (!b || !name)
        return False;
    dbus_error_init(&err);
    int r = dbus_bus_request_name(b, name, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        return False;
    }
    return r == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ||
           r == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER;
}

#else /* !HAVE_DBUS */

struct DBusConnection *
_DtSvcDbusSessionBus(XtAppContext app)
{
    (void) app;
    return NULL;
}

Boolean
_DtSvcDbusRequestName(struct DBusConnection *b, const char *name)
{
    (void) b; (void) name;
    return False;
}

#endif /* HAVE_DBUS */
