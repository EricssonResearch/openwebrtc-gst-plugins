if BUILD_ANDROID_PLUGINS
   android_SUBDIRS=androidvideo
endif

SUBDIRS = $(android_SUBDIRS)
DIST_SUBDIRS = $(SUBDIRS) # needed since we are doing a out of tree build.

-include $(top_srcdir)/git.mk
