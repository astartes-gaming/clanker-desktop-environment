MAINTAINERCLEANFILES = Makefile.in

bin_PROGRAMS = dtappgather

dtappgather_CXXFLAGS = -I../libCliSrv $(DT_INCDIR) \
    -DCDE_INSTALLATION_TOP=\"$(CDE_INSTALLATION_TOP)\" \
    -DCDE_DATA_TOP=\"$(CDE_DATA_TOP)\" \
    -DCDE_CONFIGURATION_TOP=\"$(CDE_CONFIGURATION_TOP)\" \
    -DCDE_LOGFILES_TOP=\"$(CDE_LOGFILES_TOP)\"

dtappgather_LDADD = ../libCliSrv/libCliSrv.a $(DTCLIENTLIBS) $(XTOOLLIB)

if SOLARIS
dtappgather_LDADD += -lintl
endif

dtappgather_SOURCES = Options.C dtappgather.C dtappgather.h Options.h

# dtappgather is no longer setuid root: users create their own session
# directory in these shared, sticky directories (like /tmp).
install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(CDE_LOGFILES_TOP)/appconfig/appmanager \
		$(DESTDIR)$(CDE_LOGFILES_TOP)/tmp
	-chmod 1777 $(DESTDIR)$(CDE_LOGFILES_TOP)/appconfig/appmanager \
		$(DESTDIR)$(CDE_LOGFILES_TOP)/tmp
