/*
 * SysCmd.h - run system tools (nmcli, bluetoothctl, package managers)
 * from Style Manager without blocking it.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifndef _SysCmd_h
#define _SysCmd_h

#include <X11/Intrinsic.h>

/* Called when the command ends.  rc is its exit status, -1 if it could
 * not be started, 124 if it timed out.  'output' (stdout and stderr) is
 * freed after the call. */
typedef void (*SysCmdDoneProc)(int rc, char *output, XtPointer data);

/* Run argv[0] (looked up in PATH) with argv, without a shell.  'input',
 * if not NULL, is written to its standard input (used for passwords,
 * which then never appear in the process list).  Returns False if it
 * could not be started (done is then not called). */
extern Boolean SysCmdRun(char **argv, const char *input, int timeoutSec,
                         SysCmdDoneProc done, XtPointer data);

/* True if 'prog' is an executable in PATH. */
extern Boolean SysCmdHave(const char *prog);

/* Open a terminal (dtterm, else xterm) running the shell command 'cmd',
 * which then waits for Return so the user can read the result. */
extern void SysCmdTerminal(const char *title, const char *cmd);

/* Split 'line' at unescaped ':' as "nmcli -t" prints it, unescaping
 * "\:" and "\\".  Returns the number of fields stored in fields[]
 * (pointers into line, which is modified). */
extern int SysCmdSplitTerse(char *line, char **fields, int max);

#endif /* _SysCmd_h */
