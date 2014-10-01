plugin_LTLIBRARIES = libgstercolorspace.la

libgstercolorspace_la_SOURCES = \
	src/gstercolorspace.c \
	src/gstercolorspace_neon.s

libgstercolorspace_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstercolorspace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstercolorspace_la_LIBADD = \
	$(GST_BASE_LIBS) $(GST_LIBS)
if !GST_PLUGIN_BUILD_STATIC
libgstercolorspace_la_LIBTOOLFLAGS = --tag=disable-static
endif

noinst_HEADERS = \
	src/gstercolorspace.h \
	src/gstercolorspace_neon.h

-include $(top_srcdir)/git.mk
