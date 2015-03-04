/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "include/gst_android_video_source.h"

#include "include/android_video_capture_device.h"
#include "include/gst_android_video_source_config.h"
#include "include/gst_android_video_source_log.h"

#include <assert.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <math.h>
#include <pthread.h>
#include <time.h>

#undef PACKAGE
#define PACKAGE "gst-android-video-source-plugin-package"
#define VERSION "0.10.0"

GST_DEBUG_CATEGORY_STATIC(gst_android_video_source_debug);
#define GST_CAT_DEFAULT gst_android_video_source_debug

/* Filter signals and args */
enum {
    /* FILL ME */
    LAST_SIGNAL
};

enum {
    PROP_0,
    PROP_LOG_INTERVAL,
    PROP_LOG_LEVEL_ANDROID,
    PROP_PREFER_FRONT_CAMERA,
    PROP_USE_CAMERA_INDEX
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("ANY"));

G_DEFINE_TYPE(GstAndroidVideoSource, gst_android_video_source, GST_TYPE_PUSH_SRC);

static void gst_android_video_source_dispose(GObject * p_object);
static void gst_android_video_source_finalize(GObject * p_object);

static void gst_android_video_source_set_property(GObject * p_object, guint prop_id, const GValue * p_value, GParamSpec * p_pspec);
static void gst_android_video_source_get_property(GObject * p_object, guint prop_id, GValue * p_value, GParamSpec * p_pspec);

static GstStateChangeReturn gst_android_video_source_change_state(GstElement* p_element, GstStateChange transition);

static gboolean gst_android_video_source_event(GstBaseSrc * p_gstbasesrc, GstEvent * p_event);
static gboolean gst_android_video_source_set_caps(GstBaseSrc * p_basesrc, GstCaps * p_caps);
static GstCaps * gst_android_video_source_get_caps(GstBaseSrc * p_basesrc, GstCaps * p_filter);

static GstFlowReturn gst_android_video_source_fill(GstPushSrc * p_pushsrc, GstBuffer * p_buf);

static gboolean gst_android_video_source_start(GstBaseSrc * p_gstbasesrc);
static gboolean gst_android_video_source_stop(GstBaseSrc * p_gstbasesrc);


/* Util methods */
static void LogStateChanges(GstStateChange transition);
static int RunningDebugBuild();
int time_diff_sec(struct timeval* p_timeA, struct timeval* p_timeB); // defined in device (sorry)
int time_diff_usec(struct timeval* p_timeA, struct timeval* p_timeB); // defined in device (sorry)


/* Class Init */
static void gst_android_video_source_class_init(GstAndroidVideoSourceClass * p_klass)
{
    GObjectClass *p_gobject_class;
    GstElementClass *p_gstelement_class;
    GstBaseSrcClass *p_gstbasesrc_class;
    GstPushSrcClass *p_gstpushsrc_class;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    if (RunningDebugBuild()) {
        GA_LOGINFO("%s: Running DEBUG build of Android Video Source (assert() is defined)", __FUNCTION__);
    } else {
        GA_LOGINFO("%s: Running RELEASE build of Android Video Source (assert() is not defined)", __FUNCTION__);
    }
    GA_LOGINFO("%s:    -------> build date: %s, build time: %s", __FUNCTION__, __DATE__, __TIME__);

    p_gobject_class = (GObjectClass *) p_klass;
    p_gstelement_class = (GstElementClass *) p_klass;
    p_gstbasesrc_class = (GstBaseSrcClass *) p_klass;
    p_gstpushsrc_class = (GstPushSrcClass *) p_klass;

    gst_element_class_set_metadata(
        p_gstelement_class,
        "AndroidVideoSource",
        "Source/Video",
        "Video source for Android",
        "Kristofer Dovstam <kristofer.dovstam@ericsson.com>");

    p_gobject_class->dispose = gst_android_video_source_dispose;
    p_gobject_class->finalize = gst_android_video_source_finalize;

    p_gobject_class->set_property = gst_android_video_source_set_property;
    p_gobject_class->get_property = gst_android_video_source_get_property;


    g_object_class_install_property(
        p_gobject_class, PROP_LOG_INTERVAL,
        g_param_spec_int(
            "log_interval", "Log interval",
            "Time in usec between frequent logs",
            0, G_MAXINT, DEFAULT_USEC_BETWEEN_LOGS, G_PARAM_READWRITE));

    g_object_class_install_property(
        p_gobject_class, PROP_LOG_LEVEL_ANDROID,
        g_param_spec_int(
            "log_level_android", "Log level for Android",
            "0=NONE, 1=ERROR, 2=WARNING, 3=INFO, 4=DEBUG, 5=VERBOSE, 6=TRACE",
            0, LOG_LEVEL_ANDROID_MAX, DEFAULT_LOG_LEVEL_ANDROID, G_PARAM_READWRITE));

    g_object_class_install_property(
        p_gobject_class, PROP_PREFER_FRONT_CAMERA,
        g_param_spec_boolean(
            "prefer_front_cam", "Prefer front facing camera",
            "If TRUE the element will use the first front facing camera found. If FALSE the element will use the first back facing camera found. Overridden by property 'cam_index'.",
            PREFER_FRONT_CAMERA_DEFAULT, G_PARAM_WRITABLE));

    g_object_class_install_property(
        p_gobject_class, PROP_USE_CAMERA_INDEX,
        g_param_spec_int(
            "cam_index", "Use camera with index",
            "Sets the index of the camera to use, if valid. Overrides property 'prefer_front_cam'.",
            -1, G_MAXINT, USE_CAMERA_INDEX_DEFAULT, G_PARAM_WRITABLE));


    /* Overridden GstElement methods */
    p_gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_android_video_source_change_state);

    /* Overridden GstBaseSrc methods */
    p_gstbasesrc_class->event = GST_DEBUG_FUNCPTR(gst_android_video_source_event);
    p_gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR(gst_android_video_source_get_caps);
    p_gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR(gst_android_video_source_set_caps);
    p_gstbasesrc_class->start = GST_DEBUG_FUNCPTR(gst_android_video_source_start);
    p_gstbasesrc_class->stop = GST_DEBUG_FUNCPTR(gst_android_video_source_stop);

    /* Overridden GstPushSrc methods */
    p_gstpushsrc_class->fill = GST_DEBUG_FUNCPTR(gst_android_video_source_fill);

    gst_element_class_add_pad_template(p_gstelement_class, gst_static_pad_template_get(&src_template));

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


/* Init */
static void gst_android_video_source_init(GstAndroidVideoSource * p_src)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    p_src->log_interval = DEFAULT_USEC_BETWEEN_LOGS;
    p_src->log_level_android = DEFAULT_LOG_LEVEL_ANDROID;

    gst_base_src_set_format(GST_BASE_SRC(p_src), GST_FORMAT_TIME);
    gst_base_src_set_live(GST_BASE_SRC(p_src), TRUE);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


/* Change state */
static GstStateChangeReturn gst_android_video_source_change_state(GstElement * p_element, GstStateChange transition)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    GstStateChangeReturn ret;
    GstAndroidVideoSource *p_src = GST_ANDROIDVIDEOSOURCE(p_element);

    LogStateChanges(transition);

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        p_src->m_devHandle = VCD_open((int)(p_src->log_interval));
        if (!p_src->m_devHandle) {
            goto err_state_change_failure;
        }
        AV_CHECK_ERR(VCD_prepare(p_src->m_devHandle), err_state_change_failure);
        break;
    default:
        break;
    }

    ret = GST_ELEMENT_CLASS(gst_android_video_source_parent_class)->change_state(p_element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        return ret;
    }

    switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
        AV_CHECK_ERR(VCD_unprepare(p_src->m_devHandle), err_state_change_failure);
        AV_CHECK_ERR(VCD_close(p_src->m_devHandle), err_state_change_failure);
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        AV_CHECK_ERR(VCD_unfixateMediaType(p_src->m_devHandle), err_state_change_failure);
        break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        if (p_src->vcdStarted) {
            AV_CHECK_ERR(VCD_stop(p_src->m_devHandle), err_state_change_failure);
            p_src->vcdStarted = FALSE;
        }
        break;
    default:
        break;
    }

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return ret;

    /*
     * propagate unhandled errors
     */
err_state_change_failure:
    {
        GA_LOGERROR("%s: ERROR: Could not change state", __FUNCTION__);
        return GST_STATE_CHANGE_FAILURE;
    }
}


/* Start - called when going from state READY to state PAUSED... */
static gboolean gst_android_video_source_start(GstBaseSrc * p_gstbasesrc)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    GstAndroidVideoSource *p_src = GST_ANDROIDVIDEOSOURCE(p_gstbasesrc);

    p_src->m_prev_timestamp = GST_CLOCK_TIME_NONE;
    p_src->vcdStarted = FALSE;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return TRUE;
}


/* Stop - called when going from state PAUSED to state READY... */
static gboolean gst_android_video_source_stop(GstBaseSrc * p_gstbasesrc)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    // Currently doing nothing here...

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return TRUE;
}


/* Set Property */
static void gst_android_video_source_set_property(GObject * p_object, guint prop_id, const GValue * p_value, GParamSpec * p_pspec)
{
    GstAndroidVideoSource *p_filter = GST_ANDROIDVIDEOSOURCE(p_object);

    switch (prop_id) {
    case PROP_LOG_INTERVAL:
        p_filter->log_interval = g_value_get_int(p_value);
        GA_LOGINFO("%s: Setting log_interval = %d", __FUNCTION__, p_filter->log_interval);
        break;
    case PROP_LOG_LEVEL_ANDROID:
        if (g_value_get_int(p_value) >= 0 && g_value_get_int(p_value) <= LOG_LEVEL_ANDROID_MAX) {
            p_filter->log_level_android = g_value_get_int(p_value);
            myLogLevel = p_filter->log_level_android;
            GA_LOGINFO("%s: Setting log_level_android = %d", __FUNCTION__, p_filter->log_level_android);
        } else {
            GA_LOGERROR("%s: Error setting log_level_android to %d. Value should be in range [0,%d].", __FUNCTION__, p_filter->log_level_android, LOG_LEVEL_ANDROID_MAX);
        }
        break;
    case PROP_PREFER_FRONT_CAMERA:
        (void) VCD_preferFrontFacingCamera(g_value_get_boolean(p_value));
        break;
    case PROP_USE_CAMERA_INDEX:
        (void) VCD_useCameraWithIndex(g_value_get_int(p_value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(p_object, prop_id, p_pspec);
        break;
    }
}


/* Get Property */
static void gst_android_video_source_get_property(GObject * p_object, guint prop_id, GValue * p_value, GParamSpec * p_pspec)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    GstAndroidVideoSource *p_filter = GST_ANDROIDVIDEOSOURCE(p_object);

    switch (prop_id) {
    case PROP_LOG_INTERVAL:
        g_value_set_int(p_value, p_filter->log_interval);
        break;
    case PROP_LOG_LEVEL_ANDROID:
        g_value_set_int(p_value, p_filter->log_level_android);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(p_object, prop_id, p_pspec);
        break;
    }

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


/* Event */
static gboolean gst_android_video_source_event(GstBaseSrc * p_gstbasesrc, GstEvent * p_event)
{
    gboolean res = TRUE;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    const char* event_name_str = GST_EVENT_TYPE_NAME(p_event);
    GA_LOGVERB("%s: Received event: %s", __FUNCTION__, event_name_str);

    // Currently doing nothing here...

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return res;
}

/* GST video format to VCD's (Android's) format */
static int gst_video_fmt_to_vcd_fmt(GstVideoFormat gst_video_fmt)
{
    if (gst_video_fmt == GST_VIDEO_FORMAT_YV12) {
        GST_FIXME("Ignoring YV12 since many Android devices that claim to support and produce YV12 actually produce NV21 instead");
        /* return VCD_PIXEL_FORMAT_YV12;  ...many Android devices that claim to support and produce YV12 actually produce NV21 instead */
        return VCD_PIXEL_FORMAT_UNKNOWN;
    }
    if (gst_video_fmt == GST_VIDEO_FORMAT_RGB16) {
        return VCD_PIXEL_FORMAT_RGB_565;
    }
    if (gst_video_fmt == GST_VIDEO_FORMAT_NV21) {
        return VCD_PIXEL_FORMAT_NV21;
    }
    if (gst_video_fmt == GST_VIDEO_FORMAT_YUY2) {
        return VCD_PIXEL_FORMAT_YUY2;
    }

    return VCD_PIXEL_FORMAT_UNKNOWN;
}

/* VCD int to fourcc */
GstVideoFormat vcd_int_to_gst_video_format(int value)
{
    if (value == VCD_PIXEL_FORMAT_NV21) {
        return GST_VIDEO_FORMAT_NV21;
    }
    if (value == VCD_PIXEL_FORMAT_RGB_565) {
        return GST_VIDEO_FORMAT_RGB16;
    }
    if (value == VCD_PIXEL_FORMAT_YUY2) {
        return GST_VIDEO_FORMAT_YUY2;
    }
    if (value == VCD_PIXEL_FORMAT_YV12) {
        GST_FIXME("Ignoring since many Android devices that claim to support and produce YV12 actually produce NV21 instead");
        /* return GST_VIDEO_FORMAT_YV12; */
        return GST_VIDEO_FORMAT_UNKNOWN;
    }
    if (value == VCD_PIXEL_FORMAT_NV16) {
        goto vcd_int_to_gst_video_format_invalid_fmt;
    }
    if (value == VCD_PIXEL_FORMAT_JPEG) {
        goto vcd_int_to_gst_video_format_invalid_fmt;
    }

    /* if here... error... */
    goto vcd_int_to_gst_video_format_invalid_fmt;

    /*
     * propagate unhandled errors
     */
vcd_int_to_gst_video_format_invalid_fmt:
    {
        GA_LOGERROR("%s: ERROR: Video fmt (%d) from Android device not supported by GStreamer. Will use GST_VIDEO_FORMAT_UNKNOWN", __FUNCTION__, value);
        return GST_VIDEO_FORMAT_UNKNOWN;
    }
}


/*
 * Get Caps
 *
 * As can be seen this method violates the API between the GST element
 * and the Android device. Should be fixed... (FIXME)
 * */
static GstCaps * gst_android_video_source_get_caps(GstBaseSrc * p_basesrc, GstCaps * p_filter)
{
    int i;
    int minFps;
    int maxFps;
    int fmtPos;
    int minWidth, minHeight;
    int maxWidth, maxHeight;
    GstCaps *caps;
    GstCaps *capsVec = NULL;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    GstAndroidVideoSource *p_src = GST_ANDROIDVIDEOSOURCE(p_basesrc);

    if (GST_STATE(p_src) == GST_STATE_NULL || GST_STATE(p_src) <= GST_STATE_NULL) {
        GA_LOGINFO("%s: Called in state %s. Don't know device support yet. Will return NULL caps.", __FUNCTION__, gst_element_state_get_name(GST_STATE(p_src)));
        return NULL;
    }

    if (VCD_GetWidestFpsRange(p_src->m_devHandle, &minFps, &maxFps) != VCD_NO_ERROR) {
        return NULL;
    }

    if (VCD_NO_ERROR != VCD_GetMinResolution(p_src->m_devHandle, &minWidth, &minHeight)) {
        return NULL;
    }
    if (VCD_NO_ERROR != VCD_GetMaxResolution(p_src->m_devHandle, &maxWidth, &maxHeight)) {
        return NULL;
    }
    capsVec = gst_caps_new_empty();
    for (fmtPos = 0; fmtPos < VCD_getMediaSupportFmtLen(p_src->m_devHandle); fmtPos++) {
        int fmt = VCD_getMediaSupportFmt(p_src->m_devHandle)[fmtPos];
        GstVideoFormat gstVideoFmt = vcd_int_to_gst_video_format(fmt);
        if (gstVideoFmt != GST_VIDEO_FORMAT_UNKNOWN) {
            caps = gst_caps_new_simple(
                "video/x-raw",
                "format", G_TYPE_STRING, gst_video_format_to_string(gstVideoFmt),
                "width", GST_TYPE_INT_RANGE, minWidth, maxWidth,
                "height", GST_TYPE_INT_RANGE, minHeight, maxHeight,
#ifdef ACCEPT_FPS_CAPS_DOWN_TO_1FPS
                "framerate", GST_TYPE_FRACTION_RANGE, 1000, ANDROID_FPS_DENOMINATOR, maxFps, ANDROID_FPS_DENOMINATOR,
#else
                "framerate", GST_TYPE_FRACTION_RANGE, minFps, ANDROID_FPS_DENOMINATOR, maxFps, ANDROID_FPS_DENOMINATOR,
#endif
                "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
                NULL);
            gst_caps_append(capsVec, caps);
        }
    }

    // Some Android devices report one or more supported formats (or other stuff)
    // more than once, which gives caps duplicates. Those are removed by doing
    // gst_caps_do_simplify()...
    capsVec = gst_caps_simplify(capsVec);

    GA_LOGINFO("%s: By Android video device supported CAPS:", __FUNCTION__);
    GA_LOGINFO("%s:-----------------------------------------------------------", __FUNCTION__);
    for (i = 0; i < gst_caps_get_size(capsVec); i++) { // Android log cannot print that long messages so we need to take one caps at a time
        GstCaps *capsCopy = gst_caps_copy_nth(capsVec, i);
        GA_LOGINFO("CAPS%d: %s", i+1, gst_caps_to_string(capsCopy));
        gst_caps_unref(capsCopy);
    }
    GA_LOGINFO("%s:-----------------------------------------------------------", __FUNCTION__);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);

    return capsVec;
}


/* Accept caps */
static gboolean accept_caps(GstAndroidVideoSource* p_src, int format, int width, int height, int framerate_num, int framerate_den)
{
    GA_LOGINFO("%s() got: format=%d, width=%d, height=%d, framerate_num=%d, framerate_den=%d", __FUNCTION__, format, width, height, framerate_num, framerate_den);

    // "Sanity" check of framerate
    int rest = framerate_num % framerate_den;
    if (rest) {
        GA_LOGERROR("%s: framerate_num %% framerate_den == %d (i.e. != 0), don't like that...", __FUNCTION__, rest);
        return FALSE;
    }

    // Check video format
    int vcd_video_fmt = gst_video_fmt_to_vcd_fmt(format);
    if (vcd_video_fmt == VCD_PIXEL_FORMAT_UNKNOWN
        || FALSE == VCD_supportsMediaFormat(p_src->m_devHandle, vcd_video_fmt)) {
        GA_LOGERROR("%s: format %d (GST) (%d (Android)) not supported", __FUNCTION__, format, vcd_video_fmt);
        return FALSE;
    }

    // Check video size
    if (FALSE == VCD_supportsMediaSize(p_src->m_devHandle, width, height)) {
        GA_LOGERROR("%s: width and height (%dx%d) not supported", __FUNCTION__, width, height);
        return FALSE;
    }

    // Check video framerate
    if (FALSE == VCD_supportsMediaFramerate(p_src->m_devHandle, framerate_num / framerate_den * ANDROID_FPS_DENOMINATOR)) {
        GA_LOGERROR("%s: framerate %d not supported", __FUNCTION__, framerate_num / framerate_den);
        return FALSE;
    }

    return TRUE;
}


/* Set Caps */
static gboolean gst_android_video_source_set_caps(GstBaseSrc * p_basesrc, GstCaps * p_caps)
{
    GstAndroidVideoSource *p_src;
    GstStructure *p_structure;
    GstVideoFormat format;
    gint width;
    gint height;
    const GValue* p_framerate;
    gint framerate_num;
    gint framerate_den;
    gboolean caps_status = FALSE;
    GstVideoInfo video_info;
    int vcd_ret;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    p_src = GST_ANDROIDVIDEOSOURCE(p_basesrc);

    if (gst_caps_get_size(p_caps) != 1) {
        goto set_caps_err_caps_not_simple;
    }
    if (!gst_video_info_from_caps(&video_info, p_caps)) {
        goto set_caps_err_parsing_caps;
    }
    format = GST_VIDEO_INFO_FORMAT(&video_info);
    width = GST_VIDEO_INFO_WIDTH(&video_info);
    height = GST_VIDEO_INFO_HEIGHT(&video_info);
    p_structure = gst_caps_get_structure(p_caps, 0);
    if (!p_structure) {
        goto set_caps_err_get_structure;
    }
    p_framerate = gst_structure_get_value(p_structure, "framerate");
    if (!p_framerate) {
        goto set_caps_err_no_framerate;
    }
    framerate_num = gst_value_get_fraction_numerator(p_framerate);
    framerate_den = gst_value_get_fraction_denominator(p_framerate);

    if (accept_caps(p_src, format, width, height, framerate_num, framerate_den)) {
        gchar *caps_str = gst_caps_to_string(p_caps);
        GA_LOGINFO("%s: Caps are accepted! - caps are: %s", __FUNCTION__, caps_str ? caps_str : "[cannot print caps, gst_caps_to_string() failed]");
        g_free(caps_str);
        caps_str = NULL;
        caps_status = TRUE;
    } else {
        goto set_caps_err_caps_not_accepted;
    }

    // Caps are ok so fixate them on the device
    vcd_ret = VCD_fixateMediaType(
        p_src->m_devHandle,
        gst_video_fmt_to_vcd_fmt(format),
        width,
        height,
        (framerate_num * ANDROID_FPS_DENOMINATOR) / framerate_den);
    if (vcd_ret != VCD_NO_ERROR) {
        return FALSE;
    }
    p_src->m_bufSize = (gint) VCD_getBufferSize(
        p_src->m_devHandle,
        gst_video_fmt_to_vcd_fmt(format),
        width,
        height);
    if (p_src->m_bufSize <= 0) {
        return FALSE;
    }
    gst_base_src_set_blocksize(p_basesrc, (guint) p_src->m_bufSize);
    p_src->m_duration = gst_util_uint64_scale(GST_SECOND, framerate_den, framerate_num);
    GA_LOGINFO("%s: setting static duration (GstBuffer) to: %"G_GUINT64_FORMAT, __FUNCTION__, p_src->m_duration);

    if (!p_src->vcdStarted) {
        AV_CHECK_ERR(VCD_start(p_src->m_devHandle), set_caps_err_vcd_start);
        p_src->vcdStarted = TRUE;
    }

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return caps_status;

    /*
     * propagate unhandled errors
     */
set_caps_err_caps_not_simple:
    {
        GA_LOGERROR("%s: ERROR: Got empty caps or caps list (not a simple caps (one caps))", __FUNCTION__);
        return FALSE;
    }
set_caps_err_parsing_caps:
    {
        GA_LOGERROR("%s: ERROR: Unknown error when parsning caps", __FUNCTION__);
        return FALSE;
    }
set_caps_err_get_structure:
    {
        GA_LOGERROR("%s: ERROR: Could not get GstStructure from caps", __FUNCTION__);
        return FALSE;
    }
set_caps_err_no_framerate:
    {
        GA_LOGERROR("%s: ERROR: Caps does not contain framerate", __FUNCTION__);
        return FALSE;
    }
set_caps_err_caps_not_accepted:
    {
        GA_LOGINFO("%s: Caps are NOT accepted. FORMAT=%d, width=%d, height=%d, framerate num=%d, framerate den=%d", __FUNCTION__, format, width, height, framerate_num, framerate_den);
        return FALSE;
    }
set_caps_err_vcd_start:
    {
        GA_LOGERROR("%s: ERROR: Could not start the video device", __FUNCTION__);
        return FALSE;
    }
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean androidvideosource_init(GstPlugin * p_androidvideosource)
{
    /* debug category for filtering log messages */
    GST_DEBUG_CATEGORY_INIT(gst_android_video_source_debug, LOG_TAG, 0, "This is the description of the debug category...");

    myLogLevel = DEFAULT_LOG_LEVEL_ANDROID;

    GA_LOGTRACE("ENTER %s   thread(%ld)", __FUNCTION__, pthread_self()); /* needs to be called after GST_DEBUG_CATEGORY_INIT */

    if (!VCD_init())
        goto err_init_videocapturedevice_module;

    if (!gst_element_register(p_androidvideosource, "androidvideosource", GST_RANK_PRIMARY, GST_TYPE_ANDROIDVIDEOSOURCE))
        goto err_element_register;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);

    return TRUE;

    /*
     * propagate unhandled errors
     */
err_init_videocapturedevice_module:
    {
        GA_LOGERROR("%s ERROR: init videocapturedevice module (VCD) failed", __FILE__);
        return FALSE;
    }

err_element_register:
    {
        GA_LOGERROR("%s ERROR: register android-video-source failed", __FILE__);
        return FALSE;
    }
}


/* Register information for androidvideosources
 */
GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    androidvideosource,
    "Android Video Source",
    androidvideosource_init,
    VERSION,
    "BSD",
    "OpenWebRTC GStreamer plugins",
    "http://www.openwebrtc.io/"
)


/* androidvideosource dispose */
static void gst_android_video_source_dispose(GObject* p_object)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    G_OBJECT_CLASS(gst_android_video_source_parent_class)->dispose(p_object);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


/* androidvideosource finalize */
static void gst_android_video_source_finalize(GObject * p_object)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    G_OBJECT_CLASS(gst_android_video_source_parent_class)->finalize((GObject*) (p_object));

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


/* Set buffer timing and offset data */
static void set_gstbuf_time_and_offset(GstAndroidVideoSource * p_src, GstBuffer * p_buf)
{
    GstElement *p_element;
    GstClock *p_clock;
    GstClockTime now;
    GstClockTime base_time;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    p_element = GST_ELEMENT_CAST(p_src);

    GST_OBJECT_LOCK(p_element);
    p_clock = GST_ELEMENT_CLOCK(p_element);
    if (p_clock) {
        gst_object_ref(p_clock);
        base_time = p_element->base_time;
        GA_LOGTRACE("%s: base_time is: %llu", __FUNCTION__, base_time);
    } else {
        base_time = GST_CLOCK_TIME_NONE;
    }
    GST_OBJECT_UNLOCK(p_element);

    if (p_clock) {
        /* Wrap around is not considered a problem due to the clock being 64 bit (famous last words? :-) ) */
        now = gst_clock_get_time(p_clock) - base_time;
        GA_LOGTRACE("%s: gst_clock_get_time returns: %llu", __FUNCTION__, gst_clock_get_time(p_clock));
    } else {
        now = GST_CLOCK_TIME_NONE;
    }

    if (p_clock) {
        gst_object_unref(p_clock);
        p_clock = NULL;
    }

    GST_BUFFER_PTS(p_buf) = now;
    GST_BUFFER_DTS(p_buf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(p_buf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_OFFSET(p_buf) = GST_BUFFER_OFFSET_NONE;
    GST_BUFFER_OFFSET_END(p_buf) = GST_BUFFER_OFFSET_NONE;

    GA_LOGTRACE("%s: setting presentation timestamp (GstBuffer) to: %llu (%"GST_TIME_FORMAT")", __FUNCTION__, now, GST_TIME_ARGS(now));
    GA_LOGTRACE("%s: m_prev_timestamp: %llu (%"GST_TIME_FORMAT")", __FUNCTION__, p_src->m_prev_timestamp, GST_TIME_ARGS(p_src->m_prev_timestamp));
    GA_LOGTRACE("%s: timestamp diff: %llu (%"GST_TIME_FORMAT")", __FUNCTION__, now - p_src->m_prev_timestamp, GST_TIME_ARGS(now - p_src->m_prev_timestamp));

    p_src->m_prev_timestamp = now;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return;
}


/* Fill */
static GstFlowReturn gst_android_video_source_fill(GstPushSrc * p_pushsrc, GstBuffer * p_buf)
{
    GstAndroidVideoSource *p_src;
    int vcd_ret;
    static struct timeval time_of_day;
    static struct timeval window_time;
    static struct timeval start_time;
    int timeDiffUsec;
    static gint frame_count = 0;
    static gint frame_count_window = 0;
    GstBuffer *p_outbuf;
    GstMapInfo mem_info;
    gboolean ok;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    p_src = GST_ANDROIDVIDEOSOURCE(p_pushsrc);

    if (gst_buffer_get_size(p_buf) != p_src->m_bufSize) {
        GA_LOGWARN("%s: WARNING: gst_buffer_get_size(p_buf)==%d != p_src->m_bufSize==%d", __FUNCTION__, gst_buffer_get_size(p_buf), p_src->m_bufSize);
        goto fill_error_negotiation;
    }

    VCD_checkChangeCamera(p_src->m_devHandle);

    if (!p_src->vcdStarted) {
        AV_CHECK_ERR(VCD_start(p_src->m_devHandle), fill_error_vcd_start);
        p_src->vcdStarted = TRUE;
    }

    if (!frame_count) { // Only first time
        gettimeofday(&start_time, NULL);
        gettimeofday(&window_time, NULL);
    }

    frame_count++;
    frame_count_window++;

    gettimeofday(&time_of_day, NULL);
    timeDiffUsec = time_diff_usec(&time_of_day, &window_time);
    if (timeDiffUsec > p_src->log_interval || !p_src->log_interval) {
        int framerate;
        int framerateWindow;
        int timeDiffSec;
        timeDiffSec = time_diff_sec(&time_of_day, &start_time);
        framerate = frame_count / (timeDiffSec > 0 ? timeDiffSec : 1);
        framerateWindow = frame_count_window * 1000000 / timeDiffUsec;
        GA_LOGVERB("%s ------> has now been called %d times --Create--> framerate since start: %d fps, framerate last %d usec: %d fps", __FUNCTION__, frame_count, framerate, timeDiffUsec, framerateWindow);
        gettimeofday(&window_time, NULL);
        frame_count_window = 0;
    }

    g_warn_if_fail(gst_buffer_is_writable(p_buf)); /* g_warn_if_fail() used for internal error (exception to our rules for this special "buffer copying case") */
    g_assert(gst_buffer_is_writable(p_buf)); /* this buf should be allocated in the base class and should always be writable */
    p_outbuf = gst_buffer_make_writable(p_buf); /* do this cause we never wanna crash in release even if somebody makes a mistake somewhere... */

    ok = gst_buffer_map(p_outbuf, &mem_info, GST_MAP_WRITE);
    if (!ok) {
        goto fill_error_gst_buffer_map;
    }
    vcd_ret = VCD_read(p_src->m_devHandle, &(mem_info.data), mem_info.size);
    if (vcd_ret == VCD_ERR_NO_DATA) {
        // This should never happen. There should always be more data from the device.
        // In any case, if it happens we don't want to end or lock or anything, we just
        // want to go on as if there actually were data. Specifically, we do not want
        // to block the streaming thread...
        memset(mem_info.data, 0, mem_info.size);
        gst_buffer_unmap(p_outbuf, &mem_info);
        GST_BUFFER_OFFSET(p_outbuf) = GST_BUFFER_OFFSET_NONE;
        GST_BUFFER_OFFSET_END(p_outbuf) = GST_BUFFER_OFFSET_NONE;
        GST_BUFFER_PTS(p_outbuf) = GST_CLOCK_TIME_NONE;
        GST_BUFFER_DTS(p_outbuf) = GST_CLOCK_TIME_NONE;
        GST_BUFFER_DURATION(p_outbuf) = GST_CLOCK_TIME_NONE;
        GA_LOGWARN("%s: WARNING: Returning GST_FLOW_OK with a buffer with zeros...", __FUNCTION__);
        return GST_FLOW_OK;
    }
    if (vcd_ret != VCD_NO_ERROR) {
        gst_buffer_unmap(p_outbuf, &mem_info);
        goto fill_error_read;
    }
    gst_buffer_unmap(p_outbuf, &mem_info);

    set_gstbuf_time_and_offset(p_src, p_outbuf);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return GST_FLOW_OK;

    /*
     * propagate unhandled errors
     */
fill_error_read:
    {
        GA_LOGERROR("%s: Error when reading data from device!", __FUNCTION__);
        return GST_FLOW_ERROR;
    }
fill_error_negotiation:
    {
        GA_LOGERROR("%s: ERROR: Strange buffer size. Negotiation not done? Disallowed renegotiation done?", __FUNCTION__);
        return GST_FLOW_ERROR;
    }
fill_error_gst_buffer_map:
    {
        GA_LOGERROR("%s: gst_buffer_map() failed!", __FUNCTION__);
        return GST_FLOW_ERROR;
    }
fill_error_vcd_start:
    {
        GA_LOGERROR("%s: FATAL ERROR: Could not start the video device!", __FUNCTION__);
        return GST_FLOW_ERROR;
    }
}


/*
 * Util methods...
 */

// LogStateChanges
static void LogStateChanges(GstStateChange transition)
{
    GA_LOGVERB(
        "Logging state change: %s -> %s",
        gst_element_state_get_name(GST_STATE_TRANSITION_CURRENT(transition)),
        gst_element_state_get_name(GST_STATE_TRANSITION_NEXT(transition)));
}

// DebugOrRelease
static int RunningDebugBuild()
{
    int isDebug = 0;
    assert(isDebug = 1);
    return isDebug;
}
