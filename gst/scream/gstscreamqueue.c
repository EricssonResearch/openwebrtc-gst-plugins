/*
* Copyright (c) 2015, Ericsson AB. All rights reserved.
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

#include "gstscreamqueue.h"

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC(gst_scream_queue_debug_category);
#define GST_CAT_DEFAULT gst_scream_queue_debug_category

#define gst_scream_queue_parent_class parent_class
G_DEFINE_TYPE(GstScreamQueue, gst_scream_queue, GST_TYPE_ELEMENT);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK,
    GST_PAD_ALWAYS, GST_STATIC_CAPS("application/x-rtp"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC,
    GST_PAD_ALWAYS, GST_STATIC_CAPS("application/x-rtp"));

typedef enum
{
    GST_SCREAM_DATA_QUEUE_ITEM_TYPE_RTP,
    GST_SCREAM_DATA_QUEUE_ITEM_TYPE_RTCP
} GstScreamDataQueueItemType;

typedef struct {
    GstDataQueueItem item;
    GstScreamDataQueueItemType type;

    guint32 rtp_ssrc;

} GstScreamDataQueueItem;

typedef struct {
    GstScreamDataQueueItem item;

    guint64 gst_ts;
    gboolean adapted;
    guint8 rtp_pt;
    guint16 rtp_seq;
    guint32 rtp_ts;
    gboolean rtp_marker;
    guint rtp_payload_size;
    guint64 enqueued_time;
} GstScreamDataQueueRtpItem;

typedef struct {
    GstScreamDataQueueItem item;

    gboolean adapted;
    guint timestamp;
    guint highest_seq;
    guint n_loss;
    guint n_ecn;
    gboolean qbit;
} GstScreamDataQueueRtcpItem;


typedef struct {
    guint ssrc, pt;
    GstDataQueue *packet_queue;
    guint enqueued_payload_size;
    guint enqueued_packets;
} GstScreamStream;

enum {
    SIGNAL_BITRATE_CHANGE,
    SIGNAL_PAYLOAD_ADAPTATION_REQUEST,
    SIGNAL_INCOMING_FEEDBACK,
    NUM_SIGNALS

};

static guint signals[NUM_SIGNALS];

enum {
    PROP_0,

    PROP_GST_SCREAM_CONTROLLER_ID,
    PROP_PASS_THROUGH,

    NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES];

#define DEFAULT_GST_SCREAM_CONTROLLER_ID 1
#define DEFAULT_PRIORITY 1.0
#define DEFAULT_PASS_THROUGH FALSE
#define SCREAM_MAX_BITRATE 5000000
#define SCREAM_MIN_BITRATE 64000

GType gst_scream_queue_pad_get_type(void);


static void gst_scream_queue_finalize(GObject *object);
static void gst_scream_queue_set_property(GObject *object, guint prop_id, const GValue *value,
    GParamSpec *pspec);
static void gst_scream_queue_get_property(GObject *object, guint prop_id, GValue *value,
    GParamSpec *pspec);
static GstStateChangeReturn gst_scream_queue_change_state(GstElement *element,
    GstStateChange transition);
static GstFlowReturn gst_scream_queue_sink_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer);
static gboolean gst_scream_queue_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);
static gboolean gst_scream_queue_src_event(GstPad *pad, GstObject *parent, GstEvent *event);

static void gst_scream_queue_srcpad_loop(GstScreamQueue *self);
static GstScreamStream * get_stream(GstScreamQueue *self, guint ssrc, guint pt);

static guint get_next_packet_rtp_payload_size(guint stream_id, GstScreamQueue *self);

static gboolean configure(GstScreamQueue *self);
static void on_bitrate_change(guint bitrate, guint stream_id, GstScreamQueue *self);
static void approve_transmit_cb(guint stream_id, GstScreamQueue *self);
static void clear_queue(guint stream_id, GstScreamQueue *self);

static void gst_scream_queue_incoming_feedback(GstScreamQueue *self, guint ssrc,
    guint timestamp, guint highest_seq, guint n_loss, guint n_ecn, gboolean qbit);
static guint64 get_gst_time_us(GstScreamQueue *self);

static void gst_scream_queue_class_init(GstScreamQueueClass *klass)
{
    GObjectClass *gobject_class;
    GstElementClass *element_class;

    gobject_class = (GObjectClass *) klass;
    element_class = (GstElementClass *) klass;

    GST_DEBUG_CATEGORY_INIT(gst_scream_queue_debug_category,
        "screamqueue", 0, "debug category for screamqueue element");

    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
        gst_static_pad_template_get(&src_template));
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
        gst_static_pad_template_get(&sink_template));

    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_scream_queue_finalize);
    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_scream_queue_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_scream_queue_get_property);

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_scream_queue_change_state);

    signals[SIGNAL_BITRATE_CHANGE] = g_signal_new("on-bitrate-change", G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(GstScreamQueueClass, gst_scream_queue_on_bitrate_change), NULL, NULL,
        g_cclosure_marshal_generic, G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

    signals[SIGNAL_PAYLOAD_ADAPTATION_REQUEST] = g_signal_new("on-payload-adaptation-request",
        G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(GstScreamQueueClass, gst_scream_queue_on_adaptation_request), NULL, NULL,
        g_cclosure_marshal_generic, G_TYPE_BOOLEAN, 1, G_TYPE_UINT);

    signals[SIGNAL_INCOMING_FEEDBACK] = g_signal_new("incoming-feedback", G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
        G_STRUCT_OFFSET(GstScreamQueueClass, gst_scream_queue_incoming_feedback), NULL, NULL,
        g_cclosure_marshal_generic, G_TYPE_NONE, 6, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
        G_TYPE_UINT, G_TYPE_UINT, G_TYPE_BOOLEAN);

    klass->gst_scream_queue_incoming_feedback = GST_DEBUG_FUNCPTR(gst_scream_queue_incoming_feedback);

    properties[PROP_GST_SCREAM_CONTROLLER_ID] =
        g_param_spec_uint("scream-controller-id",
            "SCReAM Controller ID",
            "Every queue that should be handled by the same controller should have the same "
            "scream-controller-id. This value must be set before any pads are requested.",
            0, G_MAXUINT, DEFAULT_GST_SCREAM_CONTROLLER_ID,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PASS_THROUGH] =
        g_param_spec_boolean("pass-through",
            "Pass through",
            "If set to true all packets will just pass through the plugin",
            DEFAULT_PASS_THROUGH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);



    g_object_class_install_properties(gobject_class, NUM_PROPERTIES, properties);

    gst_element_class_set_static_metadata(element_class,
        "SCREAM Queue",
        "Queue/Network/Adaptation",
        "RTP Queue for SCREAM adaptation",
        "Daniel Lindström <daniel.lindstrom@ericsson.com>");
}

static gboolean data_queue_check_full_cb(GstDataQueue *queue, guint visible, guint bytes,
    guint64 time, gpointer user_data)
{
    return FALSE;
}

static void data_queue_empty_cb(GstDataQueue *queue, gpointer user_data)
{
}

static void data_queue_full_cb(GstDataQueue *queue, gpointer user_data)
{
}

static gboolean packet_queue_check_full_cb(GstDataQueue *queue, guint visible, guint bytes,
    guint64 time, GstScreamStream *stream)
{
    return FALSE;
}

static void packet_queue_empty_cb(GstDataQueue *queue, GstScreamStream *stream)
{
}

static void packet_queue_full_cb(GstDataQueue *queue, GstScreamStream *stream)
{
}


static void gst_scream_data_queue_rtp_item_free(GstScreamDataQueueRtpItem *item)
{
    if (((GstDataQueueItem *)item)->object) {
        gst_mini_object_unref(((GstDataQueueItem *)item)->object);
    }
    g_slice_free(GstScreamDataQueueRtpItem, item);
}

static void gst_scream_data_queue_rtcp_item_free(GstScreamDataQueueRtcpItem *item)
{
    g_slice_free(GstScreamDataQueueRtcpItem, item);
}

static void destroy_stream(GstScreamStream *stream) {
    GstDataQueueItem *item;
    while (!gst_data_queue_is_empty(stream->packet_queue)) {
        gst_data_queue_pop(stream->packet_queue, &item);
        item->destroy(item);
    }
    gst_object_unref(stream->packet_queue);
    g_free(stream);
}


static void gst_scream_queue_init(GstScreamQueue *self)
{
    self->sink_pad = gst_pad_new_from_static_template(&sink_template, "sink");
    gst_pad_set_chain_function(self->sink_pad, GST_DEBUG_FUNCPTR(gst_scream_queue_sink_chain));
    gst_pad_set_event_function(self->sink_pad, GST_DEBUG_FUNCPTR(gst_scream_queue_sink_event));
    GST_PAD_SET_PROXY_CAPS(self->sink_pad);
    gst_element_add_pad(GST_ELEMENT(self), self->sink_pad);

    self->src_pad = gst_pad_new_from_static_template(&src_template, "src");
    gst_pad_set_event_function(self->src_pad, GST_DEBUG_FUNCPTR(gst_scream_queue_src_event));
    GST_PAD_SET_PROXY_CAPS(self->src_pad);
    gst_element_add_pad(GST_ELEMENT(self), self->src_pad);

    g_rw_lock_init(&self->lock);
    self->streams = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)destroy_stream);
    self->adapted_stream_ids = g_hash_table_new(NULL, NULL);
    self->ignored_stream_ids = g_hash_table_new(NULL, NULL);

    self->scream_controller_id = DEFAULT_GST_SCREAM_CONTROLLER_ID;
    self->scream_controller = NULL;
    self->approved_packets = gst_data_queue_new(
        (GstDataQueueCheckFullFunction)data_queue_check_full_cb,
        (GstDataQueueFullCallback)data_queue_full_cb,
        (GstDataQueueEmptyCallback)data_queue_empty_cb, self);
    self->number_of_approved_packets = 0;

    self->incoming_packets = gst_data_queue_new(
        (GstDataQueueCheckFullFunction)data_queue_check_full_cb,
        (GstDataQueueFullCallback)data_queue_full_cb,
        (GstDataQueueEmptyCallback)data_queue_empty_cb, self);

    self->priority = DEFAULT_PRIORITY;
    self->pass_through = DEFAULT_PASS_THROUGH;
    self->next_approve_time = 0;
}

static void gst_scream_queue_finalize(GObject *object)
{
    GstScreamQueue *self = GST_SCREAM_QUEUE(object);
    GstDataQueueItem *item;

    while (!gst_data_queue_is_empty(self->approved_packets)) {
        gst_data_queue_pop(self->approved_packets, &item);
        item->destroy(item);
    }
    gst_object_unref(self->approved_packets);

    while (!gst_data_queue_is_empty(self->incoming_packets)) {
        gst_data_queue_pop(self->incoming_packets, &item);
        item->destroy(item);
    }
    gst_object_unref(self->incoming_packets);

    g_hash_table_unref(self->streams);
    g_hash_table_unref(self->adapted_stream_ids);
    g_hash_table_unref(self->ignored_stream_ids);

    if (self->scream_controller) {
        g_object_unref(self->scream_controller);
    }

    G_OBJECT_CLASS(parent_class)->finalize (object);
}

static void gst_scream_queue_set_property(GObject *object, guint prop_id, const GValue *value,
    GParamSpec *pspec)
{
    GstScreamQueue *self = GST_SCREAM_QUEUE(object);

    switch (prop_id) {
    case PROP_GST_SCREAM_CONTROLLER_ID:
        self->scream_controller_id = g_value_get_uint(value);
        break;
    case PROP_PASS_THROUGH:
        self->pass_through = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(self, prop_id, pspec);
        break;
    }
}

static void gst_scream_queue_get_property(GObject *object, guint prop_id, GValue *value,
    GParamSpec *pspec)
{
    GstScreamQueue *self = GST_SCREAM_QUEUE(object);

    switch (prop_id) {
    case PROP_GST_SCREAM_CONTROLLER_ID:
        g_value_set_uint(value, self->scream_controller_id);
        break;
    case PROP_PASS_THROUGH:
        g_value_set_boolean(value, self->pass_through);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(self, prop_id, pspec);
        break;
    }
}

static GstStateChangeReturn gst_scream_queue_change_state(GstElement *element,
    GstStateChange transition)
{
    GstScreamQueue *self = GST_SCREAM_QUEUE(element);
    GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
    gboolean res = TRUE;

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        break;

    case GST_STATE_CHANGE_READY_TO_PAUSED:
        if (configure(GST_SCREAM_QUEUE(element))) {
            gst_pad_start_task(self->src_pad, (GstTaskFunction)gst_scream_queue_srcpad_loop,
            self, NULL);

        } else {
            g_warning("Failed to change state!");
            res = FALSE;
        }
        break;

    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
        break;

    case GST_STATE_CHANGE_READY_TO_NULL:
        break;
    default:
        break;
    }

    if (res)
        ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

    return ret;
}


static GstFlowReturn gst_scream_queue_sink_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
    GstScreamQueue *self = GST_SCREAM_QUEUE(parent);
    GstRTPBuffer rtp_buffer = GST_RTP_BUFFER_INIT;
    GstFlowReturn flow_ret = GST_FLOW_OK;
    GstScreamDataQueueRtpItem *rtp_item;

    if (GST_PAD_IS_FLUSHING(pad))
        goto end;

    if (!gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp_buffer))
        goto end;

    rtp_item = g_slice_new(GstScreamDataQueueRtpItem);
    ((GstDataQueueItem *)rtp_item)->object = GST_MINI_OBJECT(buffer);
    ((GstDataQueueItem *)rtp_item)->size = gst_buffer_get_size(buffer);
    ((GstDataQueueItem *)rtp_item)->visible = TRUE;
    ((GstDataQueueItem *)rtp_item)->duration = GST_BUFFER_DURATION(buffer);
    ((GstDataQueueItem *)rtp_item)->destroy = (GDestroyNotify) gst_scream_data_queue_rtp_item_free;

    ((GstScreamDataQueueItem *)rtp_item)->type = GST_SCREAM_DATA_QUEUE_ITEM_TYPE_RTP;
    ((GstScreamDataQueueItem *)rtp_item)->rtp_ssrc = gst_rtp_buffer_get_ssrc(&rtp_buffer);
    rtp_item->rtp_pt = gst_rtp_buffer_get_payload_type(&rtp_buffer);
    rtp_item->gst_ts = GST_BUFFER_PTS(buffer);
    rtp_item->rtp_seq = gst_rtp_buffer_get_seq(&rtp_buffer);
    rtp_item->rtp_ts = gst_rtp_buffer_get_timestamp(&rtp_buffer);
    rtp_item->rtp_marker = gst_rtp_buffer_get_marker(&rtp_buffer);
    rtp_item->rtp_payload_size = gst_rtp_buffer_get_payload_len(&rtp_buffer);
    rtp_item->enqueued_time = get_gst_time_us(self);
    gst_rtp_buffer_unmap(&rtp_buffer);

    if (self->pass_through) {
        rtp_item->adapted = FALSE;
        self->number_of_approved_packets++;
        gst_data_queue_push(self->approved_packets, (GstDataQueueItem *)rtp_item);
        goto end;
    }

    if (G_UNLIKELY(!gst_data_queue_push(self->incoming_packets, (GstDataQueueItem *)rtp_item))) {
        g_log(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "Failed pusing RTP packet to incoming packet queue. flushing?");
        ((GstDataQueueItem *)rtp_item)->destroy(rtp_item);
    }
end:
    flow_ret = GST_PAD_IS_FLUSHING(pad) ? GST_FLOW_FLUSHING : flow_ret;
    return flow_ret;
}

static gboolean gst_scream_queue_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    gboolean ret;

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS:
        case GST_EVENT_FLUSH_STOP:
        case GST_EVENT_STREAM_START:
        case GST_EVENT_SEGMENT:
        default:
            ret = gst_pad_event_default(pad, parent, event);
            break;
    }

    return ret;
}

static gboolean gst_scream_queue_src_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    gboolean ret;

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_FLUSH_START:
        case GST_EVENT_RECONFIGURE:
        case GST_EVENT_FLUSH_STOP:
        default:
            ret = gst_pad_event_default(pad, parent, event);
            break;
    }

    return ret;
}


static void gst_scream_queue_srcpad_loop(GstScreamQueue *self)
{
    GstScreamDataQueueItem *item;
    GstScreamDataQueueRtpItem *rtp_item;
    GstScreamStream *stream;
    guint stream_id;
    guint64 time_now_us, time_until_next_approve = 0;
    GstBuffer *buffer;

    time_now_us = get_gst_time_us(self);
    if (G_UNLIKELY(time_now_us == 0)) {
        goto end;
    }

    if (time_now_us >= self->next_approve_time) {
        time_until_next_approve = gst_scream_controller_approve_transmits(self->scream_controller,
            time_now_us);
    }

    /* Send all approved packets */
    while (!gst_data_queue_is_empty(self->approved_packets)) {
        if (G_UNLIKELY(!gst_data_queue_pop(self->approved_packets,
            (GstDataQueueItem **)&rtp_item))) {
            g_warning("Failed to pop from approved packets queue. Flushing?");
            goto end; /* flushing */
        }

        buffer = GST_BUFFER(((GstDataQueueItem *)rtp_item)->object);
        gst_pad_push(self->src_pad, buffer);
        self->number_of_approved_packets--;

        if (rtp_item->adapted) {
            guint tmp_time;
            stream_id = ((GstScreamDataQueueItem *)rtp_item)->rtp_ssrc;
            tmp_time = gst_scream_controller_packet_transmitted(self->scream_controller, stream_id,
                rtp_item->rtp_payload_size, rtp_item->rtp_seq, time_now_us);
            time_until_next_approve = MIN(time_until_next_approve, tmp_time);
        }
        g_slice_free(GstScreamDataQueueRtpItem, rtp_item);
    }
    self->next_approve_time = time_now_us + time_until_next_approve;

    if (!gst_data_queue_pop(self->incoming_packets, (GstDataQueueItem **)&item)) {
        /* flushing */
        g_warning("Failed to pop from incoming packets queue. Flushing?");
        goto end;
    }

    stream_id = item->rtp_ssrc;
    if (item->type == GST_SCREAM_DATA_QUEUE_ITEM_TYPE_RTP) {
        GstScreamDataQueueRtpItem *rtp_item = (GstScreamDataQueueRtpItem *)item;
        stream = get_stream(self, item->rtp_ssrc, rtp_item->rtp_pt);
        if (!stream) {
            self->number_of_approved_packets++;
            rtp_item->adapted = FALSE;
            gst_data_queue_push(self->approved_packets, (GstDataQueueItem *)item);
        } else {
            if (G_LIKELY(gst_data_queue_push(stream->packet_queue, (GstDataQueueItem *)rtp_item))) {
                stream->enqueued_payload_size += rtp_item->rtp_payload_size;
                stream->enqueued_packets++;
                rtp_item->adapted = TRUE;
                self->next_approve_time = 0;
                gst_scream_controller_new_rtp_packet(self->scream_controller, stream_id, rtp_item->rtp_ts,
                    rtp_item->enqueued_time, stream->enqueued_payload_size, rtp_item->rtp_payload_size);
            } else {
                g_log(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "Failed pusing RTP packet to the stream packet queue. flushing?");
                ((GstDataQueueItem *)rtp_item)->destroy(rtp_item);
            }
        }
    } else { /* item->type == GST_SCREAM_DATA_QUEUE_ITEM_TYPE_RTCP */
        GstScreamDataQueueRtcpItem *rtcp_item = (GstScreamDataQueueRtcpItem *)item;

        gst_scream_controller_incoming_feedback(self->scream_controller, stream_id, time_now_us,
            rtcp_item->timestamp, rtcp_item->highest_seq, rtcp_item->n_loss, rtcp_item->n_ecn, rtcp_item->qbit);

        ((GstDataQueueItem *)item)->destroy(item);
    }

end:
    return;

}


static GstScreamStream * get_stream(GstScreamQueue *self, guint ssrc, guint pt)
{
    GstScreamStream *stream = NULL;
    gboolean adapt_stream = FALSE;
    guint stream_id = ssrc;

    if (G_LIKELY(g_hash_table_contains(self->adapted_stream_ids, GUINT_TO_POINTER(stream_id)))) {
        g_rw_lock_reader_lock(&self->lock);
        stream = g_hash_table_lookup(self->streams, GUINT_TO_POINTER(stream_id));
        g_rw_lock_reader_unlock(&self->lock);
    } else if (g_hash_table_contains(self->ignored_stream_ids, GUINT_TO_POINTER(stream_id))) {
        /* DO NOTHING */
    } else {
        g_signal_emit_by_name(self, "on-payload-adaptation-request", pt, &adapt_stream);
        if (!adapt_stream) {
            g_log(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "Ignoring adaptation for payload %u for ssrc %u", pt, stream_id);
            g_hash_table_add(self->ignored_stream_ids, GUINT_TO_POINTER(stream_id));
        } else {
            if (gst_scream_controller_register_new_stream(self->scream_controller,
                stream_id, self->priority, SCREAM_MIN_BITRATE, SCREAM_MAX_BITRATE,
                (GstScreamQueueBitrateRequestedCb)on_bitrate_change,
                (GstScreamQueueNextPacketSizeCb)get_next_packet_rtp_payload_size,
                (GstScreamQueueApproveTransmitCb)approve_transmit_cb,
                (GstScreamQueueClearQueueCb)clear_queue,
                (gpointer)self)) {

                stream = g_new0(GstScreamStream, 1);
                stream->ssrc = ssrc;
                stream->pt = pt;
                stream->packet_queue = gst_data_queue_new(
                (GstDataQueueCheckFullFunction)packet_queue_check_full_cb,
                (GstDataQueueFullCallback)packet_queue_full_cb,
                (GstDataQueueEmptyCallback)packet_queue_empty_cb, self);
                stream->enqueued_payload_size = 0;
                stream->enqueued_packets = 0;
                g_rw_lock_writer_lock(&self->lock);
                g_hash_table_insert(self->streams, GUINT_TO_POINTER(stream_id), stream);
                g_rw_lock_writer_unlock(&self->lock);
                g_hash_table_add(self->adapted_stream_ids, GUINT_TO_POINTER(stream_id));
            } else {
                g_warning("Failed to register new stream\n");
            }
        }
    }
    return stream;
}



static guint get_next_packet_rtp_payload_size(guint stream_id, GstScreamQueue *self)
{
    GstScreamDataQueueRtpItem *item;
    GstScreamStream *stream;
    guint size = 0;

    g_rw_lock_reader_lock(&self->lock);
    stream = g_hash_table_lookup(self->streams, GUINT_TO_POINTER(stream_id));
    g_rw_lock_reader_unlock(&self->lock);
    if (!gst_data_queue_is_empty(stream->packet_queue) && gst_data_queue_peek(stream->packet_queue, (GstDataQueueItem **)&item)) {
        size = item->rtp_payload_size;
    }
    return size;
}


static gboolean configure(GstScreamQueue *self) {
    gboolean res = TRUE;
    GstScreamController *controller = gst_scream_controller_get(self->scream_controller_id);

    if (controller) {
        self->scream_controller = controller;
    } else {
        res = FALSE;
        g_warning("Could not create Scream Controller");
    }

    return res;
}

typedef struct
{
    guint bitrate;
    guint ssrc;
    guint pt;
    GstScreamQueue *scream_queue;
} BitrateChangeStruct;

static gboolean emit_on_bitrate_change(BitrateChangeStruct *bitrate_struct)
{
    g_signal_emit_by_name(bitrate_struct->scream_queue, "on-bitrate-change",
        bitrate_struct->bitrate, bitrate_struct->ssrc, bitrate_struct->pt);
    gst_object_unref(GST_ELEMENT(bitrate_struct->scream_queue));
    g_slice_free(BitrateChangeStruct, bitrate_struct);
    return G_SOURCE_REMOVE;
}

static void on_bitrate_change(guint bitrate, guint stream_id, GstScreamQueue *self)
{
    BitrateChangeStruct *bitrate_struct;
    GstScreamStream *stream;

    g_rw_lock_reader_lock(&self->lock);
    stream = g_hash_table_lookup(self->streams, GUINT_TO_POINTER(stream_id));
    g_rw_lock_reader_unlock(&self->lock);
    g_return_if_fail(stream);

    bitrate_struct = g_slice_new(BitrateChangeStruct);
    bitrate_struct->bitrate = bitrate;
    bitrate_struct->ssrc = stream->ssrc;
    bitrate_struct->pt = stream->pt;
    bitrate_struct->scream_queue = self;
    gst_object_ref(GST_ELEMENT(self));
    g_timeout_add(0, (GSourceFunc)emit_on_bitrate_change, bitrate_struct);
}


static void approve_transmit_cb(guint stream_id, GstScreamQueue *self) {
    GstScreamDataQueueRtpItem *item;
    GstScreamStream *stream;

    g_rw_lock_reader_lock(&self->lock);
    stream = g_hash_table_lookup(self->streams, GUINT_TO_POINTER(stream_id));
    g_rw_lock_reader_unlock(&self->lock);

    if (gst_data_queue_pop(stream->packet_queue, (GstDataQueueItem **)&item)) {
        stream->enqueued_payload_size -= item->rtp_payload_size;
        stream->enqueued_packets--;
        gst_data_queue_push(self->approved_packets, (GstDataQueueItem *)item);
        self->number_of_approved_packets++;
    }
}

static void clear_queue(guint stream_id, GstScreamQueue *self)
{
    GstScreamStream *stream;
    g_rw_lock_reader_lock(&self->lock);
    stream = g_hash_table_lookup(self->streams, GUINT_TO_POINTER(stream_id));
    g_rw_lock_reader_unlock(&self->lock);
    gst_data_queue_flush(stream->packet_queue);
    stream->enqueued_payload_size = 0;
    stream->enqueued_packets = 0;
    gst_pad_push_event(self->sink_pad,
        gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, FALSE, 0));
}


static void gst_scream_queue_incoming_feedback(GstScreamQueue *self, guint ssrc,
    guint timestamp, guint highest_seq, guint n_loss, guint n_ecn, gboolean qbit)
{
    GstScreamDataQueueRtcpItem *rtcp_item;
    rtcp_item = g_slice_new(GstScreamDataQueueRtcpItem);
    ((GstDataQueueItem *)rtcp_item)->object = NULL;
    ((GstDataQueueItem *)rtcp_item)->size = 0;
    ((GstDataQueueItem *)rtcp_item)->visible = TRUE;
    ((GstDataQueueItem *)rtcp_item)->duration = 0;
    ((GstDataQueueItem *)rtcp_item)->destroy = (GDestroyNotify) gst_scream_data_queue_rtcp_item_free;
    ((GstScreamDataQueueItem *)rtcp_item)->type = GST_SCREAM_DATA_QUEUE_ITEM_TYPE_RTCP;
    ((GstScreamDataQueueItem *)rtcp_item)->rtp_ssrc = ssrc;
    rtcp_item->highest_seq = highest_seq;
    rtcp_item->n_loss = n_loss;
    rtcp_item->n_ecn = n_ecn;
    rtcp_item->timestamp = timestamp;
    rtcp_item->qbit = qbit;

    if (G_UNLIKELY(!gst_data_queue_push(self->incoming_packets, (GstDataQueueItem *)rtcp_item))) {
        ((GstDataQueueItem *)rtcp_item)->destroy(rtcp_item);
    }
}

static guint64 get_gst_time_us(GstScreamQueue *self)
{
    GstClock *clock = NULL;
    GstClockTime time = 0;

    clock = gst_element_get_clock(GST_ELEMENT(self));
    if (G_LIKELY(clock)) {
        time = gst_clock_get_time(clock);
    }
    return time / 1000;
}
