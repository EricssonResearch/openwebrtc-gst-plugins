

plugin_LTLIBRARIES = libgstandroidvideosrc.la

libgstandroidvideosrc_la_SOURCES = \
    jni/gst_android_video_source.c \
    jni/android_video_capture_device.c \
    jni/jni_utils.c

BUILT_SOURCES = \
    classes_dex.h

libgstandroidvideosrc_la_LIBADD = -ldl -llog

libgstandroidvideosrc_la_CFLAGS = -I$(srcdir)/jni

classes_dex.h: classes_dex.jar
	$(XXD) -i $< > $@

classes_dex.jar: src/com/ericsson/gstreamer/VideoCaptureDevicePreviewCallbackHandler.class
	$(DX) --dex --output=$@ src

%.class: %.java
	test -d src || mkdir src
	$(JAVAC) -classpath $(ANDROID_CLASSPATH) -d src $<

-include $(top_srcdir)/git.mk
