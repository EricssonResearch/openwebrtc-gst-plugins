###
#
# Android video source plugin makefile for GStreamer 1.x
#
# Uses the Android build system.
# Is used for testing purposes.
#
###

LOCAL_PATH := $(call my-dir)
GLIB_GST_PATH := ../../../gst-build-armv7-android

include $(CLEAR_VARS)

LOCAL_MODULE := \
    gstandroidvideosrc

LOCAL_SRC_FILES := \
    gst_android_video_source.c \
    android_video_capture_device.c \
    jni_utils.c

LOCAL_C_INCLUDES = $(LOCAL_PATH)

LOCAL_C_INCLUDES += \
    $(GLIB_GST_PATH)/include/glib-2.0      \
    $(GLIB_GST_PATH)/include/glib-2.0/glib \
    $(GLIB_GST_PATH)/include/gstreamer-1.0

LOCAL_CFLAGS += -DGST_PLUGIN_BUILD_STATIC -Wall

# I do not use LOCAL_LDFLAGS, it seems not to work...
#    instead I put any -L options in LOCAL_LDLIBS, that seems to work...
LOCAL_LDFLAGS :=

LOCAL_LDLIBS := \
    -L$(GLIB_GST_PATH)/lib \
    -ldl                   \
    -llog                  \
    -lglib-2.0             \
    -lgobject-2.0          \
    -lgmodule-2.0          \
    -lgstreamer-1.0        \
    -lgstbase-1.0          \
    -lgstvideo-1.0

LOCAL_STATIC_LIBRARIES :=
LOCAL_SHARED_LIBRARIES :=

include $(BUILD_STATIC_LIBRARY)

# something that depends on the static lib
include $(CLEAR_VARS)
LOCAL_MODULE := abcabc
LOCAL_STATIC_LIBRARIES := gstandroidvideosrc

include $(BUILD_SHARED_LIBRARY)
