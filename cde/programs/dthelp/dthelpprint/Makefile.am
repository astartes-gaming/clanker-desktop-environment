MAINTAINERCLEANFILES = Makefile.in

bin_PROGRAMS = dthelpprint
bin_SCRIPTS = dthelpprint.sh

BUILT_SOURCES = dthelpprint.sh
CLEANFILES = dthelpprint.sh
EXTRA_DIST = dthelpprint.sh.in

dthelpprint.sh: $(srcdir)/dthelpprint.sh.in Makefile
	sed -e 's|@CDE_BINDIR@|$(bindir)|g' $< > $@.tmp && \
	    chmod 755 $@.tmp && mv $@.tmp $@

dthelpprint_CPPFLAGS = -I.. -I ../../../lib/DtHelp \
	-DCDE_INSTALLATION_TOP='"$(CDE_INSTALLATION_TOP)"' \
	-DCDE_DATA_TOP='"$(CDE_DATA_TOP)"'

dthelpprint_LDADD = $(DTCLIENTLIBS) $(XTOOLLIB)

if SOLARIS
dthelpprint_LDADD += -lw -lgen
endif

dthelpprint_SOURCES = Main.c Initialize.c PrintUtil.c PrintTopics.c \
		      PrintManStrFile.c
