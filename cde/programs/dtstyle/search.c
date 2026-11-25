/*
 * search.c - what "dtappfind -search" (the Front Panel's Search button)
 * finds besides applications, as KRunner or Spotlight do:
 *
 *   calculator   "2*(3+4)", "=sqrt 2" is not supported; + - * / % ^ ( )
 *   Settings     its pages, by name and by what they set ("wallpaper")
 *   windows      open windows, by title (switches to it)
 *   recent       recently used files (~/.local/share/recently-used.xbel)
 *   files        by name: plocate, locate, Baloo, or a bounded walk of
 *                the home folder; in the background, as you type
 *   web          "Search the web for ..." (the engine is configurable)
 *   run          "Run ..." when the text starts with a program
 *
 * Each source can be turned off in Settings > Search, which writes
 * ~/.dt/search ("name=value" lines).  Nothing runs through a shell
 * except the "Run" row, which is a command you typed.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <X11/Intrinsic.h>
#include <X11/Xatom.h>
#include <Xm/Xm.h>
#include <Xm/TextF.h>

#include "search.h"

#ifndef CDE_INSTALLATION_TOP
#define CDE_INSTALLATION_TOP "/usr/dt"
#endif

#define MAX_BEFORE  2
#define MAX_AFTER   80
#define FILE_DELAY  250		/* ms after the last key, then search */

SearchConfig searchConfig;

static Hit     before[MAX_BEFORE], after[MAX_AFTER];
static int     nBefore, nAfter;
static int     nFileHits;	/* files are the last of "after" but web/run */

static Widget        topW;
static XtAppContext  appC;
static void        (*changedCB)(void);
static char          query[512];

/* ------------------------------------------------------------------ */
/* configuration                                                      */

static void
Defaults(SearchConfig *c)
{
    c->apps = c->settings = c->windows = c->recent = c->files = True;
    c->calc = c->web = c->run = True;
    c->filesEverywhere = False;
    c->maxApps = 8;
    c->maxFiles = 12;
    snprintf(c->engine, sizeof(c->engine), "https://duckduckgo.com/?q=%%s");
    snprintf(c->engineName, sizeof(c->engineName), "DuckDuckGo");
    snprintf(c->fileTool, sizeof(c->fileTool), "auto");
}

static Boolean
OnOff(const char *v)
{
    return !(strcmp(v, "0") == 0 || strcasecmp(v, "off") == 0 ||
             strcasecmp(v, "no") == 0 || strcasecmp(v, "false") == 0);
}

void
SearchReadConfig(void)
{
    char  path[PATH_MAX], line[1024];
    const char *home = getenv("HOME");
    FILE *fp;

    Defaults(&searchConfig);
    if (!home)
        return;
    snprintf(path, sizeof(path), "%s/.dt/search", home);
    if ((fp = fopen(path, "r")) == NULL)
        return;
    while (fgets(line, sizeof(line), fp))
    {
        char *eq, *v, *e;

        if (line[0] == '#' || (eq = strchr(line, '=')) == NULL)
            continue;
        *eq = '\0';
        v = eq + 1;
        if ((e = strchr(v, '\n')) != NULL)
            *e = '\0';
#define B(n) if (strcmp(line, #n) == 0) searchConfig.n = OnOff(v); else
        B(apps) B(settings) B(windows) B(recent) B(files) B(calc) B(web)
        B(run) B(filesEverywhere)
#undef B
        if (strcmp(line, "maxApps") == 0 && atoi(v) > 0)
            searchConfig.maxApps = atoi(v) > 100 ? 100 : atoi(v);
        else if (strcmp(line, "maxFiles") == 0 && atoi(v) > 0)
            searchConfig.maxFiles = atoi(v) > 50 ? 50 : atoi(v);
        else if (strcmp(line, "engine") == 0 && strstr(v, "%s") &&
                 (strncmp(v, "https://", 8) == 0 || strncmp(v, "http://", 7) == 0))
        {
            /* one %s, no other % directives */
            const char *p = strstr(v, "%s");
            if (!strchr(p + 2, '%') && strchr(v, '%') == p)
            {
                snprintf(searchConfig.engine, sizeof(searchConfig.engine),
                         "%s", v);
            }
        }
        else if (strcmp(line, "engineName") == 0)
            snprintf(searchConfig.engineName, sizeof(searchConfig.engineName),
                     "%s", v);
        else if (strcmp(line, "fileTool") == 0)
            snprintf(searchConfig.fileTool, sizeof(searchConfig.fileTool),
                     "%s", v);
    }
    fclose(fp);
}

/* ------------------------------------------------------------------ */
/* helpers                                                            */

static void
Clear(Hit *h, int n)
{
    int i;

    for (i = 0; i < n; i++)
    {
        XtFree(h[i].label);
        XtFree(h[i].icon);
        XtFree(h[i].data);
        h[i].label = h[i].icon = h[i].data = NULL;
    }
}

static void
AddAfter(int kind, const char *label, const char *icon, const char *data)
{
    if (nAfter >= MAX_AFTER)
        return;
    after[nAfter].kind = kind;
    after[nAfter].label = XtNewString(label);
    after[nAfter].icon = XtNewString(icon);
    after[nAfter].data = XtNewString(data);
    nAfter++;
}

static Boolean
HasCase(const char *hay, const char *want)
{
    size_t n = strlen(want);

    if (n == 0)
        return False;
    for (; *hay; hay++)
        if (strncasecmp(hay, want, n) == 0)
            return True;
    return False;
}

static Boolean
InPath(const char *prog)
{
    char  *path = getenv("PATH"), *p, *d, *save = NULL;
    char   full[PATH_MAX];
    Boolean found = False;

    if (!prog || !*prog)
        return False;
    if (strchr(prog, '/'))
        return access(prog, X_OK) == 0;
    if (!path)
        return False;
    p = XtNewString(path);
    for (d = strtok_r(p, ":", &save); d && !found; d = strtok_r(NULL, ":", &save))
    {
        snprintf(full, sizeof(full), "%s/%s", d, prog);
        found = access(full, X_OK) == 0;
    }
    XtFree(p);
    return found;
}

/* Run argv in the background, detached (no shell). */
static void
Spawn(char *const argv[])
{
    pid_t pid;

    if ((pid = fork()) == 0)
    {
        if (fork() == 0)
        {
            setsid();
            if (topW && XtDisplay(topW))
                close(ConnectionNumber(XtDisplay(topW)));
            execvp(argv[0], argv);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0)
        waitpid(pid, NULL, 0);
}

static void
OpenThing(const char *what)
{
    if (InPath("xdg-open"))
    {
        char *argv[] = { "xdg-open", (char *) what, NULL };
        Spawn(argv);
    }
    else
    {
        char *argv[] = { CDE_INSTALLATION_TOP "/bin/dtaction", "Open",
                         (char *) what, NULL };
        Spawn(argv);
    }
}

/* The home folder shown as "~". */
static void
Tilde(const char *path, char *out, size_t len)
{
    const char *home = getenv("HOME");
    size_t      n = home ? strlen(home) : 0;

    if (n > 1 && strncmp(path, home, n) == 0 && (path[n] == '/' || !path[n]))
        snprintf(out, len, "~%s", path + n);
    else
        snprintf(out, len, "%s", path);
}

/* "name   (folder)" for a file row */
static void
FileLabel(const char *prefix, const char *path, char *out, size_t len)
{
    char        dir[PATH_MAX], t[PATH_MAX];
    const char *base = strrchr(path, '/');

    snprintf(dir, sizeof(dir), "%.*s", base ? (int) (base - path) : 0, path);
    Tilde(dir[0] ? dir : "/", t, sizeof(t));
    snprintf(out, len, "%s%s   (%s)", prefix, base ? base + 1 : path, t);
}

/* An icon for a file, by its extension (theme names, then CDE's). */
static const char *
FileIcon(const char *path)
{
    static const struct { const char *ext, *icon; } map[] = {
        { ".pdf", "application-pdf|Dtpdf" },
        { ".txt", "text-x-generic|Dtpenpd" }, { ".md", "text-x-generic|Dtpenpd" },
        { ".c", "text-x-csrc|Dtpenpd" }, { ".h", "text-x-chdr|Dtpenpd" },
        { ".sh", "text-x-script|Dtpenpd" }, { ".py", "text-x-python|Dtpenpd" },
        { ".png", "image-x-generic|Dtpaint" }, { ".jpg", "image-x-generic|Dtpaint" },
        { ".jpeg", "image-x-generic|Dtpaint" }, { ".gif", "image-x-generic|Dtpaint" },
        { ".svg", "image-x-generic|Dtpaint" }, { ".webp", "image-x-generic|Dtpaint" },
        { ".mp3", "audio-x-generic|Dtaudio" }, { ".ogg", "audio-x-generic|Dtaudio" },
        { ".flac", "audio-x-generic|Dtaudio" }, { ".wav", "audio-x-generic|Dtaudio" },
        { ".mp4", "video-x-generic|Dtaudio" }, { ".mkv", "video-x-generic|Dtaudio" },
        { ".webm", "video-x-generic|Dtaudio" },
        { ".odt", "x-office-document|Dtpenpd" }, { ".docx", "x-office-document|Dtpenpd" },
        { ".ods", "x-office-spreadsheet|Dtpenpd" }, { ".xlsx", "x-office-spreadsheet|Dtpenpd" },
        { ".zip", "package-x-generic|Dtpkg" }, { ".tar", "package-x-generic|Dtpkg" },
        { ".gz", "package-x-generic|Dtpkg" }, { ".xz", "package-x-generic|Dtpkg" },
        { ".deb", "package-x-generic|Dtpkg" }, { ".rpm", "package-x-generic|Dtpkg" },
        { ".html", "text-html|DtWeb" },
    };
    const char *dot = strrchr(path, '.'), *slash = strrchr(path, '/');
    struct stat st;
    size_t i;

    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return "folder|Dtdirect";
    if (dot && (!slash || dot > slash))
        for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
            if (strcasecmp(dot, map[i].ext) == 0)
                return map[i].icon;
    return "text-x-generic|Dtdata";
}

/* ------------------------------------------------------------------ */
/* calculator                                                         */

static const char *cp;
static int         cerr;

static double CExpr(void);

static void
CSkip(void)
{
    while (*cp == ' ')
        cp++;
}

static double
CAtom(void)
{
    double v;
    char  *end;

    CSkip();
    if (*cp == '(')
    {
        cp++;
        v = CExpr();
        CSkip();
        if (*cp == ')')
            cp++;
        else
            cerr = 1;
        return v;
    }
    if (*cp == '-')
    {
        cp++;
        return -CAtom();
    }
    if (*cp == '+')
    {
        cp++;
        return CAtom();
    }
    if (!isdigit((unsigned char) *cp) && *cp != '.')
    {
        cerr = 1;
        return 0;
    }
    v = strtod(cp, &end);
    if (end == cp)
        cerr = 1;
    cp = end;
    return v;
}

static double
CPow(void)
{
    double b = CAtom(), e, r = 1;
    int    i, neg;

    CSkip();
    if (*cp == '^')
    {
        cp++;
        e = CPow();			/* right associative */
        if (e != (int) e || e > 1000 || e < -1000)
        {
            cerr = 1;		/* whole powers only, no libm needed */
            return 0;
        }
        neg = e < 0;
        for (i = 0; i < (neg ? -e : e); i++)
            r *= b;
        return neg ? (r != 0 ? 1 / r : (cerr = 1, 0)) : r;
    }
    return b;
}

static double
CTerm(void)
{
    double v = CPow(), r;

    for (;;)
    {
        CSkip();
        if (*cp == '*' || *cp == 'x' || *cp == 'X')
        {
            cp++;
            v *= CPow();
        }
        else if (*cp == '/')
        {
            cp++;
            r = CPow();
            if (r == 0)
                cerr = 1;
            else
                v /= r;
        }
        else if (*cp == '%')
        {
            cp++;
            r = CPow();
            if (r == 0)
                cerr = 1;
            else
                v = v - r * (double) (long long) (v / r);
        }
        else
            return v;
    }
}

static double
CExpr(void)
{
    double v = CTerm();

    for (;;)
    {
        CSkip();
        if (*cp == '+')
        {
            cp++;
            v += CTerm();
        }
        else if (*cp == '-')
        {
            cp++;
            v -= CTerm();
        }
        else
            return v;
    }
}

static void
Calculate(const char *text)
{
    char        buf[512], res[64], *b;
    const char *t = text;
    Boolean     op = False, digit = False;
    double      v;

    if (*t == '=')
        t++;
    /* only arithmetic: digits, operators, parentheses, spaces; "," as
     * a decimal point too */
    for (b = buf; *t && b < buf + sizeof(buf) - 1; t++)
    {
        if (isdigit((unsigned char) *t))
            digit = True;
        else if (strchr("+-*/%^()xX", *t))
            op = op || (*t != '(' && *t != ')');
        else if (*t != ' ' && *t != '.' && *t != ',')
            return;
        *b++ = (*t == ',') ? '.' : *t;
    }
    *b = '\0';
    if (!digit || !op)
        return;
    cp = buf;
    cerr = 0;
    v = CExpr();
    CSkip();
    if (cerr || *cp)
        return;
    snprintf(res, sizeof(res), "%.12g", v);
    snprintf(buf, sizeof(buf), "= %s", res);
    before[0].kind = HIT_CALC;
    before[0].label = XtNewString(buf);
    before[0].icon = XtNewString("accessories-calculator|Dtcalc");
    before[0].data = XtNewString(res);
    nBefore = 1;
}

/* ------------------------------------------------------------------ */
/* Settings pages                                                     */

static const struct {
    const char *label, *keywords, *page, *icon;
} pages[] = {
    { "Color", "colour palette theme dark light gtk qt", "Color", "preferences-desktop-color|DtColor" },
    { "Font", "text size fonts", "Font", "preferences-desktop-font|DtFont" },
    { "Backdrop", "wallpaper background picture", "Backdrop", "preferences-desktop-wallpaper|DtBkdrp" },
    { "Keyboard", "repeat click layout typing", "Keyboard", "preferences-desktop-keyboard|DtKeybd" },
    { "Mouse", "pointer touchpad tap scroll speed", "Mouse", "preferences-desktop-peripherals|DtMouse" },
    { "Beep", "sound bell volume", "Beep", "preferences-desktop-sound|DtBeep" },
    { "Screen", "screensaver lock blank power", "Screen", "preferences-desktop-screensaver|DtScrn" },
    { "Window", "focus compositor windows borders title bar", "Dtwm", "preferences-system-windows|DtDtwm" },
    { "Startup", "login session restore", "Startup", "preferences-system-session|DtStart" },
    { "Language", "input method locale", "I18N", "preferences-desktop-locale|DtI18N" },
    { "Network", "wifi wi-fi wireless ethernet internet vpn", "Network", "preferences-system-network|DtNetwk" },
    { "Bluetooth", "devices pair headphones", "Bluetooth", "preferences-system-bluetooth|DtBluet" },
    { "Updates", "upgrade software packages", "Updates", "system-software-update|DtUpdate" },
    { "Autostart", "startup applications login", "Autostart", "preferences-system-session|DtStart" },
    { "Front Panel", "panel dock drawers position hide", "FrontPanel", "preferences-desktop|Fpstyle" },
    { "Session", "window manager applications terminal games gamescope", "Session", "preferences-system-session|DtStart" },
    { "Accessibility", "sticky slow keys on-screen keyboard large pointer", "Accessibility", "preferences-desktop-accessibility|DtAccess" },
    { "Shortcuts", "keyboard shortcuts hotkeys keys", "Shortcuts", "preferences-desktop-keyboard-shortcuts|DtKeybd" },
    { "Search", "search find runner spotlight files web engine", "Search", "system-search|DtSearch" },
    { "Displays", "monitor screen resolution projector mirror extend", "!dtdisplays", "video-display|DtScrn" },
    { "Sound", "volume audio mixer speakers microphone", "!dtmixer", "multimedia-volume-control|DtBeep" },
};

static void
Settings(const char *text)
{
    char   label[256];
    size_t i;

    for (i = 0; i < sizeof(pages) / sizeof(pages[0]); i++)
        if (HasCase(pages[i].label, text) || HasCase(pages[i].keywords, text))
        {
            snprintf(label, sizeof(label), "Settings: %s", pages[i].label);
            AddAfter(HIT_SETTING, label, pages[i].icon, pages[i].page);
        }
}

/* ------------------------------------------------------------------ */
/* open windows                                                       */

static char *
WindowTitle(Display *dpy, Window w)
{
    Atom           type, utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    int            fmt;
    unsigned long  n, left;
    unsigned char *data = NULL;
    char          *t = NULL;

    if (XGetWindowProperty(dpy, w, XInternAtom(dpy, "_NET_WM_NAME", False),
                           0, 256, False, utf8, &type, &fmt, &n, &left,
                           &data) == Success && data && n)
        t = XtNewString((char *) data);
    if (data)
        XFree(data);
    if (!t)
    {
        char *name = NULL;

        if (XFetchName(dpy, w, &name) && name)
        {
            t = XtNewString(name);
            XFree(name);
        }
    }
    return t;
}

static void
Windows(const char *text)
{
    Display       *dpy = XtDisplay(topW);
    Atom           type;
    int            fmt;
    unsigned long  n, left, i;
    unsigned char *data = NULL;
    Window        *wins;

    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy),
                           XInternAtom(dpy, "_NET_CLIENT_LIST", False), 0,
                           1024, False, XA_WINDOW, &type, &fmt, &n, &left,
                           &data) != Success || !data)
        return;
    wins = (Window *) data;
    for (i = 0; i < n; i++)
    {
        char *t;

        if (wins[i] == XtWindow(topW))
            continue;
        if ((t = WindowTitle(dpy, wins[i])) != NULL)
        {
            if (HasCase(t, text))
            {
                char label[300], id[32];

                snprintf(label, sizeof(label), "Window: %s", t);
                snprintf(id, sizeof(id), "%lu", (unsigned long) wins[i]);
                AddAfter(HIT_WINDOW, label, "preferences-system-windows|DtDtwm",
                         id);
            }
            XtFree(t);
        }
    }
    XFree(data);
}

static void
ActivateWindow(Window w)
{
    Display *dpy = XtDisplay(topW);
    XEvent   ev;

    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 2;		/* from a pager / user */
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, DefaultRootWindow(dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XMapRaised(dpy, w);
    XFlush(dpy);
}

/* ------------------------------------------------------------------ */
/* recent files                                                       */

static int
Hex(int c)
{
    return isdigit(c) ? c - '0' : tolower(c) - 'a' + 10;
}

static void
UrlDecode(const char *in, char *out, size_t len)
{
    size_t k = 0;

    for (; *in && k + 1 < len; in++)
    {
        if (*in == '%' && isxdigit((unsigned char) in[1]) &&
            isxdigit((unsigned char) in[2]))
        {
            out[k++] = (char) (Hex((unsigned char) in[1]) * 16 +
                               Hex((unsigned char) in[2]));
            in += 2;
        }
        else
            out[k++] = *in;
    }
    out[k] = '\0';
}

/* The recently used files, read once per search window (the file can
 * be large; it is not re-read at every key). */
static char **recentFiles;
static int    nRecentFiles = -1;

static void
ReadRecent(void)
{
    char        path[PATH_MAX], line[4096];
    const char *home = getenv("HOME"), *xdg = getenv("XDG_DATA_HOME");
    FILE       *fp;

    nRecentFiles = 0;
    if (xdg && *xdg == '/')
        snprintf(path, sizeof(path), "%s/recently-used.xbel", xdg);
    else if (home)
        snprintf(path, sizeof(path), "%s/.local/share/recently-used.xbel", home);
    else
        return;
    if ((fp = fopen(path, "r")) == NULL)
        return;
    while (fgets(line, sizeof(line), fp) && nRecentFiles < 2000)
    {
        char *h = strstr(line, "href=\"file://"), *e, file[PATH_MAX];

        if (!h)
            continue;
        h += 13;
        if ((e = strchr(h, '"')) == NULL)
            continue;
        *e = '\0';
        UrlDecode(h, file, sizeof(file));
        recentFiles = (char **) XtRealloc((char *) recentFiles,
                                          (nRecentFiles + 1) * sizeof(char *));
        recentFiles[nRecentFiles++] = XtNewString(file);
    }
    fclose(fp);
}

static void
Recent(const char *text)
{
    int i, found = 0;

    if (nRecentFiles < 0)
        ReadRecent();
    /* the newest are last in the file */
    for (i = nRecentFiles - 1; i >= 0 && found < 5; i--)
    {
        const char *file = recentFiles[i], *base = strrchr(file, '/');
        char        label[PATH_MAX + 64];

        if (!HasCase(base ? base + 1 : file, text) || access(file, F_OK) != 0)
            continue;
        FileLabel("Recent: ", file, label, sizeof(label));
        AddAfter(HIT_RECENT, label, FileIcon(file), file);
        found++;
    }
}

/* ------------------------------------------------------------------ */
/* files (in the background)                                          */

static pid_t        filePid = -1;
static int          fileFd = -1;
static XtInputId    fileInput;
static XtIntervalId fileTimer;
static char         fileBuf[8192];
static size_t       fileLen;
static int          fileWanted;	/* the query the running search is for */
static int          queryNo;

static void
StopFiles(void)
{
    if (fileInput)
    {
        XtRemoveInput(fileInput);
        fileInput = 0;
    }
    if (fileFd >= 0)
    {
        close(fileFd);
        fileFd = -1;
    }
    if (filePid > 0)
    {
        kill(-filePid, SIGTERM);		/* its process group */
        waitpid(filePid, NULL, WNOHANG);
        filePid = -1;
    }
    fileLen = 0;
}

static Boolean
Hidden(const char *path)
{
    return strstr(path, "/.") != NULL;
}

/* Insert a file hit before the web and run rows. */
static void
AddFile(const char *path)
{
    char label[PATH_MAX + 64];
    int  i, at;
    const char *home = getenv("HOME");

    if (nFileHits >= searchConfig.maxFiles || nAfter >= MAX_AFTER)
        return;
    if (!searchConfig.filesEverywhere && home &&
        (strncmp(path, home, strlen(home)) != 0 || path[strlen(home)] != '/'))
        return;
    if (Hidden(path))
        return;
    for (i = 0; i < nAfter; i++)
        if ((after[i].kind == HIT_FILE || after[i].kind == HIT_RECENT) &&
            strcmp(after[i].data, path) == 0)
            return;
    /* after the other files, before web and run */
    for (at = nAfter; at > 0 && (after[at - 1].kind == HIT_WEB ||
                                 after[at - 1].kind == HIT_RUN); at--)
        ;
    memmove(&after[at + 1], &after[at], (nAfter - at) * sizeof(Hit));
    FileLabel("File: ", path, label, sizeof(label));
    after[at].kind = HIT_FILE;
    after[at].label = XtNewString(label);
    after[at].icon = XtNewString(FileIcon(path));
    after[at].data = XtNewString(path);
    nAfter++;
    nFileHits++;
}

static void
FileInputCB(XtPointer client, int *fd, XtInputId *id)
{
    ssize_t r;
    char   *nl, *start;
    int     before = nFileHits;

    if (fileWanted != queryNo)
    {
        StopFiles();
        return;
    }
    r = read(*fd, fileBuf + fileLen, sizeof(fileBuf) - 1 - fileLen);
    if (r <= 0)
    {
        StopFiles();
        return;
    }
    fileLen += (size_t) r;
    fileBuf[fileLen] = '\0';
    start = fileBuf;
    while ((nl = strchr(start, '\n')) != NULL)
    {
        *nl = '\0';
        if (*start == '/')
            AddFile(start);
        start = nl + 1;
    }
    fileLen = strlen(start);
    memmove(fileBuf, start, fileLen + 1);
    if (fileLen == sizeof(fileBuf) - 1)
        fileLen = 0;			/* an absurd line: drop it */
    if (nFileHits >= searchConfig.maxFiles)
        StopFiles();
    if (nFileHits != before && changedCB)
        changedCB();
}

/* KDE's file index exists and is used (Baloo, as KRunner searches). */
static Boolean
BalooIndex(void)
{
    char        path[PATH_MAX];
    const char *home = getenv("HOME"), *x = getenv("XDG_DATA_HOME");

    if (x && *x == '/')
        snprintf(path, sizeof(path), "%s/baloo/index", x);
    else if (home)
        snprintf(path, sizeof(path), "%s/.local/share/baloo/index", home);
    else
        return False;
    return access(path, R_OK) == 0;
}

/* The command that lists files whose name contains text: an index
 * when there is one (Baloo, plocate, locate), else the home folder. */
static int
FileCommand(const char *text, char **argv, char *num, size_t numlen)
{
    const char *tool = searchConfig.fileTool;
    const char *home = getenv("HOME");
    Boolean     autoT = strcmp(tool, "auto") == 0;
    int         n = 0;
    static char pat[600];

    snprintf(num, numlen, "%d", searchConfig.maxFiles * 20);
    if ((autoT ? BalooIndex() : strcmp(tool, "baloo") == 0) &&
        (InPath("baloosearch6") || InPath("baloosearch")))
    {
        argv[n++] = InPath("baloosearch6") ? "baloosearch6" : "baloosearch";
        argv[n++] = "-l";
        argv[n++] = num;
        argv[n++] = (char *) text;
    }
    else if ((autoT ? access("/var/lib/plocate/plocate.db", R_OK) == 0 :
              strcmp(tool, "plocate") == 0) && InPath("plocate"))
    {
        argv[n++] = "plocate"; argv[n++] = "-i"; argv[n++] = "-b";
        argv[n++] = "-l"; argv[n++] = num; argv[n++] = "--";
        argv[n++] = (char *) text;
    }
    else if ((autoT ? (access("/var/lib/mlocate/mlocate.db", R_OK) == 0 ||
                       access("/var/lib/locate/locatedb", R_OK) == 0 ||
                       access("/var/db/locate.database", R_OK) == 0) :
              strcmp(tool, "locate") == 0) && InPath("locate"))
    {
        argv[n++] = "locate"; argv[n++] = "-i"; argv[n++] = "-b";
        argv[n++] = "-l"; argv[n++] = num; argv[n++] = "--";
        argv[n++] = (char *) text;
    }
    else if (home)
    {
        /* no index: the home folder, five levels deep, no hidden ones */
        snprintf(pat, sizeof(pat), "*%s*", text);
        argv[n++] = "find"; argv[n++] = (char *) home;
        argv[n++] = "-maxdepth"; argv[n++] = "5";
        argv[n++] = "-name"; argv[n++] = ".*"; argv[n++] = "-prune";
        argv[n++] = "-o"; argv[n++] = "-iname"; argv[n++] = pat;
        argv[n++] = "-print";
    }
    argv[n] = NULL;
    return n;
}

static void
StartFiles(XtPointer client, XtIntervalId *id)
{
    char *argv[20], num[16];
    int   p[2];

    fileTimer = 0;
    StopFiles();
    if (strlen(query) < 2 || FileCommand(query, argv, num, sizeof(num)) == 0)
        return;
    if (pipe(p) != 0)
        return;
    if ((filePid = fork()) == 0)
    {
        int nul = open("/dev/null", O_RDWR);

        setpgid(0, 0);
        dup2(p[1], 1);
        if (nul >= 0)
        {
            dup2(nul, 0);
            dup2(nul, 2);
        }
        close(p[0]);
        close(p[1]);
        close(ConnectionNumber(XtDisplay(topW)));
        nice(10);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(p[1]);
    if (filePid < 0)
    {
        close(p[0]);
        return;
    }
    setpgid(filePid, filePid);
    fileFd = p[0];
    fcntl(fileFd, F_SETFD, FD_CLOEXEC);
    fileWanted = queryNo;
    fileInput = XtAppAddInput(appC, fileFd, (XtPointer) XtInputReadMask,
                              FileInputCB, NULL);
}

/* ------------------------------------------------------------------ */
/* web, run                                                           */

static void
Web(const char *text)
{
    char label[600];

    snprintf(label, sizeof(label), "Search the web for \"%s\" (%s)", text,
             searchConfig.engineName);
    AddAfter(HIT_WEB, label, "web-browser|DtWeb", text);
}

static void
Run(const char *text)
{
    char word[256], label[600];
    int  i;

    for (i = 0; text[i] && text[i] != ' ' && i < (int) sizeof(word) - 1; i++)
        word[i] = text[i];
    word[i] = '\0';
    if (!InPath(word))
        return;
    snprintf(label, sizeof(label), "Run: %s", text);
    AddAfter(HIT_RUN, label, "utilities-terminal|Dtterm", text);
}

/* ------------------------------------------------------------------ */
/* public                                                             */

void
SearchInit(Widget top, XtAppContext app, void (*changed)(void))
{
    topW = top;
    appC = app;
    changedCB = changed;
    signal(SIGCHLD, SIG_DFL);
}

void
SearchQuery(const char *text)
{
    while (*text == ' ')
        text++;
    snprintf(query, sizeof(query), "%s", text);
    queryNo++;
    Clear(before, nBefore);
    Clear(after, nAfter);
    nBefore = nAfter = nFileHits = 0;
    StopFiles();
    if (fileTimer)
    {
        XtRemoveTimeOut(fileTimer);
        fileTimer = 0;
    }
    if (!*query)
        return;
    if (searchConfig.calc)
        Calculate(query);
    if (searchConfig.settings)
        Settings(query);
    if (searchConfig.windows)
        Windows(query);
    if (searchConfig.recent)
        Recent(query);
    if (searchConfig.web)
        Web(query);
    if (searchConfig.run)
        Run(query);
    if (searchConfig.files)
        fileTimer = XtAppAddTimeOut(appC, FILE_DELAY, StartFiles, NULL);
}

int SearchBefore(void) { return nBefore; }
int SearchAfter(void) { return nAfter; }
const Hit *SearchHitBefore(int i) { return (i >= 0 && i < nBefore) ? &before[i] : NULL; }
const Hit *SearchHitAfter(int i) { return (i >= 0 && i < nAfter) ? &after[i] : NULL; }

static void
UrlEncode(const char *in, char *out, size_t len)
{
    static const char hexd[] = "0123456789ABCDEF";
    size_t k = 0;

    for (; *in && k + 4 < len; in++)
    {
        unsigned char c = (unsigned char) *in;

        if (isalnum(c) || strchr("-_.~", c))
            out[k++] = (char) c;
        else if (c == ' ')
            out[k++] = '+';
        else
        {
            out[k++] = '%';
            out[k++] = hexd[c >> 4];
            out[k++] = hexd[c & 15];
        }
    }
    out[k] = '\0';
}

static Boolean
CopyText(const char *text)
{
    int   p[2];
    pid_t pid;
    char *xclip[] = { "xclip", "-selection", "clipboard", NULL };
    char *xsel[] = { "xsel", "-b", "-i", NULL };
    char *wl[] = { "wl-copy", NULL };
    char **argv = InPath("xclip") ? xclip : InPath("xsel") ? xsel :
                  (getenv("WAYLAND_DISPLAY") && InPath("wl-copy")) ? wl : NULL;

    if (!argv || pipe(p) != 0)
        return False;
    if ((pid = fork()) == 0)
    {
        dup2(p[0], 0);
        close(p[0]);
        close(p[1]);
        setsid();
        execvp(argv[0], argv);
        _exit(127);
    }
    close(p[0]);
    if (pid < 0)
    {
        close(p[1]);
        return False;
    }
    (void) write(p[1], text, strlen(text));
    close(p[1]);
    return True;
}

Boolean
SearchActivate(const Hit *h, Widget field)
{
    if (!h)
        return False;
    StopFiles();
    switch (h->kind)
    {
    case HIT_CALC:
        /* copy the result; without a clipboard tool, show it in the
         * field to copy from there */
        if (CopyText(h->data))
            return True;
        XmTextFieldSetString(field, h->data);
        return False;
    case HIT_SETTING:
        if (h->data[0] == '!')
        {
            char *argv[] = { h->data + 1, NULL };
            Spawn(argv);
        }
        else
        {
            char *argv[] = { CDE_INSTALLATION_TOP "/bin/dtstyle", "-open",
                             h->data, NULL };
            Spawn(argv);
        }
        return True;
    case HIT_WINDOW:
        ActivateWindow((Window) strtoul(h->data, NULL, 10));
        return True;
    case HIT_RECENT:
    case HIT_FILE:
        OpenThing(h->data);
        return True;
    case HIT_WEB:
    {
        char enc[1600], url[2200];
        const char *s = strstr(searchConfig.engine, "%s");

        UrlEncode(h->data, enc, sizeof(enc));
        if (!s)
            return True;
        snprintf(url, sizeof(url), "%.*s%s%s", (int) (s - searchConfig.engine),
                 searchConfig.engine, enc, s + 2);
        OpenThing(url);
        return True;
    }
    case HIT_RUN:
    {
        char *argv[] = { "/bin/sh", "-c", h->data, NULL };
        Spawn(argv);
        return True;
    }
    }
    return False;
}
