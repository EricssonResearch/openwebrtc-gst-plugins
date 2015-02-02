SUBDIRS = gst-libs sys gst ext

ACLOCAL_AMFLAGS = -I m4

DISTCLEANFILES = aclocal.m4 compile config.guess config.h.in config.sub \
    depcomp gtk-doc.make m4 install-sh ltmain.sh missing

CLEANFILES = build out

GITIGNOREFILES = gst-build-* .build_successful_tag_file.txt

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gstreamer-sctp-1.0.pc

-include $(top_srcdir)/git.mk
