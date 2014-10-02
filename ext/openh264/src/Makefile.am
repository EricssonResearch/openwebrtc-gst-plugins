plugin_LTLIBRARIES = libgstopenh264.la

# sources used to compile this plug-in
libgstopenh264_la_SOURCES = \
    gstopenh264plugin.c \
    gstopenh264enc.cpp \
    gstopenh264dec.cpp \
    gstopenh264dec.h \
    gstopenh264enc.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstopenh264_la_CFLAGS = $(GST_CFLAGS) $(OPENH264_CFLAGS) -DGST_USE_UNSTABLE_API=1
libgstopenh264_la_CXXFLAGS = $(GST_CFLAGS) $(OPENH264_CFLAGS) -DGST_USE_UNSTABLE_API=1
libgstopenh264_la_LIBADD = $(GST_LIBS)
libgstopenh264_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(OPENH264_LIBS)


-include $(top_srcdir)/git.mk
