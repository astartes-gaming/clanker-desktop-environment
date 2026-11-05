/*
 * ExtTheme.h - export the current CDE palette to GTK and Qt applications.
 *
 * This file is part of the Clanker Desktop Environment, a fork of the
 * Common Desktop Environment, and is distributed under the same terms
 * (GNU Lesser General Public License, version 2 or later).
 */
#ifndef _ExtTheme_h
#define _ExtTheme_h

#include "ColorMain.h"

/* True when "Apply colors to GTK and Qt applications" is enabled.
 * The state is the presence of ~/.dt/theming/env, so it persists
 * across sessions and is honored at login by Xsession.d/0070.dtmodern. */
extern Boolean ExtThemeEnabled(void);

/* Write (enable) or remove (disable) the theme files and session env. */
extern void ExtThemeSetEnabled(Boolean enable, palette *p);

/* Regenerate theme files from palette p if theming is enabled. */
extern void ExtThemeApply(palette *p);

#endif /* _ExtTheme_h */
