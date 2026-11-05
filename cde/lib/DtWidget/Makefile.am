unixdir = $(CDE_DATA_TOP)/app-defaults/C

MAINTAINERCLEANFILES = Makefile.in

lib_LTLIBRARIES = libDtWidget.la

AM_CPPFLAGS = -DCDE_INSTALLATION_TOP=$(CDE_INSTALLATION_TOP) $(DT_INCDIR) \
-DCDE_DATA_TOP=$(CDE_DATA_TOP)

libDtWidget_la_CFLAGS = -DI18N_MSG -DMULTIBYTE

# in order to try to keep lib versions the same across platforms, (2.1.0)
if BSD
libDtWidget_la_LDFLAGS = -version-info 2:1:0
else
libDtWidget_la_LDFLAGS = -version-info 3:0:1
endif

libDtWidget_la_LIBADD = $(PNGLIB)

if SOLARIS
libDtWidget_la_CFLAGS += -DNO_REGCOMP
libDtWidget_la_LIBADD += -lw -lintl
endif


libDtWidget_la_SOURCES = ComboBox.c      Control.c       DialogBox.c \
			 DtMsgs.c        DtStrDefs.c     EditAreaData.c \
			 EditCalls.c     Editor.c        Icon.c \
			 MenuButton.c    NewCvt.c        SearchCalls.c \
			 SearchDlg.c     SpinBox.c       TitleBox.c \
			 View.c          Picture.c

# cpp this...
BUILT_SOURCES = UNIXbindings
CLEANFILES = UNIXbindings

$(top_builddir)/util/tradcpp/tradcpp:
	$(MAKE) -C $(top_builddir)/util/tradcpp tradcpp

UNIXbindings: UNIXbindings.cpp $(top_builddir)/util/tradcpp/tradcpp
	$(RM) $@
	$(GENCPP) -P -DXCOMM=# $(AM_CPPFLAGS) $< > $@
