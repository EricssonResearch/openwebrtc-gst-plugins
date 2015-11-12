plugin_LTLIBRARIES = libgstscream.la

libgstscream_la_SOURCES = \
    gstscreamplugin.c \
    gstscreamcontroller.c \
    gstscreamqueue.c

libgstscream_la_CFLAGS = \
    -Wall -Wextra -Werror \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_BASE_CFLAGS) \
    $(GST_CFLAGS)

libgstscream_la_LIBADD = \
    -lgstrtp-1.0 \
    $(GST_PLUGINS_BASE_LIBS) \
    $(GST_BASE_LIBS) \
    $(GST_LIBS)

libgstscream_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
if !GST_PLUGIN_BUILD_STATIC
libgstscream_la_LIBTOOLFLAGS = --tag=disable-static
endif

noinst_HEADERS = \
    gstscreamcontroller.h \
    gstscreamqueue.h

-include $(top_srcdir)/git.mk
