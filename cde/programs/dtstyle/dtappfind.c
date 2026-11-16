/*
 * dtappfind - find and start an installed application by typing.
 *
 * A search field over the list of installed applications (XDG .desktop
 * entries, as other desktops list them).  Typing narrows the list by
 * name, generic name ("Web Browser") or keywords; Up and Down move in
 * it; Enter or a double-click starts the selected application (the
 * first one if none is selected) and closes the window; Escape closes
 * it.  Text that matches no application runs as a command (Alt+F2).  Started from the Workspace menu (Applications...) and the Front
 * Panel's Applications drawer.
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

#define MAX_APPS 2000

typedef struct {
    char *name, *generic, *keywords, *exec;
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
        apps[nApps].terminal = term;
        nApps++;
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

int
main(int argc, char **argv)
{
    XtAppContext app;
    Widget  form, label;
    Arg     a[12];
    int     n;
    Atom    del;
    XmString s;

    XtSetLanguageProc(NULL, NULL, NULL);
    top = XtVaAppInitialize(&app, "Dtappfind", NULL, 0, &argc, argv, NULL,
                            XmNtitle, "Applications",
                            XmNiconName, "Applications", NULL);
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

    Filter("");
    XtRealizeWidget(top);
    del = XmInternAtom(XtDisplay(top), "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(top, del, CloseCB, NULL);
    XmProcessTraversal(field, XmTRAVERSE_CURRENT);
    XtAppMainLoop(app);
    return 0;
}
