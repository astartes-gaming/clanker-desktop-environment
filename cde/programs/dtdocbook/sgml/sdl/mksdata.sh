#!/bin/sh
# mksdata.sh - make the SDATA entity sets used for help volumes (SDL).
#
# Usage (from cde/programs/dtdocbook/sgml): sh sdl/mksdata.sh
#
# The ISO entity sets in ../ define characters as Unicode (CDATA "&#N;").
# DtHelp cannot show most of those in a help volume: they are dropped
# in the C locale and drawn as a wrong glyph in UTF-8 locales.  It does
# draw its own special characters (lib/DtHelp/XInterface.c, SpcTable)
# from the DT-SYMBOL-1 and ISO-8859-1 fonts in any locale, so for
# those entities these copies use SDATA "[name  ]", which dtdocbook2sdl
# turns into <SPC NAME="[name  ]">, as CDE's entity sets did before.
# Other entities stay Unicode.  dtdocbook2sdl puts sdl/catalog first.
# Part of the Clanker Desktop Environment (LGPL-2.0-or-later).

SPC=../../../lib/DtHelp/XInterface.c
names=`grep -o '{"\[[a-zA-Z0-9 ]\{6\}\]"' $SPC | sed 's/{"\[//; s/\]"//; s/ *$//' | sort -u`

for f in isoamsa isoamsb isoamso isogrk3 isolat1 isonum isopub isotech; do
    awk -v names="$names" '
        BEGIN { n = split(names, a, /[ \n]+/); for (i = 1; i <= n; i++) spc[a[i]] = 1 }
        /^<!ENTITY [A-Za-z0-9]+ +CDATA "&#[0-9]+;"/ {
            name = $2
            if (name in spc) {
                pad = substr(name "      ", 1, 6)
                sub(/CDATA "&#[0-9]+;"/, "SDATA \"[" pad "]\"")
            }
        }
        { print }
    ' $f.gml > sdl/$f.gml
done
