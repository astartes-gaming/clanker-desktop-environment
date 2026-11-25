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
 * The entries and categories are those of other desktops' menus: the
 * .desktop files of $XDG_DATA_HOME and $XDG_DATA_DIRS (CDE's own
 * applications have theirs in /usr/dt/share/applications), honoring
 * NoDisplay, Hidden, TryExec and OnlyShowIn/NotShowIn for
 * $XDG_CURRENT_DESKTOP, in the freedesktop.org main categories, listed
 * beside the applications.  Typing searches all of them.  Any row can be
 * dragged onto a Front Panel drawer's "Install Icon" to add it there.
 *
 *   dtappfind -menu         the Windows-key menu: just above the Front
 *                           Panel, closed by clicking elsewhere or by
 *                           the Windows key again
 *   dtappfind -launch FILE  start the application of a .desktop file
 *                           (what a drawer icon made from one runs)
 *   dtappfind -pick         choose an application; its .desktop file is
 *                           printed (a drawer's Install Icon adds it)
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
#include <limits.h>
#include <sys/stat.h>

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
#include <Xm/MessageB.h>
#include <Xm/AtomMgr.h>
#include <X11/Xatom.h>
#include <Dt/Dt.h>
#include <Dt/Action.h>
#include <Dt/Dnd.h>
#include <Dt/DtXinerama.h>
#include "applist.h"
#include "search.h"

#define MAX_APPS 2000
#define CDE_INSTALLATION_TOP_BIN "/usr/dt/bin"

typedef struct {
    char *name, *generic, *keywords, *exec;
    char *label;		/* what the list shows (name, or "name (prog)") */
    char *icon;			/* Icon= (theme name or path), or "" */
    char *path;			/* the .desktop file */
    char *id;			/* its desktop file ID (file name) */
    char *action;		/* unused (NULL) */
    Boolean terminal;
    unsigned cats;		/* bit i: in categories[i] */
} App;

/* The menu's categories: those of other desktops' application menus
 * (the freedesktop.org main categories, as the default applications.menu
 * of GNOME, Xfce, MATE and LXDE files them).  An application is in every
 * category its Categories= names; one naming none of these is in Other. */
static const struct {
    const char *label, *xdg;	/* xdg: ";"-separated */
} categories[] = {
    { "All Applications", "" },
    { "Accessories",  "Utility;Accessibility;" },
    { "Development",  "Development;" },
    { "Education",    "Education;Science;" },
    { "Games",        "Game;" },
    { "Graphics",     "Graphics;" },
    { "Internet",     "Network;" },
    { "Multimedia",   "AudioVideo;Audio;Video;" },
    { "Office",       "Office;" },
    { "Settings",     "Settings;" },
    { "System",       "System;Emulator;" },
    { "Other",        "" },
};
#define NCATS ((int) (sizeof(categories) / sizeof(categories[0])))
#define CAT_OTHER (NCATS - 1)
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
            for (i = 1; i < CAT_OTHER; i++)
                if (strstr(categories[i].xdg, want) &&
                    (categories[i].xdg == strstr(categories[i].xdg, want) ||
                     strstr(categories[i].xdg, want)[-1] == ';'))
                    bits |= 1u << i;
        }
    }
    if (bits == 0)
        bits = 1u << CAT_OTHER;
    return bits;
}

static App     apps[MAX_APPS];
static int     nApps;
static int    *shown;		/* indexes into apps[] of the listed rows */
static int     nShown;
static Widget  top, field;
static AppList *appList, *catList;

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
    char  *path, *dir, *copy, *save = NULL, buf[4096];
    Boolean ok = False;

    if (strchr(prog, '/'))
        return access(prog, X_OK) == 0;
    if ((path = getenv("PATH")) == NULL)
        path = "/usr/bin:/bin";
    copy = XtNewString(path);
    /* strtok_r: callers walk their own lists with strtok */
    for (dir = strtok_r(copy, ":", &save); dir && !ok;
         dir = strtok_r(NULL, ":", &save))
    {
        snprintf(buf, sizeof(buf), "%s/%s", dir, prog);
        ok = (access(buf, X_OK) == 0);
    }
    XtFree(copy);
    return ok;
}

/* Is the program an Exec= value starts runnable?  (An entry without
 * TryExec whose program was uninstalled stays out of the menu.)  The
 * first word, skipping "env" and its VAR=value and -options; a
 * quoted first word may hold spaces. */
static Boolean
ExecRunnable(const char *exec)
{
    char        word[1024];
    const char *p = exec;
    size_t      n;
    Boolean     afterEnv = False;

    for (;;)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            return True;		/* nothing to check */
        if (*p == '"')
        {
            const char *q = strchr(p + 1, '"');

            n = q ? (size_t) (q - p - 1) : strlen(p + 1);
            snprintf(word, sizeof(word), "%.*s", (int) n, p + 1);
            p = q ? q + 1 : p + strlen(p);
        }
        else
        {
            n = strcspn(p, " \t");
            snprintf(word, sizeof(word), "%.*s", (int) n, p);
            p += n;
        }
        if (strcmp(word, "env") == 0)
        {
            afterEnv = True;
            continue;
        }
        if (afterEnv && (word[0] == '-' ||
                         (strchr(word, '=') && word[0] != '/')))
            continue;
        if (!word[0] || strchr(word, '%'))
            return True;
        return HaveProgram(word);
    }
}

/* Desktop file IDs already seen (earlier folders win). */
static char *ids[MAX_APPS * 2];
static int   nIds;

/* OnlyShowIn / NotShowIn against $XDG_CURRENT_DESKTOP ("CDE"; a
 * ":"-separated list). */
static Boolean
InList(const char *list, const char *desktops)
{
    char  copy[512], *d, *save = NULL, want[80];

    snprintf(copy, sizeof(copy), "%s", desktops);
    for (d = strtok_r(copy, ":", &save); d; d = strtok_r(NULL, ":", &save))
    {
        size_t n;

        snprintf(want, sizeof(want), "%s;", d);
        n = strlen(want);
        if (strncmp(list, want, n) == 0)
            return True;
        {
            const char *p = list;

            while ((p = strstr(p, want)) != NULL)
            {
                if (p > list && p[-1] == ';')
                    return True;
                p++;
            }
        }
        /* a last item without the ending ";" */
        n--;
        if (strlen(list) >= n && strcmp(list + strlen(list) - n, d) == 0 &&
            (strlen(list) == n || list[strlen(list) - n - 1] == ';'))
            return True;
    }
    return False;
}

static Boolean
ShownHere(const char *only, const char *notin)
{
    const char *cur = getenv("XDG_CURRENT_DESKTOP");

    if (!cur || !*cur)
        cur = "CDE";
    if (only[0] && !InList(only, cur))
        return False;
    if (notin[0] && InList(notin, cur))
        return False;
    return True;
}

static void
ReadDir(const char *dir)
{
    DIR   *d = opendir(dir);
    struct dirent *e;
    char   path[4200], line[2048], *v;
    char   name[256], generic[256], keywords[512], exec[2048], tryexec[512];
    char   cats[512], only[512], notin[512], icon[512];
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
        cats[0] = only[0] = notin[0] = icon[0] = '\0';
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
            else if (strcmp(line, "Icon") == 0)
                snprintf(icon, sizeof(icon), "%s", v);
            else if (strcmp(line, "OnlyShowIn") == 0)
                snprintf(only, sizeof(only), "%s", v);
            else if (strcmp(line, "NotShowIn") == 0)
                snprintf(notin, sizeof(notin), "%s", v);
            else if (strcmp(line, "Type") == 0)
                app = (strcmp(v, "Application") == 0);
            else if (strcmp(line, "Terminal") == 0)
                term = (strcmp(v, "true") == 0);
            else if ((strcmp(line, "NoDisplay") == 0 ||
                      strcmp(line, "Hidden") == 0) && strcmp(v, "true") == 0)
                hide = True;
        }
        fclose(fp);
        /* As the freedesktop.org specs say: an ID (file name) listed
         * by an earlier folder hides this one, even when that one is
         * hidden; then the entry's own rules. */
        for (i = 0; i < nIds; i++)
            if (strcmp(ids[i], e->d_name) == 0)
                break;
        if (i < nIds)
            continue;
        if (nIds < MAX_APPS * 2)
            ids[nIds++] = XtNewString(e->d_name);
        if (!app || hide || !name[0] || !exec[0] ||
            !ShownHere(only, notin) ||
            (tryexec[0] ? !HaveProgram(tryexec) : !ExecRunnable(exec)))
            continue;
        apps[nApps].name = XtNewString(name);
        apps[nApps].generic = XtNewString(generic);
        apps[nApps].keywords = XtNewString(keywords);
        apps[nApps].cats = CategoryBits(cats);
        apps[nApps].exec = ExecToCommand(exec);
        apps[nApps].path = XtNewString(path);
        apps[nApps].icon = XtNewString(icon);
        apps[nApps].id = ids[nIds - 1];
        apps[nApps].action = NULL;
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

/* Call fn for $XDG_DATA_HOME/applications, then for the applications
 * folder of each of $XDG_DATA_DIRS. */
static void
ForEachAppDir(void (*fn)(const char *dir))
{
    char  dir[4200], *dirs, *t, *c, *save = NULL;

    c = getenv("XDG_DATA_HOME");
    if (c && *c)
        snprintf(dir, sizeof(dir), "%s/applications", c);
    else
        snprintf(dir, sizeof(dir), "%s/.local/share/applications",
                 getenv("HOME") ? getenv("HOME") : "");
    fn(dir);
    c = getenv("XDG_DATA_DIRS");
    dirs = XtNewString((c && *c) ? c : "/usr/local/share:/usr/share");
    for (t = strtok_r(dirs, ":", &save); t; t = strtok_r(NULL, ":", &save))
    {
        snprintf(dir, sizeof(dir), "%s/applications", t);
        fn(dir);
    }
    XtFree(dirs);
}

/* A value that changes when a .desktop file is added to, removed from
 * or renamed in an application folder (the folder's change time), or
 * a folder appears or goes: installing and removing programs.  Plain
 * stat(), so it works on every system (no inotify needed). */
static unsigned long dirsStamp;

static void
StampDir(const char *dir)
{
    struct stat st;

    dirsStamp *= 31;
    if (stat(dir, &st) == 0)
        dirsStamp += (unsigned long) st.st_mtime * 7 +
                     (unsigned long) st.st_ctime * 3 +
                     (unsigned long) st.st_ino + 1;
}

static unsigned long
AppDirsStamp(void)
{
    dirsStamp = 0;
    ForEachAppDir(StampDir);
    return dirsStamp;
}

static void
FreeApps(void)
{
    int i;

    for (i = 0; i < nApps; i++)
    {
        if (apps[i].label != apps[i].name)
            XtFree(apps[i].label);
        XtFree(apps[i].name);
        XtFree(apps[i].generic);
        XtFree(apps[i].keywords);
        XtFree(apps[i].exec);
        XtFree(apps[i].path);
        XtFree(apps[i].icon);
    }
    nApps = 0;
    for (i = 0; i < nIds; i++)
        XtFree(ids[i]);
    nIds = 0;
    XtFree((char *) shown);
    shown = NULL;
    nShown = 0;
}

static void
ReadApps(void)
{
    ForEachAppDir(ReadDir);
    qsort(apps, nApps, sizeof(App), NameCmp);
    /* Two applications of one name (CDE's and another desktop's
     * Calculator): show their programs too. */
    {
        int i;

        for (i = 0; i < nApps; i++)
        {
            Boolean twin = (i > 0 && strcasecmp(apps[i].name,
                                                apps[i - 1].name) == 0) ||
                           (i + 1 < nApps && strcasecmp(apps[i].name,
                                                        apps[i + 1].name) == 0);
            char    prog[256], *b;

            apps[i].label = apps[i].name;
            if (!twin)
                continue;
            snprintf(prog, sizeof(prog), "%s", apps[i].exec);
            prog[strcspn(prog, " \t")] = '\0';
            b = strrchr(prog, '/') ? strrchr(prog, '/') + 1 : prog;
            if (!*b)
                continue;
            apps[i].label = XtMalloc(strlen(apps[i].name) + strlen(b) + 4);
            sprintf(apps[i].label, "%s (%s)", apps[i].name, b);
        }
    }
    shown = (int *) XtMalloc((nApps ? nApps : 1) * sizeof(int));
}

static Boolean searchMode;
static int RowCount(void);

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
    size_t    n;
    int       i, k = 0;

    int       pass;

    while (*want == ' ')
        want++;
    n = strlen(want);

    /* best matches first: names that start with the text, then names
     * that contain it, then generic names and keywords */
    for (pass = 0; pass < 3; pass++)
        for (i = 0; i < nApps; i++)
        {
            Boolean starts = (strncasecmp(apps[i].name, want, n) == 0);
            Boolean inName = Contains(apps[i].name, want, n);
            Boolean hit;

            /* typing searches every category, as other menus do */
            if (curCat > 0 && n == 0 && !(apps[i].cats & (1u << curCat)))
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
                shown[k++] = i;
        }
    nShown = k;
    if (searchMode)
    {
        /* an empty search field shows nothing, as other runners do */
        if (n == 0)
            nShown = 0;
        SearchQuery(want);
    }
    AppListSetCount(appList, RowCount());
}

/* -pick: choosing an application prints its .desktop file instead of
 * starting it (a Front Panel drawer's Install Icon adds it). */
static Boolean pick = False;

/* -search: the Front Panel's Search button.  The rows are the search
 * sources' (search.c) around the applications: a calculator result,
 * then applications, then Settings pages, windows, files, the web. */
static Boolean searchMode = False;

static int
AppRows(void)
{
    if (!searchMode)
        return nShown;
    if (!searchConfig.apps)
        return 0;
    return nShown < searchConfig.maxApps ? nShown : searchConfig.maxApps;
}

static int
RowCount(void)
{
    if (!searchMode)
        return nShown;
    return SearchBefore() + AppRows() + SearchAfter();
}

/* The application of a row (0-based), or -1 (another kind of row). */
static int
RowApp(int row)
{
    if (searchMode)
        row -= SearchBefore();
    return (row >= 0 && row < AppRows()) ? shown[row] : -1;
}

/* The search hit of a row, or NULL. */
static const Hit *
RowHit(int row)
{
    if (!searchMode)
        return NULL;
    if (row < SearchBefore())
        return SearchHitBefore(row);
    return SearchHitAfter(row - SearchBefore() - AppRows());
}

/* A game (Categories=Game), with Settings > Session > Games set to
 * gamescope (~/.dt/gamescope).  Steam entries run the Steam client,
 * which dtgamescope leaves alone. */
static Boolean
RunInGamescope(const App *a)
{
    char path[1100];
    const char *home = getenv("HOME");
    int i;

    if (!home || !a->exec)
        return False;
    for (i = 0; i < NCATS; i++)
        if (strcmp(categories[i].xdg, "Game;") == 0)
            break;
    if (i == NCATS || !(a->cats & (1u << i)))
        return False;
    snprintf(path, sizeof(path), "%s/.dt/gamescope", home);
    return access(path, F_OK) == 0;
}

/* Start row "pos" (1-based) and quit. */
static void
Launch(int pos)
{
    App *a;
    pid_t pid;
    int   ai;

    if (pos < 1 || pos > RowCount())
        return;
    if ((ai = RowApp(pos - 1)) < 0)
    {
        if (SearchActivate(RowHit(pos - 1), field))
            exit(0);
        return;
    }
    a = &apps[ai];
    if (pick)
    {
        printf("%s\n", a->path);
        fflush(stdout);
        exit(0);
    }
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
        if (RunInGamescope(a))
        {
            /* Settings > Session > Games: dtgamescope runs it in
             * gamescope at the monitor's size (or just runs it) */
            execl(CDE_INSTALLATION_TOP_BIN "/dtgamescope", "dtgamescope",
                  "/bin/sh", "-c", a->exec, (char *) NULL);
            execlp("dtgamescope", "dtgamescope", "/bin/sh", "-c", a->exec,
                   (char *) NULL);
        }
        execl("/bin/sh", "sh", "-c", a->exec, (char *) NULL);
        _exit(127);
    }
    exit(0);
}

static int
Selected(void)
{
    return AppListSelected(appList) + 1;	/* 1-based; 0: none */
}

static void
CategoryCB(int row)
{
    char *t;

    curCat = row;
    t = XmTextFieldGetString(field);
    Filter(t);
    XtFree(t);
    /* typing keeps going to the search field */
    XmProcessTraversal(field, XmTRAVERSE_CURRENT);
}

static void
ChangedCB(Widget w, XtPointer client, XtPointer call)
{
    char *t = XmTextFieldGetString(w);

    Filter(t);
    XtFree(t);
}

/* Programs installed or removed while the menu or window is open: the
 * list follows, as other desktops' menus do. */
#define WATCH_MS 2000
static XtAppContext  watchApp;
static unsigned long appsStamp;

static void
WatchAppsCB(XtPointer client, XtIntervalId *id)
{
    unsigned long now = AppDirsStamp();

    if (now != appsStamp)
    {
        char *t;

        appsStamp = now;
        FreeApps();
        ReadApps();
        AppListForgetIcons(appList);
        t = XmTextFieldGetString(field);
        Filter(t);
        XtFree(t);
    }
    XtAppAddTimeOut(watchApp, WATCH_MS, WatchAppsCB, NULL);
}

/* appsStamp is that of the list read (taken before reading it, so a
 * change during startup shows at the first check). */
static void
WatchApps(XtAppContext app)
{
    watchApp = app;
    XtAppAddTimeOut(app, WATCH_MS, WatchAppsCB, NULL);
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
    if (RowCount() == 0)
    {
        char *t = XmTextFieldGetString(w);

        if (!pick)			/* picking runs no commands */
            RunCommand(t);
        XtFree(t);
        return;
    }
    Launch(Selected());
}

static void
ActivateRow(int row)
{
    Launch(row + 1);
}

/* What the two lists show. */
static const char *
AppText(int row)
{
    int        ai = RowApp(row);
    const Hit *h;

    if (ai >= 0)
        return apps[ai].label;
    h = RowHit(row);
    return h ? h->label : "";
}

static const char *
AppIcon(int row)
{
    int         ai = RowApp(row);
    const Hit  *h;
    const char *i;

    if (ai < 0)
    {
        h = RowHit(row);
        return h ? h->icon : NULL;
    }
    i = apps[ai].icon;
    return (i && *i) ? i : "application-x-executable";
}

static const char *
CatText(int row)
{
    return categories[row].label;
}

/* The icon names other desktops use for these menu categories. */
static const char *
CatIcon(int row)
{
    /* the theme icon, then a CDE icon for systems without themes */
    static const char *icons[] = {
        "applications-other|Fpapps", "applications-accessories|Dtcalc",
        "applications-development|DtBldr", "applications-science|Dtinfo",
        "applications-games|DtGamesCards", "applications-graphics|Dtpaint",
        "applications-internet|DtMail",
        "applications-multimedia|Dtapplications-multimedia",
        "applications-office|Dtpenpd", "preferences-desktop|Fpstyle",
        "applications-system|DtSystem", "applications-other|Fpapps"
    };

    return (row >= 0 && row < (int) (sizeof(icons) / sizeof(icons[0]))) ?
        icons[row] : NULL;
}

/* Up, Down, Page keys in the field move the selection in the list;
 * Escape closes. */
static void
KeyEH(Widget w, XtPointer client, XEvent *ev, Boolean *cont)
{
    KeySym ks = XLookupKeysym(&ev->xkey, 0);
    int    p, visible;

    if (ks == XK_Escape)
        exit(0);
    /* Left and Right in an empty search field: the previous or next
     * category (the field has nothing to move through then) */
    if ((ks == XK_Left || ks == XK_Right) && w == field && !searchMode)
    {
        char *t = XmTextFieldGetString(field);
        Boolean empty = !t || !*t;

        XtFree(t);
        if (empty)
        {
            int c = curCat + (ks == XK_Right ? 1 : -1);

            if (c >= 0 && c < NCATS)
            {
                curCat = c;
                AppListSelect(catList, c);
                Filter("");
            }
            *cont = False;
            return;
        }
    }
    if (RowCount() == 0 || (ks != XK_Up && ks != XK_Down &&
                        ks != XK_Page_Up && ks != XK_Page_Down))
        return;
    *cont = False;
    visible = AppListVisibleRows(appList);
    p = Selected();
    if (ks == XK_Up) p--;
    else if (ks == XK_Down) p++;
    else if (ks == XK_Page_Up) p -= visible;
    else p += visible;
    if (p < 1) p = 1;
    if (p > RowCount()) p = RowCount();
    AppListSelect(appList, p - 1);
}

static void
CloseCB(Widget w, XtPointer client, XtPointer call)
{
    exit(0);
}

/*
 * A .desktop file can run any command, so like other desktops only
 * trusted ones start: installed applications (under an XDG
 * "applications" folder) or files their owner (you, or root) marked
 * executable.  A downloaded one does not run by a double-click.
 */
static Boolean
UnderDir(const char *real, const char *dir)
{
    char   rdir[PATH_MAX];
    size_t n;

    if (!dir || !*dir || !realpath(dir, rdir))
        return False;
    n = strlen(rdir);
    return strncmp(real, rdir, n) == 0 && real[n] == '/';
}

static Boolean
Trusted(const char *file)
{
    char        real[PATH_MAX], dir[PATH_MAX];
    const char *v;
    struct stat st;

    if (!realpath(file, real) || stat(real, &st) != 0 || !S_ISREG(st.st_mode))
        return False;
    /* marked executable by its owner, who is you or root */
    if ((st.st_mode & S_IXUSR) && (st.st_uid == getuid() || st.st_uid == 0) &&
        !(st.st_mode & S_IWOTH))
        return True;
    v = getenv("XDG_DATA_HOME");
    if (v && *v)
        snprintf(dir, sizeof(dir), "%s/applications", v);
    else
        snprintf(dir, sizeof(dir), "%s/.local/share/applications",
                 getenv("HOME") ? getenv("HOME") : "/nonexistent");
    if (UnderDir(real, dir))
        return True;
    v = getenv("XDG_DATA_DIRS");
    {
        char *dirs = XtNewString((v && *v) ? v : "/usr/local/share:/usr/share");
        char *t;
        Boolean ok = False;

        for (t = strtok(dirs, ":"); t && !ok; t = strtok(NULL, ":"))
        {
            snprintf(dir, sizeof(dir), "%s/applications", t);
            ok = UnderDir(real, dir);
        }
        XtFree(dirs);
        return ok;
    }
}

static void
ErrorOK(Widget w, XtPointer client, XtPointer call)
{
    exit(1);
}

/* Say why an untrusted file did not start. */
static int
Refuse(int argc, char **argv, const char *file)
{
    XtAppContext app;
    Widget       shell, box;
    XmString     msg;
    char         text[PATH_MAX + 400];

    snprintf(text, sizeof(text),
             "%s\n\nis not an installed application, so it was not started:\n"
             "a file like this can run any command.\n\n"
             "If you trust it, make it executable (File Manager: Properties,\n"
             "Execute permission for the owner) and open it again.",
             strrchr(file, '/') ? strrchr(file, '/') + 1 : file);
    shell = XtVaAppInitialize(&app, "Dtappfind", NULL, 0, &argc, argv, NULL,
                              XmNmappedWhenManaged, False, NULL);
    XtRealizeWidget(shell);
    msg = XmStringCreateLocalized(text);
    box = XmCreateErrorDialog(shell, "notTrusted", NULL, 0);
    XtVaSetValues(box, XmNmessageString, msg,
                  XtVaTypedArg, XmNdialogTitle, XmRString,
                  "Application not started", 24, NULL);
    XmStringFree(msg);
    XtUnmanageChild(XmMessageBoxGetChild(box, XmDIALOG_CANCEL_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(box, XmDIALOG_HELP_BUTTON));
    XtAddCallback(box, XmNokCallback, ErrorOK, NULL);
    XtManageChild(box);
    XtAppMainLoop(app);
    return 1;
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

static void
ConvertCB(Widget w, XtPointer client, XtPointer call)
{
    DtDndConvertCallbackStruct *c = (DtDndConvertCallbackStruct *) call;

    if (c->reason == DtCR_DND_CONVERT_DATA && dragRow >= 1 &&
        RowApp(dragRow - 1) >= 0)
    {
        c->dragData->data.files[0] =
            XtNewString(apps[RowApp(dragRow - 1)].path);
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
DragRow(int row, XEvent *ev)
{
    static XtCallbackRec convertRec[] = { { ConvertCB, NULL }, { NULL, NULL } };
    static XtCallbackRec finishRec[] = { { DragFinishCB, NULL }, { NULL, NULL } };

    dragRow = row + 1;
    if (dragRow < 1 || RowApp(row) < 0)
        return;
    (void) DtDndVaDragStart(AppListWidget(appList), ev,
                            DtDND_FILENAME_TRANSFER, 1, XmDROP_COPY,
                            convertRec, finishRec, NULL);
}

/* Files found in the background (search.c): the rows grew; keep the
 * selected row. */
static void
SearchChanged(void)
{
    int sel = AppListSelected(appList);

    AppListSetCount(appList, RowCount());
    if (sel > 0 && sel < RowCount())
        AppListSelect(appList, sel);
}

/* Settings > Front Panel > Position: top */
static Boolean
PanelOnTop(void)
{
    char  path[PATH_MAX], word[16] = "";
    const char *home = getenv("HOME");
    FILE *fp;

    if (!home)
        return False;
    snprintf(path, sizeof(path), "%s/.dt/frontpanel-position", home);
    if ((fp = fopen(path, "r")) == NULL)
        return False;
    if (fscanf(fp, "%15s", word) != 1)
        word[0] = '\0';
    fclose(fp);
    return strcmp(word, "top") == 0;
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
    {
        if (!Trusted(argv[2]))
        {
            char *file = argv[2];

            argc = 1;
            return Refuse(argc, argv, file);
        }
        return LaunchFile(argv[2]);
    }
    if (argc == 2 && strcmp(argv[1], "-menu") == 0)
    {
        menu = True;
        argc = 1;
    }
    if (argc == 2 && strcmp(argv[1], "-pick") == 0)
    {
        pick = True;
        argc = 1;
    }
    if (argc == 2 && strcmp(argv[1], "-search") == 0)
    {
        searchMode = True;
        menu = True;			/* placed, closes on focus loss */
        argc = 1;
        SearchReadConfig();
    }

    XtSetLanguageProc(NULL, NULL, NULL);
    top = XtVaAppInitialize(&app, "Dtappfind", NULL, 0, &argc, argv, NULL,
                            XmNtitle, pick ? "Add to Drawer" :
                                      searchMode ? "Search" : "Applications",
                            XmNiconName, pick ? "Add to Drawer" :
                                      searchMode ? "Search" : "Applications",
                            NULL);
    {
        char name[64];

        snprintf(name, sizeof(name), searchMode ? "_CLANKER_SEARCH_S%d" :
                 "_CLANKER_APPFIND_S%d", DefaultScreen(XtDisplay(top)));
        selAtom = XInternAtom(XtDisplay(top), name, False);
        cmdAtom = XInternAtom(XtDisplay(top), "_CLANKER_APPFIND_CMD", False);
        /* a pick window is its own: no hand-off to an open menu */
        if (!pick && HandOff(XtDisplay(top), menu))
            return 0;
    }
    /* for dragging rows (DtDnd); the action database is not needed */
    (void) DtInitialize(XtDisplay(top), top, "dtappfind", "Dtappfind");
    appsStamp = AppDirsStamp();		/* before reading: none missed */
    ReadApps();

    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, top,
                                   XmNmarginWidth, 8, XmNmarginHeight, 8,
                                   NULL);
    s = XmStringCreateLocalized(pick ?
        "Choose the application to add to the drawer (Enter or double-click):" :
        searchMode ?
        "Search applications, settings, windows, files and the web; calculate:" :
        "Search, or type a command to run:");
    label = XtVaCreateManagedWidget("label", xmLabelWidgetClass, form,
                                    XmNlabelString, s,
                                    XmNtopAttachment, XmATTACH_FORM,
                                    XmNleftAttachment, XmATTACH_FORM, NULL);
    XmStringFree(s);
    field = XtVaCreateManagedWidget("search", xmTextFieldWidgetClass, form,
                                    XmNcolumns, 46,
                                    XmNtopAttachment, XmATTACH_WIDGET,
                                    XmNtopWidget, label, XmNtopOffset, 4,
                                    XmNleftAttachment, XmATTACH_FORM,
                                    XmNrightAttachment, XmATTACH_FORM, NULL);
    /* the categories on the left, as other desktops' menus show them,
     * and the applications, each with its icon */
    n = 0;
    XtSetArg(a[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(a[n], XmNtopWidget, field); n++;
    XtSetArg(a[n], XmNtopOffset, 6); n++;
    XtSetArg(a[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    {
        /* wide enough for the longest category name */
        XmRenderTable rt = NULL;
        Dimension     widest = 0;
        int           i;

        XtVaGetValues(field, XmNrenderTable, &rt, NULL);
        for (i = 0; rt && i < NCATS; i++)
        {
            XmString  xs = XmStringCreateLocalized((char *) categories[i].label);
            Dimension w = XmStringWidth(rt, xs);

            XmStringFree(xs);
            if (w > widest)
                widest = w;
        }
        XtSetArg(a[n], XmNwidth, widest + APPLIST_ICON + 40); n++;  /* icon gap, shadows */
    }
    XtSetArg(a[n], XmNheight, NCATS * 26 + 8); n++;
    if (!searchMode)
        catList = AppListCreate(form, "categories", a, n, field, CatText,
                                CatIcon, CategoryCB, NULL, NULL);
    n = 0;
    XtSetArg(a[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(a[n], XmNtopWidget, field); n++;
    XtSetArg(a[n], XmNtopOffset, 6); n++;
    if (searchMode)
    {
        XtSetArg(a[n], XmNleftAttachment, XmATTACH_FORM); n++;
        XtSetArg(a[n], XmNwidth, 600); n++;
        XtSetArg(a[n], XmNheight, 14 * 26 + 8); n++;
    }
    else
    {
        XtSetArg(a[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
        XtSetArg(a[n], XmNleftWidget, AppListWidget(catList)); n++;
        XtSetArg(a[n], XmNleftOffset, 6); n++;
        XtSetArg(a[n], XmNwidth, 330); n++;
    }
    XtSetArg(a[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetArg(a[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    appList = AppListCreate(form, "apps", a, n, field, AppText, AppIcon,
                            NULL, ActivateRow, DragRow);

    XtAddCallback(field, XmNvalueChangedCallback, ChangedCB, NULL);
    XtAddCallback(field, XmNactivateCallback, ActivateCB, NULL);
    XtAddEventHandler(field, KeyPressMask, False, KeyEH, NULL);

    if (catList)
    {
        AppListSetCount(catList, NCATS);
        AppListAutoHideScrollBar(catList);
    }
    if (searchMode)
        SearchInit(top, app, SearchChanged);
    Filter("");
    if (menu)
        XtSetMappedWhenManaged(top, False);
    XtRealizeWidget(top);
    if (!pick)
        XSetSelectionOwner(XtDisplay(top), selAtom, XtWindow(top), CurrentTime);
    XtAddEventHandler(top, PropertyChangeMask, False, CommandEH, NULL);
    del = XmInternAtom(XtDisplay(top), "WM_DELETE_WINDOW", False);
    XmAddWMProtocolCallback(top, del, CloseCB, NULL);
    if (menu)
    {
        /* Just above the Front Panel, centered: where a start menu is;
         * on the monitor with the pointer when there are several. */
        Dimension w, h;
        Display  *dpy = XtDisplay(top);
        int       mx = 0, my = 0;
        int       sw = DisplayWidth(dpy, DefaultScreen(dpy));
        int       sh = DisplayHeight(dpy, DefaultScreen(dpy));
        int       x, y;
        DtXineramaInfo_t *xin = _DtXineramaInit(dpy);

        if (xin && xin->numscreens > 1)
        {
            Window       r, c;
            int          px, py, wx, wy, i;
            unsigned int mask, ww, hh, ox, oy;

            if (XQueryPointer(dpy, DefaultRootWindow(dpy), &r, &c, &px, &py,
                              &wx, &wy, &mask))
                for (i = 0; i < xin->numscreens; i++)
                    if (_DtXineramaGetScreen(xin, i, &ww, &hh, &ox, &oy) &&
                        px >= (int) ox && px < (int) (ox + ww) &&
                        py >= (int) oy && py < (int) (oy + hh))
                    {
                        mx = ox; my = oy; sw = ww; sh = hh;
                        break;
                    }
        }
        XtVaGetValues(top, XmNwidth, &w, XmNheight, &h, NULL);
        x = mx + (sw - (int) w) / 2;
        if (searchMode)
            y = my + sh / 6;		/* high, as runners open */
        else if (PanelOnTop())
            y = my + 110;		/* below a Front Panel at the top */
        else
            y = my + sh - (int) h - 110;
        if (y < my)
            y = my;
        XtVaSetValues(top, XmNx, x, XmNy, y, NULL);
        XtMapWidget(top);
        XtAddEventHandler(top, FocusChangeMask, False, FocusEH, NULL);
    }
    XmProcessTraversal(field, XmTRAVERSE_CURRENT);
    WatchApps(app);
    XtAppMainLoop(app);
    return 0;
}
