/*
 * DbusFM.c - org.freedesktop.FileManager1 on the D-Bus session bus.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 *
 * Browsers, download managers and editors call this interface for
 * "Show in folder".  ShowFolders opens a File Manager view of each
 * folder; ShowItems and ShowItemProperties open each item's folder and
 * select the item.  dtfile only takes the name when no other file
 * manager owns it, and does nothing without a session bus.
 */
#include <cde_config.h>

#include <Xm/Xm.h>

#include "DbusFM.h"

#ifdef HAVE_DBUS

#include <dbus/dbus.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Dt/Connect.h>
#include <Dt/SvcDbus.h>
#include <Tt/tttk.h>

#include "Encaps.h"
#include "FileMgr.h"
#include "Desktop.h"
#include "Main.h"

#define FM1_NAME  "org.freedesktop.FileManager1"
#define FM1_PATH  "/org/freedesktop/FileManager1"
#define FM1_IFACE "org.freedesktop.FileManager1"

/* At most this many URIs per call: a caller cannot open a flood of
 * windows by accident. */
enum { MAX_URIS = 32 };

static const char introspectXml[] =
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE
    "<node>\n"
    " <interface name=\"" FM1_IFACE "\">\n"
    "  <method name=\"ShowFolders\">\n"
    "   <arg type=\"as\" name=\"URIs\" direction=\"in\"/>\n"
    "   <arg type=\"s\" name=\"StartupId\" direction=\"in\"/>\n"
    "  </method>\n"
    "  <method name=\"ShowItems\">\n"
    "   <arg type=\"as\" name=\"URIs\" direction=\"in\"/>\n"
    "   <arg type=\"s\" name=\"StartupId\" direction=\"in\"/>\n"
    "  </method>\n"
    "  <method name=\"ShowItemProperties\">\n"
    "   <arg type=\"as\" name=\"URIs\" direction=\"in\"/>\n"
    "   <arg type=\"s\" name=\"StartupId\" direction=\"in\"/>\n"
    "  </method>\n"
    " </interface>\n"
    " <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
    "  <method name=\"Introspect\">\n"
    "   <arg type=\"s\" name=\"xml\" direction=\"out\"/>\n"
    "  </method>\n"
    " </interface>\n"
    "</node>\n";

static int
HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/*
 * Turn a file: URI (or an absolute path) into a local path in `out`.
 * Only local files are accepted: the host part must be empty,
 * "localhost" or this machine's name.
 */
static bool
UriToPath(const char *uri, char *out, size_t outSize)
{
    const char *p;

    if (uri[0] == '/') {
        p = uri;
    } else if (strncmp(uri, "file://", 7) == 0) {
        const char *host = uri + 7;
        const char *slash = strchr(host, '/');
        size_t hostLen;

        if (!slash)
            return false;
        hostLen = (size_t) (slash - host);
        if (hostLen > 0 &&
            !(hostLen == 9 && strncmp(host, "localhost", 9) == 0)) {
            char me[256];

            if (gethostname(me, sizeof me) != 0)
                return false;
            me[sizeof me - 1] = '\0';
            if (strlen(me) != hostLen || strncmp(host, me, hostLen) != 0)
                return false;
        }
        p = slash;
    } else {
        return false;
    }

    size_t n = 0;
    for (; *p && *p != '?' && *p != '#'; p++) {
        char c = *p;

        if (c == '%') {
            int hi = HexValue(p[1]), lo = hi < 0 ? -1 : HexValue(p[2]);

            if (lo < 0)
                return false;
            c = (char) (hi * 16 + lo);
            if (c == '\0')
                return false;
            p += 2;
        }
        if (n + 1 >= outSize)
            return false;
        out[n++] = c;
    }
    out[n] = '\0';

    /* Drop trailing slashes ("/a/b/" -> "/a/b"), but keep "/". */
    while (n > 1 && out[n - 1] == '/')
        out[--n] = '\0';
    return n > 0 && out[0] == '/';
}

/* Select `name` in the view, deselecting anything else there. */
static void
SelectInView(DialogData *dialog, const char *name)
{
    FileMgrData *fmd = (FileMgrData *) dialog->data;
    FileMgrRec *rec = (FileMgrRec *) fmd->file_mgr_rec;
    DirectorySet *dir = fmd->directory_set[0];

    for (int i = 0; i < dir->file_count; i++) {
        FileViewData *fv = dir->file_view_data[i];

        if (fv->filtered || strcmp(name, fv->file_data->file_name) != 0)
            continue;
        DeselectAllFiles(fmd);
        SelectFile(fmd, fv);
        ActivateSingleSelect(rec, fv->file_data->logical_type);
        PositionFileView(fv, fmd);
        return;
    }

    /* The folder is still being read, or the item is new (a download
     * that just finished): select it once the listing is updated. */
    XtFree(fmd->desktop_file);
    fmd->desktop_file = XtNewString(name);
}

/*
 * Raise the open view of `dir`, or open one.  GetNewView only reuses
 * a view when the user chose "open folders in a new window", and then
 * returns no view to select in, so look for one first.
 */
static DialogData *
OpenFolder(char *dir)
{
    (void) DtEliminateDots(dir);
    for (int i = 0; i < view_count; i++) {
        View *v = view_set[i];
        DialogData *dialog = (DialogData *) v->dialog_data;

        if (dialog && strcmp(v->directory_name, dir) == 0 &&
            strcmp(v->host_name, home_host_name) == 0) {
            FileMgrData *fmd = (FileMgrData *) dialog->data;

            /* Not the Trash, a toolbox or another restricted view */
            if (fmd->restricted_directory == NULL &&
                fmd != trashFileMgrData) {
                RaiseView(dialog);
                return dialog;
            }
        }
    }
    return ViewDirectoryProc(dir, False, NULL);
}

static void
ShowFolder(char *path)
{
    (void) OpenFolder(path);
}

static void
ShowItem(char *path)
{
    char *slash = strrchr(path, '/');   /* UriToPath: absolute, no trailing / */
    char dir[MAX_PATH];

    if (slash[1] == '\0') {             /* "/" itself */
        ShowFolder(path);
        return;
    }
    if (slash == path) {
        strcpy(dir, "/");
    } else {
        size_t len = (size_t) (slash - path);

        memcpy(dir, path, len);
        dir[len] = '\0';
    }

    DialogData *dialog = OpenFolder(dir);
    if (dialog)
        SelectInView(dialog, slash + 1);
}

static DBusHandlerResult
HandleShow(DBusConnection *bus, DBusMessage *msg, bool items)
{
    char **uris = NULL;
    int count = 0;
    const char *startupId = NULL;
    DBusError err;

    dbus_error_init(&err);
    if (!dbus_message_get_args(msg, &err,
                               DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &uris, &count,
                               DBUS_TYPE_STRING, &startupId,
                               DBUS_TYPE_INVALID)) {
        DBusMessage *reply = dbus_message_new_error(msg,
                                 DBUS_ERROR_INVALID_ARGS, err.message);
        dbus_error_free(&err);
        if (reply) {
            dbus_connection_send(bus, reply, NULL);
            dbus_message_unref(reply);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    /* Answer first: opening views can take a while, and the caller
     * only wants to know the request arrived. */
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (reply) {
        dbus_connection_send(bus, reply, NULL);
        dbus_message_unref(reply);
    }

    for (int i = 0; i < count && i < MAX_URIS; i++) {
        char path[MAX_PATH];

        if (!UriToPath(uris[i], path, sizeof path))
            continue;
        if (items)
            ShowItem(path);
        else
            ShowFolder(path);
    }
    dbus_free_string_array(uris);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
HandleMessage(DBusConnection *bus, DBusMessage *msg, void *data)
{
    (void) data;

    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable",
                                    "Introspect")) {
        const char *xml = introspectXml;
        DBusMessage *reply = dbus_message_new_method_return(msg);

        if (reply) {
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml,
                                     DBUS_TYPE_INVALID);
            dbus_connection_send(bus, reply, NULL);
            dbus_message_unref(reply);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, FM1_IFACE, "ShowFolders"))
        return HandleShow(bus, msg, false);
    if (dbus_message_is_method_call(msg, FM1_IFACE, "ShowItems") ||
        dbus_message_is_method_call(msg, FM1_IFACE, "ShowItemProperties"))
        return HandleShow(bus, msg, true);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void
DtfileDbusInit(Widget top)
{
    static const DBusObjectPathVTable vtable = { .message_function = HandleMessage };

    /* A restricted dtfile (a toolbox, -restricted) must not open
     * arbitrary folders for other programs. */
    if (restrictMode)
        return;

    DBusConnection *bus = _DtSvcDbusSessionBus(XtWidgetToApplicationContext(top));
    if (!bus)
        return;
    if (!dbus_connection_register_object_path(bus, FM1_PATH, &vtable, NULL))
        return;
    if (!_DtSvcDbusRequestName(bus, FM1_NAME))
        dbus_connection_unregister_object_path(bus, FM1_PATH);
}

#else /* !HAVE_DBUS */

void
DtfileDbusInit(Widget top)
{
    (void) top;
}

#endif /* HAVE_DBUS */
