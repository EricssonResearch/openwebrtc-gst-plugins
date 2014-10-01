if BUILD_OPENH264
   openh264_SUBDIRS=openh264
endif

SUBDIRS=erdtls $(openh264_SUBDIRS)
DIST_SUBDIRS = $(SUBDIRS) # needed since we are doing a out of tree build.
-include $(top_srcdir)/git.mk
