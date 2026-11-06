MAINTAINERCLEANFILES = Makefile.in

LOCAL_CPP_DEFINES = -DCDE_INSTALLATION_TOP=$(CDE_INSTALLATION_TOP) \
-DCDE_DATA_TOP=$(CDE_DATA_TOP) \
	-DCDE_CONFIGURATION_TOP=$(CDE_CONFIGURATION_TOP) \
	-DCDE_LOGFILES_TOP=$(CDE_LOGFILES_TOP) -DKORNSHELL=$(KSH) \
	-DDTKORNSHELL=$(CDE_INSTALLATION_TOP)/bin/dtksh

if SOLARIS
LOCAL_CPP_DEFINES += -D__sun
endif

# cpp does not expand macros inside '...' (it takes them for character
# constants), which is where most EXEC_STRINGs keep their commands.
FIXPATHS = sed -e 's|CDE_INSTALLATION_TOP|$(CDE_INSTALLATION_TOP)|g' \
	-e 's|CDE_CONFIGURATION_TOP|$(CDE_CONFIGURATION_TOP)|g' \
	-e 's|CDE_DATA_TOP|$(CDE_DATA_TOP)|g' \
	-e 's|CDE_LOGFILES_TOP|$(CDE_LOGFILES_TOP)|g'

BUILT_SOURCES = autoStart.dt compat.dt datatypes.dt develop.dt		\
	dtappman.dt dt.dt dtcm.dt dtfile.dt dthelp.dt dthelpprint.dt	\
	dthelptag.dt dtims.dt dtinfo.dt dtmail.dt dtpad.dt dtscreen.dt	\
	dt.softinit dtwm.fp error.ds hp.dt ibm.dt IconBrowse.dt		\
	laserrom.dt miscActions.dt miscImages.dt print.dt		\
	printerNN.dt sunOW.dt unsupported.dt user-prefs.dt uxstd.dt	\
	xclients.dt

CLEANFILES = $(BUILT_SOURCES)

# Use -C to GENCPP to preserve /* and */ sequences

autoStart.dt: autoStart.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

compat.dt: compat.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

datatypes.dt: datatypes.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@
dtcm.dt: dtcm.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

dtmail.dt: dtmail.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@


develop.dt: develop.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

dtappman.dt: dtappman.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

dt.dt: dt.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

dtfile.dt: dtfile.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

dthelp.dt: dthelp.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

dthelpprint.dt: dthelpprint.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

dthelptag.dt: dthelptag.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

dtims.dt: dtims.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

dtinfo.dt: dtinfo.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

dtpad.dt: dtpad.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

dtscreen.dt: dtscreen.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

dt.softinit: dt.softinit.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

dtwm.fp: dtwm.fp.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

error.ds: error.ds.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

hp.dt: hp.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

ibm.dt: ibm.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

IconBrowse.dt: IconBrowse.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

laserrom.dt: laserrom.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

miscActions.dt: miscActions.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

miscImages.dt: miscImages.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

print.dt: print.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

printerNN.dt: printerNN.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

sunOW.dt: sunOW.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

unsupported.dt: unsupported.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

user-prefs.dt: user-prefs.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

uxstd.dt: uxstd.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

xclients.dt: xclients.dt.src
	$(RM) $@
	$(GENCPP) -P -C -DXCOMM=# $(LOCAL_CPP_DEFINES) $< | $(FIXPATHS) > $@

