plugin_LTLIBRARIES = libgstercolorspace.la

libgstercolorspace_la_SOURCES = \
	src/gstercolorspace.c

if BUILD_ERCOLORSPACE_NEON
libgstercolorspace_la_SOURCES += \
	src/gstercolorspace_neon.s
endif

libgstercolorspace_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstercolorspace_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstercolorspace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
if !GST_PLUGIN_BUILD_STATIC
libgstercolorspace_la_LIBTOOLFLAGS = --tag=disable-static
endif

noinst_HEADERS = \
	src/gstercolorspace.h \
	src/gstercolorspace_neon.h

-include $(top_srcdir)/git.mk
