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
#include <strings.h>
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
#include <Dt/WsmP.h>

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


/* ------------------------------------------------------------------ */
/* Front Panel: which application each button opens                   */
/* ------------------------------------------------------------------ */

/*
 * The main panel's buttons come from dtwm.fp.  A changed button is
 * written to ~/.dt/types/clanker-panel.fp as a DELETE of the original
 * control plus a new control in its place, whose action (in
 * clanker-panel.dt) runs the chosen application; dtwm is restarted to
 * show it.  "#@ control<TAB>label<TAB>command" lines in the .fp file
 * remember the choices.
 */

#define FP_MAX 32
#define FP_APPS_MAX 1000

typedef struct {
    char name[64], label[128], icon[128], pos[16], action[128];
} FpControl;

typedef struct {
    char control[64], label[128], exec[1024];
} FpChoice;

static Panel    *fpPanel = NULL;
static Widget    fpChooser = NULL;
static FpControl fpControls[FP_MAX];
static int       fpNControls;
static FpChoice  fpChoices[FP_MAX];
static int       fpNChoices;
static char     *fpAppName[FP_APPS_MAX], *fpAppExec[FP_APPS_MAX];
static int       fpNApps;

static void
FpUserFile(char *buf, size_t len, const char *ext)
{
    snprintf(buf, len, "%s/.dt/types/clanker-panel.%s", style.home, ext);
}

static char *
FpTrim(char *s)
{
    char *e;

    while (*s == ' ' || *s == '\t')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' ||
                     e[-1] == '\r'))
        *--e = '\0';
    return s;
}

/* The system dtwm.fp, for the session's language or C. */
static FILE *
FpOpenSystem(void)
{
    const char *dirs[] = { CDE_CONFIGURATION_TOP, CDE_INSTALLATION_TOP };
    const char *lang = getenv("LANG");
    char path[1024];
    FILE *fp;
    int i, j;

    for (i = 0; i < 2; i++)
        for (j = 0; j < 2; j++)
        {
            const char *l = j == 0 ? lang : "C";

            if (!l || !*l)
                continue;
            snprintf(path, sizeof(path), "%s/appconfig/types/%s/dtwm.fp",
                     dirs[i], l);
            if ((fp = fopen(path, "r")) != NULL)
                return fp;
        }
    return NULL;
}

/* The main panel's buttons that run an action. */
static void
FpReadControls(void)
{
    FILE *fp = FpOpenSystem();
    char  line[512], *t, *v;
    FpControl c;
    Boolean in = False, top = False, icon = False;

    fpNControls = 0;
    if (!fp)
        return;
    while (fgets(line, sizeof(line), fp))
    {
        t = FpTrim(line);
        if (*t == '#' || !*t)
            continue;
        if (strncmp(t, "CONTROL ", 8) == 0)
        {
            memset(&c, 0, sizeof(c));
            snprintf(c.name, sizeof(c.name), "%s", FpTrim(t + 8));
            in = True; top = False; icon = False;
            continue;
        }
        if (!in)
            continue;
        if (*t == '}')
        {
            if (top && icon && c.action[0] && fpNControls < FP_MAX)
                fpControls[fpNControls++] = c;
            in = False;
            continue;
        }
        for (v = t; *v && *v != ' ' && *v != '\t'; v++)
            ;
        if (*v)
            *v++ = '\0';
        v = FpTrim(v);
        if (strcmp(t, "CONTAINER_NAME") == 0)
            top = (strcmp(v, "Top") == 0);
        else if (strcmp(t, "TYPE") == 0)
            icon = (strcmp(v, "icon") == 0);
        else if (strcmp(t, "LABEL") == 0)
            snprintf(c.label, sizeof(c.label), "%s", v);
        else if (strcmp(t, "ICON") == 0)
            snprintf(c.icon, sizeof(c.icon), "%s", v);
        else if (strcmp(t, "POSITION_HINTS") == 0)
            snprintf(c.pos, sizeof(c.pos), "%s", v);
        else if (strcmp(t, "PUSH_ACTION") == 0)
            snprintf(c.action, sizeof(c.action), "%s", v);
    }
    fclose(fp);
}

static void
FpReadChoices(void)
{
    char  path[1100], line[1400], *f[3], *t;
    FILE *fp;
    int   i;

    fpNChoices = 0;
    FpUserFile(path, sizeof(path), "fp");
    if ((fp = fopen(path, "r")) == NULL)
        return;
    while (fgets(line, sizeof(line), fp) && fpNChoices < FP_MAX)
    {
        if (strncmp(line, "#@ ", 3) != 0)
            continue;
        t = line + 3;
        t[strcspn(t, "\r\n")] = '\0';
        for (i = 0; i < 3; i++)
        {
            f[i] = t;
            t = strchr(t, '\t');
            if (t)
                *t++ = '\0';
            else if (i < 2)
                break;
        }
        if (i < 3)
            continue;
        snprintf(fpChoices[fpNChoices].control, 64, "%s", f[0]);
        snprintf(fpChoices[fpNChoices].label, 128, "%s", f[1]);
        snprintf(fpChoices[fpNChoices].exec, 1024, "%s", f[2]);
        fpNChoices++;
    }
    fclose(fp);
}

static FpChoice *
FpChoiceFor(const char *control)
{
    int i;

    for (i = 0; i < fpNChoices; i++)
        if (strcmp(fpChoices[i].control, control) == 0)
            return &fpChoices[i];
    return NULL;
}

static FpControl *
FpControlNamed(const char *name)
{
    int i;

    for (i = 0; i < fpNControls; i++)
        if (strcmp(fpControls[i].name, name) == 0)
            return &fpControls[i];
    return NULL;
}

/* Write both files (temporary file, then rename), or remove them when
 * nothing is changed. */
static Boolean
FpWriteChoices(void)
{
    char  fpPath[1100], dtPath[1100], tmp[1120], dir[1100];
    FILE *fp, *dt;
    int   i, fd1, fd2;
    char  tmp2[1120];

    FpUserFile(fpPath, sizeof(fpPath), "fp");
    FpUserFile(dtPath, sizeof(dtPath), "dt");
    if (fpNChoices == 0)
    {
        unlink(fpPath);
        unlink(dtPath);
        return True;
    }
    snprintf(dir, sizeof(dir), "%s/.dt", style.home);
    (void) mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/.dt/types", style.home);
    (void) mkdir(dir, 0755);

    snprintf(tmp, sizeof(tmp), "%s.XXXXXX", fpPath);
    snprintf(tmp2, sizeof(tmp2), "%s.XXXXXX", dtPath);
    fd1 = mkstemp(tmp);
    fd2 = fd1 >= 0 ? mkstemp(tmp2) : -1;
    fp = fd1 >= 0 ? fdopen(fd1, "w") : NULL;
    dt = fd2 >= 0 ? fdopen(fd2, "w") : NULL;
    if (!fp || !dt)
    {
        if (fp) fclose(fp); else if (fd1 >= 0) close(fd1);
        if (dt) fclose(dt); else if (fd2 >= 0) close(fd2);
        if (fd1 >= 0) unlink(tmp);
        if (fd2 >= 0) unlink(tmp2);
        return False;
    }

    fprintf(fp, "# Front Panel buttons changed in Style Manager > Front Panel.\n"
                "# Written by dtstyle; the #@ lines record the choices.\n\n");
    fprintf(dt, "# Actions for the Front Panel buttons changed in Style Manager.\n"
                "# Written by dtstyle.\n\n");
    for (i = 0; i < fpNChoices; i++)
    {
        FpChoice  *ch = &fpChoices[i];
        FpControl *c = FpControlNamed(ch->control);

        if (!c)
            continue;
        fprintf(fp, "#@ %s\t%s\t%s\n", ch->control, ch->label, ch->exec);
        fprintf(fp, "CONTROL %s\n{\n  TYPE\t\t\ticon\n"
                    "  CONTAINER_NAME\tTop\n  CONTAINER_TYPE\tBOX\n"
                    "  DELETE\t\tTrue\n}\n", c->name);
        fprintf(fp, "CONTROL Clanker%s\n{\n  TYPE\t\t\ticon\n"
                    "  CONTAINER_NAME\tTop\n  CONTAINER_TYPE\tBOX\n"
                    "  POSITION_HINTS\t%s\n  ICON\t\t\t%s\n  LABEL\t\t\t%s\n"
                    "  PUSH_ACTION\t\tClankerPanel%s\n"
                    "  DROP_ACTION\t\tClankerPanel%s\n}\n\n",
                c->name, c->pos, c->icon, ch->label, c->name, c->name);
        fprintf(dt, "ACTION ClankerPanel%s\n{\n  LABEL\t\t%s\n"
                    "  TYPE\t\tCOMMAND\n  WINDOW_TYPE\tNO_STDIO\n"
                    "  EXEC_STRING\t%s %%Args%%\n}\n\n",
                c->name, ch->label, ch->exec);
    }
    fchmod(fd1, 0644);
    fchmod(fd2, 0644);
    if (fclose(fp) != 0 || fclose(dt) != 0 ||
        rename(tmp2, dtPath) != 0 || rename(tmp, fpPath) != 0)
    {
        unlink(tmp);
        unlink(tmp2);
        return False;
    }
    return True;
}

/* A .desktop Exec= value as a command: no field codes (%f, %U, ...). */
static void
FpExecToCommand(const char *exec, char *out, size_t len)
{
    size_t n = 0;

    for (; *exec && n + 1 < len; exec++)
    {
        if (*exec == '%')
        {
            if (exec[1] == '%')
                exec++;			/* "%%" is a literal "%": drop it */
            else if (exec[1])
                exec++;			/* a field code */
            continue;
        }
        if (*exec == '\n' || *exec == '\t')
            continue;
        out[n++] = *exec;
    }
    while (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = '\0';
}

static void
FpAddAppsFrom(const char *dir)
{
    DIR  *d = opendir(dir);
    struct dirent *e;
    char  path[1200], line[1024], name[128], exec[1024], *v;
    FILE *fp;
    size_t len;
    int   i;
    Boolean in, hide, app;

    if (!d)
        return;
    while ((e = readdir(d)) != NULL && fpNApps < FP_APPS_MAX)
    {
        len = strlen(e->d_name);
        if (len < 9 || strcmp(e->d_name + len - 8, ".desktop") != 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if ((fp = fopen(path, "r")) == NULL)
            continue;
        name[0] = exec[0] = '\0';
        in = hide = app = False;
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
            else if (strcmp(line, "Exec") == 0)
                FpExecToCommand(v, exec, sizeof(exec));
            else if (strcmp(line, "Type") == 0)
                app = (strcmp(v, "Application") == 0);
            else if ((strcmp(line, "NoDisplay") == 0 ||
                      strcmp(line, "Hidden") == 0) && strcmp(v, "true") == 0)
                hide = True;
        }
        fclose(fp);
        if (!app || hide || !name[0] || !exec[0] || strchr(name, '\t'))
            continue;
        for (i = 0; i < fpNApps; i++)		/* first one wins */
            if (strcmp(fpAppName[i], name) == 0)
                break;
        if (i < fpNApps)
            continue;
        fpAppName[fpNApps] = XtNewString(name);
        fpAppExec[fpNApps] = XtNewString(exec);
        fpNApps++;
    }
    closedir(d);
}

static int
FpNameCmp(const void *a, const void *b)
{
    int ia = *(const int *) a, ib = *(const int *) b;

    return strcasecmp(fpAppName[ia], fpAppName[ib]);
}

/* Installed applications (XDG .desktop entries), sorted by name. */
static void
FpReadApps(void)
{
    char  dir[1100], dirs[1100], *t, *c;
    int   i;

    for (i = 0; i < fpNApps; i++)
    {
        XtFree(fpAppName[i]);
        XtFree(fpAppExec[i]);
    }
    fpNApps = 0;
    c = getenv("XDG_DATA_HOME");
    if (c && *c)
        snprintf(dir, sizeof(dir), "%s/applications", c);
    else
        snprintf(dir, sizeof(dir), "%s/.local/share/applications", style.home);
    FpAddAppsFrom(dir);
    c = getenv("XDG_DATA_DIRS");
    snprintf(dirs, sizeof(dirs), "%s",
             (c && *c) ? c : "/usr/local/share:/usr/share");
    for (t = strtok(dirs, ":"); t; t = strtok(NULL, ":"))
    {
        snprintf(dir, sizeof(dir), "%s/applications", t);
        FpAddAppsFrom(dir);
    }
}

static void
FpRefresh(Panel *p)
{
    char label[400];
    int  i;

    FpReadControls();
    FpReadChoices();
    PanelClearItems(p);
    for (i = 0; i < fpNControls; i++)
    {
        FpControl *c = &fpControls[i];
        FpChoice  *ch = FpChoiceFor(c->name);

        if (ch)
            snprintf(label, sizeof(label), "%s:  %s  %s", c->label,
                     ch->label, MSG(66, "(changed)"));
        else
            snprintf(label, sizeof(label), "%s:  %s", c->label, c->action);
        PanelAddItem(p, label, c->name, ch ? "c" : "s");
    }
    PanelBusy(p, False);
    if (fpNControls)
        PanelSetStatus(p, MSG(61,
            "Front Panel buttons and what they open:"));
    else
        PanelSetStatus(p, MSG(67, "The Front Panel definition was not found."));
}

/* Save, then have the Workspace Manager rebuild the panel. */
static void
FpApply(Panel *p)
{
    if (!FpWriteChoices())
    {
        ErrDialog(MSG(65, "The Front Panel could not be changed."),
                  style.shell);
        return;
    }
    _DtWmRestartNoConfirm(style.display, RootWindow(style.display, style.screenNum));
    FpRefresh(p);
}

static void
FpChooserDoneCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = fpPanel;
    XmSelectionBoxCallbackStruct *cb =
        (XmSelectionBoxCallbackStruct *) call_data;
    int    row;
    char  *text = NULL, *t, label[128], exec[1024], *s;
    FpChoice *ch;
    int    i;

    XtUnmanageChild(fpChooser);
    row = PanelSelected(p);
    if (cb->reason != XmCR_OK || row < 0 ||
        !XmStringGetLtoR(cb->value, XmFONTLIST_DEFAULT_TAG, &text) || !text)
        return;
    t = FpTrim(text);
    if (!*t || strchr(t, '\n') || strchr(t, '\t'))
    {
        XtFree(text);
        return;
    }
    /* an installed application by name, else a command */
    for (i = 0; i < fpNApps; i++)
        if (strcmp(fpAppName[i], t) == 0)
            break;
    if (i < fpNApps)
    {
        snprintf(label, sizeof(label), "%s", fpAppName[i]);
        snprintf(exec, sizeof(exec), "%s", fpAppExec[i]);
    }
    else
    {
        FpExecToCommand(t, exec, sizeof(exec));
        s = strrchr(exec, '/');
        snprintf(label, sizeof(label), "%.*s",
                 (int) strcspn(s ? s + 1 : exec, " "), s ? s + 1 : exec);
    }
    XtFree(text);
    if (!exec[0])
        return;

    ch = FpChoiceFor(p->keys[row]);
    if (!ch)
    {
        if (fpNChoices >= FP_MAX)
            return;
        ch = &fpChoices[fpNChoices++];
        snprintf(ch->control, sizeof(ch->control), "%s", p->keys[row]);
    }
    snprintf(ch->label, sizeof(ch->label), "%s", label);
    snprintf(ch->exec, sizeof(ch->exec), "%s", exec);
    FpApply(p);
}

static void
FpChangeCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel   *p = (Panel *) client_data;
    int      row = PanelSelected(p), i, *order;
    XmString *items, s;

    if (row < 0)
        return;
    FpReadApps();
    if (!fpChooser)
    {
        fpChooser = XmCreateSelectionDialog(style.shell, "frontPanelChoose",
                                            NULL, 0);
        XtVaSetValues(XtParent(fpChooser), XmNtitle,
                      MSG(62, "Style Manager - Choose Application"), NULL);
        s = CMPSTR(MSG(63, "Application, or a command to run:"));
        XtVaSetValues(fpChooser, XmNselectionLabelString, s, NULL);
        XmStringFree(s);
        s = CMPSTR(MSG(69, "Installed applications:"));
        XtVaSetValues(fpChooser, XmNlistLabelString, s, NULL);
        XmStringFree(s);
        XtAddCallback(fpChooser, XmNokCallback, FpChooserDoneCB, NULL);
        XtAddCallback(fpChooser, XmNcancelCallback, FpChooserDoneCB, NULL);
        XtUnmanageChild(XmSelectionBoxGetChild(fpChooser,
                                               XmDIALOG_HELP_BUTTON));
        XtUnmanageChild(XmSelectionBoxGetChild(fpChooser,
                                               XmDIALOG_APPLY_BUTTON));
    }
    order = (int *) XtMalloc((fpNApps ? fpNApps : 1) * sizeof(int));
    for (i = 0; i < fpNApps; i++)
        order[i] = i;
    qsort(order, fpNApps, sizeof(int), FpNameCmp);
    items = (XmString *) XtMalloc((fpNApps ? fpNApps : 1) * sizeof(XmString));
    for (i = 0; i < fpNApps; i++)
        items[i] = CMPSTR(fpAppName[order[i]]);
    s = CMPSTR("");
    XtVaSetValues(fpChooser, XmNlistItems, items, XmNlistItemCount, fpNApps,
                  XmNtextString, s, XmNlistVisibleItemCount, 12, NULL);
    XmStringFree(s);
    for (i = 0; i < fpNApps; i++)
        XmStringFree(items[i]);
    XtFree((char *) items);
    XtFree((char *) order);
    XtManageChild(fpChooser);
}

static void
FpResetCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p), i;

    if (row < 0)
        return;
    for (i = 0; i < fpNChoices; i++)
        if (strcmp(fpChoices[i].control, p->keys[row]) == 0)
        {
            fpChoices[i] = fpChoices[--fpNChoices];
            FpApply(p);
            return;
        }
}

void
popup_frontPanelBB(Widget shell)
{
    if (!fpPanel)
    {
        fpPanel = PanelCreate(shell, "frontPanelDialog",
                              MSG(60, "Style Manager - Front Panel"), NULL);
        fpPanel->refresh = FpRefresh;
        PanelButton(fpPanel, MSG(64, "Change..."), FpChangeCB);
        PanelButton(fpPanel, MSG(68, "Reset"), FpResetCB);
        XtAddCallback(fpPanel->list, XmNdefaultActionCallback, FpChangeCB,
                      (XtPointer) fpPanel);
    }
    if (XtIsManaged(fpPanel->dialog))
        FpRefresh(fpPanel);
    else
        XtManageChild(fpPanel->dialog);
    XMapRaised(style.display, XtWindow(XtParent(fpPanel->dialog)));
}


/* ------------------------------------------------------------------ */
/* Session: the options in ~/.dtprofile                               */
/* ------------------------------------------------------------------ */

/*
 * Xsession reads ~/.dtprofile at login.  This panel keeps its settings
 * in a marked block at the end of the file; the rest of the file is
 * left as it is.  A setting the user wrote elsewhere in the file is
 * shown, and overridden by the block when it is changed here (the
 * last assignment wins).
 */

#define SES_BEGIN "# >>> Style Manager > Session (changed there) >>>"
#define SES_END   "# <<< Style Manager > Session <<<"

typedef struct {
    const char *var;
    int         msg;
    const char *label;
    Boolean     onWhenSet;	/* the variable turns something on */
    const char *value;		/* value written to turn it on/off */
} SesOption;

static const SesOption sesOptions[] = {
  { "DT_NO_DBUS",            71, "Start a D-Bus session bus",               False, "1" },
  { "DT_NO_XDG_AUTOSTART",   72, "Run autostart applications",              False, "1" },
  { "DT_NO_TOUCHPAD",        73, "Apply the touchpad settings",             False, "1" },
  { "DT_NO_LOCK_ON_SLEEP",   74, "Lock the screen before suspend",          False, "1" },
  { "DT_NO_COMPOSITOR",      75, "Allow the compositor",                    False, "1" },
  { "DT_NO_SYSTEMD_SESSION", 76, "Start systemd's graphical session",        False, "1" },
  { "DT_ALTWM_NO_PANEL",     77, "Front Panel under other window managers", False, "1" },
  { "DTSOURCEPROFILE",       78, "Read ~/.profile at login",                True,  "true" },
  { "TT_ALLOW_REMOTE",       79, "Allow ToolTalk from other computers",     True,  "1" },
  { "DT_ICE_TCP",            80, "Session manager over TCP (other computers)", True, "1" },
};
#define SES_NOPTIONS ((int) (sizeof(sesOptions) / sizeof(sesOptions[0])))
#define SES_WM SES_NOPTIONS	/* the window manager row */

static Panel *sesPanel = NULL;
static char  *sesValue[SES_NOPTIONS + 1];	/* current value, or NULL */
static Boolean sesInBlock[SES_NOPTIONS + 1];	/* set in our block */

static void
SesFile(char *buf, size_t len)
{
    snprintf(buf, len, "%s/.dtprofile", style.home);
}

static const char *
SesVar(int i)
{
    return i == SES_WM ? "DT_WINDOW_MANAGER" : sesOptions[i].var;
}

/* Value of an assignment "[export ]VAR=value" ("unset VAR" -> ""). */
static Boolean
SesParse(char *line, const char *var, char **value)
{
    char  *t = line + strspn(line, " \t");
    size_t n = strlen(var);

    if (strncmp(t, "unset ", 6) == 0)
    {
        t += 6 + strspn(t + 6, " \t");
        if (strncmp(t, var, n) == 0 && (t[n] == '\0' || t[n] == ' ' ||
                                         t[n] == ';' || t[n] == '\n'))
        {
            *value = NULL;
            return True;
        }
        return False;
    }
    if (strncmp(t, "export ", 7) == 0)
        t += 7 + strspn(t + 7, " \t");
    if (strncmp(t, var, n) != 0 || t[n] != '=')
        return False;
    t += n + 1;
    t[strcspn(t, ";\n")] = '\0';
    if (*t == '"' || *t == '\'')
    {
        char q = *t++;
        char *e = strchr(t, q);

        if (e)
            *e = '\0';
    }
    *value = t;
    return True;
}

static void
SesRead(void)
{
    char  path[1100], line[1024], *v;
    FILE *fp;
    int   i;
    Boolean block = False;

    for (i = 0; i <= SES_NOPTIONS; i++)
    {
        XtFree(sesValue[i]);
        sesValue[i] = NULL;
        sesInBlock[i] = False;
    }
    SesFile(path, sizeof(path));
    if ((fp = fopen(path, "r")) == NULL)
        return;
    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, SES_BEGIN, strlen(SES_BEGIN)) == 0)
            block = True;
        else if (strncmp(line, SES_END, strlen(SES_END)) == 0)
            block = False;
        if (line[strspn(line, " \t")] == '#')
            continue;
        for (i = 0; i <= SES_NOPTIONS; i++)
        {
            char copy[1024];

            snprintf(copy, sizeof(copy), "%s", line);
            if (SesParse(copy, SesVar(i), &v))
            {
                XtFree(sesValue[i]);
                sesValue[i] = (v && *v) ? XtNewString(v) : NULL;
                if (block)
                    sesInBlock[i] = True;
            }
        }
    }
    fclose(fp);
}

static Boolean
SesIsOn(int i)
{
    const SesOption *o = &sesOptions[i];
    Boolean set = sesValue[i] != NULL;

    if (strcmp(o->var, "DTSOURCEPROFILE") == 0)
        set = set && strcmp(sesValue[i], "true") == 0;
    return o->onWhenSet ? set : !set;
}

/* Rewrite ~/.dtprofile with our block at the end. */
static Boolean
SesWrite(void)
{
    char  path[1100], tmp[1120], line[1024];
    FILE *in, *out;
    int   fd, i;
    Boolean block = False, any = False;
    struct stat st;

    SesFile(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
    if ((fd = mkstemp(tmp)) < 0 || (out = fdopen(fd, "w")) == NULL)
    {
        if (fd >= 0) { close(fd); unlink(tmp); }
        return False;
    }
    if ((in = fopen(path, "r")) != NULL)
    {
        while (fgets(line, sizeof(line), in))
        {
            if (strncmp(line, SES_BEGIN, strlen(SES_BEGIN)) == 0)
                block = True;
            if (!block)
                fputs(line, out);
            if (strncmp(line, SES_END, strlen(SES_END)) == 0)
                block = False;
        }
        fstat(fileno(in), &st);
        fchmod(fd, st.st_mode & 07777);
        fclose(in);
    }
    else
        fchmod(fd, 0644);

    for (i = 0; i <= SES_NOPTIONS; i++)
        any = any || sesInBlock[i];
    if (any)
    {
        fprintf(out, "%s\n", SES_BEGIN);
        for (i = 0; i <= SES_NOPTIONS; i++)
        {
            if (!sesInBlock[i])
                continue;
            if (sesValue[i])
                fprintf(out, "%s=%s; export %s\n", SesVar(i), sesValue[i],
                        SesVar(i));
            else
                fprintf(out, "unset %s\n", SesVar(i));
        }
        fprintf(out, "%s\n", SES_END);
    }
    if (fclose(out) != 0 || rename(tmp, path) != 0)
    {
        unlink(tmp);
        return False;
    }
    return True;
}

static const char *
SesWmLabel(void)
{
    const char *v = sesValue[SES_WM];

    if (!v)
        return MSG(82, "automatic (xfwm4 when installed)");
    if (strcmp(v, "dtwm") == 0)
        return MSG(83, "dtwm (CDE)");
    if (strcmp(v, "xfwm4") == 0)
        return MSG(84, "xfwm4 (Xfce)");
    return v;
}

static void
SesRefresh(Panel *p)
{
    char label[300];
    int  i;

    SesRead();
    PanelClearItems(p);
    for (i = 0; i < SES_NOPTIONS; i++)
    {
        snprintf(label, sizeof(label), "[%s] %s", SesIsOn(i) ? "x" : " ",
                 MSG(sesOptions[i].msg, (char *) sesOptions[i].label));
        PanelAddItem(p, label, sesOptions[i].var, "");
    }
    snprintf(label, sizeof(label), "%s: %s",
             MSG(81, "Window manager"), SesWmLabel());
    PanelAddItem(p, label, "DT_WINDOW_MANAGER", "");
    PanelBusy(p, False);
    PanelSetStatus(p, MSG(85,
        "Session options in ~/.dtprofile (used at the next login):"));
}

static void
SesSet(int i, const char *value)
{
    XtFree(sesValue[i]);
    sesValue[i] = value ? XtNewString(value) : NULL;
    sesInBlock[i] = True;
}

static void
SesToggleCB(Widget w, XtPointer client_data, XtPointer call_data)
{
    Panel *p = (Panel *) client_data;
    int    row = PanelSelected(p);

    if (row < 0)
        return;
    SesRead();
    if (row == SES_WM)
    {
        /* automatic -> dtwm -> xfwm4 -> automatic */
        const char *v = sesValue[SES_WM];

        SesSet(SES_WM, !v ? "dtwm" : strcmp(v, "dtwm") == 0 ? "xfwm4" : NULL);
    }
    else
    {
        const SesOption *o = &sesOptions[row];
        Boolean on = !SesIsOn(row);

        SesSet(row, on == o->onWhenSet ? o->value : NULL);
    }
    if (!SesWrite())
        ErrDialog(MSG(86, "~/.dtprofile could not be changed."), style.shell);
    SesRefresh(p);
    XmListSelectPos(p->list, row + 1, False);
}

void
popup_sessionBB(Widget shell)
{
    if (!sesPanel)
    {
        sesPanel = PanelCreate(shell, "sessionDialog",
                               MSG(70, "Style Manager - Session"), NULL);
        sesPanel->refresh = SesRefresh;
        XtVaSetValues(sesPanel->list,
                      XmNvisibleItemCount, SES_NOPTIONS + 1, NULL);
        PanelButton(sesPanel, MSG(87, "Change"), SesToggleCB);
        XtAddCallback(sesPanel->list, XmNdefaultActionCallback, SesToggleCB,
                      (XtPointer) sesPanel);
    }
    if (XtIsManaged(sesPanel->dialog))
        SesRefresh(sesPanel);
    else
        XtManageChild(sesPanel->dialog);
    XMapRaised(style.display, XtWindow(XtParent(sesPanel->dialog)));
}
