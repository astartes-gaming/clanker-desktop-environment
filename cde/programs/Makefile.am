MAINTAINERCLEANFILES = Makefile.in

if BUILD_DTKSH
DTKSH_DIR = dtksh
endif

SUBDIRS = backdrops icons dsdm dtmail dtpad dtfile dtwm dtlogin		\
        dtsession dthello dtstyle dtexec dtdbcache dticon dtterm	\
        dtcalc dtaction dtspcd dtscreen $(DTKSH_DIR) dtcm dtsearchpath	\
        dtappbuilder palettes dtappintegrate dtprintegrate dtconfig	\
        dtcreate dtprintinfo fontaliases dtdspmsg ttsnoop dtimsstart	\
        dtpdm dtsr dtpdmd dtinfo types tttypes util dtopen localized	\
        dthelp dtdocbook

install-data-hook:
	-if [ $(CDE_INSTALLATION_TOP)/bin != $(bindir) ];then \
		cd $(DESTDIR)$(CDE_INSTALLATION_TOP) && $(LN_S) $(bindir) bin ; fi
