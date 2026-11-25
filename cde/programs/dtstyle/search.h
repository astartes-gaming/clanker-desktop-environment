/*
 * search.h - the sources of "dtappfind -search" besides applications:
 * a calculator, Settings pages, open windows, recent files, files,
 * web search and running a command.  See search.c.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */
#ifndef SEARCH_H
#define SEARCH_H

#include <X11/Intrinsic.h>

typedef struct {
    int   kind;			/* HIT_* */
    char *label;		/* what the row shows */
    char *icon;			/* icon name(s), "a|b" = a, else b */
    char *data;			/* page, path, window id, URL, command */
} Hit;

enum { HIT_CALC, HIT_SETTING, HIT_WINDOW, HIT_RECENT, HIT_FILE, HIT_WEB,
       HIT_RUN };

/* ~/.dt/search (Settings > Search): which sources are searched. */
typedef struct {
    Boolean apps, settings, windows, recent, files, calc, web, run;
    Boolean filesEverywhere;	/* not only the home folder */
    int     maxApps, maxFiles;
    char    engine[512];	/* URL with %s for the words */
    char    engineName[64];
    char    fileTool[32];	/* auto, plocate, locate, baloo, find */
} SearchConfig;

extern SearchConfig searchConfig;

void SearchReadConfig(void);
/* changed: called when results arrive later (the file search) */
void SearchInit(Widget top, XtAppContext app, void (*changed)(void));
void SearchQuery(const char *text);

/* The rows: "before" (the calculator) come above the applications,
 * "after" (the rest) below them. */
int  SearchBefore(void);
int  SearchAfter(void);
const Hit *SearchHitBefore(int i);
const Hit *SearchHitAfter(int i);

/* Enter on a hit: do it.  Returns True if dtappfind should quit. */
Boolean SearchActivate(const Hit *h, Widget field);

#endif
