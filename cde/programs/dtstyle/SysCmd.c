/*
 * SysCmd.c - run system tools from Style Manager without blocking it.
 * See SysCmd.h.
 *
 * Commands run through "/bin/sh -c '"$@"; echo @@rc=$?'" so that the
 * exit status arrives in the output: Style Manager's SIGCHLD handler
 * reaps children itself (Main.c), so waitpid() can't be relied on.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <X11/Intrinsic.h>

#include "Main.h"
#include "SysCmd.h"

#define RC_MARK "@@rc="

typedef struct {
    pid_t          pid;
    int            fd;
    XtInputId      inputId;
    XtIntervalId   timerId;
    Boolean        timedOut;
    char          *buf;
    size_t         len, size;
    SysCmdDoneProc done;
    XtPointer      data;
} SysCmd;

static void
Finish(SysCmd *c)
{
    char *mark, *p;
    int   rc = -1;

    XtRemoveInput(c->inputId);
    if (c->timerId)
        XtRemoveTimeOut(c->timerId);
    close(c->fd);

    c->buf[c->len] = '\0';
    /* the last RC_MARK line is ours */
    mark = NULL;
    for (p = c->buf; (p = strstr(p, RC_MARK)) != NULL; p++)
        mark = p;
    if (mark && (mark == c->buf || mark[-1] == '\n'))
    {
        rc = atoi(mark + strlen(RC_MARK));
        *mark = '\0';
    }
    if (c->timedOut)
        rc = 124;

    if (c->done)
        (*c->done)(rc, c->buf, c->data);
    XtFree(c->buf);
    XtFree((char *) c);
}

static void
ReadCB(XtPointer client_data, int *fd, XtInputId *id)
{
    SysCmd *c = (SysCmd *) client_data;
    ssize_t n;

    if (c->len + 4097 > c->size)
    {
        c->size = c->size * 2 + 4097;
        c->buf = XtRealloc(c->buf, c->size);
    }
    n = read(c->fd, c->buf + c->len, 4096);
    if (n > 0)
    {
        c->len += n;
        return;
    }
    if (n < 0 && (errno == EINTR || errno == EAGAIN))
        return;
    Finish(c);			/* EOF: the command ended */
}

static void
TimeoutCB(XtPointer client_data, XtIntervalId *id)
{
    SysCmd *c = (SysCmd *) client_data;

    c->timerId = 0;
    c->timedOut = True;
    kill(-c->pid, SIGTERM);	/* its process group */
}

static void
CloseOnExec(int fd)
{
    fcntl(fd, F_SETFD, FD_CLOEXEC);
}

Boolean
SysCmdRun(char **argv, const char *input, int timeoutSec,
          SysCmdDoneProc done, XtPointer data)
{
    static char *script = "\"$@\"; echo \"" RC_MARK "$?\"";
    int     out[2], in[2] = { -1, -1 };
    int     n;
    char  **args;
    pid_t   pid;
    SysCmd *c;
    XtAppContext app = XtWidgetToApplicationContext(style.shell);

    for (n = 0; argv[n]; n++)
        ;
    if (n == 0 || pipe(out) < 0)
        return False;
    if (input && pipe(in) < 0)
    {
        close(out[0]); close(out[1]);
        return False;
    }
    CloseOnExec(out[0]);
    if (input)
        CloseOnExec(in[1]);

    /* sh -c SCRIPT sh ARGV... */
    args = (char **) XtMalloc((n + 5) * sizeof(char *));
    args[0] = "sh";
    args[1] = "-c";
    args[2] = script;
    args[3] = "sh";
    memcpy(args + 4, argv, (n + 1) * sizeof(char *));

    pid = fork();
    if (pid < 0)
    {
        XtFree((char *) args);
        close(out[0]); close(out[1]);
        if (input) { close(in[0]); close(in[1]); }
        return False;
    }
    if (pid == 0)
    {
        int devnull;

        setpgid(0, 0);
        if (input)
            dup2(in[0], 0);
        else if ((devnull = open("/dev/null", O_RDONLY)) >= 0)
            dup2(devnull, 0);
        dup2(out[1], 1);
        dup2(out[1], 2);
        /* answer "C" so the output is parseable */
        putenv("LC_ALL=C");
        execv("/bin/sh", args);
        _exit(127);
    }

    XtFree((char *) args);
    close(out[1]);
    if (input)
    {
        size_t left = strlen(input);
        const char *p = input;
        ssize_t w;

        close(in[0]);
        while (left > 0 && (w = write(in[1], p, left)) > 0)
        {
            p += w;
            left -= w;
        }
        close(in[1]);
    }

    c = (SysCmd *) XtCalloc(1, sizeof(SysCmd));
    c->pid = pid;
    c->fd = out[0];
    c->size = 4097;
    c->buf = XtMalloc(c->size);
    c->done = done;
    c->data = data;
    c->inputId = XtAppAddInput(app, c->fd, (XtPointer) XtInputReadMask,
                               ReadCB, (XtPointer) c);
    if (timeoutSec > 0)
        c->timerId = XtAppAddTimeOut(app, (unsigned long) timeoutSec * 1000,
                                     TimeoutCB, (XtPointer) c);
    return True;
}

Boolean
SysCmdHave(const char *prog)
{
    char *path, *dir, *buf;
    Boolean found = False;
    struct stat st;

    if (strchr(prog, '/'))
        return access(prog, X_OK) == 0;
    if ((path = getenv("PATH")) == NULL)
        path = "/usr/bin:/bin";
    path = XtNewString(path);
    buf = XtMalloc(strlen(path) + strlen(prog) + 2);
    for (dir = strtok(path, ":"); dir && !found; dir = strtok(NULL, ":"))
    {
        sprintf(buf, "%s/%s", *dir ? dir : ".", prog);
        found = (stat(buf, &st) == 0 && S_ISREG(st.st_mode) &&
                 access(buf, X_OK) == 0);
    }
    /* sbin is often not in a user's PATH */
    if (!found)
    {
        sprintf(buf, "/usr/sbin/%s", prog);
        found = (access(buf, X_OK) == 0);
    }
    XtFree(buf);
    XtFree(path);
    return found;
}

void
SysCmdTerminal(const char *title, const char *cmd)
{
    static char *tail = "; echo; echo 'Press Return to close this window.'; read x";
    char  *full;
    char  *argv[8];
    pid_t  pid;

    full = XtMalloc(strlen(cmd) + strlen(tail) + 1);
    sprintf(full, "%s%s", cmd, tail);

    if (access(CDE_INSTALLATION_TOP "/bin/dtterm", X_OK) == 0)
        argv[0] = CDE_INSTALLATION_TOP "/bin/dtterm";
    else
        argv[0] = "xterm";
    argv[1] = "-title";
    argv[2] = (char *) title;
    argv[3] = "-e";
    argv[4] = "/bin/sh";
    argv[5] = "-c";
    argv[6] = full;
    argv[7] = NULL;

    if ((pid = fork()) == 0)
    {
        setsid();
        execvp(argv[0], argv);
        _exit(127);
    }
    XtFree(full);
}

int
SysCmdSplitTerse(char *line, char **fields, int max)
{
    int   n = 0;
    char *r = line, *w = line;

    if (max <= 0)
        return 0;
    fields[n++] = w;
    while (*r)
    {
        if (*r == '\\' && (r[1] == ':' || r[1] == '\\'))
        {
            *w++ = r[1];
            r += 2;
        }
        else if (*r == ':')
        {
            *w++ = '\0';
            r++;
            if (n < max)
                fields[n++] = w;
        }
        else
            *w++ = *r++;
    }
    *w = '\0';
    return n;
}
