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

#include "gstvideorepair.h"

#include <gst/gst.h>
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC(gst_videorepair_debug_category);
#define GST_CAT_DEFAULT gst_videorepair_debug_category

/* prototypes */

static void gst_videorepair_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec);
static void gst_videorepair_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);

static GstStateChangeReturn gst_videorepair_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_videorepair_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);
static GstFlowReturn gst_videorepair_sink_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer);


/* properties */

#define DEFAULT_DROP_UNTIL_INTRA TRUE
#define DEFAULT_RETRY_INTERVAL 30

enum
{
    PROP_0,

    PROP_DROP_UNTIL_INTRA,
    PROP_RETRY_INTERVAL,

    NUM_PROPERTIES
};

static GParamSpec *obj_properties[NUM_PROPERTIES] = {NULL, };


/* pad templates */

static GstStaticPadTemplate gst_videorepair_sink_template =
GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-h264; video/x-vp8")
    );

static GstStaticPadTemplate gst_videorepair_src_template =
GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-h264; video/x-vp8")
    );


/* class initialization */

#define gst_videorepair_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstVideoRepair, gst_videorepair, GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT(gst_videorepair_debug_category, "videorepair", 0,
    "debug category for videorepair element"));

static void gst_videorepair_class_init(GstVideoRepairClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_pad_template(element_class,
        gst_static_pad_template_get(&gst_videorepair_sink_template));
    gst_element_class_add_pad_template (element_class,
        gst_static_pad_template_get(&gst_videorepair_src_template));

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
        "Video repair", "Filter/Video/Network",
        "Requests intra pictures on GAP events and (optionally) drops buffers until they arrive",
        "Ericsson AB, http://www.ericsson.com");

    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_videorepair_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_videorepair_get_property);

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_videorepair_change_state);

    obj_properties[PROP_DROP_UNTIL_INTRA] = g_param_spec_boolean("drop-until-intra",
        "Drop until intra",
        "Drop buffers until an intra picture arrives when there was a GAP",
        DEFAULT_DROP_UNTIL_INTRA,
        G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

    obj_properties[PROP_RETRY_INTERVAL] = g_param_spec_uint("retry-interval",
        "Retry interval",
        "Number of buffers dropped until a new key unit request is made (0=disable)",
        0, G_MAXUINT, DEFAULT_RETRY_INTERVAL,
        G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

    g_object_class_install_properties(gobject_class, NUM_PROPERTIES, obj_properties);
}

static void gst_videorepair_reset(GstVideoRepair *videorepair)
{
    videorepair->needs_intra = TRUE;
    videorepair->drop_count = 0;
}

static void gst_videorepair_init(GstVideoRepair *videorepair)
{
    videorepair->sinkpad = gst_pad_new_from_static_template(
        &gst_videorepair_sink_template, "sink");
    gst_pad_set_chain_function(videorepair->sinkpad,
        GST_DEBUG_FUNCPTR(gst_videorepair_sink_chain));
    gst_pad_set_event_function(videorepair->sinkpad,
        GST_DEBUG_FUNCPTR(gst_videorepair_sink_event));
    GST_PAD_SET_PROXY_CAPS(videorepair->sinkpad);
    gst_element_add_pad(GST_ELEMENT(videorepair), videorepair->sinkpad);

    videorepair->srcpad = gst_pad_new_from_static_template(
        &gst_videorepair_src_template, "src");
    GST_PAD_SET_PROXY_CAPS(videorepair->srcpad);
    gst_element_add_pad(GST_ELEMENT(videorepair), videorepair->srcpad);

    videorepair->drop_until_intra = DEFAULT_DROP_UNTIL_INTRA;
    videorepair->retry_interval = DEFAULT_RETRY_INTERVAL;

    gst_videorepair_reset(videorepair);
}

void gst_videorepair_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
    GstVideoRepair *videorepair = GST_VIDEOREPAIR(object);

    switch (property_id) {
    case PROP_DROP_UNTIL_INTRA:
        videorepair->drop_until_intra = g_value_get_boolean(value);
        break;

    case PROP_RETRY_INTERVAL:
        videorepair->retry_interval = g_value_get_uint(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_videorepair_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
    GstVideoRepair *videorepair = GST_VIDEOREPAIR(object);

    switch (property_id) {
    case PROP_DROP_UNTIL_INTRA:
        g_value_set_boolean(value, videorepair->drop_until_intra);
        break;

    case PROP_RETRY_INTERVAL:
        g_value_set_uint(value, videorepair->retry_interval);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static GstStateChangeReturn gst_videorepair_change_state(GstElement *element, GstStateChange transition)
{
    GstVideoRepair *videorepair = GST_VIDEOREPAIR(element);
    GstStateChangeReturn ret;

    /* We don't need to do anything for upward transitions */
    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
    if (ret != GST_STATE_CHANGE_SUCCESS)
        return ret;

    switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        gst_videorepair_reset(videorepair);
        break;

    default:
        break;
    }

    return GST_STATE_CHANGE_SUCCESS;
}

static gboolean gst_videorepair_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    GstVideoRepair *videorepair = GST_VIDEOREPAIR(parent);
    gboolean ret;

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_GAP:
        GST_INFO_OBJECT(videorepair, "got GAP event");
        gst_pad_push_event(videorepair->sinkpad,
            gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, FALSE, 0));
        videorepair->needs_intra = TRUE;
        ret = TRUE;
        break;

    default:
        ret = gst_pad_event_default(pad, parent, event);
    }

    return ret;
}

static GstFlowReturn gst_videorepair_sink_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
    GstVideoRepair *videorepair = GST_VIDEOREPAIR(parent);
    (void)pad;

    if (!GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
        GST_DEBUG_OBJECT(videorepair, "got keyframe");
        videorepair->needs_intra = FALSE;
        videorepair->drop_count = 0;
    }

    if (videorepair->needs_intra && videorepair->drop_until_intra) {
        GST_DEBUG_OBJECT(videorepair, "dropping buffer waiting for intra");
        videorepair->drop_count++;
        if (videorepair->retry_interval
            && (videorepair->drop_count >= videorepair->retry_interval)) {
            GST_INFO_OBJECT(videorepair, "still no intra picture, requesting a key unit again");
            gst_pad_push_event(videorepair->sinkpad,
                gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, FALSE, 0));
            videorepair->drop_count = 0;
        }
        gst_buffer_unref(buffer);
        return GST_FLOW_OK;
    }

    return gst_pad_push(videorepair->srcpad, buffer);
}

static gboolean plugin_init(GstPlugin *plugin)
{
    return gst_element_register(plugin, "videorepair", GST_RANK_NONE, GST_TYPE_VIDEOREPAIR);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    videorepair,
    "Requests intra pictures on GAP events and (optionally) drops buffers until they arrive",
    plugin_init,
    "0.0.1",
    "BSD",
    "OpenWebRTC GStreamer plugins",
    "http://www.ericsson.com")
