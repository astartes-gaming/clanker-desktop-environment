/*
 * SysPanelsMore.c - more Settings pages, in the style of the system
 * settings of other desktops: Search, Displays, Sound, Controllers,
 * Users, Region, Input (keyboard layouts), Removable Media and Window
 * Borders.
 *
 * Included at the end of SysPanels.c (not compiled on its own): the
 * pages use its generic Panel (a status line, a list and buttons).
 * Every page runs the system's own tools (xrandr, pactl/wpctl,
 * setxkbmap, timedatectl, passwd...) without a shell, and shows only
 * what the system can do.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */

#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <Xm/SelectioB.h>
#include "search.h"

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

/* "[x] label" rows */
static void
CheckRow(Panel *p, Boolean on, const char *label, const char *key)
{
    char buf[400];

    snprintf(buf, sizeof(buf), "[%s] %s", on ? "x" : " ", label);
    PanelAddItem(p, buf, key, "");
}

static void
ValueRow(Panel *p, const char *label, const char *value, const char *key)
{
    char buf[600];

    snprintf(buf, sizeof(buf), "%s: %s", label, value);
    PanelAddItem(p, buf, key, "");
}

static void
Reselect(Panel *p, int row)
{
    if (row >= 0 && row < p->nItems)
        XmListSelectPos(p->list, row + 1, False);
}

/* Run a tool, ignoring its result (the page refreshes afterwards). */
static void
QuietDone(int rc, char *output, XtPointer data)
{
    Panel *p = (Panel *) data;

    if (rc != 0 && p)
        PanelShowFailure(p, MSG(150, "The change could not be made:"), output);
    if (p && p->refresh)
        (*p->refresh)(p);
}

static void
Run(Panel *p, char **argv)
{
    if (!SysCmdRun(argv, NULL, 60, QuietDone, (XtPointer) p) && p)
        ErrDialog(MSG(151, "The program for this is not installed."),
                  style.shell);
}

/* Start a program on its own (a settings tool), not waited for. */
static Boolean
Launch1(const char *prog, const char *arg)
{
    pid_t pid;

    if (!SysCmdHave(prog))
        return False;
    if ((pid = fork()) == 0)
    {
        setsid();
        close(ConnectionNumber(style.display));
        execlp(prog, prog, arg, (char *) NULL);
        _exit(127);
    }
    return pid > 0;
}

/* The first of a NULL-terminated list of programs that is installed. */
static const char *
FirstHave(const char *const *progs)
{
    for (; *progs; progs++)
        if (SysCmdHave(*progs))
            return *progs;
    return NULL;
}

/* ---- choosing from a list (with typing to narrow) ----------------- */

typedef void (*ChooseProc)(const char *choice, XtPointer data);
static ChooseProc chooseProc;
static XtPointer  chooseData;
static Widget     chooseDialog;

static void
ChooseOkCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    XmSelectionBoxCallbackStruct *cb = (XmSelectionBoxCallbackStruct *) call_data;
    char *t = NULL;

    if (cb->value)
        t = (char *) XmStringUnparse(cb->value, NULL, XmCHARSET_TEXT,
                                     XmCHARSET_TEXT, NULL, 0, XmOUTPUT_ALL);
    XtUnmanageChild(w);
    if (t && *t && chooseProc)
        (*chooseProc)(t, chooseData);
    XtFree(t);
}

static void
ChooseCancelCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    XtUnmanageChild(w);
}

static void
Choose(const char *title, const char *prompt, char **items, int n,
       ChooseProc proc, XtPointer data)
{
    XmString *xs, t, pr;
    int       i;

    chooseProc = proc;
    chooseData = data;
    if (!chooseDialog)
    {
        chooseDialog = XmCreateSelectionDialog(style.shell, "chooseDialog",
                                               NULL, 0);
        XtAddCallback(chooseDialog, XmNokCallback, ChooseOkCB, NULL);
        XtAddCallback(chooseDialog, XmNcancelCallback, ChooseCancelCB, NULL);
        XtUnmanageChild(XmSelectionBoxGetChild(chooseDialog,
                                               XmDIALOG_HELP_BUTTON));
        XtUnmanageChild(XmSelectionBoxGetChild(chooseDialog,
                                               XmDIALOG_APPLY_BUTTON));
    }
    xs = (XmString *) XtMalloc((n ? n : 1) * sizeof(XmString));
    for (i = 0; i < n; i++)
        xs[i] = CMPSTR(items[i]);
    t = CMPSTR((char *) prompt);
    pr = CMPSTR("");
    XtVaSetValues(chooseDialog, XmNlistItems, xs, XmNlistItemCount, n,
                  XmNlistLabelString, t, XmNtextString, pr,
                  XmNmustMatch, False, XmNlistVisibleItemCount, 14, NULL);
    XtVaSetValues(XtParent(chooseDialog), XmNtitle, title, NULL);
    for (i = 0; i < n; i++)
        XmStringFree(xs[i]);
    XtFree((char *) xs);
    XmStringFree(t);
    XmStringFree(pr);
    XtManageChild(chooseDialog);
}

/* The lines of a command's output, for Choose(). */
typedef struct {
    char  *title, *prompt;
    ChooseProc proc;
    XtPointer  data;
} ChooseCmd;

static void
ChooseCmdDone(int rc, char *output, XtPointer data)
{
    ChooseCmd *c = (ChooseCmd *) data;
    char     **items = NULL, *line, *text = output;
    int        n = 0;

    while ((line = NextLine(&text)) != NULL)
    {
        while (*line == ' ' || *line == '\t')
            line++;
        if (!*line || strncmp(line, "__RC", 4) == 0)
            continue;
        items = (char **) XtRealloc((char *) items, (n + 1) * sizeof(char *));
        items[n++] = line;
    }
    if (n)
        Choose(c->title, c->prompt, items, n, c->proc, c->data);
    else
        ErrDialog(MSG(152, "Nothing to choose from."), style.shell);
    XtFree((char *) items);
    XtFree((char *) c);
}

static void
ChooseFromCommand(char **argv, char *title, char *prompt, ChooseProc proc,
                  XtPointer data)
{
    ChooseCmd *c = (ChooseCmd *) XtCalloc(1, sizeof(ChooseCmd));

    c->title = title;
    c->prompt = prompt;
    c->proc = proc;
    c->data = data;
    if (!SysCmdRun(argv, NULL, 30, ChooseCmdDone, (XtPointer) c))
    {
        XtFree((char *) c);
        ErrDialog(MSG(151, "The program for this is not installed."),
                  style.shell);
    }
}

/* Show a page, created by 'make' the first time. */
static void
ShowPanel(Panel **pp, Panel *(*make)(Widget))
{
    if (!*pp)
        *pp = (*make)(style.shell);
    if (XtIsManaged((*pp)->dialog))
    {
        if ((*pp)->refresh)
            (*(*pp)->refresh)(*pp);
    }
    else
        XtManageChild((*pp)->dialog);
    XMapRaised(style.display, XtWindow(XtParent((*pp)->dialog)));
}

/* ------------------------------------------------------------------ */
/* Search: what the Front Panel's Search button finds (search.c)       */
/* ------------------------------------------------------------------ */

static Panel *srPanel;

static const struct { const char *name, *url; } engines[] = {
    { "DuckDuckGo", "https://duckduckgo.com/?q=%s" },
    { "Google", "https://www.google.com/search?q=%s" },
    { "Bing", "https://www.bing.com/search?q=%s" },
    { "Startpage", "https://www.startpage.com/do/search?query=%s" },
    { "Brave Search", "https://search.brave.com/search?q=%s" },
    { "Ecosia", "https://www.ecosia.org/search?q=%s" },
    { "Qwant", "https://www.qwant.com/?q=%s" },
    { "Wikipedia", "https://en.wikipedia.org/w/index.php?search=%s" },
};
#define NENGINES ((int) (sizeof(engines) / sizeof(engines[0])))
static const char *fileTools[] = { "auto", "baloo", "plocate", "locate", "find" };
#define NFILETOOLS 5

static Boolean
SrWrite(void)
{
    char  path[1100], tmp[1100];
    FILE *fp;
    SearchConfig *c = &searchConfig;

    snprintf(path, sizeof(path), "%s/.dt/search", style.home);
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    if ((fp = fopen(tmp, "w")) == NULL)
        return False;
    fprintf(fp, "# Settings > Search: what the Front Panel's Search finds\n"
            "apps=%d\nsettings=%d\nwindows=%d\nrecent=%d\nfiles=%d\n"
            "filesEverywhere=%d\ncalc=%d\nweb=%d\nrun=%d\n"
            "maxApps=%d\nmaxFiles=%d\nengine=%s\nengineName=%s\n"
            "fileTool=%s\n",
            c->apps, c->settings, c->windows, c->recent, c->files,
            c->filesEverywhere, c->calc, c->web, c->run, c->maxApps,
            c->maxFiles, c->engine, c->engineName, c->fileTool);
    if (fclose(fp) != 0)
        return False;
    return rename(tmp, path) == 0;
}

static const char *
SrToolLabel(const char *t)
{
    if (strcmp(t, "baloo") == 0) return "Baloo (KDE's file index)";
    if (strcmp(t, "plocate") == 0) return "plocate";
    if (strcmp(t, "locate") == 0) return "locate";
    if (strcmp(t, "find") == 0) return MSG(153, "Look through the home folder");
    return MSG(123, "Automatic");
}

static void
SrRefresh(Panel *p)
{
    SearchConfig *c = &searchConfig;
    char num[16];

    SearchReadConfig();
    PanelClearItems(p);
    CheckRow(p, c->apps, MSG(154, "Applications"), "apps");
    CheckRow(p, c->settings, MSG(155, "Settings pages"), "settings");
    CheckRow(p, c->windows, MSG(156, "Open windows"), "windows");
    CheckRow(p, c->recent, MSG(157, "Recently used files"), "recent");
    CheckRow(p, c->files, MSG(158, "Files and folders, by name"), "files");
    CheckRow(p, c->filesEverywhere,
             MSG(159, "Files outside the home folder too"), "filesEverywhere");
    CheckRow(p, c->calc, MSG(160, "Calculator (type 2*(3+4))"), "calc");
    CheckRow(p, c->web, MSG(161, "Web search"), "web");
    CheckRow(p, c->run, MSG(162, "Run commands"), "run");
    ValueRow(p, MSG(163, "Web search engine"), c->engineName, "engine");
    ValueRow(p, MSG(164, "Find files with"), SrToolLabel(c->fileTool),
             "fileTool");
    snprintf(num, sizeof(num), "%d", c->maxApps);
    ValueRow(p, MSG(165, "Applications shown"), num, "maxApps");
    snprintf(num, sizeof(num), "%d", c->maxFiles);
    ValueRow(p, MSG(166, "Files shown"), num, "maxFiles");
    PanelSetStatus(p, MSG(167,
        "What Search (the Front Panel's magnifier) looks through:"));
}

static void
SrChangeCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p), i;
    const char *k;
    SearchConfig *c = &searchConfig;

    if (row < 0)
        return;
    k = p->keys[row];
#define T(n) if (strcmp(k, #n) == 0) c->n = !c->n; else
    T(apps) T(settings) T(windows) T(recent) T(files) T(filesEverywhere)
    T(calc) T(web) T(run)
#undef T
    if (strcmp(k, "engine") == 0)
    {
        for (i = 0; i < NENGINES; i++)
            if (strcmp(engines[i].name, c->engineName) == 0)
                break;
        i = (i + 1) % NENGINES;
        snprintf(c->engine, sizeof(c->engine), "%s", engines[i].url);
        snprintf(c->engineName, sizeof(c->engineName), "%s", engines[i].name);
    }
    else if (strcmp(k, "fileTool") == 0)
    {
        for (i = 0; i < NFILETOOLS; i++)
            if (strcmp(fileTools[i], c->fileTool) == 0)
                break;
        snprintf(c->fileTool, sizeof(c->fileTool), "%s",
                 fileTools[(i + 1) % NFILETOOLS]);
    }
    else if (strcmp(k, "maxApps") == 0)
        c->maxApps = c->maxApps >= 20 ? 4 : c->maxApps + 4;
    else if (strcmp(k, "maxFiles") == 0)
        c->maxFiles = c->maxFiles >= 30 ? 6 : c->maxFiles + 6;
    if (!SrWrite())
        ErrDialog(MSG(150, "The change could not be made:"), style.shell);
    SrRefresh(p);
    Reselect(p, row);
}

static Panel *
SrMake(Widget shell)
{
    Panel *p = PanelCreate(shell, "searchDialog", MSG(168, "Settings - Search"),
                           NULL);

    p->refresh = SrRefresh;
    XtVaSetValues(p->list, XmNvisibleItemCount, 13, NULL);
    PanelButton(p, MSG(87, "Change"), SrChangeCB);
    XtAddCallback(p->list, XmNdefaultActionCallback, SrChangeCB, (XtPointer) p);
    return p;
}

void
popup_searchBB(Widget shell)
{
    ShowPanel(&srPanel, SrMake);
}

/* ------------------------------------------------------------------ */
/* Displays: the monitors (xrandr; dtdisplays remembers layouts)       */
/* ------------------------------------------------------------------ */

static Panel *dpPanel;

static void
DpListDone(int rc, char *output, XtPointer data)
{
    Panel *p = (Panel *) data;
    char  *line, *text = output, label[400], name[64];
    int    found = 0;

    PanelClearItems(p);
    while ((line = NextLine(&text)) != NULL)
    {
        char geo[64] = "", *s;
        Boolean primary;

        if (line[0] == ' ' || !strstr(line, " connected"))
            continue;
        if (sscanf(line, "%63s", name) != 1)
            continue;
        primary = strstr(line, " primary ") != NULL;
        for (s = strstr(line, " connected") + 10; *s == ' '; s++)
            ;
        if (strncmp(s, "primary ", 8) == 0)
            s += 8;
        if (isdigit((unsigned char) *s))
            sscanf(s, "%63s", geo);
        snprintf(label, sizeof(label), "%s   %s%s", name,
                 geo[0] ? geo : MSG(169, "off"),
                 primary ? MSG(170, "   (primary)") : "");
        PanelAddItem(p, label, name, "");
        found++;
    }
    PanelBusy(p, False);
    PanelSetStatus(p, found ? MSG(171,
        "Connected monitors (Windows+P switches quickly):") :
        MSG(172, "No monitor information (xrandr) is available."));
}

static void
DpRefresh(Panel *p)
{
    char *argv[] = { "xrandr", "--current", NULL };

    PanelBusy(p, True);
    if (!SysCmdRun(argv, NULL, 15, DpListDone, (XtPointer) p))
    {
        PanelBusy(p, False);
        PanelSetStatus(p, MSG(172, "No monitor information (xrandr) is available."));
    }
}

static void
DpMode(Panel *p, const char *mode)
{
    char *argv[] = { CDE_INSTALLATION_TOP "/bin/dtdisplays", (char *) mode, NULL };

    Run(p, argv);
}

static void DpExtendCB(Widget w, XtPointer c, XtPointer d) { DpMode((Panel *) c, "extend"); }
static void DpMirrorCB(Widget w, XtPointer c, XtPointer d) { DpMode((Panel *) c, "mirror"); }

static void
DpPrimaryCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p);
    char  *argv[] = { "sh", "-c",
        "xrandr --output \"$1\" --primary && \"$2\" save", "sh", NULL,
        CDE_INSTALLATION_TOP "/bin/dtdisplays", NULL };

    if (row < 0)
        return;
    argv[4] = p->keys[row];
    Run(p, argv);
}

static void
DpOnlyCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p), i, n = 0;
    char  *argv[64];

    if (row < 0)
        return;
    argv[n++] = "xrandr";
    argv[n++] = "--output"; argv[n++] = p->keys[row];
    argv[n++] = "--auto"; argv[n++] = "--primary";
    for (i = 0; i < p->nItems && n < 60; i++)
        if (i != row)
        {
            argv[n++] = "--output"; argv[n++] = p->keys[i];
            argv[n++] = "--off";
        }
    argv[n] = NULL;
    Run(p, argv);
}

static void
DpArrangeCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    if (!Launch1(CDE_INSTALLATION_TOP "/bin/dtdisplays", NULL))
        Launch1("dtdisplays", NULL);
}

static Panel *
DpMake(Widget shell)
{
    Panel *p = PanelCreate(shell, "displaysDialog",
                           MSG(173, "Settings - Displays"), NULL);

    p->refresh = DpRefresh;
    PanelButton(p, MSG(174, "Extend"), DpExtendCB);
    PanelButton(p, MSG(175, "Mirror"), DpMirrorCB);
    PanelButton(p, MSG(176, "Only This One"), DpOnlyCB);
    PanelButton(p, MSG(177, "Make Primary"), DpPrimaryCB);
    PanelButton(p, MSG(178, "Arrange..."), DpArrangeCB);
    return p;
}

void
popup_displaysBB(Widget shell)
{
    ShowPanel(&dpPanel, DpMake);
}

/* ------------------------------------------------------------------ */
/* Sound: output and input devices (PulseAudio / PipeWire's pactl)     */
/* ------------------------------------------------------------------ */

static Panel *snPanel;
static char   snDefSink[256], snDefSource[256];

/* "Name:" / "Description:" pairs of "pactl list sinks|sources". */
static void
SnAdd(Panel *p, char *text, const char *kind, const char *def)
{
    char *line, name[256] = "", label[600];

    while ((line = NextLine(&text)) != NULL)
    {
        while (*line == ' ' || *line == '\t')
            line++;
        if (strncmp(line, "Name: ", 6) == 0)
            snprintf(name, sizeof(name), "%s", line + 6);
        else if (strncmp(line, "Description: ", 13) == 0 && name[0] &&
                 !strstr(name, ".monitor"))
        {
            snprintf(label, sizeof(label), "%s %s: %s",
                     strcmp(name, def) == 0 ? "(*)" : "( )", kind, line + 13);
            PanelAddItem(p, label, name, kind);
            name[0] = '\0';
        }
    }
}

static void
SnSourcesDone(int rc, char *output, XtPointer data)
{
    Panel *p = (Panel *) data;

    if (rc == 0)
        SnAdd(p, output, MSG(179, "Input"), snDefSource);
    PanelBusy(p, False);
    PanelSetStatus(p, p->nItems ? MSG(180,
        "Sound devices; (*) is the one in use:") :
        MSG(181, "No sound server (PipeWire or PulseAudio) answered."));
}

static void
SnSinksDone(int rc, char *output, XtPointer data)
{
    Panel *p = (Panel *) data;
    char  *argv[] = { "pactl", "list", "sources", NULL };

    if (rc == 0)
        SnAdd(p, output, MSG(182, "Output"), snDefSink);
    if (!SysCmdRun(argv, NULL, 15, SnSourcesDone, data))
        PanelBusy(p, False);
}

static void
SnInfoDone(int rc, char *output, XtPointer data)
{
    Panel *p = (Panel *) data;
    char  *line, *text = output;
    char  *argv[] = { "pactl", "list", "sinks", NULL };

    snDefSink[0] = snDefSource[0] = '\0';
    while ((line = NextLine(&text)) != NULL)
    {
        if (strncmp(line, "Default Sink: ", 14) == 0)
            snprintf(snDefSink, sizeof(snDefSink), "%s", line + 14);
        else if (strncmp(line, "Default Source: ", 16) == 0)
            snprintf(snDefSource, sizeof(snDefSource), "%s", line + 16);
    }
    if (!SysCmdRun(argv, NULL, 15, SnSinksDone, data))
        PanelBusy(p, False);
}

static void
SnRefresh(Panel *p)
{
    char *argv[] = { "pactl", "info", NULL };

    PanelClearItems(p);
    PanelBusy(p, True);
    if (!SysCmdRun(argv, NULL, 15, SnInfoDone, (XtPointer) p))
    {
        PanelBusy(p, False);
        PanelSetStatus(p, MSG(181, "No sound server (PipeWire or PulseAudio) answered."));
    }
}

static void
SnDefaultCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p);
    char  *argv[] = { "pactl", NULL, NULL, NULL };

    if (row < 0)
        return;
    argv[1] = strcmp(p->extra[row], MSG(179, "Input")) == 0 ?
              "set-default-source" : "set-default-sink";
    argv[2] = p->keys[row];
    Run(p, argv);
}

static void
SnKeyCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    char *argv[] = { CDE_INSTALLATION_TOP "/bin/dthwkeys", NULL, NULL };

    argv[1] = (char *) client_data;
    (void) SysCmdRun(argv, NULL, 10, NULL, NULL);
}

static void
SnMixerCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Launch1(CDE_INSTALLATION_TOP "/bin/dtmixer", NULL);
}

static Panel *
SnMake(Widget shell)
{
    Panel *p = PanelCreate(shell, "soundDialog", MSG(183, "Settings - Sound"),
                           NULL);
    Widget b;

    p->refresh = SnRefresh;
    PanelButton(p, MSG(184, "Use This One"), SnDefaultCB);
    b = PanelButton(p, MSG(185, "Volume Down"), SnKeyCB);
    XtRemoveAllCallbacks(b, XmNactivateCallback);
    XtAddCallback(b, XmNactivateCallback, SnKeyCB, (XtPointer) "volume-down");
    b = PanelButton(p, MSG(186, "Volume Up"), SnKeyCB);
    XtRemoveAllCallbacks(b, XmNactivateCallback);
    XtAddCallback(b, XmNactivateCallback, SnKeyCB, (XtPointer) "volume-up");
    b = PanelButton(p, MSG(187, "Mute"), SnKeyCB);
    XtRemoveAllCallbacks(b, XmNactivateCallback);
    XtAddCallback(b, XmNactivateCallback, SnKeyCB, (XtPointer) "volume-mute");
    PanelButton(p, MSG(188, "Mixer..."), SnMixerCB);
    XtAddCallback(p->list, XmNdefaultActionCallback, SnDefaultCB, (XtPointer) p);
    return p;
}

void
popup_soundBB(Widget shell)
{
    ShowPanel(&snPanel, SnMake);
}

/* ------------------------------------------------------------------ */
/* Controllers: game controllers (joysticks, gamepads)                 */
/* ------------------------------------------------------------------ */

static Panel *ctPanel;

/* A controller's battery, when the system reports it. */
static void
CtBattery(const char *name, char *out, size_t len)
{
    DIR           *d = opendir("/sys/class/power_supply");
    struct dirent *e;

    out[0] = '\0';
    if (!d)
        return;
    while ((e = readdir(d)) != NULL)
    {
        char  path[600], model[256] = "", cap[16] = "";
        FILE *fp;
        Boolean device;

        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/scope",
                 e->d_name);
        device = False;
        if ((fp = fopen(path, "r")) != NULL)
        {
            char s[32] = "";

            if (fgets(s, sizeof(s), fp) && strncmp(s, "Device", 6) == 0)
                device = True;
            fclose(fp);
        }
        if (!device)
            continue;
        snprintf(path, sizeof(path),
                 "/sys/class/power_supply/%s/model_name", e->d_name);
        if ((fp = fopen(path, "r")) != NULL)
        {
            if (!fgets(model, sizeof(model), fp))
                model[0] = '\0';
            fclose(fp);
        }
        model[strcspn(model, "\n")] = '\0';
        snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity",
                 e->d_name);
        if ((fp = fopen(path, "r")) != NULL)
        {
            if (!fgets(cap, sizeof(cap), fp))
                cap[0] = '\0';
            fclose(fp);
        }
        cap[strcspn(cap, "\n")] = '\0';
        if (cap[0] && (strstr(e->d_name, "controller") ||
                       (model[0] && strstr(name, model))))
        {
            snprintf(out, len, "%s %s%%", MSG(189, ", battery"), cap);
            break;
        }
    }
    closedir(d);
}

static void
CtRefresh(Panel *p)
{
    FILE *fp = fopen("/proc/bus/input/devices", "r");
    char  line[512], name[256] = "", label[700], bat[64];
    int   found = 0;

    PanelClearItems(p);
    if (fp)
    {
        while (fgets(line, sizeof(line), fp))
        {
            line[strcspn(line, "\n")] = '\0';
            if (strncmp(line, "N: Name=", 8) == 0)
            {
                char *s = line + 8;
                size_t n;

                if (*s == '"')
                    s++;
                snprintf(name, sizeof(name), "%s", s);
                n = strlen(name);
                if (n && name[n - 1] == '"')
                    name[n - 1] = '\0';
            }
            else if (strncmp(line, "H: Handlers=", 12) == 0 && name[0])
            {
                char *js = strstr(line, "js");

                if (js && isdigit((unsigned char) js[2]))
                {
                    char dev[32];
                    int  num = atoi(js + 2);

                    snprintf(dev, sizeof(dev), "/dev/input/js%d", num);
                    CtBattery(name, bat, sizeof(bat));
                    snprintf(label, sizeof(label), "%s   (%s%s)", name, dev, bat);
                    PanelAddItem(p, label, dev, name);
                    found++;
                }
                name[0] = '\0';
            }
        }
        fclose(fp);
    }
    PanelSetStatus(p, found ? MSG(190, "Game controllers connected now:") :
        MSG(191, "No game controller is connected (plug one in, or pair it "
                 "under Bluetooth)."));
}

static void
CtTestCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p);
    char   cmd[300];

    if (Launch1("jstest-gtk", NULL))
        return;
    if (row < 0)
        row = 0;
    if (p->nItems == 0)
        return;
    if (SysCmdHave("jstest"))
        snprintf(cmd, sizeof(cmd), "jstest --normal '%s'", p->keys[row]);
    else if (SysCmdHave("evtest"))
        snprintf(cmd, sizeof(cmd), "evtest");
    else
    {
        ErrDialog(MSG(192, "Install jstest-gtk (or the joystick package) "
                           "to test controllers."), style.shell);
        return;
    }
    /* the device name is ours (/dev/input/jsN), safe to quote */
    SysCmdTerminal(MSG(193, "Controller Test"), cmd);
}

static void
CtMapCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    static const char *const mappers[] = { "antimicrox", "AntiMicroX",
                                           "qjoypad", "sc-controller", NULL };
    const char *m = FirstHave(mappers);

    if (!m || !Launch1(m, NULL))
        ErrDialog(MSG(194, "Install AntiMicroX to use a controller as a mouse "
                           "and keyboard, or to change its buttons."),
                  style.shell);
}

static void
CtBluetoothCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    popup_bluetoothBB(style.shell);
}

static void
CtRefreshCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    CtRefresh((Panel *) client_data);
}

static Panel *
CtMake(Widget shell)
{
    Panel *p = PanelCreate(shell, "controllersDialog",
                           MSG(195, "Settings - Controllers"), NULL);

    p->refresh = CtRefresh;
    PanelButton(p, MSG(196, "Test..."), CtTestCB);
    PanelButton(p, MSG(197, "Buttons and Mouse..."), CtMapCB);
    if (SysCmdHave("bluetoothctl"))
        PanelButton(p, MSG(198, "Pair..."), CtBluetoothCB);
    PanelButton(p, MSG(199, "Refresh"), CtRefreshCB);
    XtAddCallback(p->list, XmNdefaultActionCallback, CtTestCB, (XtPointer) p);
    return p;
}

void
popup_controllersBB(Widget shell)
{
    ShowPanel(&ctPanel, CtMake);
}

/* ------------------------------------------------------------------ */
/* Users: your account, and the system's user tool                     */
/* ------------------------------------------------------------------ */

static Panel *usPanel;

static void
UsRefresh(Panel *p)
{
    struct passwd *pw = getpwuid(getuid());
    char   full[256] = "", groups[600] = "", buf[700], face[1100];
    gid_t  list[64];
    int    n = 64, i;

    PanelClearItems(p);
    if (!pw)
        return;
    snprintf(full, sizeof(full), "%s", pw->pw_gecos ? pw->pw_gecos : "");
    full[strcspn(full, ",")] = '\0';
    ValueRow(p, MSG(200, "User name"), pw->pw_name, "name");
    ValueRow(p, MSG(201, "Full name"), full[0] ? full : "-", "full");
    ValueRow(p, MSG(202, "Home folder"), pw->pw_dir, "home");
    ValueRow(p, MSG(203, "Shell"), pw->pw_shell, "shell");
    if (getgrouplist(pw->pw_name, pw->pw_gid, list, &n) >= 0)
        for (i = 0; i < n; i++)
        {
            struct group *g = getgrgid(list[i]);

            if (g && strlen(groups) + strlen(g->gr_name) + 3 < sizeof(groups))
            {
                if (groups[0])
                    strcat(groups, ", ");
                strcat(groups, g->gr_name);
            }
        }
    ValueRow(p, MSG(204, "Groups"), groups[0] ? groups : "-", "groups");
    snprintf(face, sizeof(face), "%s/.face", pw->pw_dir);
    snprintf(buf, sizeof(buf), "%s", access(face, R_OK) == 0 ? face :
             MSG(205, "none (Change Picture... sets ~/.face)"));
    ValueRow(p, MSG(206, "Picture"), buf, "face");
    PanelSetStatus(p, MSG(207, "Your account:"));
}

static void
UsPasswordCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    SysCmdTerminal(MSG(208, "Change Password"), "passwd");
}

static void
UsNameCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    SysCmdTerminal(MSG(209, "Change Full Name"), "chfn");
}

/* ~/.face (and ~/.face.icon) from a picture: login screens and other
 * desktops show it. */
static void
UsPictureChosen(const char *file, XtPointer data)
{
    Panel *p = (Panel *) data;
    char   face[1100], icon[1100];
    char  *argv[] = { "cp", "--", (char *) file, face, NULL };
    char  *argv2[] = { "ln", "-sf", ".face", icon, NULL };

    if (access(file, R_OK) != 0)
    {
        ErrDialog(MSG(210, "That picture cannot be read."), style.shell);
        return;
    }
    snprintf(face, sizeof(face), "%s/.face", style.home);
    snprintf(icon, sizeof(icon), "%s/.face.icon", style.home);
    Run(NULL, argv);
    Run(p, argv2);
}

static void
UsPictureCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    char *argv[] = { "sh", "-c",
        "for d in \"$HOME/Pictures\" /usr/share/pixmaps/faces "
        "/usr/share/plasma/avatars /usr/share/pixmaps; do "
        "[ -d \"$d\" ] && find \"$d\" -maxdepth 2 -type f \\( -iname '*.png' "
        "-o -iname '*.jpg' -o -iname '*.jpeg' \\) 2>/dev/null | head -200; "
        "done", NULL };

    ChooseFromCommand(argv, MSG(211, "Change Picture"),
                      MSG(212, "Pictures (or type the full path of one):"),
                      UsPictureChosen, client_data);
}

static void
UsManageCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    static const char *const tools[] = { "users-admin", "kuser",
        "mate-user-admin", "lxqt-admin-user", "system-config-users",
        "gnome-control-center", NULL };
    const char *t = FirstHave(tools);

    if (t && strcmp(t, "gnome-control-center") == 0)
    {
        if (Launch1(t, "user-accounts"))
            return;
    }
    else if (t && Launch1(t, NULL))
        return;
    SysCmdTerminal(MSG(213, "Users"),
        "echo 'Accounts on this computer:'; "
        "awk -F: '$3 >= 1000 && $3 < 60000 { print \"  \" $1 \"  (\" $5 \")\" }' /etc/passwd; "
        "echo; echo 'Add one with:    sudo adduser NAME   (or: sudo useradd -m NAME)'; "
        "echo 'Remove one with: sudo deluser NAME   (or: sudo userdel NAME)'");
}

static Panel *
UsMake(Widget shell)
{
    Panel *p = PanelCreate(shell, "usersDialog", MSG(214, "Settings - Users"),
                           NULL);

    p->refresh = UsRefresh;
    PanelButton(p, MSG(215, "Change Password..."), UsPasswordCB);
    PanelButton(p, MSG(216, "Change Name..."), UsNameCB);
    PanelButton(p, MSG(217, "Change Picture..."), UsPictureCB);
    PanelButton(p, MSG(218, "Other Users..."), UsManageCB);
    return p;
}

void
popup_usersBB(Widget shell)
{
    ShowPanel(&usPanel, UsMake);
}

/* ------------------------------------------------------------------ */
/* Region: language, formats (~/.dtprofile), time zone, clock sync     */
/* ------------------------------------------------------------------ */

static Panel *rgPanel;
static char   rgZone[128], rgNtp[16];

/* Set or remove "NAME=value; export NAME" in ~/.dt/region, which
 * Xsession sources at login (0070.dtmodern). */
static Boolean
RgSet(const char *name, const char *value)
{
    char   path[1100], tmp[1100], line[512];
    FILE  *in, *out;
    size_t n = strlen(name);

    snprintf(path, sizeof(path), "%s/.dt/region", style.home);
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    if ((out = fopen(tmp, "w")) == NULL)
        return False;
    if ((in = fopen(path, "r")) != NULL)
    {
        while (fgets(line, sizeof(line), in))
            if (!(strncmp(line, name, n) == 0 && line[n] == '='))
                fputs(line, out);
        fclose(in);
    }
    else
        fputs("# Settings > Region: read at login. Changes here are kept.\n", out);
    if (value && *value)
    {
        /* locale names: letters, digits, _ . @ - only */
        const char *c;

        for (c = value; *c; c++)
            if (!isalnum((unsigned char) *c) && !strchr("_.@-", *c))
            {
                fclose(out);
                unlink(tmp);
                return False;
            }
        fprintf(out, "%s=%s; export %s\n", name, value, name);
    }
    if (fclose(out) != 0)
        return False;
    return rename(tmp, path) == 0;
}

static char *
RgRead(const char *name, char *out, size_t len)
{
    char  path[1100], line[512];
    FILE *fp;
    size_t n = strlen(name);

    out[0] = '\0';
    snprintf(path, sizeof(path), "%s/.dt/region", style.home);
    if ((fp = fopen(path, "r")) != NULL)
    {
        while (fgets(line, sizeof(line), fp))
            if (strncmp(line, name, n) == 0 && line[n] == '=')
            {
                snprintf(out, len, "%s", line + n + 1);
                out[strcspn(out, ";\n")] = '\0';
            }
        fclose(fp);
    }
    return out;
}

static void
RgTimeDone(int rc, char *output, XtPointer data)
{
    Panel *p = (Panel *) data;
    char  *line, *text = output, lang[128], fmt[128], v[300];
    const char *cur;

    rgZone[0] = rgNtp[0] = '\0';
    while (rc == 0 && (line = NextLine(&text)) != NULL)
    {
        if (strncmp(line, "Timezone=", 9) == 0)
            snprintf(rgZone, sizeof(rgZone), "%s", line + 9);
        else if (strncmp(line, "NTP=", 4) == 0)
            snprintf(rgNtp, sizeof(rgNtp), "%s", line + 4);
    }
    if (!rgZone[0])
    {
        /* no timedatectl: /etc/localtime's link, or TZ */
        char lt[512];
        ssize_t k = readlink("/etc/localtime", lt, sizeof(lt) - 1);

        if (k > 0)
        {
            lt[k] = '\0';
            snprintf(rgZone, sizeof(rgZone), "%s",
                     strstr(lt, "zoneinfo/") ? strstr(lt, "zoneinfo/") + 9 : lt);
        }
        else if (getenv("TZ"))
            snprintf(rgZone, sizeof(rgZone), "%s", getenv("TZ"));
    }
    PanelClearItems(p);
    RgRead("LANG", lang, sizeof(lang));
    cur = getenv("LANG");
    snprintf(v, sizeof(v), "%s%s", lang[0] ? lang : (cur ? cur : "C"),
             lang[0] ? MSG(219, "  (from the next login)") : "");
    ValueRow(p, MSG(220, "Language"), v, "LANG");
    RgRead("LC_TIME", fmt, sizeof(fmt));
    snprintf(v, sizeof(v), "%s", fmt[0] ? fmt : MSG(221, "as the language"));
    ValueRow(p, MSG(222, "Dates and times"), v, "LC_TIME");
    RgRead("LC_NUMERIC", fmt, sizeof(fmt));
    snprintf(v, sizeof(v), "%s", fmt[0] ? fmt : MSG(221, "as the language"));
    ValueRow(p, MSG(223, "Numbers"), v, "LC_NUMERIC");
    RgRead("LC_MONETARY", fmt, sizeof(fmt));
    snprintf(v, sizeof(v), "%s", fmt[0] ? fmt : MSG(221, "as the language"));
    ValueRow(p, MSG(224, "Currency"), v, "LC_MONETARY");
    RgRead("LC_PAPER", fmt, sizeof(fmt));
    snprintf(v, sizeof(v), "%s", fmt[0] ? fmt : MSG(221, "as the language"));
    ValueRow(p, MSG(225, "Paper size"), v, "LC_PAPER");
    ValueRow(p, MSG(226, "Time zone"), rgZone[0] ? rgZone : "?", "zone");
    if (rgNtp[0])
        ValueRow(p, MSG(227, "Set the clock from the Internet"),
                 strcmp(rgNtp, "yes") == 0 ? MSG(228, "yes") : MSG(229, "no"),
                 "ntp");
    PanelBusy(p, False);
    PanelSetStatus(p, MSG(230, "Language, formats and time:"));
}

static void
RgRefresh(Panel *p)
{
    char *argv[] = { "timedatectl", "show", NULL };

    PanelBusy(p, True);
    if (!SysCmdRun(argv, NULL, 15, RgTimeDone, (XtPointer) p))
        RgTimeDone(1, "", (XtPointer) p);
}

static char rgChoosing[32];

static void
RgLocaleChosen(const char *choice, XtPointer data)
{
    Panel *p = (Panel *) data;
    const char *v = choice;

    if (strcmp(choice, MSG(221, "as the language")) == 0)
        v = NULL;
    if (!RgSet(rgChoosing, v))
        ErrDialog(MSG(150, "The change could not be made:"), style.shell);
    RgRefresh(p);
}

static void
RgZoneChosen(const char *choice, XtPointer data)
{
    char *argv[] = { "timedatectl", "set-timezone", (char *) choice, NULL };

    Run((Panel *) data, argv);
}

static void
RgChangeCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p);
    const char *k;

    if (row < 0)
        return;
    k = p->keys[row];
    if (strcmp(k, "zone") == 0)
    {
        char *argv[] = { "timedatectl", "list-timezones", NULL };

        ChooseFromCommand(argv, MSG(231, "Time Zone"),
                          MSG(232, "Time zones (type to narrow):"),
                          RgZoneChosen, (XtPointer) p);
    }
    else if (strcmp(k, "ntp") == 0)
    {
        char *argv[] = { "timedatectl", "set-ntp",
                         strcmp(rgNtp, "yes") == 0 ? "false" : "true", NULL };

        Run(p, argv);
    }
    else
    {
        /* the installed locales, UTF-8 ones */
        char *argv[] = { "sh", "-c",
            "locale -a 2>/dev/null | grep -i -E 'utf-?8' | "
            "sed 's/\\.utf8$/.UTF-8/; s/\\.utf-8$/.UTF-8/' | sort -u", NULL };

        snprintf(rgChoosing, sizeof(rgChoosing), "%s", k);
        ChooseFromCommand(argv, MSG(233, "Language and Formats"),
            MSG(234, "Installed languages (add more with your system's "
                     "locale tools):"), RgLocaleChosen, (XtPointer) p);
    }
}

static void
RgDefaultCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p);

    if (row < 0 || p->keys[row][0] != 'L')
        return;
    RgSet(p->keys[row], NULL);
    RgRefresh(p);
}

static Panel *
RgMake(Widget shell)
{
    Panel *p = PanelCreate(shell, "regionDialog", MSG(235, "Settings - Region"),
                           NULL);

    p->refresh = RgRefresh;
    PanelButton(p, MSG(64, "Change..."), RgChangeCB);
    PanelButton(p, MSG(68, "Reset"), RgDefaultCB);
    XtAddCallback(p->list, XmNdefaultActionCallback, RgChangeCB, (XtPointer) p);
    return p;
}

void
popup_regionBB(Widget shell)
{
    ShowPanel(&rgPanel, RgMake);
}

/* ------------------------------------------------------------------ */
/* Input: keyboard layouts and options (setxkbmap; ~/.dt/keyboard)     */
/* ------------------------------------------------------------------ */

static Panel *inPanel;

typedef struct {
    char layouts[128], variants[128], options[256];
} Kbd;

static Kbd inKbd;

static void
InWrite(void)
{
    char  path[1100], tmp[1100];
    FILE *fp;

    snprintf(path, sizeof(path), "%s/.dt/keyboard", style.home);
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    if ((fp = fopen(tmp, "w")) == NULL)
        return;
    fprintf(fp, "# Settings > Input: applied at login with setxkbmap.\n"
            "layout=%s\nvariant=%s\noptions=%s\n",
            inKbd.layouts, inKbd.variants, inKbd.options);
    if (fclose(fp) == 0)
        rename(tmp, path);
}

static void
InApply(Panel *p)
{
    /* -option "" clears, then the chosen ones */
    char *argv[12];
    int   n = 0;

    argv[n++] = "setxkbmap";
    argv[n++] = "-layout"; argv[n++] = inKbd.layouts[0] ? inKbd.layouts : "us";
    argv[n++] = "-variant"; argv[n++] = inKbd.variants;
    argv[n++] = "-option"; argv[n++] = "";
    if (inKbd.options[0])
    {
        argv[n++] = "-option";
        argv[n++] = inKbd.options;
    }
    argv[n] = NULL;
    InWrite();
    Run(p, argv);
}

static void
InQueryDone(int rc, char *output, XtPointer data)
{
    Panel *p = (Panel *) data;
    char  *line, *text = output, *l, *v, *ls = NULL, *vs = NULL, buf[300];
    int    i = 0;

    memset(&inKbd, 0, sizeof(inKbd));
    while ((line = NextLine(&text)) != NULL)
    {
        if (strncmp(line, "layout:", 7) == 0)
            snprintf(inKbd.layouts, sizeof(inKbd.layouts), "%s", line + 7 + strspn(line + 7, " "));
        else if (strncmp(line, "variant:", 8) == 0)
            snprintf(inKbd.variants, sizeof(inKbd.variants), "%s", line + 8 + strspn(line + 8, " "));
        else if (strncmp(line, "options:", 8) == 0)
            snprintf(inKbd.options, sizeof(inKbd.options), "%s", line + 8 + strspn(line + 8, " "));
    }
    PanelClearItems(p);
    {
        char lay[128], var[128];

        snprintf(lay, sizeof(lay), "%s", inKbd.layouts);
        snprintf(var, sizeof(var), "%s", inKbd.variants);
        for (l = strtok_r(lay, ",", &ls), v = strtok_r(var, ",", &vs); l;
             l = strtok_r(NULL, ",", &ls), v = v ? strtok_r(NULL, ",", &vs) : NULL)
        {
            char key[16];

            snprintf(buf, sizeof(buf), "%s %d: %s%s%s%s", MSG(236, "Layout"),
                     i + 1, l, v && *v ? " (" : "", v && *v ? v : "",
                     v && *v ? ")" : "");
            snprintf(key, sizeof(key), "L%d", i);
            PanelAddItem(p, buf, key, l);
            i++;
        }
    }
    ValueRow(p, MSG(237, "Switch layouts with"),
             strstr(inKbd.options, "grp:win_space_toggle") ? "Windows+Space" :
             strstr(inKbd.options, "grp:alt_shift_toggle") ? "Alt+Shift" :
             strstr(inKbd.options, "grp:ctrl_shift_toggle") ? "Ctrl+Shift" :
             MSG(238, "(no keys)"), "grp");
    ValueRow(p, MSG(239, "Caps Lock"),
             strstr(inKbd.options, "ctrl:nocaps") ? MSG(240, "another Ctrl") :
             strstr(inKbd.options, "caps:escape") ? MSG(241, "Escape") :
             MSG(242, "Caps Lock"), "caps");
    ValueRow(p, MSG(243, "Compose key (accents: Compose, then ' and e)"),
             strstr(inKbd.options, "compose:ralt") ? MSG(244, "Right Alt") :
             strstr(inKbd.options, "compose:menu") ? MSG(245, "Menu") :
             MSG(246, "none"), "compose");
    PanelBusy(p, False);
    PanelSetStatus(p, MSG(247, "Keyboard layouts (the first is the usual one):"));
}

static void
InRefresh(Panel *p)
{
    char *argv[] = { "setxkbmap", "-query", NULL };

    PanelBusy(p, True);
    if (!SysCmdRun(argv, NULL, 15, InQueryDone, (XtPointer) p))
    {
        PanelBusy(p, False);
        PanelSetStatus(p, MSG(248, "setxkbmap is not installed."));
    }
}

/* Replace the xkb options of one group ("grp:", "ctrl:nocaps"...) */
static void
InOption(const char *prefixes, const char *value)
{
    char  out[256] = "", opts[256], *o, *save = NULL;

    snprintf(opts, sizeof(opts), "%s", inKbd.options);
    for (o = strtok_r(opts, ",", &save); o; o = strtok_r(NULL, ",", &save))
    {
        char pre[64];
        const char *p = prefixes;
        Boolean drop = False;

        while (*p)
        {
            size_t n = strcspn(p, " ");

            snprintf(pre, sizeof(pre), "%.*s", (int) n, p);
            if (strncmp(o, pre, strlen(pre)) == 0)
                drop = True;
            p += n;
            while (*p == ' ')
                p++;
        }
        if (!drop && strlen(out) + strlen(o) + 2 < sizeof(out))
        {
            if (out[0])
                strcat(out, ",");
            strcat(out, o);
        }
    }
    if (value && *value && strlen(out) + strlen(value) + 2 < sizeof(out))
    {
        if (out[0])
            strcat(out, ",");
        strcat(out, value);
    }
    snprintf(inKbd.options, sizeof(inKbd.options), "%s", out);
}

static void
InLayoutChosen(const char *choice, XtPointer data)
{
    char code[64];
    const char *c;

    /* "es  Spanish" -> es */
    sscanf(choice, "%63s", code);
    for (c = code; *c; c++)
        if (!isalnum((unsigned char) *c) && *c != '_' && *c != '-')
            return;
    if (strlen(inKbd.layouts) + strlen(code) + 2 >= sizeof(inKbd.layouts) ||
        strlen(inKbd.variants) + 2 >= sizeof(inKbd.variants))
        return;
    if (inKbd.layouts[0])
    {
        strcat(inKbd.layouts, ",");
        strcat(inKbd.variants, ",");
    }
    strcat(inKbd.layouts, code);
    /* a second layout needs keys to switch */
    if (strchr(inKbd.layouts, ',') && !strstr(inKbd.options, "grp:"))
        InOption("grp:", "grp:win_space_toggle");
    InApply((Panel *) data);
}

static void
InAddCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    char *argv[] = { "sh", "-c",
        "for f in /usr/share/X11/xkb/rules/evdev.lst "
        "/usr/X11R6/lib/X11/xkb/rules/xorg.lst "
        "/usr/local/share/X11/xkb/rules/evdev.lst; do "
        "[ -r \"$f\" ] && { sed -n '/^! layout/,/^!/p' \"$f\" | "
        "grep -v '^!'; break; }; done", NULL };

    ChooseFromCommand(argv, MSG(249, "Add Layout"),
                      MSG(250, "Keyboard layouts (type to narrow):"),
                      InLayoutChosen, client_data);
}

static void
InRemoveCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p), idx, i, nl = 0, nv = 0, kept = 0;
    char   lay[128], var[128], outL[128] = "", outV[128] = "";
    char  *ls[16], *vs[16], *c;

    if (row < 0 || p->keys[row][0] != 'L')
        return;
    idx = atoi(p->keys[row] + 1);
    if (!strchr(inKbd.layouts, ','))
    {
        ErrDialog(MSG(251, "The last layout cannot be removed."), style.shell);
        return;
    }
    /* split at commas, keeping empty fields (variants may be empty) */
    snprintf(lay, sizeof(lay), "%s", inKbd.layouts);
    snprintf(var, sizeof(var), "%s", inKbd.variants);
    ls[nl++] = lay;
    for (c = lay; *c && nl < 16; c++)
        if (*c == ',') { *c = '\0'; ls[nl++] = c + 1; }
    vs[nv++] = var;
    for (c = var; *c && nv < 16; c++)
        if (*c == ',') { *c = '\0'; vs[nv++] = c + 1; }
    for (i = 0; i < nl; i++)
    {
        if (i == idx)
            continue;
        if (kept++)
        {
            strcat(outL, ",");
            strcat(outV, ",");
        }
        strcat(outL, ls[i]);
        strcat(outV, i < nv ? vs[i] : "");
    }
    snprintf(inKbd.layouts, sizeof(inKbd.layouts), "%s", outL);
    snprintf(inKbd.variants, sizeof(inKbd.variants), "%s", outV);
    InApply(p);
}

static void
InChangeCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p);
    const char *k;

    if (row < 0)
        return;
    k = p->keys[row];
    if (strcmp(k, "grp") == 0)
        InOption("grp:", strstr(inKbd.options, "grp:win_space_toggle") ?
                 "grp:alt_shift_toggle" :
                 strstr(inKbd.options, "grp:alt_shift_toggle") ?
                 "grp:ctrl_shift_toggle" :
                 strstr(inKbd.options, "grp:ctrl_shift_toggle") ? "" :
                 "grp:win_space_toggle");
    else if (strcmp(k, "caps") == 0)
        InOption("ctrl:nocaps caps:", strstr(inKbd.options, "ctrl:nocaps") ?
                 "caps:escape" : strstr(inKbd.options, "caps:escape") ? "" :
                 "ctrl:nocaps");
    else if (strcmp(k, "compose") == 0)
        InOption("compose:", strstr(inKbd.options, "compose:ralt") ?
                 "compose:menu" : strstr(inKbd.options, "compose:menu") ? "" :
                 "compose:ralt");
    else
    {
        InAddCB(w, client_data, call_data);
        return;
    }
    InApply(p);
    Reselect(p, row);
}

static Panel *
InMake(Widget shell)
{
    Panel *p = PanelCreate(shell, "inputDialog", MSG(252, "Settings - Input"),
                           NULL);

    p->refresh = InRefresh;
    PanelButton(p, MSG(253, "Add Layout..."), InAddCB);
    PanelButton(p, MSG(59, "Remove"), InRemoveCB);
    PanelButton(p, MSG(87, "Change"), InChangeCB);
    XtAddCallback(p->list, XmNdefaultActionCallback, InChangeCB, (XtPointer) p);
    return p;
}

void
popup_inputBB(Widget shell)
{
    ShowPanel(&inPanel, InMake);
}

/* ------------------------------------------------------------------ */
/* Removable Media: USB drives, discs (dtmedia; ~/.dt/media)           */
/* ------------------------------------------------------------------ */

static Panel *mdPanel;

static Boolean
MdGet(const char *key, Boolean def)
{
    char  path[1100], line[128];
    FILE *fp;
    size_t n = strlen(key);
    Boolean v = def;

    snprintf(path, sizeof(path), "%s/.dt/media", style.home);
    if ((fp = fopen(path, "r")) == NULL)
        return def;
    while (fgets(line, sizeof(line), fp))
        if (strncmp(line, key, n) == 0 && line[n] == '=')
            v = line[n + 1] == '1';
    fclose(fp);
    return v;
}

static void
MdWrite(Boolean mount, Boolean open, Boolean notify)
{
    char  path[1100], tmp[1100];
    FILE *fp;

    snprintf(path, sizeof(path), "%s/.dt/media", style.home);
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    if ((fp = fopen(tmp, "w")) == NULL)
        return;
    fprintf(fp, "# Settings > Removable Media (dtmedia)\n"
            "mount=%d\nopen=%d\nnotify=%d\n", mount, open, notify);
    if (fclose(fp) == 0)
        rename(tmp, path);
}

static void
MdRefresh(Panel *p)
{
    PanelClearItems(p);
    CheckRow(p, MdGet("mount", True),
             MSG(254, "Mount USB drives, memory cards and discs when inserted"),
             "mount");
    CheckRow(p, MdGet("open", True),
             MSG(255, "Open a File Manager window for them"), "open");
    CheckRow(p, MdGet("notify", True),
             MSG(256, "Say so in a notification (and when it is safe to "
                      "unplug)"), "notify");
    PanelAddItem(p, MSG(257, "[ ] Run programs found on them (autorun): "
                             "never, for your safety"), "autorun", "");
    PanelSetStatus(p, SysCmdHave("udisksctl") ? MSG(258,
        "When a drive is plugged in (changes apply at once):") :
        MSG(259, "udisks2 (udisksctl) is needed to mount drives."));
}

static void
MdChangeCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p);
    Boolean m = MdGet("mount", True), o = MdGet("open", True),
            n = MdGet("notify", True);
    char  *argv[] = { CDE_INSTALLATION_TOP "/bin/dtmedia", "restart", NULL };

    if (row < 0)
        return;
    if (strcmp(p->keys[row], "mount") == 0) m = !m;
    else if (strcmp(p->keys[row], "open") == 0) o = !o;
    else if (strcmp(p->keys[row], "notify") == 0) n = !n;
    else
        return;
    MdWrite(m, o, n);
    (void) SysCmdRun(argv, NULL, 10, NULL, NULL);
    MdRefresh(p);
    Reselect(p, row);
}

static Panel *
MdMake(Widget shell)
{
    Panel *p = PanelCreate(shell, "mediaDialog",
                           MSG(260, "Settings - Removable Media"), NULL);

    p->refresh = MdRefresh;
    PanelButton(p, MSG(87, "Change"), MdChangeCB);
    XtAddCallback(p->list, XmNdefaultActionCallback, MdChangeCB, (XtPointer) p);
    return p;
}

void
popup_mediaBB(Widget shell)
{
    ShowPanel(&mdPanel, MdMake);
}

/* ------------------------------------------------------------------ */
/* Window Borders: title bar buttons, border width (dtwm resources,    */
/* xfwm4's settings)                                                   */
/* ------------------------------------------------------------------ */

static Panel *wbPanel;

/* ~/.dt/window-borders: "buttons=all|nominimize|nomaximize|close" and
 * "border=thin|normal|thick" (what the page shows); dtwm gets them as
 * resources, xfwm4 its button layout through xfconf. */
static void
WbGet(char *buttons, size_t bl, char *border, size_t brl)
{
    char  path[1100], line[128];
    FILE *fp;

    snprintf(buttons, bl, "all");
    snprintf(border, brl, "normal");
    snprintf(path, sizeof(path), "%s/.dt/window-borders", style.home);
    if ((fp = fopen(path, "r")) == NULL)
        return;
    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\n")] = '\0';
        if (strncmp(line, "buttons=", 8) == 0)
            snprintf(buttons, bl, "%s", line + 8);
        else if (strncmp(line, "border=", 7) == 0)
            snprintf(border, brl, "%s", line + 7);
    }
    fclose(fp);
}

static void
WbApply(Panel *p, const char *buttons, const char *border)
{
    char  path[1100], tmp[1100], res[300];
    FILE *fp;
    const char *deco, *xfwm;
    int   width;

    snprintf(path, sizeof(path), "%s/.dt/window-borders", style.home);
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    if ((fp = fopen(tmp, "w")) == NULL)
        return;
    fprintf(fp, "buttons=%s\nborder=%s\n", buttons, border);
    if (fclose(fp) != 0 || rename(tmp, path) != 0)
        return;

    /* dtwm: resources, kept with the session as Settings > Window's */
    deco = strcmp(buttons, "nominimize") == 0 ? "-minimize" :
           strcmp(buttons, "nomaximize") == 0 ? "-maximize" :
           strcmp(buttons, "close") == 0 ? "-minimize -maximize" : "all";
    width = strcmp(border, "thin") == 0 ? 2 :
            strcmp(border, "thick") == 0 ? 9 : 5;
    snprintf(res, sizeof(res), "Dtwm*clientDecoration: %s\n"
             "Dtwm*FrontPanel*clientDecoration: border\n"
             "Dtwm*resizeBorderWidth: %d\n", deco, width);
    _DtAddToResource(style.display, res);

    /* xfwm4: its button layout (menu | minimize, maximize, close) */
    xfwm = strcmp(buttons, "nominimize") == 0 ? "O|MC" :
           strcmp(buttons, "nomaximize") == 0 ? "O|HC" :
           strcmp(buttons, "close") == 0 ? "O|C" : "O|HMC";
    if (SysCmdHave("xfconf-query"))
    {
        char *argv[] = { "xfconf-query", "-c", "xfwm4", "-p",
                         "/general/button_layout", "-s", (char *) xfwm, NULL };
        (void) SysCmdRun(argv, NULL, 10, NULL, NULL);
    }
    _DtWmRestartNoConfirm(style.display,
                          RootWindow(style.display, style.screenNum));
}

static const char *
WbButtonsLabel(const char *b)
{
    if (strcmp(b, "nominimize") == 0)
        return MSG(261, "maximize and close");
    if (strcmp(b, "nomaximize") == 0)
        return MSG(262, "minimize and close");
    if (strcmp(b, "close") == 0)
        return MSG(263, "close only");
    return MSG(264, "minimize, maximize and close");
}

static void
WbRefresh(Panel *p)
{
    char b[32], w[32];

    WbGet(b, sizeof(b), w, sizeof(w));
    PanelClearItems(p);
    ValueRow(p, MSG(265, "Title bar buttons"), WbButtonsLabel(b), "buttons");
    ValueRow(p, MSG(266, "Border width"),
             strcmp(w, "thin") == 0 ? MSG(267, "thin") :
             strcmp(w, "thick") == 0 ? MSG(268, "thick (easier to grab)") :
             MSG(269, "normal"), "border");
    PanelSetStatus(p, MSG(270, "Window frames (Settings > Window has focus "
                               "and placement):"));
}

static void
WbChangeCB(Widget wid, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p);
    char   b[32], w[32];

    if (row < 0)
        return;
    WbGet(b, sizeof(b), w, sizeof(w));
    if (strcmp(p->keys[row], "buttons") == 0)
        snprintf(b, sizeof(b), "%s",
                 strcmp(b, "all") == 0 ? "nominimize" :
                 strcmp(b, "nominimize") == 0 ? "nomaximize" :
                 strcmp(b, "nomaximize") == 0 ? "close" : "all");
    else
        snprintf(w, sizeof(w), "%s",
                 strcmp(w, "normal") == 0 ? "thick" :
                 strcmp(w, "thick") == 0 ? "thin" : "normal");
    WbApply(p, b, w);
    WbRefresh(p);
    Reselect(p, row);
}

static Panel *
WbMake(Widget shell)
{
    Panel *p = PanelCreate(shell, "bordersDialog",
                           MSG(271, "Settings - Window Borders"), NULL);

    p->refresh = WbRefresh;
    PanelButton(p, MSG(87, "Change"), WbChangeCB);
    XtAddCallback(p->list, XmNdefaultActionCallback, WbChangeCB, (XtPointer) p);
    return p;
}

void
popup_bordersBB(Widget shell)
{
    ShowPanel(&wbPanel, WbMake);
}

/* ------------------------------------------------------------------ */
/* Network page extras: VPN connections, airplane mode (nmcli)         */
/* ------------------------------------------------------------------ */

/* "NAME (on)" / "NAME (off)" rows: choosing one turns it the other way */
static void
NetVpnChosen(const char *choice, XtPointer data)
{
    char   name[256];
    size_t n;
    Boolean on;
    char  *argv[] = { "nmcli", "connection", NULL, "id", name, NULL };

    snprintf(name, sizeof(name), "%s", choice);
    n = strlen(name);
    on = n > 5 && strcmp(name + n - 5, " (on)") == 0;
    if (on)
        name[n - 5] = '\0';
    else if (n > 6 && strcmp(name + n - 6, " (off)") == 0)
        name[n - 6] = '\0';
    argv[2] = on ? "down" : "up";
    Run((Panel *) data, argv);
}

static void
NetVpnListDone(int rc, char *output, XtPointer data)
{
    char  *line, *text = output, **items = NULL, *f[4];
    int    n = 0, i;

    while ((line = NextLine(&text)) != NULL)
    {
        char row[300];

        /* NAME:TYPE:DEVICE (terse, ':' escaped) */
        if (SysCmdSplitTerse(line, f, 4) < 3)
            continue;
        if (!strstr(f[1], "vpn") && !strstr(f[1], "wireguard"))
            continue;
        snprintf(row, sizeof(row), "%s (%s)", f[0], f[2][0] ? "on" : "off");
        items = (char **) XtRealloc((char *) items, (n + 1) * sizeof(char *));
        items[n++] = XtNewString(row);
    }
    if (n)
        Choose(MSG(274, "VPN"), MSG(275, "VPN connections (choose one to "
               "turn it on or off; add them with Edit Connections...):"),
               items, n, NetVpnChosen, data);
    else
        ErrDialog(MSG(276, "No VPN connection is set up. Add one with "
                           "Edit Connections... (or import a WireGuard or "
                           "OpenVPN file with nmcli)."), style.shell);
    for (i = 0; i < n; i++)
        XtFree(items[i]);
    XtFree((char *) items);
}

static void
NetVpnCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    char *argv[] = { "nmcli", "-t", "-f", "NAME,TYPE,DEVICE", "connection",
                     "show", NULL };

    if (!SysCmdRun(argv, NULL, 20, NetVpnListDone, client_data))
        ErrDialog(MSG(151, "The program for this is not installed."),
                  style.shell);
}

static void
NetAirplaneDone(int rc, char *output, XtPointer data)
{
    /* "enabled:disabled" (Wi-Fi, mobile): any radio on -> all off,
     * all off -> on ("disabled" does not contain "enabled") */
    Boolean anyOn = output && strstr(output, "enabled") != NULL;
    char   *argv[] = { "nmcli", "radio", "all", NULL, NULL };

    argv[3] = anyOn ? "off" : "on";
    Run((Panel *) data, argv);
}

static void
NetAirplaneCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    char *argv[] = { "nmcli", "-t", "-f", "WIFI,WWAN", "radio", NULL };

    (void) SysCmdRun(argv, NULL, 20, NetAirplaneDone, client_data);
}
