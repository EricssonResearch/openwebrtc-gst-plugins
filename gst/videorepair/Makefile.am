# Note: plugindir is set in configure

##############################################################################
# TODO: change libgstplugin.la to something else, e.g. libmysomething.la     #
##############################################################################
plugin_LTLIBRARIES = libgstvideorepair.la

##############################################################################
# TODO: for the next set of variables, name the prefix if you named the .la, #
#  e.g. libmysomething.la => libmysomething_la_SOURCES                       #
#                            libmysomething_la_CFLAGS                        #
#                            libmysomething_la_LIBADD                        #
#                            libmysomething_la_LDFLAGS                       #
##############################################################################

# sources used to compile this plug-in
libgstvideorepair_la_SOURCES = gstvideorepair.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstvideorepair_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) -Wall -Wextra -Werror
libgstvideorepair_la_LIBADD = $(GST_LIBS) -lgstvideo-1.0
libgstvideorepair_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
if !GST_PLUGIN_BUILD_STATIC
libgstvideorepair_la_LIBTOOLFLAGS = --tag=disable-static
endif

# headers we need but don't want installed
noinst_HEADERS = gstvideorepair.h

-include $(top_srcdir)/git.mk
