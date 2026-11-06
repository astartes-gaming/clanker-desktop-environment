MAINTAINERCLEANFILES = Makefile.in

include ../dtdocbook.am

sgmldir = $(dtdocbookdatadir)/sgml

sgml_DATA = docbook.dcl \
	    docbook.dtd \
	    dtinfo.af \
	    dtinfoBook.dtd \
	    dtinfoStyle.dtd \
	    dtinfoTOC.dtd \
	    catalog \
	    iso-catalog \
	    isoamsa.gml \
	    isoamsb.gml \
	    isoamsc.gml \
	    isoamsn.gml \
	    isoamso.gml \
	    isoamsr.gml \
	    isobox.gml \
	    isocyr1.gml \
	    isocyr2.gml \
	    isodia.gml \
	    isogrk1.gml \
	    isogrk2.gml \
	    isogrk3.gml \
	    isogrk4.gml \
	    isolat1.gml \
	    isolat2.gml \
	    isonum.gml \
	    isopub.gml \
	    isotech.gml \
	    styprolog.sgml

# Entity sets for help volumes (dtdocbook2sdl); regenerate with
# "sh sdl/mksdata.sh" after changing the ISO sets or DtHelp's SpcTable.
sgmlsdldir = $(sgmldir)/sdl
sgmlsdl_DATA = sdl/catalog sdl/isoamsa.gml sdl/isoamsb.gml sdl/isoamso.gml \
	       sdl/isogrk3.gml sdl/isolat1.gml sdl/isonum.gml sdl/isopub.gml \
	       sdl/isotech.gml
EXTRA_DIST = sdl/mksdata.sh
