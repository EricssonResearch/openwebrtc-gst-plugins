plugin_LTLIBRARIES = libgstercolorspace.la

libgstercolorspace_la_SOURCES = \
	src/gstercolorspace.c

libgstercolorspace_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstercolorspace_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstercolorspace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
if !GST_PLUGIN_BUILD_STATIC
libgstercolorspace_la_LIBTOOLFLAGS = --tag=disable-static
endif

noinst_HEADERS = \
	src/gstercolorspace.h

-include $(top_srcdir)/git.mk
