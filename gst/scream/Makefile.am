plugin_LTLIBRARIES = libgstscream.la

libgstscream_la_SOURCES = \
    gstscreamplugin.c \
    gstscreamcontroller.c \
    gstscreamqueue.c

libgstscream_la_CFLAGS = \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_BASE_CFLAGS) \
    $(GST_CFLAGS)

libgstscream_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) -lgstrtp-1.0

noinst_HEADERS = \
    gstscreamcontroller.h \
    gstscreamqueue.h

-include $(top_srcdir)/git.mk
