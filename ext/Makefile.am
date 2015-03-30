SUBDIRS = sctp

if ERDTLS
SUBDIRS += erdtls
endif

# needed since we are doing a out of tree build.
DIST_SUBDIRS = $(SUBDIRS)

-include $(top_srcdir)/git.mk
