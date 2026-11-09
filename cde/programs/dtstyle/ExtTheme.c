/*
 * ExtTheme.c - export the current CDE palette to GTK and Qt applications.
 *
 * This file is part of the Clanker Desktop Environment, a fork of the
 * Common Desktop Environment, and is distributed under the same terms
 * (GNU Lesser General Public License, version 2 or later).
 *
 * When "Apply colors to GTK and Qt applications" is enabled in the Color
 * dialog, every palette change regenerates:
 *
 *   ~/.themes/CDE/gtk-2.0/gtkrc          GTK 2 (default engine draws the
 *                                         Motif-style 3D bevels itself)
 *   ~/.themes/CDE/gtk-3.0/gtk.css        GTK 3 (built-in theme + overrides)
 *   ~/.themes/CDE/gtk-4.0/gtk.css        GTK 4 (built-in theme + overrides)
 *   $XDG_CONFIG_HOME/qt5ct/colors/CDE.conf, qt6ct/colors/CDE.conf
 *                                         Qt 5/6 palettes for qt5ct/qt6ct
 *   ~/.dt/theming/env                     environment for CDE sessions
 *
 * Nothing global is touched: the theme is selected only through the
 * environment set up by ~/.dt/theming/env, which Xsession.d/0070.dtmodern
 * sources at login, so other desktops on the same account (Plasma, GNOME,
 * ...) keep their own GTK/Qt settings.  qt5ct.conf/qt6ct.conf are only
 * written when absent or previously written by dtstyle.
 *
 * Only C89 and POSIX.1-2001 interfaces are used, so this builds on every
 * system CDE supports; GTK and Qt are runtime consumers only, never build
 * dependencies.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>		/* MAXPATHLEN */

#include <X11/Intrinsic.h>
#include <Xm/Xm.h>

#include "Main.h"
#include "ColorMain.h"
#include "SysCmd.h"
#include "ExtTheme.h"

#define THEME_NAME	"CDE"
#define GEN_MARKER	"dtstyle_generated=true"

typedef struct { int r, g, b; } Rgb;

typedef struct {
    Rgb bg, fg, ts, bs, sc;	/* primary color set: windows, buttons */
    Rgb base, text;		/* text fields, lists (primary select color) */
    Rgb sel_bg, sel_fg;		/* active color set: selection, accent */
    Rgb dis;			/* insensitive text */
    Rgb mid, midlight;		/* Qt bevel helpers */
    Rgb alt_base, placeholder;
} Scheme;

/* ------------------------------------------------------------------ */
/* colors                                                             */

static Rgb
rgb_of(XColor *c)
{
    Rgb r;
    r.r = c->red >> 8;
    r.g = c->green >> 8;
    r.b = c->blue >> 8;
    return r;
}

static Rgb
mix(Rgb a, Rgb b, int pct_b)
{
    Rgb r;
    r.r = (a.r * (100 - pct_b) + b.r * pct_b) / 100;
    r.g = (a.g * (100 - pct_b) + b.g * pct_b) / 100;
    r.b = (a.b * (100 - pct_b) + b.b * pct_b) / 100;
    return r;
}

/* Rotating set of buffers so a few hex() results can be live at once
 * (emit_template uses one at a time). */
#define HEX_BUFS 32
static const char *
hex(Rgb c)
{
    static char buf[HEX_BUFS][8];
    static int  n = 0;
    char *s = buf[n];
    n = (n + 1) % HEX_BUFS;
    sprintf(s, "#%02x%02x%02x", c.r & 0xff, c.g & 0xff, c.b & 0xff);
    return s;
}

static int
valid_index(palette *p, int i)
{
    return i >= 0 && i < XmCO_MAX_NUM_COLORS && i < p->num_of_colors;
}

static void
make_scheme(palette *p, Scheme *s)
{
    int pri = valid_index(p, p->primary)   ? p->primary   : 0;
    int act = valid_index(p, p->active)    ? p->active    : pri;
    ColorSet *P = &p->color[pri];
    ColorSet *A = &p->color[act];

    s->bg   = rgb_of(&P->bg);
    s->fg   = rgb_of(&P->fg);
    s->ts   = rgb_of(&P->ts);
    s->bs   = rgb_of(&P->bs);
    s->sc   = rgb_of(&P->sc);
    /* Like Motif under CDE (see dtterm's backgroundIsSelect), text
     * fields and lists use the primary set's select color. */
    s->base = rgb_of(&P->sc);
    s->text = rgb_of(&P->fg);
    s->sel_bg = rgb_of(&A->bg);
    s->sel_fg = rgb_of(&A->fg);
    s->dis  = mix(s->fg, s->bg, 55);
    s->mid  = mix(s->bg, s->bs, 50);
    s->midlight = mix(s->bg, s->ts, 50);
    s->alt_base = mix(s->base, s->bs, 8);
    s->placeholder = mix(s->text, s->base, 50);
}

/* ------------------------------------------------------------------ */
/* files                                                              */

/* mkdir -p, mode 0755 (umask applies). */
static int
make_dirs(const char *path)
{
    char  buf[MAXPATHLEN];
    char *p;

    if (strlen(path) >= sizeof(buf))
	return -1;
    strcpy(buf, path);
    for (p = buf + 1; *p; p++) {
	if (*p == '/') {
	    *p = '\0';
	    if (mkdir(buf, 0755) < 0 && errno != EEXIST)
		return -1;
	    *p = '/';
	}
    }
    if (mkdir(buf, 0755) < 0 && errno != EEXIST)
	return -1;
    return 0;
}

typedef void (*Emitter)(FILE *, Scheme *, const char *);

/* Write dir/name atomically: temp file in the same directory + rename(),
 * so applications never read a half-written theme. */
static int
write_file(const char *dir, const char *name, Emitter emit, Scheme *s,
	   const char *arg)
{
    char  path[MAXPATHLEN], tmp[MAXPATHLEN];
    int   fd;
    FILE *f;
    int   ok;

    if (make_dirs(dir) < 0)
	return -1;
    if (strlen(dir) + strlen(name) + 16 >= sizeof(path))
	return -1;
    sprintf(path, "%s/%s", dir, name);
    sprintf(tmp, "%s/.%s.XXXXXX", dir, name);

    if ((fd = mkstemp(tmp)) < 0)
	return -1;
    if ((f = fdopen(fd, "w")) == NULL) {
	close(fd);
	unlink(tmp);
	return -1;
    }
    (*emit)(f, s, arg);
    ok = !ferror(f);
    if (fclose(f) != 0)
	ok = 0;
    if (ok)
	(void) chmod(tmp, 0644);
    if (!ok || rename(tmp, path) < 0) {
	unlink(tmp);
	return -1;
    }
    return 0;
}

static const char *
home_dir(void)
{
    const char *h = getenv("HOME");
    return (h && *h) ? h : "/";
}

static void
config_home(char *buf, size_t len)
{
    const char *x = getenv("XDG_CONFIG_HOME");
    if (x && *x == '/')
	snprintf(buf, len, "%s", x);
    else
	snprintf(buf, len, "%s/.config", home_dir());
}

/* ------------------------------------------------------------------ */
/* templates                                                          */

/*
 * Templates are arrays of short lines (every literal stays under the
 * 509 characters ISO C90 guarantees, for old vendor compilers) in which
 * "%{name}" is replaced by a scheme color as #rrggbb.
 */
static Rgb *
lookup_color(Scheme *s, const char *name, size_t len)
{
#define C(n, field) if (len == sizeof(n) - 1 && strncmp(name, n, len) == 0) \
			return &s->field
    C("bg", bg); C("fg", fg); C("ts", ts); C("bs", bs); C("sc", sc);
    C("base", base); C("text", text); C("sel_bg", sel_bg);
    C("sel_fg", sel_fg); C("dis", dis); C("mid", mid);
    C("midlight", midlight); C("alt_base", alt_base);
    C("placeholder", placeholder);
#undef C
    return NULL;
}

static void
emit_template(FILE *f, Scheme *s, const char *const *lines)
{
    const char *p, *end;
    Rgb        *c;

    for (; *lines; lines++) {
	for (p = *lines; *p; p++) {
	    if (p[0] == '%' && p[1] == '{' && s != NULL &&
		(end = strchr(p + 2, '}')) != NULL &&
		(c = lookup_color(s, p + 2, (size_t) (end - p - 2))) != NULL) {
		fputs(hex(*c), f);
		p = end;
	    } else
		putc(*p, f);
	}
	putc('\n', f);
    }
}

static const char *const header_lines[] = {
    "# Generated by dtstyle from the current CDE palette. Do not edit:",
    "# changes are overwritten when the palette changes.",
    "",
    NULL
};

/* ------------------------------------------------------------------ */
/* GTK 2                                                              */

static const char *const gtkrc_lines[] = {
    "gtk-menu-images = 0",
    "gtk-button-images = 0",
    "",
    "style \"cde-default\"",
    "{",
    "    xthickness = 2",
    "    ythickness = 2",
    "    GtkWidget::focus-line-width = 1",
    "    GtkButton::default-border = { 1, 1, 1, 1 }",
    "    GtkScrollbar::min-slider-length = 16",
    "    GtkRange::slider-width = 15",
    "    GtkRange::stepper-size = 15",
    "",
    "    bg[NORMAL]        = \"%{bg}\"",
    "    bg[PRELIGHT]      = \"%{bg}\"",
    "    bg[ACTIVE]        = \"%{sc}\"",
    "    bg[SELECTED]      = \"%{sel_bg}\"",
    "    bg[INSENSITIVE]   = \"%{bg}\"",
    "    fg[NORMAL]        = \"%{fg}\"",
    "    fg[PRELIGHT]      = \"%{fg}\"",
    "    fg[ACTIVE]        = \"%{fg}\"",
    "    fg[SELECTED]      = \"%{sel_fg}\"",
    "    fg[INSENSITIVE]   = \"%{dis}\"",
    "    base[NORMAL]      = \"%{base}\"",
    "    base[PRELIGHT]    = \"%{base}\"",
    "    base[ACTIVE]      = \"%{sel_bg}\"",
    "    base[SELECTED]    = \"%{sel_bg}\"",
    "    base[INSENSITIVE] = \"%{bg}\"",
    "    text[NORMAL]      = \"%{text}\"",
    "    text[PRELIGHT]    = \"%{text}\"",
    "    text[ACTIVE]      = \"%{sel_fg}\"",
    "    text[SELECTED]    = \"%{sel_fg}\"",
    "    text[INSENSITIVE] = \"%{dis}\"",
    "}",
    "",
    "style \"cde-menuitem\" = \"cde-default\"",
    "{",
    "    bg[PRELIGHT] = \"%{bg}\"",
    "    fg[PRELIGHT] = \"%{fg}\"",
    "}",
    "",
    "class \"GtkWidget\" style \"cde-default\"",
    "widget_class \"*MenuItem*\" style \"cde-menuitem\"",
    NULL
};

static void
emit_gtkrc(FILE *f, Scheme *s, const char *unused)
{
    emit_template(f, s, header_lines);
    emit_template(f, s, gtkrc_lines);
}

/* ------------------------------------------------------------------ */
/* GTK 3 / GTK 4                                                      */

/* Raised (outset) and sunken (inset) Motif bevels as CSS borders. */
#define RAISED	"border: 2px solid; border-color: @cde_ts @cde_bs @cde_bs @cde_ts;" \
		" border-radius: 0; box-shadow: none;"
#define SUNKEN	"border: 2px solid; border-color: @cde_bs @cde_ts @cde_ts @cde_bs;" \
		" border-radius: 0; box-shadow: none;"

/* GTK's built-in theme gives sane metrics for every widget; the CDE
 * look is layered on top.  GTK 3 calls it Adwaita, GTK 4 "Default". */
static const char *const gtk3_import[] = {
    "@import url(\"resource:///org/gtk/libgtk/theme/Adwaita/gtk-contained.css\");",
    "",
    NULL
};
static const char *const gtk4_import[] = {
    "@import url(\"resource:///org/gtk/libgtk/theme/Default/gtk.css\");",
    "",
    NULL
};

static const char *const gtk_css_lines[] = {
    "@define-color cde_bg %{bg};",
    "@define-color cde_fg %{fg};",
    "@define-color cde_ts %{ts};",
    "@define-color cde_bs %{bs};",
    "@define-color cde_sc %{sc};",
    "@define-color cde_base %{base};",
    "@define-color cde_text %{text};",
    "@define-color cde_sel_bg %{sel_bg};",
    "@define-color cde_sel_fg %{sel_fg};",
    "@define-color cde_dis %{dis};",
    "",
    "/* Named colors that applications and libadwaita look up */",
    "@define-color theme_bg_color @cde_bg;",
    "@define-color theme_fg_color @cde_fg;",
    "@define-color theme_base_color @cde_base;",
    "@define-color theme_text_color @cde_text;",
    "@define-color theme_selected_bg_color @cde_sel_bg;",
    "@define-color theme_selected_fg_color @cde_sel_fg;",
    "@define-color insensitive_bg_color @cde_bg;",
    "@define-color insensitive_fg_color @cde_dis;",
    "@define-color borders @cde_bs;",
    "@define-color window_bg_color @cde_bg;",
    "@define-color window_fg_color @cde_fg;",
    "@define-color view_bg_color @cde_base;",
    "@define-color view_fg_color @cde_text;",
    "@define-color accent_bg_color @cde_sel_bg;",
    "@define-color accent_fg_color @cde_sel_fg;",
    "@define-color accent_color @cde_sel_bg;",
    "@define-color headerbar_bg_color @cde_bg;",
    "@define-color headerbar_fg_color @cde_fg;",
    "@define-color popover_bg_color @cde_bg;",
    "@define-color popover_fg_color @cde_fg;",
    "@define-color dialog_bg_color @cde_bg;",
    "@define-color dialog_fg_color @cde_fg;",
    "@define-color card_bg_color @cde_base;",
    "@define-color card_fg_color @cde_text;",
    "@define-color sidebar_bg_color @cde_bg;",
    "@define-color sidebar_fg_color @cde_fg;",
    "",
    "window, window.background, dialog, .background, menubar, toolbar, .toolbar,",
    "headerbar, .titlebar, actionbar, statusbar, notebook > header, notebook > stack,",
    "stacksidebar, placessidebar, .sidebar, .navigation-sidebar,",
    "menu, .menu, .context-menu, popover, popover > contents {",
    "  background-color: @cde_bg; background-image: none; color: @cde_fg;",
    "  box-shadow: none; text-shadow: none;",
    "}",
    "headerbar, .titlebar { border-bottom: 2px solid @cde_bs; }",
    "separator { background-color: @cde_bs; }",
    "*:disabled { color: @cde_dis; }",
    "",
    "/* Push buttons: raised; pressed = sunken + select color, like Motif */",
    "button {",
    "  background-color: @cde_bg; background-image: none; color: @cde_fg;",
    "  text-shadow: none; -gtk-icon-shadow: none; " RAISED,
    "}",
    "button:hover { background-color: shade(@cde_bg, 1.04); background-image: none; }",
    "button:active, button:checked {",
    "  background-color: @cde_sc; background-image: none; " SUNKEN,
    "}",
    "button:disabled {",
    "  background-color: @cde_bg; background-image: none; color: @cde_dis; " RAISED,
    "}",
    "/* List/tree column headers are push buttons in Motif too */",
    "treeview.view header button, columnview > header > button,",
    "treeview.view header button:hover {",
    "  background-color: @cde_bg; background-image: none; color: @cde_fg; " RAISED,
    "}",
    "button.suggested-action, button.destructive-action {",
    "  background-color: @cde_bg; color: @cde_fg;",
    "}",
    "button.flat { background-color: transparent; border-color: transparent; }",
    "button.flat:hover { background-color: @cde_bg; " RAISED " }",
    "button.flat:active, button.flat:checked { background-color: @cde_sc; " SUNKEN " }",
    "",
    "/* Text fields and views: sunken, primary select color */",
    "entry, spinbutton:not(.vertical), searchbar entry, textview, .view,",
    "treeview.view, iconview, list, listview, columnview, gridview {",
    "  background-color: @cde_base; background-image: none; color: @cde_text;",
    "}",
    "entry, spinbutton:not(.vertical) { " SUNKEN " }",
    "entry:focus, entry:focus-within, spinbutton:focus-within {",
    "  border-color: @cde_bs @cde_ts @cde_ts @cde_bs;",
    "  box-shadow: inset 0 0 0 1px @cde_fg; outline: none;",
    "}",
    "frame > border, scrolledwindow.frame { " SUNKEN " }",
    "",
    "*:selected, .view:selected, treeview.view:selected, row:selected,",
    "iconview:selected, flowboxchild:selected,",
    "selection, entry selection, textview text selection, label selection {",
    "  background-color: @cde_sel_bg; color: @cde_sel_fg;",
    "}",
    "",
    "/* Menus: raised frame; the highlighted item is raised, as in Motif */",
    "menu, .menu, .context-menu, popover.menu, popover.menu > contents {",
    "  padding: 2px; " RAISED,
    "}",
    "menu menuitem, .menu menuitem, menubar > menuitem, popover.menu modelbutton {",
    "  color: @cde_fg; border: 2px solid transparent; border-radius: 0;",
    "}",
    "menu menuitem:hover, .menu menuitem:hover, menubar > menuitem:hover,",
    "popover.menu modelbutton:hover {",
    "  background-color: @cde_bg; color: @cde_fg; " RAISED,
    "}",
    "",
    "/* Scrollbars: select-color trough, raised slider */",
    "scrollbar, scrollbar trough {",
    "  background-color: @cde_sc; background-image: none;",
    "  border: none; border-radius: 0;",
    "}",
    "scrollbar slider {",
    "  min-width: 13px; min-height: 13px; margin: 0;",
    "  background-color: @cde_bg; " RAISED,
    "}",
    "scrollbar slider:hover { background-color: @cde_bg; }",
    "scrollbar slider:active { background-color: @cde_sc; }",
    "",
    "check, radio {",
    "  min-width: 12px; min-height: 12px;",
    "  background-color: @cde_base; background-image: none; color: @cde_text;",
    "  -gtk-icon-shadow: none; " SUNKEN,
    "}",
    "radio { border-radius: 50%; }",
    "check:checked, check:indeterminate, radio:checked,",
    "check:hover, radio:hover, check:checked:hover, radio:checked:hover {",
    "  background-color: @cde_sc; background-image: none; color: @cde_fg;",
    "  border-color: @cde_bs @cde_ts @cde_ts @cde_bs; box-shadow: none;",
    "}",
    "check:not(:checked):not(:indeterminate):hover, radio:not(:checked):hover {",
    "  background-color: @cde_base;",
    "}",
    "",
    "switch { background-color: @cde_sc; background-image: none; " SUNKEN " }",
    "switch:checked { background-color: @cde_sel_bg; }",
    "switch slider { background-color: @cde_bg; background-image: none; " RAISED " }",
    "",
    "progressbar trough, scale trough, levelbar trough {",
    "  background-color: @cde_sc; background-image: none; " SUNKEN,
    "}",
    "progressbar progress, scale highlight, scale trough highlight,",
    "levelbar block.filled {",
    "  background-color: @cde_sel_bg; background-image: none; border: none;",
    "  border-radius: 0; box-shadow: none;",
    "}",
    "scale slider { background-color: @cde_bg; background-image: none; " RAISED " }",
    "",
    "notebook > header > tabs > tab {",
    "  background-color: @cde_sc; color: @cde_fg; " RAISED,
    "}",
    "notebook > header > tabs > tab:checked { background-color: @cde_bg; }",
    "",
    "tooltip, tooltip.background, tooltip > contents {",
    "  background-color: @cde_base; color: @cde_text;",
    "  border: 1px solid @cde_fg; border-radius: 0; box-shadow: none;",
    "}",
    "tooltip * { color: @cde_text; }",
    NULL
};

/* GTK 4 dropped the "border" sub-node of frames. */
static const char *const gtk4_extra_lines[] = {
    "frame { " SUNKEN " }",
    NULL
};

static void
emit_gtk_css(FILE *f, Scheme *s, const char *version)
{
    int gtk4 = (version && strcmp(version, "4") == 0);

    fputs("/* Generated by dtstyle from the current CDE palette. Do not edit:\n"
	  " * changes are overwritten when the palette changes. */\n\n", f);
    emit_template(f, s, gtk4 ? gtk4_import : gtk3_import);
    emit_template(f, s, gtk_css_lines);
    if (gtk4)
	emit_template(f, s, gtk4_extra_lines);
}

static const char *const index_theme_lines[] = {
    "[Desktop Entry]",
    "Type=X-GNOME-Metatheme",
    "Name=" THEME_NAME,
    "Comment=Generated by dtstyle from the current CDE palette",
    "Encoding=UTF-8",
    "",
    "[X-GNOME-Metatheme]",
    "GtkTheme=" THEME_NAME,
    NULL
};

static void
emit_index_theme(FILE *f, Scheme *s, const char *unused)
{
    emit_template(f, s, index_theme_lines);
}

/* ------------------------------------------------------------------ */
/* Qt 5 / Qt 6 (qt5ct / qt6ct color scheme)                           */

/*
 * QPalette::ColorRole order used by qt5ct/qt6ct:
 * WindowText Button Light Midlight Dark Mid Text BrightText ButtonText
 * Base Window Shadow Highlight HighlightedText Link LinkVisited
 * AlternateBase NoRole ToolTipBase ToolTipText PlaceholderText
 */
static const char *const qt_color_lines[] = {
    "[ColorScheme]",
    "active_colors=%{fg}, %{bg}, %{ts}, %{midlight}, %{bs}, %{mid}, %{text},"
	" #ffffff, %{fg}, %{base}, %{bg}, #000000, %{sel_bg}, %{sel_fg},"
	" #0000ee, #551a8b, %{alt_base}, %{bg}, %{base}, %{text}, %{placeholder}",
    "inactive_colors=%{fg}, %{bg}, %{ts}, %{midlight}, %{bs}, %{mid}, %{text},"
	" #ffffff, %{fg}, %{base}, %{bg}, #000000, %{sel_bg}, %{sel_fg},"
	" #0000ee, #551a8b, %{alt_base}, %{bg}, %{base}, %{text}, %{placeholder}",
    "disabled_colors=%{dis}, %{bg}, %{ts}, %{midlight}, %{bs}, %{mid},"
	" %{placeholder}, #ffffff, %{dis}, %{base}, %{bg}, #000000, %{sel_bg},"
	" %{sel_fg}, #0000ee, #551a8b, %{alt_base}, %{bg}, %{base}, %{text},"
	" %{placeholder}",
    NULL
};

static void
emit_qt_colors(FILE *f, Scheme *s, const char *unused)
{
    emit_template(f, s, qt_color_lines);
}

static void
emit_qtct_conf(FILE *f, Scheme *s, const char *scheme_path)
{
    fputs("[Appearance]\ncolor_scheme_path=", f);
    fputs(scheme_path, f);
    fputs("\ncustom_palette=true\nstyle=Fusion\n\n[Clanker]\n" GEN_MARKER "\n", f);
}

/* True when path is missing or carries our marker (i.e. ours to own). */
static int
may_write_conf(const char *path)
{
    FILE *f;
    char  line[256];
    int   ours = 0;

    if ((f = fopen(path, "r")) == NULL)
	return errno == ENOENT;
    while (fgets(line, sizeof(line), f))
	if (strncmp(line, GEN_MARKER, strlen(GEN_MARKER)) == 0) {
	    ours = 1;
	    break;
	}
    fclose(f);
    return ours;
}

static void
write_qt(Scheme *s, const char *tool)
{
    char cfg[MAXPATHLEN], dir[MAXPATHLEN], scheme[MAXPATHLEN], conf[MAXPATHLEN];
    char name[32];

    config_home(cfg, sizeof(cfg));
    snprintf(dir, sizeof(dir), "%s/%s/colors", cfg, tool);
    snprintf(scheme, sizeof(scheme), "%s/" THEME_NAME ".conf", dir);
    write_file(dir, THEME_NAME ".conf", emit_qt_colors, s, NULL);

    snprintf(dir, sizeof(dir), "%s/%s", cfg, tool);
    snprintf(conf, sizeof(conf), "%s/%s.conf", dir, tool);
    if (may_write_conf(conf)) {
	snprintf(name, sizeof(name), "%s.conf", tool);
	write_file(dir, name, emit_qtct_conf, s, scheme);
    }
}

/* ------------------------------------------------------------------ */
/* session environment                                                */

/* Plain Bourne/ksh88 syntax: sourced by the ksh Xsession everywhere. */
static const char *const env_lines[] = {
    "# Generated by dtstyle: apply the CDE palette to GTK and Qt applications.",
    "# Sourced at login by Xsession.d/0070.dtmodern.  To turn this off, untick",
    "# \"Apply colors to GTK and Qt applications\" in Settings > Color.",
    "# GTK 3/4 follow the theme live through XSETTINGS (dtxsettings);",
    "# without a settings daemon, GTK_THEME selects it at start.",
    "if ! \"${DT_BINPATH:-/usr/dt/bin}/dtxsettings\" start 2>/dev/null; then",
    "  GTK_THEME=" THEME_NAME "; export GTK_THEME",
    "fi",
    "GTK2_RC_FILES=\"$HOME/.themes/" THEME_NAME "/gtk-2.0/gtkrc\"",
    "GTK_OVERLAY_SCROLLING=0",
    "export GTK2_RC_FILES GTK_OVERLAY_SCROLLING",
    "if [ -z \"$QT_QPA_PLATFORMTHEME\" ]; then",
    "  if type qt5ct >/dev/null 2>&1; then",
    "    QT_QPA_PLATFORMTHEME=qt5ct; export QT_QPA_PLATFORMTHEME",
    "  elif type qt6ct >/dev/null 2>&1; then",
    "    QT_QPA_PLATFORMTHEME=qt6ct; export QT_QPA_PLATFORMTHEME",
    "  fi",
    "fi",
    NULL
};

static void
emit_env(FILE *f, Scheme *s, const char *unused)
{
    emit_template(f, NULL, env_lines);
}

static void
env_dir(char *buf, size_t len)
{
    snprintf(buf, len, "%s/.dt/theming", home_dir());
}

/* ------------------------------------------------------------------ */
/* public                                                             */

Boolean
ExtThemeEnabled(void)
{
    char dir[MAXPATHLEN], path[MAXPATHLEN];

    env_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/env", dir);
    return access(path, F_OK) == 0;
}

static void
generate(palette *p)
{
    Scheme s;
    char   dir[MAXPATHLEN];

    if (p == NULL || p->num_of_colors <= 0)
	return;
    make_scheme(p, &s);

    snprintf(dir, sizeof(dir), "%s/.themes/" THEME_NAME, home_dir());
    write_file(dir, "index.theme", emit_index_theme, &s, NULL);
    snprintf(dir, sizeof(dir), "%s/.themes/" THEME_NAME "/gtk-2.0", home_dir());
    write_file(dir, "gtkrc", emit_gtkrc, &s, NULL);
    snprintf(dir, sizeof(dir), "%s/.themes/" THEME_NAME "/gtk-3.0", home_dir());
    write_file(dir, "gtk.css", emit_gtk_css, &s, "3");
    snprintf(dir, sizeof(dir), "%s/.themes/" THEME_NAME "/gtk-4.0", home_dir());
    write_file(dir, "gtk.css", emit_gtk_css, &s, "4");

    write_qt(&s, "qt5ct");
    write_qt(&s, "qt6ct");
}

/* The theme files alone (for the Xfce applications, see dtmodernapp),
 * without selecting them for every GTK/Qt application. */
void
ExtThemeGenerate(palette *p)
{
    generate(p);
}

static Boolean
ModernAppsOn(void)
{
    char path[MAXPATHLEN];

    snprintf(path, sizeof(path), "%s/.dt/modern-apps", home_dir());
    return access(path, F_OK) == 0;
}

/* Running GTK applications pick up the new files (dtxsettings). */
void
ExtThemeLive(void)
{
    static char *argv[] = { "dtxsettings", "reload", NULL };

    (void) SysCmdRun(argv, NULL, 10, NULL, NULL);
}

void
ExtThemeApply(palette *p)
{
    if (ExtThemeEnabled() || ModernAppsOn()) {
	generate(p);
	if (ExtThemeEnabled()) {	/* refresh an older env file too */
	    char dir[MAXPATHLEN];

	    env_dir(dir, sizeof(dir));
	    write_file(dir, "env", emit_env, NULL, NULL);
	}
	ExtThemeLive();
    }
}

void
ExtThemeSetEnabled(Boolean enable, palette *p)
{
    char dir[MAXPATHLEN], path[MAXPATHLEN];

    env_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/env", dir);

    if (enable) {
	generate(p);
	write_file(dir, "env", emit_env, NULL, NULL);
    } else {
	/* Leave the generated theme files; only stop selecting them. */
	(void) unlink(path);
    }
    ExtThemeLive();	/* starts or stops xsettingsd as needed */
}
