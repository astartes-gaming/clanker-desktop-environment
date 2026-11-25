/*
 * dticontheme - CDE's icons as a freedesktop.org icon theme, "CDE", so
 * that GTK and Qt applications (and the application menu) can show
 * them.  Settings > Session > Icons runs it once; GTK and Qt then use
 * the theme through XSETTINGS (dtxsettings).
 *
 *   dticontheme [DESTDIR]    default: $XDG_DATA_HOME/icons/CDE
 *
 * Every color CDE icon (XPM, the .t .s .m .l sizes) becomes a PNG under
 * its own name, and the common freedesktop names (folder, user-home,
 * utilities-terminal, accessories-calculator, firefox...) point to the
 * CDE picture for them.  What CDE has no picture for comes from the
 * themes the index inherits (Breeze, Adwaita, hicolor).  The CDE
 * symbolic colors take their default values.
 *
 * Needs libXpm and libpng (already used by the desktop); an X display
 * is only used to look up color names, when there is one.
 *
 * Part of the Clanker Desktop Environment (LGPL-2.0-or-later).
 */

#ifdef HAVE_CONFIG_H
#include <cde_config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <X11/Xlib.h>
#include <X11/xpm.h>
#ifdef HAVE_LIBPNG
#include <png.h>
#endif

#ifndef CDE_INSTALLATION_TOP
#define CDE_INSTALLATION_TOP "/usr/dt"
#endif

static const int sizes[] = { 16, 24, 32, 48 };
#define NSIZES 4

/* freedesktop name -> CDE icon */
static const char *const aliases[][2] = {
    { "user-home", "Dthome" }, { "folder-home", "Dthome" },
    { "go-home", "Fphome" },
    { "folder", "DtdirB" }, { "inode-directory", "DtdirB" },
    { "folder-open", "DtdirY" }, { "folder-locked", "Dtdirlk" },
    { "folder-new", "Dtdirnw" }, { "go-up", "Dtdirup" },
    { "user-trash", "Fptrsh" }, { "user-trash-full", "Fptrsh7" },
    { "text-x-generic", "Dtdata" }, { "unknown", "Dtdata" },
    { "application-octet-stream", "Dtdata" },
    { "application-x-executable", "Dtexec" },
    { "application-x-shellscript", "Dtshell" },
    { "text-x-script", "Dtshell" }, { "text-x-csrc", "DtdotC" },
    { "text-x-chdr", "DtdotH" }, { "text-x-c++src", "DtC++" },
    { "text-x-makefile", "Dtimake" }, { "text-x-readme", "DtRdMe" },
    { "text-html", "SDthtml" }, { "application-x-archive", "Dtcmprs" },
    { "package-x-generic", "Dtcmprs" }, { "application-x-compressed-tar", "Dtcmprs" },
    { "application-pdf", "Dtps" }, { "application-postscript", "Dtps" },
    { "image-x-generic", "Dtimage" }, { "image-gif", "Dtgif" },
    { "image-jpeg", "Dtjpeg" }, { "image-tiff", "Dttif" },
    { "image-x-xpixmap", "Dtpixmp" }, { "image-x-xbitmap", "Dtbitmp" },
    { "audio-x-generic", "Dtaudio" }, { "video-x-generic", "Dtmpeg" },
    { "x-office-spreadsheet", "Dtsprds" }, { "x-office-document", "Dtpenpd" },
    { "x-office-calendar", "DtCM" }, { "office-calendar", "DtCM" },
    { "appointment-new", "DtCMapt" },
    { "utilities-terminal", "Dtterm" }, { "terminal", "Dtterm" },
    { "org.gnome.Terminal", "Dtterm" }, { "org.kde.konsole", "Dtterm" },
    { "xfce4-terminal", "Dtterm" }, { "kitty", "Dtterm" },
    { "accessories-text-editor", "Dtpenpd" }, { "text-editor", "Dtpenpd" },
    { "org.gnome.TextEditor", "Dtpenpd" }, { "org.kde.kate", "Dtpenpd" },
    { "org.kde.kwrite", "Dtpenpd" }, { "mousepad", "Dtpenpd" },
    { "accessories-calculator", "Dtcalc" }, { "org.gnome.Calculator", "Dtcalc" },
    { "org.kde.kcalc", "Dtcalc" }, { "galculator", "Dtcalc" },
    { "internet-mail", "DtMail" }, { "mail-client", "DtMail" },
    { "evolution", "DtMail" }, { "mail-unread", "DtMnew" },
    { "mail-read", "DtMmsg" }, { "mail-message-new", "DtMnew" },
    { "system-file-manager", "Dtadskt" }, { "file-manager", "Dtadskt" },
    { "org.gnome.Nautilus", "Dtadskt" }, { "org.kde.dolphin", "Dtadskt" },
    { "thunar", "Dtadskt" }, { "pcmanfm", "Dtadskt" },
    { "preferences-desktop", "Fpstyle" }, { "preferences-system", "Fpstyle" },
    { "systemsettings", "Fpstyle" }, { "org.gnome.Settings", "Fpstyle" },
    { "preferences-desktop-color", "DtColor" },
    { "preferences-desktop-font", "DtFont" },
    { "preferences-desktop-wallpaper", "DtBkdrp" },
    { "preferences-desktop-keyboard", "DtKeybd" },
    { "input-keyboard", "DtKeybd" }, { "input-mouse", "DtMouse" },
    { "preferences-desktop-peripherals", "DtMouse" },
    { "preferences-desktop-screensaver", "DtScrn" }, { "video-display", "DtScrn" },
    { "preferences-desktop-locale", "DtI18N" },
    { "preferences-desktop-accessibility", "DtAccess" },
    { "preferences-system-network", "DtNetwk" }, { "network-wired", "DtNetwk" },
    { "network-workgroup", "DtNetwk" },
    { "preferences-system-bluetooth", "DtBlue" }, { "bluetooth", "DtBlue" },
    { "system-software-update", "DtUpdat" },
    { "preferences-system-windows", "DtDtwm" },
    { "preferences-system-session", "DtStart" },
    { "audio-volume-high", "Dthvol" }, { "multimedia-volume-control", "Dthvol" },
    { "help-browser", "Dthelp" }, { "help-contents", "Dthelp" },
    { "system-help", "Dthelp" }, { "help-about", "Dthinfo" },
    { "system-search", "DtSearch" }, { "edit-find", "Dtsrch" },
    { "printer", "Fpprnt" }, { "document-print", "Dtprint" },
    { "printer-printing", "DtPrtjb" },
    { "applications-graphics", "DtGraphics" }, { "applications-games", "DtGamesCards" },
    { "applications-office", "DtOffice" }, { "applications-education", "DtEducation" },
    { "applications-system", "DtSystem" }, { "applications-other", "Fpapps" },
    { "applications-utilities", "Dtautil" }, { "applications-accessories", "Dtautil" },
    { "applications-multimedia", "Dtapplications-multimedia" },
    { "applications-development", "DtBldr" }, { "applications-science", "Dtinfo" },
    { "applications-internet", "SDtwebbr" },
    { "web-browser", "SDtwebbr" }, { "internet-web-browser", "SDtwebbr" },
    { "system-lock-screen", "Fplock" }, { "system-log-out", "Fpexit" },
    { "application-exit", "Fpexit" },
    { "clock", "Fpclock" }, { "preferences-system-time", "Dtclock" },
    { "accessories-dictionary", "Dtinfo" }, { "accessories-screenshot", "Dtpaint" },
    { "org.gnome.Screenshot", "Dtpaint" }, { "spectacle", "Dtpaint" },
    { "image-viewer", "Dtimage" }, { "eog", "Dtimage" },
    { "org.gnome.eog", "Dtimage" }, { "gwenview", "Dtimage" },
    { "graphics-viewer", "Dtimage" },
    { "utilities-system-monitor", "Dtload" }, { "org.gnome.SystemMonitor", "Dtload" },
    { "media-optical", "Dtmagtp" }, { "drive-removable-media", "Dtmagtp" },
    { "media-floppy", "Dtmagtp" },
    { "dialog-information", "Dthinfo" }, { "emblem-symbolic-link", "Dtlink" },
    { "list-add", "Dtplus" }, { "list-remove", "Dtminus" },
    { "bookmark-new", "Dtbmark" }, { "bookmarks", "Dtbmark" },
    { "manpages", "Dtmanpg" }, { "help-faq", "DtRdMe" },
    { "chromium", "chrome" }, { "google-chrome", "chrome" },
    { "org.mozilla.firefox", "firefox" }, { "firefox-esr", "firefox" },
    { "org.mozilla.Thunderbird", "thunderbird" },
    { "org.videolan.VLC", "vlc" }, { "org.gimp.GIMP", "gimp" },
    { "org.inkscape.Inkscape", "inkscape" },
    { "org.kde.okular", "okular" }, { "nautilus", "Dtadskt" },
    { NULL, NULL }
};

static Display *dpy;

/* "#rrggbb", "#rrrrggggbbbb", a color name, or None. */
static int
ParseColor(const char *s, unsigned char rgba[4])
{
    XColor c;
    unsigned v[3];
    size_t n;

    rgba[3] = 255;
    if (!s || strcasecmp(s, "none") == 0 || strcasecmp(s, "transparent") == 0)
    {
        rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
        return 1;
    }
    n = strlen(s);
    if (s[0] == '#' && n == 13 &&
        sscanf(s + 1, "%4x%4x%4x", &v[0], &v[1], &v[2]) == 3)
    {
        rgba[0] = v[0] >> 8; rgba[1] = v[1] >> 8; rgba[2] = v[2] >> 8;
        return 1;
    }
    if (s[0] == '#' && n == 7 &&
        sscanf(s + 1, "%2x%2x%2x", &v[0], &v[1], &v[2]) == 3)
    {
        rgba[0] = v[0]; rgba[1] = v[1]; rgba[2] = v[2];
        return 1;
    }
    if (dpy && XParseColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), s, &c))
    {
        rgba[0] = c.red >> 8; rgba[1] = c.green >> 8; rgba[2] = c.blue >> 8;
        return 1;
    }
    if (strcasecmp(s, "black") == 0) { rgba[0] = rgba[1] = rgba[2] = 0; return 1; }
    if (strcasecmp(s, "white") == 0) { rgba[0] = rgba[1] = rgba[2] = 255; return 1; }
    rgba[0] = rgba[1] = rgba[2] = 128;	/* unknown name: gray */
    return 1;
}

static int
MkDirs(const char *path)
{
    char buf[PATH_MAX], *p;

    if (strlen(path) >= sizeof(buf))
        return -1;
    strcpy(buf, path);
    for (p = buf + 1; *p; p++)
        if (*p == '/')
        {
            *p = '\0';
            if (mkdir(buf, 0755) < 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    return (mkdir(buf, 0755) < 0 && errno != EEXIST) ? -1 : 0;
}

/* The standard size for an icon w x h. */
static int
SizeFor(int w, int h)
{
    int m = w > h ? w : h, i;

    for (i = 0; i < NSIZES; i++)
        if (m <= sizes[i] + sizes[i] / 4)
            return sizes[i];
    return sizes[NSIZES - 1];
}

#ifdef HAVE_LIBPNG
/* XPM file -> PNG of size x size (centered, cropped if larger). */
static int
Convert(const char *xpmPath, const char *pngPath, int size)
{
    XpmImage  img;
    XpmInfo   info;
    unsigned char (*pal)[4], *rgba, **rows;
    unsigned  i, x, y;
    int       ox, oy;
    volatile int ok = 0;
    FILE     *fp;
    png_structp png;
    png_infop   pinfo;

    if (XpmReadFileToXpmImage((char *) xpmPath, &img, &info) != XpmSuccess)
        return 0;
    pal = calloc(img.ncolors ? img.ncolors : 1, 4);
    rgba = calloc((size_t) size * size, 4);
    rows = calloc(size, sizeof(*rows));
    if (!pal || !rgba || !rows)
        goto out;
    for (i = 0; i < img.ncolors; i++)
    {
        XpmColor *c = &img.colorTable[i];
        const char *s = c->c_color ? c->c_color : c->g_color ? c->g_color :
                        c->g4_color ? c->g4_color : c->m_color;

        ParseColor(s, pal[i]);
    }
    ox = (size - (int) img.width) / 2;
    oy = (size - (int) img.height) / 2;
    for (y = 0; y < img.height; y++)
        for (x = 0; x < img.width; x++)
        {
            int tx = (int) x + ox, ty = (int) y + oy;
            unsigned idx = img.data[y * img.width + x];

            if (tx < 0 || ty < 0 || tx >= size || ty >= size || idx >= img.ncolors)
                continue;
            memcpy(rgba + ((size_t) ty * size + tx) * 4, pal[idx], 4);
        }
    for (y = 0; y < (unsigned) size; y++)
        rows[y] = rgba + (size_t) y * size * 4;
    if ((fp = fopen(pngPath, "wb")) == NULL)
        goto out;
    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    pinfo = png ? png_create_info_struct(png) : NULL;
    if (png && pinfo && !setjmp(png_jmpbuf(png)))
    {
        png_init_io(png, fp);
        png_set_IHDR(png, pinfo, size, size, 8, PNG_COLOR_TYPE_RGBA,
                     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                     PNG_FILTER_TYPE_DEFAULT);
        png_write_info(png, pinfo);
        png_write_image(png, rows);
        png_write_end(png, NULL);
        ok = 1;
    }
    if (png)
        png_destroy_write_struct(&png, pinfo ? &pinfo : NULL);
    fclose(fp);
    if (!ok)
        unlink(pngPath);
out:
    free(pal);
    free(rgba);
    free(rows);
    XpmFreeXpmImage(&img);
    XpmFreeXpmInfo(&info);
    return ok;
}
#endif

/* "Dtcalc.l.pm" -> "Dtcalc"; NULL if not a color icon file */
static int
BaseName(const char *file, char *out, size_t len)
{
    size_t n = strlen(file);

    if (n < 4 || strcmp(file + n - 3, ".pm") != 0)
        return 0;
    n -= 3;
    if (n > 2 && file[n - 2] == '.' && strchr("tsml", file[n - 1]))
        n -= 2;
    if (n == 0 || n >= len)
        return 0;
    memcpy(out, file, n);
    out[n] = '\0';
    return 1;
}

static int
Exists(const char *p)
{
    return access(p, F_OK) == 0;
}

int
main(int argc, char **argv)
{
    char  dest[PATH_MAX], src[PATH_MAX], path[PATH_MAX], png[PATH_MAX];
    const char *home = getenv("HOME"), *xdg = getenv("XDG_DATA_HOME");
    const char *srcdirs[] = { CDE_INSTALLATION_TOP "/appconfig/icons/C",
                              "/etc/dt/appconfig/icons/C", NULL };
    DIR  *d;
    struct dirent *e;
    FILE *fp;
    int   i, s, made = 0;

    if (argc > 1)
        snprintf(dest, sizeof(dest), "%s", argv[1]);
    else if (xdg && *xdg == '/')
        snprintf(dest, sizeof(dest), "%s/icons/CDE", xdg);
    else if (home)
        snprintf(dest, sizeof(dest), "%s/.local/share/icons/CDE", home);
    else
        return 1;
#ifndef HAVE_LIBPNG
    fprintf(stderr, "dticontheme: built without libpng\n");
    return 1;
#else
    dpy = XOpenDisplay(NULL);		/* for color names; optional */
    for (s = 0; s < NSIZES; s++)
    {
        snprintf(path, sizeof(path), "%s/%dx%d/apps", dest, sizes[s], sizes[s]);
        if (MkDirs(path) != 0)
        {
            perror(path);
            return 1;
        }
    }
    for (i = 0; srcdirs[i]; i++)
    {
        if ((d = opendir(srcdirs[i])) == NULL)
            continue;
        while ((e = readdir(d)) != NULL)
        {
            char base[256];
            XpmImage img;
            XpmInfo  info;
            int      size;

            if (!BaseName(e->d_name, base, sizeof(base)))
                continue;
            snprintf(src, sizeof(src), "%s/%s", srcdirs[i], e->d_name);
            /* the size from the header */
            if (XpmReadFileToXpmImage(src, &img, &info) != XpmSuccess)
                continue;
            size = SizeFor((int) img.width, (int) img.height);
            XpmFreeXpmImage(&img);
            XpmFreeXpmInfo(&info);
            snprintf(png, sizeof(png), "%s/%dx%d/apps/%s.png", dest, size,
                     size, base);
            if (Exists(png))
                continue;			/* a site icon (first dir) wins */
            made += Convert(src, png, size);
        }
        closedir(d);
    }
    /* freedesktop names: symbolic links to the CDE picture */
    for (s = 0; s < NSIZES; s++)
        for (i = 0; aliases[i][0]; i++)
        {
            char target[300];

            snprintf(png, sizeof(png), "%s/%dx%d/apps/%s.png", dest, sizes[s],
                     sizes[s], aliases[i][1]);
            if (!Exists(png))
                continue;
            snprintf(path, sizeof(path), "%s/%dx%d/apps/%s.png", dest,
                     sizes[s], sizes[s], aliases[i][0]);
            snprintf(target, sizeof(target), "%s.png", aliases[i][1]);
            unlink(path);
            if (symlink(target, path) != 0)
                perror(path);
        }
    snprintf(path, sizeof(path), "%s/index.theme", dest);
    if ((fp = fopen(path, "w")) == NULL)
    {
        perror(path);
        return 1;
    }
    fprintf(fp, "[Icon Theme]\nName=CDE\n"
            "Comment=The Common Desktop Environment's icons (Clanker)\n"
            "Inherits=breeze,Adwaita,gnome,hicolor\nExample=Dthome\n"
            "Directories=");
    for (s = 0; s < NSIZES; s++)
        fprintf(fp, "%s%dx%d/apps", s ? "," : "", sizes[s], sizes[s]);
    fprintf(fp, "\n");
    for (s = 0; s < NSIZES; s++)
        fprintf(fp, "\n[%dx%d/apps]\nSize=%d\nContext=Applications\n"
                "Type=Threshold\nThreshold=%d\n", sizes[s], sizes[s],
                sizes[s], sizes[s] / 3);
    fclose(fp);
    if (dpy)
        XCloseDisplay(dpy);
    printf("%d icons in %s\n", made, dest);
    return 0;
#endif
}
