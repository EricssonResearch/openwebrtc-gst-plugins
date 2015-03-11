

plugin_LTLIBRARIES = libgstandroidvideosrc.la

libgstandroidvideosrc_la_SOURCES = \
    jni/gst_android_video_source.c \
    jni/android_video_capture_device.c \
    jni/jni_utils.c

BUILT_SOURCES = \
    classes_dex.h

libgstandroidvideosrc_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) -ldl -llog
libgstandroidvideosrc_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) -I$(srcdir)/jni
libgstandroidvideosrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
if !GST_PLUGIN_BUILD_STATIC
libgstandroidvideosrc_la_LIBTOOLFLAGS = --tag=disable-static
endif

classes_dex.h: classes_dex.jar
	$(XXD) -i $< > $@

classes_dex.jar: src/com/ericsson/gstreamer/VideoCaptureDevicePreviewCallbackHandler.class
	$(DX) --dex --output=$@ src

%.class: %.java
	test -d src || mkdir src
	$(JAVAC) -source 1.7 -target 1.7 -classpath $(ANDROID_CLASSPATH) -d src $<

-include $(top_srcdir)/git.mk
