/*
 * dtappfind - find and start an installed application by typing.
 *
 * A search field over the list of installed applications (XDG .desktop
 * entries, as other desktops list them).  Typing narrows the list by
 * name, generic name ("Web Browser") or keywords; Up and Down move in
 * it; Enter or a double-click starts the selected application (the
 * first one if none is selected) and closes the window; Escape closes
 * it.  Text that matches no application runs as a command (Alt+F2).
 * Started from the Workspace menu (Applications...), the Front Panel's
 * Applications drawer, and the Windows key (dtsuperkey).
 *
 * CDE's own applications (the Application Manager's groups) are listed
 * too, by their action labels.  Any row can be dragged onto a Front
 * Panel drawer's "Install Icon" to add it there.
 *
 *   dtappfind -menu         the Windows-key menu: just above the Front
 *                           Panel, closed by clicking elsewhere or by
 *                           the Windows key again
 *   dtappfind -launch FILE  start the application of a .desktop file
 *                           (what a drawer icon made from one runs)
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <stdint.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/keysym.h>
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/TextF.h>
#include <Xm/List.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/Protocols.h>
#include <Xm/AtomMgr.h>
#include <X11/Xatom.h>
#include <Dt/Dt.h>
#include <Dt/Action.h>
#include <Dt/Dnd.h>

#define MAX_APPS 2000
#define CDE_INSTALLATION_TOP_BIN "/usr/dt/bin"

typedef struct {
    char *name, *generic, *keywords, *exec;
    char *path;			/* the .desktop file, or the App Manager file */
    char *action;		/* a CDE action (exec unused), or NULL */
    Boolean terminal;
    unsigned cats;		/* bit i: in categories[i] */
} App;

/* The menu's categories, from the freedesktop.org main categories (an
 * application lists them in Categories=). */
static const struct {
    const char *label, *xdg;	/* xdg: ";"-separated */
} categories[] = {
    { "All",          "" },
    { "Accessories",  "Utility;" },
    { "Development",  "Development;" },
    { "Education",    "Education;Science;" },
    { "Games",        "Game;" },
    { "Graphics",     "Graphics;" },
    { "Internet",     "Network;" },
    { "Multimedia",   "AudioVideo;Audio;Video;" },
    { "Office",       "Office;" },
    { "Settings",     "Settings;" },
    { "System",       "System;" },
};
#define NCATS ((int) (sizeof(categories) / sizeof(categories[0])))
static int curCat = 0;			/* 0: all */

static unsigned
CategoryBits(const char *list)
{
    unsigned bits = 0;
    char     want[64];
    int      i;
    const char *p, *e;

    for (p = list; *p; p = (*e ? e + 1 : e))
    {
        e = strchr(p, ';');
        if (!e)
            e = p + strlen(p);
        if (e - p > 0 && e - p < (int) sizeof(want) - 1)
        {
            snprintf(want, sizeof(want), "%.*s;", (int) (e - p), p);
            for (i = 1; i < NCATS; i++)
                if (strstr(categories[i].xdg, want) &&
                    (categories[i].xdg == strstr(categories[i].xdg, want) ||
                     strstr(categories[i].xdg, want)[-1] == ';'))
                    bits |= 1u << i;
        }
    }
    return bits;
}

static App     apps[MAX_APPS];
static int     nApps;
static int    *shown;		/* indexes into apps[] of the listed rows */
static int     nShown;
static Widget  top, field, list;

/* "Exec" without its field codes (%f, %U...); "%%" is a literal "%". */
static char *
ExecToCommand(const char *exec)
{
    char  *out = XtMalloc(strlen(exec) + 1);
    size_t n = 0;

    for (; *exec; exec++)
    {
        if (*exec == '%')
        {
            if (exec[1] == '%')
                out[n++] = '%';
            if (exec[1])
                exec++;
            continue;
        }
        if (*exec != '\n' && *exec != '\t')
            out[n++] = *exec;
    }
    while (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = '\0';
    return out;
}

/* Is "prog" (a TryExec value) runnable? */
static Boolean
HaveProgram(const char *prog)
{
    char  *path, *dir, *copy, buf[4096];
    Boolean ok = False;

    if (strchr(prog, '/'))
        return access(prog, X_OK) == 0;
    if ((path = getenv("PATH")) == NULL)
        path = "/usr/bin:/bin";
    copy = XtNewString(path);
    for (dir = strtok(copy, ":"); dir && !ok; dir = strtok(NULL, ":"))
    {
        snprintf(buf, sizeof(buf), "%s/%s", dir, prog);
        ok = (access(buf, X_OK) == 0);
    }
    XtFree(copy);
    return ok;
}

static void
ReadDir(const char *dir)
{
    DIR   *d = opendir(dir);
    struct dirent *e;
    char   path[4200], line[2048], *v;
    char   name[256], generic[256], keywords[512], exec[2048], tryexec[512];
    char   cats[512];
    FILE  *fp;
    size_t len;
    int    i;
    Boolean in, hide, app, term;

    if (!d)
        return;
    while ((e = readdir(d)) != NULL && nApps < MAX_APPS)
    {
        len = strlen(e->d_name);
        if (len < 9 || strcmp(e->d_name + len - 8, ".desktop") != 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if ((fp = fopen(path, "r")) == NULL)
            continue;
        name[0] = generic[0] = keywords[0] = exec[0] = tryexec[0] = '\0';
        cats[0] = '\0';
        in = hide = app = term = False;
        while (fgets(line, sizeof(line), fp))
        {
            line[strcspn(line, "\r\n")] = '\0';
            if (line[0] == '[')
            {
                in = (strcmp(line, "[Desktop Entry]") == 0);
                continue;
            }
            if (!in || !(v = strchr(line, '=')))
                continue;
            *v++ = '\0';
            if (strcmp(line, "Name") == 0)
                snprintf(name, sizeof(name), "%s", v);
            else if (strcmp(line, "GenericName") == 0)
                snprintf(generic, sizeof(generic), "%s", v);
            else if (strcmp(line, "Categories") == 0)
                snprintf(cats, sizeof(cats), "%s", v);
            else if (strcmp(line, "Keywords") == 0)
                snprintf(keywords, sizeof(keywords), "%s", v);
            else if (strcmp(line, "Exec") == 0)
                snprintf(exec, sizeof(exec), "%s", v);
            else if (strcmp(line, "TryExec") == 0)
                snprintf(tryexec, sizeof(tryexec), "%s", v);
            else if (strcmp(line, "Type") == 0)
                app = (strcmp(v, "Application") == 0);
            else if (strcmp(line, "Terminal") == 0)
                term = (strcmp(v, "true") == 0);
            else if ((strcmp(line, "NoDisplay") == 0 ||
                      strcmp(line, "Hidden") == 0) && strcmp(v, "true") == 0)
                hide = True;
        }
        fclose(fp);
        if (!app || hide || !name[0] || !exec[0] ||
            (tryexec[0] && !HaveProgram(tryexec)))
            continue;
        for (i = 0; i < nApps; i++)	/* the first of a name wins */
            if (strcmp(apps[i].name, name) == 0)
                break;
        if (i < nApps)
            continue;
        apps[nApps].name = XtNewString(name);
        apps[nApps].generic = XtNewString(generic);
        apps[nApps].keywords = XtNewString(keywords);
        apps[nApps].cats = CategoryBits(cats);
        apps[nApps].exec = ExecToCommand(exec);
        apps[nApps].path = XtNewString(path);
        apps[nApps].action = NULL;
        apps[nApps].terminal = term;
        nApps++;
    }
    closedir(d);
}

/* CDE's applications: the Application Manager's groups (folders of
 * files named after actions), listed by the actions' labels. */
static unsigned
GroupBits(const char *g)
{
    if (strstr(g, "Admin") || strstr(g, "System"))
        return 1u << 10;
    if (strstr(g, "Media"))
        return 1u << 7;
    if (strstr(g, "Game"))
        return 1u << 4;
    if (strstr(g, "Tool") || strstr(g, "Desktop_Apps"))
        return 1u << 1;
    return 0;
}

static void
ReadGroup(const char *gdir, const char *group)
{
    DIR    *d = opendir(gdir);
    struct dirent *e;
    char    path[4600], generic[256], *label;
    int     i;

    if (!d)
        return;
    while ((e = readdir(d)) != NULL && nApps < MAX_APPS)
    {
        if (e->d_name[0] == '.' || !DtActionExists(e->d_name))
            continue;
        label = DtActionLabel(e->d_name);
        if (!label || !*label)
        {
            XtFree(label);
            label = XtNewString(e->d_name);
        }
        for (i = 0; i < nApps; i++)	/* an XDG entry of the name wins */
            if (strcasecmp(apps[i].name, label) == 0)
                break;
        if (i < nApps)
        {
            XtFree(label);
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", gdir, e->d_name);
        snprintf(generic, sizeof(generic), "%s", group);
        for (i = 0; generic[i]; i++)
            if (generic[i] == '_')
                generic[i] = ' ';
        apps[nApps].name = label;
        apps[nApps].generic = XtNewString(generic);
        apps[nApps].keywords = XtNewString(e->d_name);
        apps[nApps].exec = XtNewString("");
        apps[nApps].path = XtNewString(path);
        apps[nApps].action = XtNewString(e->d_name);
        apps[nApps].terminal = False;
        apps[nApps].cats = GroupBits(group);
        nApps++;
    }
    closedir(d);
}

static void
ReadAppManager(void)
{
    char   dir[4200], gdir[4400];
    const char *s = getenv("DTUSERSESSION");
    DIR   *d;
    struct dirent *e;

    if (!s || !*s || strchr(s, '/'))
        return;
    snprintf(dir, sizeof(dir), CDE_LOGFILES_TOP "/appconfig/appmanager/%s", s);
    if ((d = opendir(dir)) == NULL)
        return;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        snprintf(gdir, sizeof(gdir), "%s/%s", dir, e->d_name);
        ReadGroup(gdir, e->d_name);
    }
    closedir(d);
}

static int
NameCmp(const void *a, const void *b)
{
    return strcasecmp(((const App *) a)->name, ((const App *) b)->name);
}

/* $XDG_DATA_HOME/applications, then each of $XDG_DATA_DIRS. */
static void
ReadApps(void)
{
    char  dir[4200], *dirs, *t, *c;

    c = getenv("XDG_DATA_HOME");
    if (c && *c)
        snprintf(dir, sizeof(dir), "%s/applications", c);
    else
        snprintf(dir, sizeof(dir), "%s/.local/share/applications",
                 getenv("HOME") ? getenv("HOME") : "");
    ReadDir(dir);
    c = getenv("XDG_DATA_DIRS");
    dirs = XtNewString((c && *c) ? c : "/usr/local/share:/usr/share");
    for (t = strtok(dirs, ":"); t; t = strtok(NULL, ":"))
    {
        snprintf(dir, sizeof(dir), "%s/applications", t);
        ReadDir(dir);
    }
    XtFree(dirs);
    ReadAppManager();
    qsort(apps, nApps, sizeof(App), NameCmp);
    shown = (int *) XtMalloc((nApps ? nApps : 1) * sizeof(int));
}

static Boolean
Contains(const char *hay, const char *want, size_t n)
{
    for (; *hay; hay++)
        if (strncasecmp(hay, want, n) == 0)
            return True;
    return False;
}

static void
Filter(const char *want)
{
    XmString *items;
    size_t    n;
    int       i, k = 0;

    int       pass;

    while (*want == ' ')
        want++;
    n = strlen(want);

    items = (XmString *) XtMalloc((nApps ? nApps : 1) * sizeof(XmString));
    /* best matches first: names that start with the text, then names
     * that contain it, then generic names and keywords */
    for (pass = 0; pass < 3; pass++)
        for (i = 0; i < nApps; i++)
        {
            Boolean starts = (strncasecmp(apps[i].name, want, n) == 0);
            Boolean inName = Contains(apps[i].name, want, n);
            Boolean hit;

            if (curCat > 0 && !(apps[i].cats & (1u << curCat)))
                continue;
            if (pass == 0)
                hit = (n == 0 || starts);
            else if (pass == 1)
                hit = (n > 0 && !starts && inName);
            else
                hit = (n > 0 && !inName &&
                       (Contains(apps[i].generic, want, n) ||
                        Contains(apps[i].keywords, want, n)));
            if (hit)
            {
                shown[k] = i;
                items[k++] = XmStringCreateLocalized(apps[i].name);
            }
        }
    nShown = k;
    XmListDeleteAllItems(list);
    XmListAddItemsUnselected(list, items, k, 1);
    for (i = 0; i < k; i++)
        XmStringFree(items[i]);
    XtFree((char *) items);
    if (k > 0)
        XmListSelectPos(list, 1, False);
}

/* Start row "pos" (1-based) and quit. */
static void
Launch(int pos)
{
    App *a;
    pid_t pid;

    if (pos < 1 || pos > nShown)
        return;
    a = &apps[shown[pos - 1]];
    signal(SIGCHLD, SIG_IGN);		/* nobody waits for it */
    if ((pid = fork()) == 0)
    {
        setsid();
        if (XtDisplay(top))
            close(ConnectionNumber(XtDisplay(top)));
        if (a->action)
        {
            execl(CDE_INSTALLATION_TOP_BIN "/dtaction", "dtaction", a->action,
                  (char *) NULL);
            execlp("dtaction", "dtaction", a->action, (char *) NULL);
            _exit(127);
        }
        if (a->terminal)
        {
            execl("/usr/dt/bin/dtterminal", "dtterminal", "-e", "/bin/sh",
                  "-c", a->exec, (char *) NULL);
            execl("/usr/dt/bin/dtterm", "dtterm", "-e", "/bin/sh", "-c",
                  a->exec, (char *) NULL);
        }
        execl("/bin/sh", "sh", "-c", a->exec, (char *) NULL);
        _exit(127);
    }
    exit(0);
}

static int
Selected(void)
{
    int *pos, n, p = 1;

    if (XmListGetSelectedPos(list, &pos, &n))
    {
        p = pos[0];
        XtFree((char *) pos);
    }
    return p;
}

static void
CategoryCB(Widget w, XtPointer client, XtPointer call)
{
    char *t;

    curCat = (int) (intptr_t) client;
    t = XmTextFieldGetString(field);
    Filter(t);
    XtFree(t);
    XmProcessTraversal(field, XmTRAVERSE_CURRENT);
}

static void
ChangedCB(Widget w, XtPointer client, XtPointer call)
{
    char *t = XmTextFieldGetString(w);

    Filter(t);
    XtFree(t);
}

/* No application matches: run the text as a command (like Alt+F2
 * "Run" boxes), in the background, and quit. */
static void
RunCommand(const char *cmd)
{
    pid_t pid;

    while (*cmd == ' ')
        cmd++;
    if (!*cmd)
        return;
    signal(SIGCHLD, SIG_IGN);
    if ((pid = fork()) == 0)
    {
        setsid();
        close(ConnectionNumber(XtDisplay(top)));
        execl("/bin/sh", "sh", "-c", cmd, (char *) NULL);
        _exit(127);
    }
    exit(0);
}

static void
ActivateCB(Widget w, XtPointer client, XtPointer call)
{
    if (nShown == 0)
    {
        char *t = XmTextFieldGetString(w);

        RunCommand(t);
        XtFree(t);
        return;
    }
    Launch(Selected());
}

static void
DefaultActionCB(Widget w, XtPointer client, XtPointer call)
{
    Launch(((XmListCallbackStruct *) call)->item_position);
}

/* Up, Down, Page keys in the field move the selection in the list;
 * Escape closes. */
static void
KeyEH(Widget w, XtPointer client, XEvent *ev, Boolean *cont)
{
    KeySym ks = XLookupKeysym(&ev->xkey, 0);
    int    p, top_item, visible;

    if (ks == XK_Escape)
        exit(0);
    if (nShown == 0 || (ks != XK_Up && ks != XK_Down &&
                        ks != XK_Page_Up && ks != XK_Page_Down))
        return;
    *cont = False;
    XtVaGetValues(list, XmNtopItemPosition, &top_item,
                  XmNvisibleItemCount, &visible, NULL);
    p = Selected();
    if (ks == XK_Up) p--;
    else if (ks == XK_Down) p++;
    else if (ks == XK_Page_Up) p -= visible;
    else p += visible;
    if (p < 1) p = 1;
    if (p > nShown) p = nShown;
    XmListSelectPos(list, p, False);
    if (p < top_item)
        XmListSetPos(list, p);
    else if (p >= top_item + visible)
        XmListSetBottomPos(list, p);
}

static void
CloseCB(Widget w, XtPointer client, XtPointer call)
{
    exit(0);
}

/* -launch FILE: start the application of a .desktop file. */
static int
LaunchFile(const char *file)
{
    FILE   *fp = fopen(file, "r");
    char    line[4096], exec[4096], *v, *cmd;
    Boolean in = False, term = False;

    if (!fp)
        return 1;
    exec[0] = '\0';
    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '[')
        {
            in = (strcmp(line, "[Desktop Entry]") == 0);
            continue;
        }
        if (!in || !(v = strchr(line, '=')))
            continue;
        *v++ = '\0';
        if (strcmp(line, "Exec") == 0)
            snprintf(exec, sizeof(exec), "%s", v);
        else if (strcmp(line, "Terminal") == 0)
            term = (strcmp(v, "true") == 0);
    }
    fclose(fp);
    if (!exec[0])
        return 1;
    cmd = ExecToCommand(exec);
    signal(SIGCHLD, SIG_IGN);
    if (fork() == 0)
    {
        setsid();
        if (term)
        {
            execl(CDE_INSTALLATION_TOP_BIN "/dtterminal", "dtterminal", "-e",
                  "/bin/sh", "-c", cmd, (char *) NULL);
            execl(CDE_INSTALLATION_TOP_BIN "/dtterm", "dtterm", "-e",
                  "/bin/sh", "-c", cmd, (char *) NULL);
        }
        execl("/bin/sh", "sh", "-c", cmd, (char *) NULL);
        _exit(127);
    }
    return 0;
}

/* --- one window: the Windows key opens it, and closes it again --- */

static Atom selAtom, cmdAtom;

static void
CommandEH(Widget w, XtPointer client, XEvent *ev, Boolean *cont)
{
    Atom           type;
    int            fmt;
    unsigned long  n, after;
    unsigned char *data = NULL;

    if (ev->type != PropertyNotify || ev->xproperty.atom != cmdAtom ||
        ev->xproperty.state != PropertyNewValue)
        return;
    if (XGetWindowProperty(XtDisplay(w), XtWindow(w), cmdAtom, 0, 16, True,
                           XA_STRING, &type, &fmt, &n, &after, &data)
        == Success && data)
    {
        if (strcmp((char *) data, "close") == 0)
            exit(0);
        /* "raise": come to the front, search field ready */
        XMapRaised(XtDisplay(w), XtWindow(w));
        XmProcessTraversal(field, XmTRAVERSE_CURRENT);
        XFree(data);
    }
}

/* Another one runs: ask it to close (the menu) or come forward. */
static Boolean
HandOff(Display *dpy, Boolean menu)
{
    Window other = XGetSelectionOwner(dpy, selAtom);
    const char *what = menu ? "close" : "raise";

    if (other == None)
        return False;
    XChangeProperty(dpy, other, cmdAtom, XA_STRING, 8, PropModeReplace,
                    (unsigned char *) what, strlen(what) + 1);
    XFlush(dpy);
    return True;
}

/* The menu closes when another window gets the focus (not while its
 * own category menu is pulled down: that is a grab). */
static void
FocusEH(Widget w, XtPointer client, XEvent *ev, Boolean *cont)
{
    if (ev->type == FocusOut && ev->xfocus.mode == NotifyNormal &&
        ev->xfocus.detail != NotifyInferior &&
        ev->xfocus.detail != NotifyPointer)
        exit(0);
}

/* --- dragging a row onto a Front Panel drawer --- */

static int     dragRow;			/* 1-based row being dragged */
static int     pressX, pressY;
static Boolean pressed;

static void
ConvertCB(Widget w, XtPointer client, XtPointer call)
{
    DtDndConvertCallbackStruct *c = (DtDndConvertCallbackStruct *) call;

    if (c->reason == DtCR_DND_CONVERT_DATA && dragRow >= 1 &&
        dragRow <= nShown)
    {
        c->dragData->data.files[0] =
            XtNewString(apps[shown[dragRow - 1]].path);
        c->status = DtDND_SUCCESS;
    }
    else if (c->reason == DtCR_DND_CONVERT_DATA)
        c->status = DtDND_FAILURE;
}

static void
DragFinishCB(Widget w, XtPointer client, XtPointer call)
{
    DtDndDragFinishCallbackStruct *f = (DtDndDragFinishCallbackStruct *) call;

    if (f->dragData && f->dragData->numItems > 0 && f->dragData->data.files)
        XtFree(f->dragData->data.files[0]);
}

static void
DragEH(Widget w, XtPointer client, XEvent *ev, Boolean *cont)
{
    static XtCallbackRec convertRec[] = { { ConvertCB, NULL }, { NULL, NULL } };
    static XtCallbackRec finishRec[] = { { DragFinishCB, NULL }, { NULL, NULL } };

    if (ev->type == ButtonPress && ev->xbutton.button == Button1)
    {
        pressed = True;
        pressX = ev->xbutton.x;
        pressY = ev->xbutton.y;
    }
    else if (ev->type == ButtonRelease)
        pressed = False;
    else if (ev->type == MotionNotify && pressed &&
             (abs(ev->xmotion.x - pressX) > 8 ||
              abs(ev->xmotion.y - pressY) > 8))
    {
        pressed = False;
        dragRow = XmListYToPos(list, pressY);
        if (dragRow < 1 || dragRow > nShown)
            return;
        (void) DtDndVaDragStart(list, ev, DtDND_FILENAME_TRANSFER, 1,
                                XmDROP_COPY, convertRec, finishRec, NULL);
    }
}

int
main(int argc, char **argv)
{
    XtAppContext app;
    Widget  form, label;
    Arg     a[12];
    int     n;
    Atom    del;
    XmString s;

    Boolean menu = False;

    if (argc == 3 && strcmp(argv[1], "-launch") == 0)
        return LaunchFile(argv[2]);
    if (argc == 2 && strcmp(argv[1], "-menu") == 0)
    {
        menu = True;
        argc = 1;
    }

    XtSetLanguageProc(NULL, NULL, NULL);
    top = XtVaAppInitialize(&app, "Dtappfind", NULL, 0, &argc, argv, NULL,
                            XmNtitle, "Applications",
                            XmNiconName, "Applications", NULL);
    {
        char name[64];

        snprintf(name, sizeof(name), "_CLANKER_APPFIND_S%d",
                 DefaultScreen(XtDisplay(top)));
        selAtom = XInternAtom(XtDisplay(top), name, False);
        cmdAtom = XInternAtom(XtDisplay(top), "_CLANKER_APPFIND_CMD", False);
        if (HandOff(XtDisplay(top), menu))
            return 0;
    }
    if (DtInitialize(XtDisplay(top), top, "dtappfind", "Dtappfind"))
        DtDbLoad();
    ReadApps();

    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, top,
                                   XmNmarginWidth, 8, XmNmarginHeight, 8,
                                   NULL);
    s = XmStringCreateLocalized("Type to find an application, or a command to run:");
    label = XtVaCreateManagedWidget("label", xmLabelWidgetClass, form,
                                    XmNlabelString, s,
                                    XmNtopAttachment, XmATTACH_FORM,
                                    XmNleftAttachment, XmATTACH_FORM, NULL);
    XmStringFree(s);
    {
        Widget pull, opt, b;
        int    i;

        pull = XmCreatePulldownMenu(form, "categoryMenu", NULL, 0);
        for (i = 0; i < NCATS; i++)
        {
            s = XmStringCreateLocalized((char *) categories[i].label);
            b = XtVaCreateManagedWidget("category", xmPushButtonWidgetClass,
                                        pull, XmNlabelString, s, NULL);
            XmStringFree(s);
            XtAddCallback(b, XmNactivateCallback, CategoryCB,
                          (XtPointer) (intptr_t) i);
        }
        s = XmStringCreateLocalized("Category:");
        n = 0;
        XtSetArg(a[n], XmNsubMenuId, pull); n++;
        XtSetArg(a[n], XmNlabelString, s); n++;
        XtSetArg(a[n], XmNtopAttachment, XmATTACH_FORM); n++;
        XtSetArg(a[n], XmNleftAttachment, XmATTACH_FORM); n++;
        opt = XmCreateOptionMenu(form, "category", a, n);
        XmStringFree(s);
        XtManageChild(opt);
        /* the category row on top, then the label, then the field */
        XtVaSetValues(label, XmNtopAttachment, XmATTACH_WIDGET,
                      XmNtopWidget, opt, XmNtopOffset, 6, NULL);
    }
    field = XtVaCreateManagedWidget("search", xmTextFieldWidgetClass, form,
                                    XmNcolumns, 34,
                                    XmNtopAttachment, XmATTACH_WIDGET,
                                    XmNtopWidget, label, XmNtopOffset, 4,
                                    XmNleftAttachment, XmATTACH_FORM,
                                    XmNrightAttachment, XmATTACH_FORM, NULL);
    n = 0;
    XtSetArg(a[n], XmNvisibleItemCount, 14); n++;
    XtSetArg(a[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(a[n], XmNscrollBarDisplayPolicy, XmSTATIC); n++;
    XtSetArg(a[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(a[n], XmNtopWidget, field); n++;
    XtSetArg(a[n], XmNtopOffset, 6); n++;
    XtSetArg(a[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    list = XmCreateScrolledList(form, "apps", a, n);
    XtManageChild(list);

    XtAddCallback(field, XmNvalueChangedCallback, ChangedCB, NULL);
    XtAddCallback(field, XmNactivateCallback, ActivateCB, NULL);
    XtAddCallback(list, XmNdefaultActionCallback, DefaultActionCB, NULL);
    XtAddEventHandler(field, KeyPressMask, False, KeyEH, NULL);
    XtAddEventHandler(list, KeyPressMask, False, KeyEH, NULL);
    XtAddEventHandler(list, ButtonPressMask | ButtonReleaseMask |
                      Button1MotionMask, False, DragEH, NULL);

    Filter("");
    if (menu)
        XtSetMappedWhenManaged(top, False);
    XtRealizeWidget(top);
    XSetSelectionOwner(XtDisplay(top), selAtom, XtWindow(top), CurrentTime);
    XtAddEventHandler(top, PropertyChangeMask, False, CommandEH, NULL);
    del = XmInternAtom(XtDisplay(top), "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(top, del, CloseCB, NULL);
    if (menu)
    {
        /* Just above the Front Panel, centered: where a start menu is. */
        Dimension w, h;
        Display  *dpy = XtDisplay(top);
        int       sw = DisplayWidth(dpy, DefaultScreen(dpy));
        int       sh = DisplayHeight(dpy, DefaultScreen(dpy));
        int       x, y;

        XtVaGetValues(top, XmNwidth, &w, XmNheight, &h, NULL);
        x = (sw - (int) w) / 2;
        y = sh - (int) h - 110;
        if (y < 0)
            y = 0;
        XtVaSetValues(top, XmNx, x, XmNy, y, NULL);
        XtMapWidget(top);
        XtAddEventHandler(top, FocusChangeMask, False, FocusEH, NULL);
    }
    XmProcessTraversal(field, XmTRAVERSE_CURRENT);
    XtAppMainLoop(app);
    return 0;
}
