plugin_LTLIBRARIES = libgsterdtls.la

libgsterdtls_la_SOURCES = \
    src/plugin.c \
    src/gsterdtlsdec.c \
    src/gsterdtlsenc.c \
    src/gsterdtlssrtpdemux.c \
    src/gsterdtlssrtpbin.c \
    src/gsterdtlssrtpdec.c \
    src/gsterdtlssrtpenc.c \
    src/erdtlsagent.c \
    src/erdtlsconnection.c \
    src/erdtlscertificate.c

libgsterdtls_la_CFLAGS = \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_BASE_CFLAGS) \
    $(GST_CFLAGS) \
    $(OPENSSL_CFLAGS) \
    -DER_DTLS_USE_GST_LOG=1

if !GST_PLUGIN_BUILD_STATIC
libgsterdtls_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(OPENSSL_LIBS)
libgsterdtls_la_LIBTOOLFLAGS = --tag=disable-static
endif

noinst_HEADERS = \
    src/gsterdtlsdec.h \
    src/gsterdtlsenc.h \
    src/gsterdtlssrtpdemux.h \
    src/gsterdtlssrtpbin.h \
    src/gsterdtlssrtpdec.h \
    src/gsterdtlssrtpenc.h \
    src/erdtlsagent.h \
    src/erdtlsconnection.h \
    src/erdtlscertificate.h \
    src/erdtlscommon.h

-include $(top_srcdir)/git.mk
