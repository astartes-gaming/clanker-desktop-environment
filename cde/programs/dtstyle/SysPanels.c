/*
 * SysPanels.c - Style Manager panels for Network, Bluetooth and
 * software Updates.
 *
 * The panels drive the system's own tools, so they need no new
 * libraries and keep working as those tools evolve:
 *
 *   Network    NetworkManager's nmcli
 *   Bluetooth  BlueZ's bluetoothctl
 *   Updates    PackageKit (pkcon), apt, dnf, zypper, pacman, or the BSD
 *              package tools; installing runs in a terminal so sudo,
 *              doas or su can ask for the password there
 *
 * When a tool is missing the panel says so and stays harmless; the
 * buttons only appear in the Style Manager main window when the tool
 * is there (MainWin.c).  Commands run in the background (SysCmd.c) and
 * the Wi-Fi password never appears on a command line.
 *
 * The layout follows other desktops' settings panels (a status line
 * with an on/off switch, a list, actions underneath); no code was
 * copied from them.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/LabelG.h>
#include <Xm/List.h>
#include <Xm/PushBG.h>
#include <Xm/RowColumn.h>
#include <Xm/ToggleBG.h>
#include <Xm/SelectioB.h>
#include <Xm/TextF.h>
#include <Xm/MessageB.h>

#include <Dt/DialogBox.h>
#include <Dt/HourGlass.h>
#include <Dt/DtP.h>
#include <Dt/Wsm.h>

#include "Main.h"
#include "Help.h"
#include "SysCmd.h"
#include "SysPanels.h"

#define MSG(n, s) ((char *)GETMESSAGE(20, n, s))

#define MAX_BUTTONS 6

typedef struct _Panel Panel;
typedef void (*PanelProc)(Panel *p);

struct _Panel {
    Widget     dialog;
    Widget     status;
    Widget     toggle;
    Widget     list;
    Widget     rc;
    Widget     button[MAX_BUTTONS];
    int        nButtons;
    char     **keys;		/* per list row: SSID, address, ... */
    char     **extra;		/* per list row: security, ... */
    int        nItems;
    PanelProc  refresh;
    Boolean    mapped;
};

/* ------------------------------------------------------------------ */
/* Generic panel                                                      */
/* ------------------------------------------------------------------ */

static void
PanelSetStatus(Panel *p, const char *text)
{
    XmString s = CMPSTR((char *) text);

    XtVaSetValues(p->status, XmNlabelString, s, NULL);
    XmStringFree(s);
}

static void
PanelBusy(Panel *p, Boolean busy)
{
    int i;

    for (i = 0; i < p->nButtons; i++)
        XtSetSensitive(p->button[i], !busy);
    if (p->toggle)
        XtSetSensitive(p->toggle, !busy);
    if (busy)
        PanelSetStatus(p, MSG(1, "Working..."));
}

static void
PanelClearItems(Panel *p)
{
    int i;

    for (i = 0; i < p->nItems; i++)
    {
        XtFree(p->keys[i]);
        XtFree(p->extra[i]);
    }
    XtFree((char *) p->keys);
    XtFree((char *) p->extra);
    p->keys = p->extra = NULL;
    p->nItems = 0;
    XmListDeleteAllItems(p->list);
}

static void
PanelAddItem(Panel *p, const char *label, const char *key, const char *extra)
{
    XmString s;

    p->keys = (char **) XtRealloc((char *) p->keys,
                                  (p->nItems + 1) * sizeof(char *));
    p->extra = (char **) XtRealloc((char *) p->extra,
                                   (p->nItems + 1) * sizeof(char *));
    p->keys[p->nItems] = XtNewString(key ? key : "");
    p->extra[p->nItems] = XtNewString(extra ? extra : "");
    p->nItems++;
    s = CMPSTR((char *) label);
    XmListAddItemUnselected(p->list, s, 0);
    XmStringFree(s);
}

/* Selected row, or -1. */
static int
PanelSelected(Panel *p)
{
    int *pos = NULL, count = 0, row = -1;

    if (XmListGetSelectedPos(p->list, &pos, &count) && count > 0)
        row = pos[0] - 1;
    XtFree((char *) pos);
    return (row >= 0 && row < p->nItems) ? row : -1;
}

static void
PanelCloseCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    XtUnmanageChild(w);
}

static void
PanelMapCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;

    if (!p->mapped)
    {
        DtWsmRemoveWorkspaceFunctions(style.display, XtWindow(XtParent(w)));
        putDialog(style.shell, w);
        p->mapped = True;
    }
    if (p->refresh)
        (*p->refresh)(p);
}

static Widget
PanelButton(Panel *p, char *label, XtCallbackProc cb)
{
    XmString s = CMPSTR(label);
    Widget   b;

    b = XtVaCreateManagedWidget("button", xmPushButtonGadgetClass, p->rc,
                                XmNlabelString, s, NULL);
    XmStringFree(s);
    XtAddCallback(b, XmNactivateCallback, cb, (XtPointer) p);
    if (p->nButtons < MAX_BUTTONS)
        p->button[p->nButtons++] = b;
    return b;
}

/*
 * A dialog with a status line (and optional on/off toggle) on top, a
 * list in the middle and a row of buttons at the bottom.
 */
static Panel *
PanelCreate(Widget parent, char *name, char *title, char *toggleLabel)
{
    Panel   *p = (Panel *) XtCalloc(1, sizeof(Panel));
    Widget   form;
    XmString strings[1], s;
    Arg      args[10];
    int      n;

    strings[0] = CMPSTR((char *) _DtCloseString);
    n = 0;
    XtSetArg(args[n], XmNbuttonCount, 1); n++;
    XtSetArg(args[n], XmNbuttonLabelStrings, strings); n++;
    XtSetArg(args[n], XmNdefaultPosition, False); n++;
    p->dialog = __DtCreateDialogBoxDialog(parent, name, args, n);
    XmStringFree(strings[0]);
    XtVaSetValues(XtParent(p->dialog), XmNtitle, title,
                  XmNuseAsyncGeometry, True, NULL);
    XtAddCallback(p->dialog, XmNcallback, PanelCloseCB, NULL);
    XtAddCallback(p->dialog, XmNmapCallback, PanelMapCB, (XtPointer) p);
    XtAddCallback(p->dialog, XmNhelpCallback,
                  (XtCallbackProc) HelpRequestCB,
                  (XtPointer) HELP_MAIN_WINDOW);

    form = XtVaCreateWidget("form", xmFormWidgetClass, p->dialog,
                            XmNchildType, XmWORK_AREA,
                            XmNhorizontalSpacing, style.horizontalSpacing,
                            XmNverticalSpacing, style.verticalSpacing,
                            NULL);

    if (toggleLabel)
    {
        s = CMPSTR(toggleLabel);
        p->toggle = XtVaCreateManagedWidget("toggle",
                        xmToggleButtonGadgetClass, form,
                        XmNlabelString, s,
#ifdef XmINDICATOR_CHECK_BOX
                        XmNindicatorOn, XmINDICATOR_CHECK_BOX,
#endif
                        XmNtopAttachment, XmATTACH_FORM,
                        XmNrightAttachment, XmATTACH_FORM, NULL);
        XmStringFree(s);
    }

    s = CMPSTR(" ");
    p->status = XtVaCreateManagedWidget("status", xmLabelGadgetClass, form,
                        XmNlabelString, s,
                        XmNalignment, XmALIGNMENT_BEGINNING,
                        XmNtopAttachment, XmATTACH_FORM,
                        XmNleftAttachment, XmATTACH_FORM,
                        XmNrightAttachment,
                            p->toggle ? XmATTACH_WIDGET : XmATTACH_FORM,
                        XmNrightWidget, p->toggle, NULL);
    XmStringFree(s);

    p->rc = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, form,
                        XmNorientation, XmHORIZONTAL,
                        XmNpacking, XmPACK_TIGHT,
                        XmNbottomAttachment, XmATTACH_FORM,
                        XmNleftAttachment, XmATTACH_FORM,
                        XmNrightAttachment, XmATTACH_FORM, NULL);

    n = 0;
    XtSetArg(args[n], XmNvisibleItemCount, 10); n++;
    XtSetArg(args[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(args[n], XmNwidth, 420); n++;
    p->list = XmCreateScrolledList(form, "list", args, n);
    XtVaSetValues(XtParent(p->list),
                  XmNtopAttachment, XmATTACH_WIDGET,
                  XmNtopWidget, p->status,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM,
                  XmNbottomAttachment, XmATTACH_WIDGET,
                  XmNbottomWidget, p->rc, NULL);
    XtManageChild(p->list);
    XtManageChild(form);
    return p;
}

/* Show a command's failure output. */
static void
PanelShowFailure(Panel *p, const char *what, char *output)
{
    char *msg;
    size_t len;

    len = strlen(what) + (output ? strlen(output) : 0) + 4;
    if (len > 2000)
        len = 2000;
    msg = XtMalloc(len + 1);
    sprintf(msg, "%.200s\n\n%.1790s", what, output ? output : "");
    ErrDialog(msg, style.shell);
    XtFree(msg);
}

/* Next line of 'text' (modified in place), or NULL. */
static char *
NextLine(char **text)
{
    char *line = *text, *nl;

    if (!line || !*line)
        return NULL;
    if ((nl = strchr(line, '\n')) != NULL)
    {
        *nl = '\0';
        *text = nl + 1;
    }
    else
        *text = line + strlen(line);
    return line;
}

/* ------------------------------------------------------------------ */
/* Masked password prompt                                             */
/* ------------------------------------------------------------------ */

typedef void (*PasswordProc)(const char *password, XtPointer data);

static Widget       pwDialog = NULL;
static char         pwBuf[256];
static int          pwLen = 0;
static PasswordProc pwProc;
static XtPointer    pwData;

/* Keep the real text in pwBuf; the field only shows '*'. */
static void
PasswordVerifyCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    XmTextVerifyCallbackStruct *cb = (XmTextVerifyCallbackStruct *) call_data;
    int start = (int) cb->startPos, end = (int) cb->endPos, add, i;

    if (start < 0) start = 0;
    if (end > pwLen) end = pwLen;
    if (start > end) start = end;
    add = cb->text ? cb->text->length : 0;
    if (pwLen - (end - start) + add >= (int) sizeof(pwBuf))
    {
        cb->doit = False;
        return;
    }
    memmove(pwBuf + start + add, pwBuf + end, pwLen - end);
    if (add > 0)
    {
        memcpy(pwBuf + start, cb->text->ptr, add);
        for (i = 0; i < add; i++)
            cb->text->ptr[i] = '*';
    }
    pwLen += add - (end - start);
    pwBuf[pwLen] = '\0';
}

static void
PasswordDoneCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    XmSelectionBoxCallbackStruct *cb =
        (XmSelectionBoxCallbackStruct *) call_data;
    Widget text = XmSelectionBoxGetChild(pwDialog, XmDIALOG_TEXT);

    XtUnmanageChild(pwDialog);
    if (cb->reason == XmCR_OK && pwProc)
        (*pwProc)(pwBuf, pwData);
    memset(pwBuf, 0, sizeof(pwBuf));
    pwLen = 0;
    XmTextFieldSetString(text, "");
    pwLen = 0;
    memset(pwBuf, 0, sizeof(pwBuf));
}

static void
AskPassword(char *prompt, PasswordProc proc, XtPointer data)
{
    XmString s;
    Widget   text;

    if (!pwDialog)
    {
        pwDialog = XmCreatePromptDialog(style.shell, "passwordDialog",
                                        NULL, 0);
        XtVaSetValues(XtParent(pwDialog), XmNtitle,
                      MSG(2, "Style Manager - Password"), NULL);
        text = XmSelectionBoxGetChild(pwDialog, XmDIALOG_TEXT);
        XtAddCallback(text, XmNmodifyVerifyCallback, PasswordVerifyCB, NULL);
        XtAddCallback(pwDialog, XmNokCallback, PasswordDoneCB, NULL);
        XtAddCallback(pwDialog, XmNcancelCallback, PasswordDoneCB, NULL);
        XtUnmanageChild(XmSelectionBoxGetChild(pwDialog,
                                               XmDIALOG_HELP_BUTTON));
    }
    pwProc = proc;
    pwData = data;
    s = CMPSTR(prompt);
    XtVaSetValues(pwDialog, XmNselectionLabelString, s, NULL);
    XmStringFree(s);
    XtManageChild(pwDialog);
}

/* ------------------------------------------------------------------ */
/* Network (NetworkManager / nmcli)                                   */
/* ------------------------------------------------------------------ */

static Panel *net = NULL;
static char   netStatus[600];
static char   netSsid[256];
static char   netSecurity[64];
static char   netPwFile[512];

static void NetRefresh(Panel *p);

static void
NetListDone(int rc, char *out, XtPointer data)
{
    Panel *p = (Panel *) data;
    char  *line, *f[4], label[400];
    char  *ssid[64], *sec[64];
    int    sig[64], use[64];
    int    i, n, k = 0, s;

    /* IN-USE:SIGNAL:SECURITY:SSID, one line per access point: merge
     * them per network, keeping "in use" and the strongest signal. */
    while (k < 64 && (line = NextLine(&out)) != NULL)
    {
        n = SysCmdSplitTerse(line, f, 4);
        if (n < 4 || !*f[3])
            continue;
        s = atoi(f[1]);
        for (i = 0; i < k && strcmp(ssid[i], f[3]) != 0; i++)
            ;
        if (i == k)
        {
            ssid[k] = f[3]; sec[k] = f[2]; sig[k] = s; use[k] = 0;
            k++;
        }
        if (*f[0] == '*')
            use[i] = 1;
        if (s > sig[i])
            sig[i] = s;
    }

    PanelClearItems(p);
    for (i = 0; i < k; i++)
    {
        Boolean secured = (*sec[i] && strcmp(sec[i], "--") != 0);

        sprintf(label, "%s %3d%%  %.200s%s%.60s%s", use[i] ? "*" : " ",
                sig[i], ssid[i], secured ? "   (" : "",
                secured ? sec[i] : "", secured ? ")" : "");
        PanelAddItem(p, label, ssid[i], sec[i]);
    }
    PanelBusy(p, False);
    if (p->nItems == 0)
    {
        sprintf(label, "%.250s %.100s", netStatus,
                MSG(10, "No Wi-Fi networks found."));
        PanelSetStatus(p, label);
    }
    else
        PanelSetStatus(p, netStatus);
}

static void
NetRadioDone(int rc, char *out, XtPointer data)
{
    Panel *p = (Panel *) data;
    Boolean on = (rc == 0 && strncmp(out, "enabled", 7) == 0);
    static char *list[] = { "nmcli", "-t", "-f",
        "IN-USE,SIGNAL,SECURITY,SSID", "device", "wifi", "list",
        "--rescan", "no", NULL };

    XmToggleButtonGadgetSetState(p->toggle, on, False);
    if (on && SysCmdRun(list, NULL, 30, NetListDone, (XtPointer) p))
        return;
    PanelClearItems(p);
    PanelBusy(p, False);
    PanelSetStatus(p, netStatus);
}

static void
NetActiveDone(int rc, char *out, XtPointer data)
{
    Panel *p = (Panel *) data;
    char  *line, *f[3];
    static char *radio[] = { "nmcli", "radio", "wifi", NULL };

    if (rc != 0)
    {
        PanelClearItems(p);
        PanelBusy(p, False);
        PanelSetStatus(p, MSG(11, "NetworkManager is not running."));
        return;
    }
    netStatus[0] = '\0';
    while ((line = NextLine(&out)) != NULL)
    {
        /* NAME:TYPE:DEVICE */
        if (SysCmdSplitTerse(line, f, 3) < 3 || strcmp(f[1], "loopback") == 0)
            continue;
        if (strlen(netStatus) + strlen(f[0]) + strlen(f[2]) + 8 >=
            sizeof(netStatus))
            break;
        strcat(netStatus, netStatus[0] ? ", " : MSG(12, "Connected: "));
        strcat(netStatus, f[0]);
        strcat(netStatus, " (");
        strcat(netStatus, f[2]);
        strcat(netStatus, ")");
    }
    if (!netStatus[0])
        strcpy(netStatus, MSG(13, "Not connected."));
    if (!SysCmdRun(radio, NULL, 15, NetRadioDone, (XtPointer) p))
    {
        PanelBusy(p, False);
        PanelSetStatus(p, netStatus);
    }
}

static void
NetRefresh(Panel *p)
{
    static char *active[] = { "nmcli", "-t", "-f", "NAME,TYPE,DEVICE",
                              "connection", "show", "--active", NULL };

    if (!SysCmdHave("nmcli"))
    {
        PanelBusy(p, True);
        PanelSetStatus(p, MSG(14,
            "Network settings need NetworkManager (nmcli)."));
        return;
    }
    PanelBusy(p, True);
    if (!SysCmdRun(active, NULL, 20, NetActiveDone, (XtPointer) p))
        PanelBusy(p, False);
}

static void
NetActionDone(int rc, char *out, XtPointer data)
{
    Panel *p = (Panel *) data;

    if (netPwFile[0])
    {
        unlink(netPwFile);
        netPwFile[0] = '\0';
    }
    PanelBusy(p, False);
    if (rc != 0)
        PanelShowFailure(p, MSG(15, "The network command failed:"), out);
    NetRefresh(p);
}

static void
NetRun(Panel *p, char **argv)
{
    PanelBusy(p, True);
    if (!SysCmdRun(argv, NULL, 60, NetActionDone, (XtPointer) p))
        NetActionDone(-1, "", (XtPointer) p);
}

/* New WPA network: create the profile, then bring it up with the
 * password from a private file (never on a command line). */
static void
NetAddDone(int rc, char *out, XtPointer data)
{
    Panel *p = (Panel *) data;
    char  *up[8];

    if (rc != 0)
    {
        NetActionDone(rc, out, data);
        return;
    }
    up[0] = "nmcli"; up[1] = "connection"; up[2] = "up"; up[3] = "id";
    up[4] = netSsid; up[5] = "passwd-file"; up[6] = netPwFile; up[7] = NULL;
    if (!SysCmdRun(up, NULL, 90, NetActionDone, (XtPointer) p))
        NetActionDone(-1, "", data);
}

static void
NetPasswordGiven(const char *password, XtPointer data)
{
    Panel *p = (Panel *) data;
    char  *dir = getenv("XDG_RUNTIME_DIR");
    char  *add[14];
    FILE  *fp;
    int    fd;

    if (!dir || !*dir || strlen(dir) > 400)
        dir = "/tmp";
    sprintf(netPwFile, "%s/dtstyle-wifi.XXXXXX", dir);
    if ((fd = mkstemp(netPwFile)) < 0 || (fp = fdopen(fd, "w")) == NULL)
    {
        if (fd >= 0) close(fd);
        netPwFile[0] = '\0';
        return;
    }
    fprintf(fp, "802-11-wireless-security.psk:%s\n", password);
    fclose(fp);

    add[0] = "nmcli"; add[1] = "connection"; add[2] = "add";
    add[3] = "type"; add[4] = "wifi"; add[5] = "con-name"; add[6] = netSsid;
    add[7] = "ifname"; add[8] = "*"; add[9] = "ssid"; add[10] = netSsid;
    add[11] = "wifi-sec.key-mgmt";
    add[12] = strstr(netSecurity, "WPA3") && !strstr(netSecurity, "WPA2")
              ? "sae" : "wpa-psk";
    add[13] = NULL;
    PanelBusy(p, True);
    if (!SysCmdRun(add, NULL, 30, NetAddDone, (XtPointer) p))
        NetActionDone(-1, "", (XtPointer) p);
}

static void
NetKnownDone(int rc, char *out, XtPointer data)
{
    Panel *p = (Panel *) data;
    char  *line, *f[2], *argv[8];
    Boolean known = False;
    char   prompt[400];

    while (rc == 0 && (line = NextLine(&out)) != NULL)
        if (SysCmdSplitTerse(line, f, 2) == 2 &&
            strcmp(f[0], netSsid) == 0 && strstr(f[1], "wireless"))
            known = True;
    PanelBusy(p, False);

    if (known)
    {
        argv[0] = "nmcli"; argv[1] = "connection"; argv[2] = "up";
        argv[3] = "id"; argv[4] = netSsid; argv[5] = NULL;
        NetRun(p, argv);
    }
    else if (!netSecurity[0] || strcmp(netSecurity, "--") == 0)
    {
        argv[0] = "nmcli"; argv[1] = "device"; argv[2] = "wifi";
        argv[3] = "connect"; argv[4] = netSsid; argv[5] = NULL;
        NetRun(p, argv);
    }
    else if (strstr(netSecurity, "802.1X") || strstr(netSecurity, "WEP"))
        ErrDialog(MSG(16, "This network needs settings this panel does not\n"
                          "offer. Use Edit Connections."), style.shell);
    else
    {
        sprintf(prompt, "%.200s \"%.100s\":",
                MSG(17, "Password for"), netSsid);
        AskPassword(prompt, NetPasswordGiven, (XtPointer) p);
    }
}

static void
NetConnectCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p);
    static char *known[] = { "nmcli", "-t", "-f", "NAME,TYPE",
                             "connection", "show", NULL };

    if (row < 0)
        return;
    sprintf(netSsid, "%.250s", p->keys[row]);
    sprintf(netSecurity, "%.60s", p->extra[row]);
    PanelBusy(p, True);
    if (!SysCmdRun(known, NULL, 20, NetKnownDone, (XtPointer) p))
        PanelBusy(p, False);
}

static void
NetDisconnectDone(int rc, char *out, XtPointer data)
{
    Panel *p = (Panel *) data;
    char  *line, *f[3], dev[128], *argv[5];

    dev[0] = '\0';
    while (rc == 0 && (line = NextLine(&out)) != NULL)
        if (SysCmdSplitTerse(line, f, 3) == 3 &&
            strcmp(f[1], "wifi") == 0 && strcmp(f[2], "connected") == 0)
            sprintf(dev, "%.120s", f[0]);
    PanelBusy(p, False);
    if (!dev[0])
        return;
    argv[0] = "nmcli"; argv[1] = "device"; argv[2] = "disconnect";
    argv[3] = dev; argv[4] = NULL;
    NetRun(p, argv);
}

static void
NetDisconnectCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    static char *devs[] = { "nmcli", "-t", "-f", "DEVICE,TYPE,STATE",
                            "device", NULL };

    PanelBusy(p, True);
    if (!SysCmdRun(devs, NULL, 20, NetDisconnectDone, (XtPointer) p))
        PanelBusy(p, False);
}

static void
NetRescanDone(int rc, char *out, XtPointer data)
{
    NetRefresh((Panel *) data);
}

static void
NetRescanCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    static char *rescan[] = { "nmcli", "device", "wifi", "rescan", NULL };

    PanelBusy(p, True);
    if (!SysCmdRun(rescan, NULL, 30, NetRescanDone, (XtPointer) p))
        NetRefresh(p);
}

static void
NetEditCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    if (fork() == 0)
    {
        setsid();
        execlp("nm-connection-editor", "nm-connection-editor", (char *) NULL);
        _exit(127);
    }
}

static void
NetToggleCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    XmToggleButtonCallbackStruct *cb =
        (XmToggleButtonCallbackStruct *) call_data;
    char *argv[5];

    argv[0] = "nmcli"; argv[1] = "radio"; argv[2] = "wifi";
    argv[3] = cb->set ? "on" : "off"; argv[4] = NULL;
    NetRun(p, argv);
}

void
popup_networkBB(Widget shell)
{
    if (!net)
    {
        _DtTurnOnHourGlass(shell);
        net = PanelCreate(shell, "networkDialog",
                          MSG(3, "Style Manager - Network"), MSG(4, "Wi-Fi"));
        net->refresh = NetRefresh;
        XtAddCallback(net->toggle, XmNvalueChangedCallback, NetToggleCB,
                      (XtPointer) net);
        PanelButton(net, MSG(5, "Connect"), NetConnectCB);
        PanelButton(net, MSG(6, "Disconnect"), NetDisconnectCB);
        PanelButton(net, MSG(7, "Rescan"), NetRescanCB);
        if (SysCmdHave("nm-connection-editor"))
            PanelButton(net, MSG(8, "Edit Connections..."), NetEditCB);
        XtAddCallback(net->list, XmNdefaultActionCallback, NetConnectCB,
                      (XtPointer) net);
        _DtTurnOffHourGlass(shell);
    }
    if (XtIsManaged(net->dialog))
        NetRefresh(net);
    else
        XtManageChild(net->dialog);
    XMapRaised(style.display, XtWindow(XtParent(net->dialog)));
}

/* ------------------------------------------------------------------ */
/* Bluetooth (BlueZ / bluetoothctl)                                   */
/* ------------------------------------------------------------------ */

static Panel *bt = NULL;
static char  *btPaired = NULL;		/* bluetoothctl output, kept */
static char   btStatus[300];

static void
BtShowList(Panel *p, char *all, char *connected)
{
    char *line, label[400], *mac, *name;

    PanelClearItems(p);
    while ((line = NextLine(&all)) != NULL)
    {
        /* "Device AA:BB:CC:DD:EE:FF Name" */
        if (strncmp(line, "Device ", 7) != 0)
            continue;
        mac = line + 7;
        name = strchr(mac, ' ');
        if (!name)
            continue;
        *name++ = '\0';
        sprintf(label, "%.200s   (%s)%s", name, mac,
                connected && strstr(connected, mac)
                    ? MSG(30, "  - connected")
                    : (btPaired && strstr(btPaired, mac)
                        ? MSG(31, "  - paired") : ""));
        PanelAddItem(p, label, mac, NULL);
    }
    PanelBusy(p, False);
    PanelSetStatus(p, btStatus);
}

static char *btAll = NULL;

static void
BtConnectedDone(int rc, char *out, XtPointer data)
{
    BtShowList((Panel *) data, btAll, rc == 0 ? out : NULL);
    XtFree(btAll);
    btAll = NULL;
}

static void
BtPairedDone(int rc, char *out, XtPointer data)
{
    static char *conn[] = { "bluetoothctl", "devices", "Connected", NULL };

    XtFree(btPaired);
    btPaired = XtNewString(rc == 0 ? out : "");
    if (!SysCmdRun(conn, NULL, 15, BtConnectedDone, data))
        BtConnectedDone(-1, "", data);
}

static void
BtDevicesDone(int rc, char *out, XtPointer data)
{
    static char *paired[] = { "bluetoothctl", "devices", "Paired", NULL };

    XtFree(btAll);
    btAll = XtNewString(rc == 0 ? out : "");
    if (!SysCmdRun(paired, NULL, 15, BtPairedDone, data))
        BtPairedDone(-1, "", data);
}

static void
BtShowDone(int rc, char *out, XtPointer data)
{
    Panel *p = (Panel *) data;
    Boolean powered = (strstr(out, "Powered: yes") != NULL);
    static char *devices[] = { "bluetoothctl", "devices", NULL };

    if (rc != 0 || strstr(out, "No default controller") ||
        !strstr(out, "Controller"))
    {
        PanelClearItems(p);
        PanelBusy(p, True);
        PanelSetStatus(p, MSG(32, "No Bluetooth adapter found."));
        return;
    }
    XmToggleButtonGadgetSetState(p->toggle, powered, False);
    strcpy(btStatus, powered ? MSG(33, "Bluetooth is on.")
                             : MSG(34, "Bluetooth is off."));
    if (!SysCmdRun(devices, NULL, 15, BtDevicesDone, data))
        BtDevicesDone(-1, "", data);
}

static void
BtRefresh(Panel *p)
{
    static char *show[] = { "bluetoothctl", "show", NULL };

    if (!SysCmdHave("bluetoothctl"))
    {
        PanelBusy(p, True);
        PanelSetStatus(p, MSG(35,
            "Bluetooth settings need BlueZ (bluetoothctl)."));
        return;
    }
    PanelBusy(p, True);
    if (!SysCmdRun(show, NULL, 15, BtShowDone, (XtPointer) p))
        PanelBusy(p, False);
}

static void
BtActionDone(int rc, char *out, XtPointer data)
{
    Panel *p = (Panel *) data;

    PanelBusy(p, False);
    if (rc != 0 || strstr(out, "Failed") || strstr(out, "not available"))
        PanelShowFailure(p, MSG(36, "The Bluetooth command failed:"), out);
    BtRefresh(p);
}

/* bluetoothctl CMD [selected device] */
static void
BtRun(Panel *p, char *cmd, Boolean needDevice, int timeout)
{
    char *argv[6];
    int   row = PanelSelected(p), n = 0;

    if (needDevice && row < 0)
        return;
    argv[n++] = "bluetoothctl";
    if (strcmp(cmd, "scan") == 0)
    {
        argv[n++] = "--timeout";
        argv[n++] = "12";
        argv[n++] = "scan";
        argv[n++] = "on";
    }
    else
    {
        argv[n++] = cmd;
        if (needDevice)
            argv[n++] = p->keys[row];
    }
    argv[n] = NULL;
    PanelBusy(p, True);
    if (!SysCmdRun(argv, NULL, timeout, BtActionDone, (XtPointer) p))
        BtActionDone(-1, "", (XtPointer) p);
}

static void BtScanCB(Widget w, XtPointer c, XtPointer d)
    { BtRun((Panel *) c, "scan", False, 25); }
static void BtConnectCB(Widget w, XtPointer c, XtPointer d)
    { BtRun((Panel *) c, "connect", True, 40); }
static void BtDisconnectCB(Widget w, XtPointer c, XtPointer d)
    { BtRun((Panel *) c, "disconnect", True, 20); }
static void BtRemoveCB(Widget w, XtPointer c, XtPointer d)
    { BtRun((Panel *) c, "remove", True, 20); }

/* Pairing, then trusting so it reconnects by itself. */
static void
BtPairDone(int rc, char *out, XtPointer data)
{
    Panel *p = (Panel *) data;
    int    row = PanelSelected(p);
    char  *argv[4];

    if (rc != 0 || strstr(out, "Failed") || row < 0)
    {
        BtActionDone(rc ? rc : 1, out, data);
        return;
    }
    argv[0] = "bluetoothctl"; argv[1] = "trust"; argv[2] = p->keys[row];
    argv[3] = NULL;
    if (!SysCmdRun(argv, NULL, 15, BtActionDone, data))
        BtActionDone(-1, "", data);
}

static void
BtPairCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p);
    char  *argv[4];

    if (row < 0)
        return;
    argv[0] = "bluetoothctl"; argv[1] = "pair"; argv[2] = p->keys[row];
    argv[3] = NULL;
    PanelBusy(p, True);
    if (!SysCmdRun(argv, NULL, 40, BtPairDone, (XtPointer) p))
        BtActionDone(-1, "", (XtPointer) p);
}

static void
BtToggleCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    XmToggleButtonCallbackStruct *cb =
        (XmToggleButtonCallbackStruct *) call_data;
    char *argv[4];

    argv[0] = "bluetoothctl"; argv[1] = "power";
    argv[2] = cb->set ? "on" : "off"; argv[3] = NULL;
    PanelBusy(p, True);
    if (!SysCmdRun(argv, NULL, 15, BtActionDone, (XtPointer) p))
        BtActionDone(-1, "", (XtPointer) p);
}

void
popup_bluetoothBB(Widget shell)
{
    if (!bt)
    {
        _DtTurnOnHourGlass(shell);
        bt = PanelCreate(shell, "bluetoothDialog",
                         MSG(20, "Style Manager - Bluetooth"),
                         MSG(21, "Bluetooth"));
        bt->refresh = BtRefresh;
        XtAddCallback(bt->toggle, XmNvalueChangedCallback, BtToggleCB,
                      (XtPointer) bt);
        PanelButton(bt, MSG(22, "Scan"), BtScanCB);
        PanelButton(bt, MSG(23, "Pair"), BtPairCB);
        PanelButton(bt, MSG(24, "Connect"), BtConnectCB);
        PanelButton(bt, MSG(25, "Disconnect"), BtDisconnectCB);
        PanelButton(bt, MSG(26, "Remove"), BtRemoveCB);
        _DtTurnOffHourGlass(shell);
    }
    if (XtIsManaged(bt->dialog))
        BtRefresh(bt);
    else
        XtManageChild(bt->dialog);
    XMapRaised(style.display, XtWindow(XtParent(bt->dialog)));
}

/* ------------------------------------------------------------------ */
/* Updates                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    char *tool;		/* detected by this program */
    char *list[6];	/* lists pending updates without root, or NULL */
    char *update;	/* run in a terminal, after the root prefix */
} UpdBackend;

static UpdBackend backends[] = {
    { "pkcon",   { "pkcon", "--plain", "get-updates", NULL },
                 "pkcon update" },
    { "apt-get", { "apt", "list", "--upgradable", NULL },
                 "@apt-get update && @apt-get full-upgrade" },
    { "dnf",     { "dnf", "-q", "check-update", NULL },
                 "@dnf upgrade" },
    { "zypper",  { "zypper", "-q", "list-updates", NULL },
                 "@zypper update" },
    { "pacman",  { "checkupdates", NULL },
                 "@pacman -Syu" },
    { "pkgin",   { NULL },  "@pkgin update && @pkgin upgrade" },
    { "pkg_add", { NULL },  "@pkg_add -u && @syspatch" },
    { "pkg",     { NULL },  "@pkg upgrade" },
};

static Panel      *upd = NULL;
static UpdBackend *updBackend = NULL;

static UpdBackend *
UpdDetect(void)
{
    int i;

    for (i = 0; i < (int) XtNumber(backends); i++)
        if (SysCmdHave(backends[i].tool))
            return &backends[i];
    return NULL;
}

/* Which line of a backend's list output names an update. */
static Boolean
UpdIsItem(char *line)
{
    if (!*line || strncmp(line, "Listing", 7) == 0 ||
        strncmp(line, "Results:", 8) == 0 ||
        strncmp(line, "Transaction", 11) == 0 ||
        strncmp(line, "Status", 6) == 0 ||
        strncmp(line, "Percentage", 10) == 0 ||
        strncmp(line, "Loading", 7) == 0 ||
        strncmp(line, "There are no", 12) == 0 ||
        strncmp(line, "WARNING", 7) == 0 ||
        strncmp(line, "S |", 3) == 0 || strncmp(line, "--", 2) == 0 ||
        strncmp(line, "Last metadata", 13) == 0 ||
        strncmp(line, "@@", 2) == 0)
        return False;
    return True;
}

static void
UpdListDone(int rc, char *out, XtPointer data)
{
    Panel *p = (Panel *) data;
    char  *line, label[300], when[64], status[300];
    time_t now = time(NULL);

    PanelClearItems(p);
    /* dnf check-update exits 100 when there are updates */
    if (rc == 0 || rc == 100 || rc == 2 /* checkupdates: none */)
        while ((line = NextLine(&out)) != NULL)
            if (UpdIsItem(line))
            {
                sprintf(label, "%.290s", line);
                PanelAddItem(p, label, NULL, NULL);
            }
    PanelBusy(p, False);
    strftime(when, sizeof(when), "%H:%M", localtime(&now));
    if (p->nItems > 0)
        sprintf(status, "%d %.100s (%s)", p->nItems,
                MSG(42, "updates available"), when);
    else
        sprintf(status, "%.100s (%s)",
                MSG(43, "No updates found."), when);
    PanelSetStatus(p, status);
}

static void
UpdRefresh(Panel *p)
{
    if (!updBackend && !(updBackend = UpdDetect()))
    {
        PanelBusy(p, True);
        PanelSetStatus(p, MSG(44, "No supported package manager found."));
        return;
    }
    if (!updBackend->list[0])
    {
        PanelSetStatus(p, MSG(45,
            "Press Update Now to update this system in a terminal."));
        return;
    }
    PanelBusy(p, True);
    if (!SysCmdRun(updBackend->list, NULL, 120, UpdListDone, (XtPointer) p))
        PanelBusy(p, False);
}

static void
UpdRefreshDone(int rc, char *out, XtPointer data)
{
    UpdRefresh((Panel *) data);
}

/* PackageKit can refresh its cache without root; the others list what
 * the last refresh found and update the cache in the terminal. */
static void
UpdCheckCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    static char *refresh[] = { "pkcon", "refresh", NULL };

    if (updBackend && strcmp(updBackend->tool, "pkcon") == 0)
    {
        PanelBusy(p, True);
        if (SysCmdRun(refresh, NULL, 300, UpdRefreshDone, (XtPointer) p))
            return;
    }
    UpdRefresh(p);
}

/* The update command with "@" replaced by sudo, doas or nothing (then
 * the whole command runs under "su root -c"). */
static void
UpdNowCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    char *prefix, *cmd, *src, *dst;
    Boolean useSu = False;

    if (!updBackend)
        return;
    if (SysCmdHave("sudo"))
        prefix = "sudo ";
    else if (SysCmdHave("doas"))
        prefix = "doas ";
    else
    {
        prefix = "";
        useSu = (strchr(updBackend->update, '@') != NULL);
    }

    cmd = XtMalloc(2 * strlen(updBackend->update) + 64);
    dst = cmd;
    if (useSu)
    {
        strcpy(dst, "su root -c '");
        dst += strlen(dst);
    }
    for (src = updBackend->update; *src; src++)
    {
        if (*src == '@')
        {
            strcpy(dst, prefix);
            dst += strlen(prefix);
        }
        else
            *dst++ = *src;
    }
    *dst = '\0';
    if (useSu)
        strcat(cmd, "'");

    SysCmdTerminal(MSG(46, "System Update"), cmd);
    XtFree(cmd);
}

void
popup_updatesBB(Widget shell)
{
    if (!upd)
    {
        _DtTurnOnHourGlass(shell);
        upd = PanelCreate(shell, "updatesDialog",
                          MSG(40, "Style Manager - Updates"), NULL);
        upd->refresh = UpdRefresh;
        PanelButton(upd, MSG(41, "Check for Updates"), UpdCheckCB);
        PanelButton(upd, MSG(47, "Update Now..."), UpdNowCB);
        _DtTurnOffHourGlass(shell);
    }
    if (XtIsManaged(upd->dialog))
        UpdRefresh(upd);
    else
        XtManageChild(upd->dialog);
    XMapRaised(style.display, XtWindow(XtParent(upd->dialog)));
}

/* ------------------------------------------------------------------ */
/* Autostart (XDG autostart entries, like other session managers)     */
/* ------------------------------------------------------------------ */

static Panel *aut = NULL;
static Widget autPrompt = NULL;

/* $XDG_CONFIG_HOME/autostart, else ~/.config/autostart */
static void
AutUserDir(char *buf, size_t len)
{
    char *c = getenv("XDG_CONFIG_HOME");

    if (c && *c && strlen(c) + 12 < len)
        sprintf(buf, "%s/autostart", c);
    else
        sprintf(buf, "%.*s/.config/autostart", (int) len - 20, style.home);
}

/* Create the config directory (0700, as the XDG spec asks) and its
 * autostart directory if they don't exist yet. */
static void
AutEnsureUserDir(void)
{
    char dir[1100], *c = getenv("XDG_CONFIG_HOME");

    if (c && *c)
        sprintf(dir, "%.1000s", c);
    else
        sprintf(dir, "%.1000s/.config", style.home);
    (void) mkdir(dir, 0700);
    AutUserDir(dir, sizeof(dir));
    (void) mkdir(dir, 0755);
}

typedef struct {
    char name[200];
    Boolean hidden, enabledKey, forCDE;
} AutInfo;

/* Is CDE in a "A;B;C" list? */
static Boolean
AutListHasCDE(const char *list)
{
    char buf[512], *t;

    sprintf(buf, "%.500s", list);
    for (t = strtok(buf, ";"); t; t = strtok(NULL, ";"))
        if (strcmp(t, "CDE") == 0)
            return True;
    return False;
}

static Boolean
AutRead(const char *path, AutInfo *ai)
{
    FILE *fp = fopen(path, "r");
    char  line[1024], *v;
    Boolean inEntry = False;

    memset(ai, 0, sizeof(*ai));
    ai->enabledKey = True;
    ai->forCDE = True;
    if (!fp)
        return False;
    while (fgets(line, sizeof(line), fp))
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '[')
        {
            inEntry = (strcmp(line, "[Desktop Entry]") == 0);
            continue;
        }
        if (!inEntry || !(v = strchr(line, '=')))
            continue;
        *v++ = '\0';
        if (strcmp(line, "Name") == 0)
            sprintf(ai->name, "%.190s", v);
        else if (strcmp(line, "Hidden") == 0)
            ai->hidden = (strcmp(v, "true") == 0);
        else if (strcmp(line, "X-GNOME-Autostart-enabled") == 0)
            ai->enabledKey = (strcmp(v, "false") != 0);
        else if (strcmp(line, "OnlyShowIn") == 0)
            ai->forCDE = ai->forCDE && AutListHasCDE(v);
        else if (strcmp(line, "NotShowIn") == 0)
            ai->forCDE = ai->forCDE && !AutListHasCDE(v);
    }
    fclose(fp);
    return True;
}

/* Copy 'from' (or nothing) to 'to', with Hidden= set to 'hidden';
 * written to a temporary file and renamed. */
static Boolean
AutWrite(const char *from, const char *to, Boolean hidden)
{
    char  tmp[1100], line[1024];
    FILE *in = from ? fopen(from, "r") : NULL, *out;
    int   fd;
    Boolean inEntry = False, done = False;

    sprintf(tmp, "%.1080s.XXXXXX", to);
    if ((fd = mkstemp(tmp)) < 0 || (out = fdopen(fd, "w")) == NULL)
    {
        if (fd >= 0) close(fd);
        if (in) fclose(in);
        return False;
    }
    while (in && fgets(line, sizeof(line), in))
    {
        if (line[0] == '[')
        {
            if (inEntry && !done)
            {
                fprintf(out, "Hidden=%s\n", hidden ? "true" : "false");
                done = True;
            }
            inEntry = (strncmp(line, "[Desktop Entry]", 15) == 0);
        }
        else if (inEntry && strncmp(line, "Hidden=", 7) == 0)
            continue;
        fputs(line, out);
    }
    if (!done)
        fprintf(out, "Hidden=%s\n", hidden ? "true" : "false");
    if (in)
        fclose(in);
    fchmod(fd, 0644);
    if (fclose(out) != 0 || rename(tmp, to) != 0)
    {
        unlink(tmp);
        return False;
    }
    return True;
}

/* keys[]: file name; extra[]: "u" user-only, "s" system-only,
 * "o" user override of a system entry. */
static void
AutAddDir(Panel *p, const char *dir, const char *kind)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char path[1200], label[400];
    AutInfo ai;
    int i;
    size_t len;

    if (!d)
        return;
    while ((e = readdir(d)) != NULL)
    {
        len = strlen(e->d_name);
        if (len < 9 || strcmp(e->d_name + len - 8, ".desktop") != 0)
            continue;
        for (i = 0; i < p->nItems; i++)
            if (strcmp(p->keys[i], e->d_name) == 0)
                break;
        if (i < p->nItems)
        {
            if (strcmp(kind, "s") == 0 && strcmp(p->extra[i], "u") == 0)
            {
                XtFree(p->extra[i]);
                p->extra[i] = XtNewString("o");
            }
            continue;		/* the user's copy wins */
        }
        sprintf(path, "%.1000s/%.190s", dir, e->d_name);
        if (!AutRead(path, &ai))
            continue;
        sprintf(label, "[%s] %.190s%s",
                (!ai.hidden && ai.enabledKey) ? "x" : " ",
                ai.name[0] ? ai.name : e->d_name,
                ai.forCDE ? "" : MSG(56, "   (other desktops only)"));
        PanelAddItem(p, label, e->d_name, kind);
    }
    closedir(d);
}

static void
AutRefresh(Panel *p)
{
    char dir[1100], dirs[1100], *t;
    char *c = getenv("XDG_CONFIG_DIRS");

    PanelClearItems(p);
    AutUserDir(dir, sizeof(dir));
    AutAddDir(p, dir, "u");
    sprintf(dirs, "%.1000s", (c && *c) ? c : "/etc/xdg");
    for (t = strtok(dirs, ":"); t; t = strtok(NULL, ":"))
    {
        sprintf(dir, "%.1000s/autostart", t);
        AutAddDir(p, dir, "s");
    }
    PanelBusy(p, False);
    PanelSetStatus(p, MSG(51,
        "Applications started when you log in ([x] = on):"));
}

/* Path of entry 'row': the user's copy if there is one. */
static Boolean
AutFindSystem(const char *file, char *path, size_t len)
{
    char dirs[1100], *t, *c = getenv("XDG_CONFIG_DIRS");

    sprintf(dirs, "%.1000s", (c && *c) ? c : "/etc/xdg");
    for (t = strtok(dirs, ":"); t; t = strtok(NULL, ":"))
    {
        sprintf(path, "%.900s/autostart/%.190s", t, file);
        if (access(path, R_OK) == 0)
            return True;
    }
    return False;
}

static void
AutToggleCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel  *p = (Panel *) client_data;
    int     row = PanelSelected(p);
    char    dir[1100], user[1300], sys[1300];
    AutInfo ai;
    Boolean ok;

    if (row < 0)
        return;
    AutUserDir(dir, sizeof(dir));
    sprintf(user, "%.1080s/%.190s", dir, p->keys[row]);
    if (strcmp(p->extra[row], "s") == 0)
    {
        /* disable a system entry: an override in the user's directory */
        if (!AutFindSystem(p->keys[row], sys, sizeof(sys)))
            return;
        AutEnsureUserDir();
        ok = AutWrite(sys, user, True);
    }
    else
    {
        AutRead(user, &ai);
        ok = AutWrite(user, user, !ai.hidden);
    }
    if (!ok)
        ErrDialog(MSG(57, "The autostart entry could not be changed."),
                  style.shell);
    AutRefresh(p);
}

static void
AutRemoveCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p);
    char   dir[1100], user[1300];

    if (row < 0)
        return;
    if (strcmp(p->extra[row], "u") != 0)
    {
        ErrDialog(MSG(58, "This entry comes with the system: turn it\n"
                          "off instead of removing it."), style.shell);
        return;
    }
    AutUserDir(dir, sizeof(dir));
    sprintf(user, "%.1080s/%.190s", dir, p->keys[row]);
    unlink(user);
    AutRefresh(p);
}

static void
AutPromptDoneCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    XmSelectionBoxCallbackStruct *cb =
        (XmSelectionBoxCallbackStruct *) call_data;
    char *cmd = NULL, dir[1100], file[1300], name[64], tmp[1310], *s;
    FILE *fp;
    int   fd, i, n;

    XtUnmanageChild(autPrompt);
    if (cb->reason != XmCR_OK ||
        !XmStringGetLtoR(cb->value, XmFONTLIST_DEFAULT_TAG, &cmd) ||
        !cmd || !*cmd || strchr(cmd, '\n'))
    {
        XtFree(cmd);
        return;
    }
    /* file name from the program name: letters, digits, "-_." */
    s = cmd + strspn(cmd, " \t");
    for (n = 0; *s && *s != ' ' && n < 40; s++)
    {
        if (*s == '/')
            n = 0;
        else if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
                 (*s >= '0' && *s <= '9') || *s == '-' || *s == '_')
            name[n++] = *s;
    }
    name[n] = '\0';
    if (!n)
        strcpy(name, "command");

    AutUserDir(dir, sizeof(dir));
    AutEnsureUserDir();
    for (i = 0; i < 100; i++)
    {
        if (i)
            sprintf(file, "%.1080s/%s-%d.desktop", dir, name, i);
        else
            sprintf(file, "%.1080s/%s.desktop", dir, name);
        if (access(file, F_OK) != 0)
            break;
    }
    sprintf(tmp, "%s.XXXXXX", file);
    if ((fd = mkstemp(tmp)) >= 0 && (fp = fdopen(fd, "w")) != NULL)
    {
        fprintf(fp, "[Desktop Entry]\nType=Application\nName=%s\n"
                    "Exec=%s\nX-CDE-Added=true\n", name, cmd);
        fchmod(fd, 0644);
        if (fclose(fp) != 0 || rename(tmp, file) != 0)
            unlink(tmp);
    }
    else if (fd >= 0)
        close(fd);
    XtFree(cmd);
    AutRefresh(p);
}

static void
AutAddCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    XmString s;

    if (!autPrompt)
    {
        autPrompt = XmCreatePromptDialog(style.shell, "autostartAdd",
                                         NULL, 0);
        XtVaSetValues(XtParent(autPrompt), XmNtitle,
                      MSG(53, "Style Manager - Add Application"), NULL);
        s = CMPSTR(MSG(54, "Command to run when you log in:"));
        XtVaSetValues(autPrompt, XmNselectionLabelString, s, NULL);
        XmStringFree(s);
        XtAddCallback(autPrompt, XmNokCallback, AutPromptDoneCB, client_data);
        XtAddCallback(autPrompt, XmNcancelCallback, AutPromptDoneCB,
                      client_data);
        XtUnmanageChild(XmSelectionBoxGetChild(autPrompt,
                                               XmDIALOG_HELP_BUTTON));
    }
    XtManageChild(autPrompt);
}

void
popup_autostartBB(Widget shell)
{
    if (!aut)
    {
        aut = PanelCreate(shell, "autostartDialog",
                          MSG(50, "Style Manager - Autostart"), NULL);
        aut->refresh = AutRefresh;
        PanelButton(aut, MSG(52, "Turn On/Off"), AutToggleCB);
        PanelButton(aut, MSG(55, "Add..."), AutAddCB);
        PanelButton(aut, MSG(59, "Remove"), AutRemoveCB);
        XtAddCallback(aut->list, XmNdefaultActionCallback, AutToggleCB,
                      (XtPointer) aut);
    }
    if (XtIsManaged(aut->dialog))
        AutRefresh(aut);
    else
        XtManageChild(aut->dialog);
    XMapRaised(style.display, XtWindow(XtParent(aut->dialog)));
}
