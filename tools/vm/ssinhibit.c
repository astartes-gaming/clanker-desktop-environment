/*
 * ssinhibit - hold an org.freedesktop.ScreenSaver inhibit, like a video
 * player does, for the self-test.
 *
 *   ssinhibit SECONDS       inhibit, wait, then exit without UnInhibit
 *                           (the session must notice we left the bus)
 *   ssinhibit -u SECONDS    inhibit, wait, UnInhibit, then stay on the
 *                           bus for 60 seconds more
 *
 * Prints the cookie.  Part of the Clanker Desktop Environment
 * (LGPL-2.0-or-later).
 */
#include <dbus/dbus.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DBusMessage *
call(DBusConnection *bus, const char *method, int firstType, ...)
{
    DBusMessage *msg = dbus_message_new_method_call(
        "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
        "org.freedesktop.ScreenSaver", method);
    DBusError err;
    va_list ap;

    va_start(ap, firstType);
    dbus_message_append_args_valist(msg, firstType, ap);
    va_end(ap);
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(bus, msg,
                                                                   5000, &err);
    dbus_message_unref(msg);
    if (!reply) {
        fprintf(stderr, "ssinhibit: %s: %s\n", method, err.message);
        exit(1);
    }
    return reply;
}

int
main(int argc, char **argv)
{
    int explicitRelease = argc > 1 && strcmp(argv[1], "-u") == 0;
    int argi = explicitRelease ? 2 : 1;
    int secs = argc > argi ? atoi(argv[argi]) : 10;
    const char *app = "selftest", *reason = "testing inhibit";
    DBusError err;

    dbus_error_init(&err);
    DBusConnection *bus = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!bus) {
        fprintf(stderr, "ssinhibit: %s\n", err.message);
        return 1;
    }

    DBusMessage *reply = call(bus, "Inhibit", DBUS_TYPE_STRING, &app,
                              DBUS_TYPE_STRING, &reason, DBUS_TYPE_INVALID);
    dbus_uint32_t cookie = 0;
    dbus_message_get_args(reply, NULL, DBUS_TYPE_UINT32, &cookie,
                          DBUS_TYPE_INVALID);
    dbus_message_unref(reply);
    printf("%u\n", (unsigned) cookie);
    fflush(stdout);

    sleep((unsigned) secs);

    if (explicitRelease) {
        dbus_message_unref(call(bus, "UnInhibit", DBUS_TYPE_UINT32, &cookie,
                                DBUS_TYPE_INVALID));
        sleep(60);
    }
    return 0;
}
