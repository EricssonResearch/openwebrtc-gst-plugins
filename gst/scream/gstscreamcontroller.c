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

#include "gstscreamcontroller.h"

#include <math.h>

#include <gst/gstinfo.h>

/* Timestamp sampling rate for SCReAM feedback*/
#define TIMESTAMP_RATE 1000.0f

/*
 * A few switches to make debugging easier
 * Open a full congestion window
 */
#define OPEN_CWND FALSE

/*
 * Some good to have features, SCReAM works also with these disabled
 * Enable shared bottleneck detection and OWD target adjustement
 * good if SCReAM needs to compete with e.g FTP but
 * Can in some cases cause self-inflicted congestion
 */
#define ENABLE_SBD TRUE
/* Fast start can resume if little or no congestion detected */
#define ENABLE_CONSECUTIVE_FAST_START TRUE
/* Packet pacing reduces jitter */
#define ENABLE_PACKET_PACING TRUE

/*
 * ==== Main tuning parameters (if tuning necessary) ====
 * Most important parameters first
 * Typical frame period
 */
#define FRAME_PERIOD 0.040f
/* Max video rampup speed in bps/s (bits per second increase per second) */
#define RAMP_UP_SPEED 200000.0f // bps/s
/* CWND scale factor upon loss event */
#define LOSS_BETA 0.6f
/*
 * Compensation factor for RTP queue size
 * A higher value such as 0.2 gives less jitter esp. in wireless (LTE)
 * but potentially also lower link utilization
 */
#define TX_QUEUE_SIZE_FACTOR 1.0f
/*
 * Compensation factor for detected congestion in rate computation
 * A higher value such as 0.2 gives less jitter esp. in wireless (LTE)
 * but potentially also lower link utilization
 */
#define OWD_GUARD 0.2f

/* Video rate scaling due to loss events */
#define LOSS_EVENT_RATE_SCALE 0.9f
/*
 * Additional send window slack (if no or little congestion detected)
 * An increased value such as 0.5 can improve transmission of Key frames
 * however with a higher risk of unstable behavior in
 * sudden congestion situations
 */
#define BYTES_IN_FLIGHT_SLACK 0.0f
/* Rate adjust interval */
#define RATE_ADJUST_INTERVAL 200000 /* us */

/* ==== Less important tuning parameters ==== */
/* Min pacing interval and min pacing rate*/
#define MIN_PACE_INTERVAL 0.00f    /* s */
#define MINIMUM_PACE_BANDWIDTH 50000.0f /* bps */
/* Initial MSS */
#define INIT_MSS 100
/* Initial CWND */
#define INIT_CWND 5000
/* CWND up and down gain factors */
#define CWND_GAIN_UP 1.0f
#define CWND_GAIN_DOWN 1.0f
/* Min and max OWD target */
#define OWD_TARGET_MIN 0.1f /* ms */
#define OWD_TARGET_MAX 0.4f /* ms */
/* Congestion window validation */
#define BYTES_IN_FLIGHT_HIST_INTERVAL 1000000 /* Time (us) between stores */
#define MAX_BYTES_IN_FLIGHT_HEADROOM 1.0f
/* OWD trend and shared bottleneck detection */
#define OWD_FRACTION_HIST_INTERVAL 50000 /* us */
/* Max video rate estimation update period */
#define RATE_UPDATE_INTERVAL 50000  /* us */

/*
 * When the queued time is > than MAX_RTP_QUEUE_TIME the queue time is emptied. This allow for faster
 * "catching up" when the throughput drops from a very high to a very low value
 */
#define MAX_RTP_QUEUE_TIME 0.5f

GST_DEBUG_CATEGORY_EXTERN(gst_scream_queue_debug_category);
#define GST_CAT_DEFAULT gst_scream_queue_debug_category

G_DEFINE_TYPE(GstScreamController, gst_scream_controller, G_TYPE_OBJECT);

/* Just a high timer.. */
#define DONT_APPROVE_TRANSMIT_TIME 10000000

enum
{
  LAST_SIGNAL
};


enum {
    PROP_0,

    /*PROP_PASSTHROUGH,*/

    NUM_PROPERTIES
};

#define RATE_RTP_HIST_SIZE 21
#define RATE_UPDATE_SIZE 4

typedef struct {
    guint id;

    GstScreamQueueBitrateRequestedCb on_bitrate_callback;
    GstScreamQueueNextPacketSizeCb get_next_packet_size_callback;
    GstScreamQueueApproveTransmitCb approve_transmit_callback;
    GstScreamQueueClearQueueCb clear_queue;
    gpointer user_data;


    guint last_rtp_timestamp;
    guint64 oldest_packet_enqueue_time;
    guint bytes_in_queue;
    gfloat credit;                 /* Credit that is received if another stream gets */
                                   /*  priority to transmit */
    gfloat priority;               /* Stream priority */
    guint bytes_transmitted;       /* Number of bytes transmitted */
    guint bytes_acked;             /* Number of ACKed bytes */
    gfloat rate_transmitted;       /* Transmitted rate */
    gfloat rate_acked;             /* ACKed rate */
    gfloat min_bitrate;            /* Min bitrate */
    gfloat max_bitrate;            /* Max bitrate */
    gfloat target_bitrate;         /* Target bitrate */
    gfloat target_bitrate_i;       /* Target bitrate inflection point */
    guint64 last_bitrate_adjust_t_us;/* Last time rate was updated for this stream */
    gboolean was_fast_start;       /* Was fast start */
    gboolean loss_event_flag;      /* Was loss event */
    guint tx_size_bits_avg;        /* Avergage bits queued in RTP queue */
    guint next_packet_size;        /* Size of next RTP packet in Queue */
    guint n_loss;                  /* Number of losses, reported by receiver */
    guint64 t_last_rtp_q_clear_us; /* Last time RTP Q cleared */
    guint bytes_rtp;
    gfloat rate_rtp;
    gfloat rate_rtp_sum;
    gint rate_rtp_sum_n;
    gfloat rate_rtp_hist[RATE_RTP_HIST_SIZE];
    gint rate_rtp_hist_ptr;
    gfloat rate_rtp_median;
    gfloat rate_acked_hist[RATE_UPDATE_SIZE];
    gfloat rate_transmitted_hist[RATE_UPDATE_SIZE];
    gfloat rate_rtp_hist_sh[RATE_UPDATE_SIZE];
    gint rate_update_hist_ptr;
    guint64 last_target_bitrate_i_adjust_us;

    guint64 t_start_us;
} ScreamStream;

typedef struct {
    GstScreamController *controller;
    guint stream_id;
    guint timestamp;              /* Wall clock timestamp */
    guint highest_seq;            /* Highest received sequence number */
    guint n_loss;                 /* Number of detected losses */
    gboolean q_bit;               /* Quench bit */
} ScreamFeedback;

static GHashTable *controllers = NULL;
G_LOCK_DEFINE_STATIC(controllers_lock);

/* Interface implementations */
static void gst_scream_controller_finalize(GObject *object);
static void gst_scream_controller_set_property(GObject *object, guint prop_id, const GValue *value,
    GParamSpec *pspec);
static void gst_scream_controller_get_property(GObject *object, guint prop_id, GValue *value,
    GParamSpec *pspec);

static void add_credit(GstScreamController *self, ScreamStream *served_stream,
    int transmitted_bytes);
static void subtract_credit(ScreamStream *served_stream,
    int transmitted_bytes);

static guint bytes_in_flight(GstScreamController *self);
static void update_bytes_in_flight_history(GstScreamController *self, guint64 time_us);
static void update_rate(ScreamStream *stream, float t_delta);

static void update_target_stream_bitrate(GstScreamController *self, ScreamStream *stream,
  guint64 time_us);

static void initialize(GstScreamController *self, guint64 time_us);
static ScreamStream * get_prioritized_stream(GstScreamController *self);

static void update_cwnd(GstScreamController *self, guint64 time_us);

static guint get_max_bytes_in_flight(GstScreamController *self, guint vec[]);
static float get_owd_fraction(GstScreamController *self);
static void compute_owd_trend(GstScreamController *self);
static void compute_sbd(GstScreamController *self);
static guint estimate_owd(GstScreamController *self, guint64 time_us);
static guint get_base_owd(GstScreamController *self);
static gboolean is_competing_flows(GstScreamController *self);
static guint get_next_packet_size(ScreamStream *stream);

static void gst_scream_controller_class_init (GstScreamControllerClass *klass)
{
    GObjectClass *gobject_class;

    gobject_class = (GObjectClass *) klass;

    gobject_class->finalize = gst_scream_controller_finalize;
    gobject_class->set_property = gst_scream_controller_set_property;
    gobject_class->get_property = gst_scream_controller_get_property;
}

static void gst_scream_controller_init (GstScreamController *self)
{
    gint n;

    self->approve_timer_running = FALSE;

    for (n=0; n < MAX_TX_PACKETS; n++)
        self->transmitted_packets[n].is_used = FALSE;

    for (n=0; n < BASE_OWD_HIST_SIZE; n++)
        self->base_owd_hist[n] = G_MAXUINT32;
    self->base_owd_hist_ptr = 0;
    for (n=0; n < OWD_FRACTION_HIST_SIZE; n++)
        self->owd_fraction_hist[n] = 0.0f;
    self->owd_fraction_hist_ptr = 0;
    for (n=0; n < OWD_NORM_HIST_SIZE; n++)
        self->owd_norm_hist[n] = 0.0f;
    self->owd_norm_hist_ptr = 0;
    for (n=0; n < BYTES_IN_FLIGHT_HIST_SIZE; n++) {
        self->bytes_in_flight_lo_hist[n] = 0;
        self->bytes_in_flight_hi_hist[n] = 0;
    }
    self->bytes_in_flight_hist_ptr = 0;

    self->srtt_sh_us = 0;
    self->srtt_us = 0;
    self->acked_owd = 0;

    self->base_owd = G_MAXUINT;
    self->owd = 0.0;
    self->owd_fraction_avg = 0.0;
    self->owd_trend = 0.0;
    self->owd_trend_mem = 0.0;
    self->owd_target = OWD_TARGET_MIN;
    self->owd_sbd_var = 0.0;
    self->owd_sbd_skew = 0.0;
    self->owd_sbd_mean = 0.0;
    self->owd_sbd_mean_sh = 0.0;

    self->bytes_newly_acked = 0;
    self->mss = INIT_MSS;
    self->cwnd_min = INIT_MSS * 3;
    self->cwnd_i = 1;
    self->cwnd = INIT_CWND;
    self->was_cwnd_increase = FALSE;

    self->loss_event = FALSE;

    self->in_fast_start = TRUE;
    self->n_fast_start = 1;

    self->pacing_bitrate = 0.0;

    self->acc_bytes_in_flight_max = 0;
    self->n_acc_bytes_in_flight_max = 0;
    self->rate_transmitted = 0.0f;
    self->bytes_in_flight_hi_max = 0;

    self->is_initialized = FALSE;


    /*
     * These need to be initialized later med time_us is first known
     */
    self->last_srtt_update_t_us = 0;
    self->last_base_owd_add_t_us = 0;
    self->base_owd_reset_t_us = 0;
    self->last_add_to_owd_fraction_hist_t_us = 0;
    self->last_bytes_in_flight_t_us = 0;
    self->last_loss_event_t_us = 0;
    self->last_transmit_t_us = 0;
    self->next_transmit_t_us = 0;
    self->last_rate_update_t_us = 0;
    self->last_congestion_detected_t_us = 0;

    self->streams = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)g_free);


    g_mutex_init(&self->lock);

}

static void gst_scream_controller_finalize(GObject *object)
{
    GstScreamController *self = GST_SCREAM_CONTROLLER(object);
    g_hash_table_unref(self->streams);
    G_OBJECT_CLASS(gst_scream_controller_parent_class)->finalize(object);
}

static void gst_scream_controller_set_property(GObject *object, guint prop_id, const GValue *value,
    GParamSpec *pspec)
{
    GstScreamController *self = GST_SCREAM_CONTROLLER(object);

    SCREAM_UNUSED(value);

    switch (prop_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(self, prop_id, pspec);
        break;
    }
}

static void gst_scream_controller_get_property(GObject *object, guint prop_id, GValue *value,
    GParamSpec *pspec)
{
    GstScreamController *self = GST_SCREAM_CONTROLLER(object);

    SCREAM_UNUSED(value);

    switch (prop_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(self, prop_id, pspec);
        break;
    }
}

/* Public functions */
GstScreamController *gst_scream_controller_get(guint32 controller_id)
{
    GstScreamController *controller;

    G_LOCK(controllers_lock);
    if (!controllers) {
        controllers = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    }

    controller = g_hash_table_lookup(controllers, GUINT_TO_POINTER(controller_id));
    if (!controller) {
        controller = g_object_new(GST_SCREAM_TYPE_CONTROLLER, NULL);
        g_hash_table_insert(controllers, GUINT_TO_POINTER(controller_id), controller);
    } else {
        g_object_ref(controller);
    }
    G_UNLOCK(controllers_lock);
    return controller;
}

gboolean gst_scream_controller_register_new_stream(GstScreamController *controller,
    guint stream_id, gfloat priority, guint min_bitrate, guint max_bitrate,
    GstScreamQueueBitrateRequestedCb on_bitrate_callback,
    GstScreamQueueNextPacketSizeCb get_next_packet_size_callback,
    GstScreamQueueApproveTransmitCb approve_transmit_callback,
    GstScreamQueueClearQueueCb clear_queue,
    gpointer user_data)
{
    ScreamStream *stream;
    gboolean ret = FALSE;

    g_mutex_lock(&controller->lock);
    if (g_hash_table_contains(controller->streams, GUINT_TO_POINTER(stream_id))) {
        GST_WARNING("Failed to register new scream stream. The session id needs to be unique.");
        goto end;
    }

    stream = g_new0(ScreamStream, 1);
    stream->on_bitrate_callback = on_bitrate_callback;
    stream->get_next_packet_size_callback = get_next_packet_size_callback;
    stream->approve_transmit_callback = approve_transmit_callback;
    stream->clear_queue = clear_queue;
    stream->user_data = user_data;

    stream->id = stream_id;
    stream->priority = priority; /* TODO: Handle priority = 0. */
    stream->min_bitrate = (gfloat)min_bitrate;
    stream->max_bitrate = (gfloat)max_bitrate;
    stream->target_bitrate = stream->min_bitrate;
    stream->target_bitrate_i = 1.0f;
    stream->loss_event_flag = FALSE;
    /* Everything else is already zero-initialised */

    g_hash_table_insert(controller->streams, GUINT_TO_POINTER(stream_id), stream);
    ret = TRUE;
end:
    g_mutex_unlock(&controller->lock);
    return ret;
}

guint64 gst_scream_controller_packet_transmitted(GstScreamController *self, guint stream_id,
    guint size, guint16 seq, guint64 transmit_time_us)
{
    int k = 0;
    int ix = -1;
    TransmittedRtpPacket *packet;

    while (k < MAX_TX_PACKETS) {
        if (self->transmitted_packets[k].is_used == FALSE) {
          ix = k;
          break;
        }
        k++;
    }
    if (ix == -1) {
        /*
         * One should not really end up here, MAX_TX_PACKETS is set quite high
         * For example if mss = 1200byte and RTT=200ms then 1000 RTP packets in flight
         * corresponds to a bitrate of 8*1000*1200/0.2 = 48Mbps
         */

        /*
         * Pick any used entry
         */
        ix = 0;
        GST_WARNING("Max number of transmitted_packets allocated, consider increasing MAX_TX_PACKETS %u\n", MAX_TX_PACKETS);
    }

    packet = &self->transmitted_packets[ix];
    packet->stream_id = stream_id;
    packet->size = size;
    packet->seq = seq;
    packet->transmit_time_us = transmit_time_us;
    packet->is_used = TRUE;

    gfloat pace_interval = MIN_PACE_INTERVAL;
    guint64 time_next_transmit_us;
    guint64 time_until_approve_transmits_us = DONT_APPROVE_TRANSMIT_TIME;
    ScreamStream *stream;

    stream = g_hash_table_lookup(self->streams, GUINT_TO_POINTER(stream_id));
    stream->bytes_transmitted += size;

    if (OPEN_CWND) {
        time_until_approve_transmits_us = 0;
        goto end;
    }
    self->last_transmit_t_us = transmit_time_us;

    /*
     * Add credit to unserved streams
     */
    add_credit(self, stream, size);

    /*
     * Reduce used credit for served streams
     */
    subtract_credit(stream, size);

    /*
     * Compute paceInterval, we assume a min bw of 50kbps and a min tp of 1ms
     * for stable operation
     * this function implements the packet pacing
     */

    self->pacing_bitrate = MAX(MINIMUM_PACE_BANDWIDTH,
        self->cwnd * 8.0f / MAX(0.001f, self->srtt_us / 1000000.0));
    time_next_transmit_us = (size * 8.0f) / self->pacing_bitrate;
    if (self->owd_fraction_avg > 0.1f && ENABLE_PACKET_PACING) {
        pace_interval = MAX(MIN_PACE_INTERVAL, time_next_transmit_us);
    }

    /*
     * Determine when next RTP packet can be transmitted
     */
    time_until_approve_transmits_us = (guint64)(pace_interval * 1000000);
    self->next_transmit_t_us = transmit_time_us + time_until_approve_transmits_us;
end:
    return time_until_approve_transmits_us;
}

guint64 gst_scream_controller_approve_transmits(GstScreamController *self, guint64 time_us)
{
    ScreamStream *stream;
    guint size_of_next_rtp;
    gboolean exit;
    GList *it, *list;
    guint64 next_approve_time = DONT_APPROVE_TRANSMIT_TIME;

    /*
     * Update rateTransmitted and rateAcked if time for it
     * this is used in video rate computation
     */
    if (time_us - self->last_rate_update_t_us >= RATE_UPDATE_INTERVAL) {
        float t_delta = (time_us - self->last_rate_update_t_us)/1e6;
        list = it = g_hash_table_get_values(self->streams);
        self->rate_transmitted = 0.0f;
        while (it) {
            ScreamStream *stream  = (ScreamStream *)it->data;
            update_rate(stream, t_delta);
            self->rate_transmitted += stream->rate_transmitted;
            it = g_list_next(it);

        }
        g_list_free(list);
        self->last_rate_update_t_us = time_us;
    }

    /*
     * Get index to the prioritized RTP queue
     */
    stream = get_prioritized_stream(self);
    if (!stream) {
        goto end;
    } else
        GST_DEBUG("Current prioritized stream is %u", stream->id);

    /*
     * Enforce packet pacing
     */
    if (self->next_transmit_t_us - time_us > 1000 && self->next_transmit_t_us > time_us) {
        GST_DEBUG("Enforcing packet pacing: time: %" G_GUINT64_FORMAT
            ", next_transmit_t: %" G_GUINT64_FORMAT, time_us, self->next_transmit_t_us);
        next_approve_time = self->next_transmit_t_us - time_us;
        goto end;
    }

    /*
     * Update bytes in flight history for congestion window validation
     */
    update_bytes_in_flight_history(self, time_us);
    size_of_next_rtp = get_next_packet_size(stream);
    if (!size_of_next_rtp) {
        GST_DEBUG("Too many bytes in flight (avail: %u)", size_of_next_rtp);
        goto end;
    }

    exit = FALSE;
    /*
     * Determine if window is large enough to transmit
     * an RTP packet
     */
    if (self->owd_fraction_avg > 0.2) {
        /*
         * Disable limitation to send window if very
         * little congestion detected, this reduces
         * sensitivity to reverse path congestion in cases
         * where the forward path is more or less non-congested
         */
        if (self->owd > self->owd_target) {
            exit = (bytes_in_flight(self) + size_of_next_rtp) > self->cwnd;
            GST_DEBUG("Current OWD > target, exit = %u + %u > %u = %u",
                bytes_in_flight(self), size_of_next_rtp, self->cwnd, exit);
        } else {
            float x_cwnd, max_cwnd;
            x_cwnd = 1.0f + BYTES_IN_FLIGHT_SLACK * MAX(0.0f,
                MIN(1.0f, 1.0f - self->owd_trend / 0.5f));
            max_cwnd = MAX(self->cwnd * x_cwnd, (float)self->cwnd + self->mss);
            exit = bytes_in_flight(self) + size_of_next_rtp > max_cwnd;
            GST_DEBUG("Current OWD <= target, exit = %u + %u > %g = %u",
                bytes_in_flight(self), size_of_next_rtp, max_cwnd, exit);
        }
    }

    /*
     * A retransmission time out mechanism to avoid deadlock
     * An RTP packet is transmitted by force every 200ms if
     * feedback for some reason has not arrived
     */
    if (time_us - self->last_transmit_t_us > 200000) {
        exit = FALSE;
        GST_DEBUG("Too long since we transmitted, so overriding");
    }

    if (!exit) {
        /*
         * Return value 0.0 = RTP packet can be immediately transmitted
         */
        stream->next_packet_size = 0;
        stream->approve_transmit_callback(stream->id, stream->user_data);
    }

end:
    return next_approve_time;
}

void gst_scream_controller_new_rtp_packet(GstScreamController *self, guint stream_id,
    guint rtp_timestamp, guint64 time_us, guint bytes_in_queue, guint rtp_size)
{
    ScreamStream *stream;
    guint32 size_of_next_rtp;

    if (!self->is_initialized) {
        initialize(self, time_us);
    }

    stream = g_hash_table_lookup(self->streams, GUINT_TO_POINTER(stream_id));
    if (!stream) {
        GST_WARNING("Scream controller received an RTP packet that did not belong to a registered\n"
        "stream. stream_id is  %u\n", stream_id);
        goto end;
    }

    stream->bytes_rtp += rtp_size;
    if (stream->last_rtp_timestamp == rtp_timestamp) {
        goto end;
    }

    update_target_stream_bitrate(self, stream, time_us);

    stream->last_rtp_timestamp = rtp_timestamp;
    stream->bytes_in_queue = bytes_in_queue;

    /*
     * Update MSS and min CWND
     */
    size_of_next_rtp = get_next_packet_size(stream);
    self->mss = MAX(self->mss, size_of_next_rtp);
    self->cwnd_min = 3 * self->mss;
    self->cwnd = MAX(self->cwnd, self->cwnd_min);
end:
    return;
}

static void initialize(GstScreamController *self, guint64 time_us) {
    self->last_srtt_update_t_us = time_us;
    self->last_base_owd_add_t_us = time_us;
    self->base_owd_reset_t_us = time_us;
    self->last_add_to_owd_fraction_hist_t_us = time_us;
    self->last_bytes_in_flight_t_us = time_us;
    self->last_loss_event_t_us = time_us;
    self->last_transmit_t_us = time_us;
    self->next_transmit_t_us = time_us;
    self->last_rate_update_t_us = time_us;
    self->last_congestion_detected_t_us = time_us;
    self->lastfb = time_us;
    self->is_initialized = TRUE;
}

static ScreamStream * get_prioritized_stream(GstScreamController *self)
{
    ScreamStream *it_stream, *stream = NULL;
    GList *it, *list;
    guint next_packet_size;
    float max_prio = 0.0, max_credit = 1.0, priority;

    /*
     * Pick a stream with credit higher or equal to
     * the next RTP packet in queue for the given stream.
     */
    list = it = g_hash_table_get_values(self->streams);
    while (it) {
        it_stream = (ScreamStream *)it->data;
        if (it_stream->bytes_in_queue) {
            /*
             * Pick stream if it has the highest credit so far
             */
            next_packet_size = get_next_packet_size(it_stream);
            if (it_stream->credit >= MAX(max_credit, (float) next_packet_size)) {
                stream = it_stream;
                max_credit = stream->credit;
            }
        }
        it = g_list_next(it);
    }
    g_list_free(list);
    if (stream)
        goto end;

    /*
     * If the above doesn't work..
     * Pick the stream with the highest priority that also
     * has at least one RTP packets in queue,
     * add credit to streams with RTP packets in queue that did not
     * get served.
     */
    list = it = g_hash_table_get_values(self->streams);
    while (it) {
        it_stream = (ScreamStream *)it->data;
        priority = it_stream->priority;
        if (it_stream->bytes_in_queue > 0 && priority > max_prio) {
            max_prio = priority;
            stream = it_stream;
        }
        it = g_list_next(it);
    }
    g_list_free(list);
end:
    return stream;

}

static void add_credit(GstScreamController *self, ScreamStream *served_stream,
    int transmitted_bytes)
{
    GList *it, *list;
    ScreamStream *stream_it;
    gfloat credit;
    guint next_packet_size;

    list = it = g_hash_table_get_values(self->streams);
    while (it) {
        stream_it = (ScreamStream *)it->data;
        if (stream_it->id != served_stream->id) {
            credit = transmitted_bytes * stream_it->priority / served_stream->priority;
            next_packet_size = get_next_packet_size(stream_it);
            if (next_packet_size > 0)
                stream_it->credit += credit;
            else
                stream_it->credit = MIN((float) (2*self->mss), stream_it->credit + credit);
            }
        it = g_list_next(it);
    }
    g_list_free(list);
}

static void subtract_credit(ScreamStream *served_stream,
    int transmitted_bytes)
{
    served_stream->credit = MAX(0.0f, served_stream->credit - transmitted_bytes);
}

static guint bytes_in_flight(GstScreamController *self)
{
    gint ret = 0, n;
    for (n=0; n < MAX_TX_PACKETS; n++) {
        if (self->transmitted_packets[n].is_used)
            ret += self->transmitted_packets[n].size;
    }
    return ret;
}

static void update_bytes_in_flight_history(GstScreamController *self, guint64 time_us)
{
    /*
     * This function generates a history of two versions of max bytes in flight
     * #1 bytes_in_flight_lo_hist : An average of the max values over the last mesurement period
     * #2 bytes_in_flight_hi_hist : Max of the max values over the last mesurement period
     *
     * #1 is used when congestion is detected and thus puts a more strick limit in order to
     *   avoid that the network queues are bloated
     * #2 is used when little or no congestion is detected
     *
     * This allows large IR frames to be transmitted without being blocked by the transmission scheduler
     * if link throughput is high, and still introduces a safety net against large network delays when
     * link thorughput is limited
     */

    self->bytes_in_flight_hi_max = MAX(self->bytes_in_flight_hi_max, bytes_in_flight(self));
    if (time_us - self->last_bytes_in_flight_t_us > BYTES_IN_FLIGHT_HIST_INTERVAL) {
        guint bytes_in_flight_lo_max = 0;
        if (self->n_acc_bytes_in_flight_max > 0) {
            bytes_in_flight_lo_max = self->acc_bytes_in_flight_max/self->n_acc_bytes_in_flight_max;
            self->acc_bytes_in_flight_max = 0;
            self->n_acc_bytes_in_flight_max = 0;
        }
        self->bytes_in_flight_lo_hist[self->bytes_in_flight_hist_ptr] = MAX(self->cwnd_min, bytes_in_flight_lo_max);
        self->bytes_in_flight_hi_hist[self->bytes_in_flight_hist_ptr] = MAX(self->cwnd_min, self->bytes_in_flight_hi_max);
        self->bytes_in_flight_hist_ptr = (self->bytes_in_flight_hist_ptr + 1) % BYTES_IN_FLIGHT_HIST_SIZE;
        self->last_bytes_in_flight_t_us = time_us;
        self->bytes_in_flight_hi_max = 0;

        /*
         * In addition, reset MSS, this is useful in case for instance
         * a video stream is put on hold, leaving only audio packets to be
         * transmitted
         */
        self->mss = INIT_MSS;
        self->cwnd_min = 3 * self->mss;
    }
}

void update_rate(ScreamStream *stream, float t_delta)
{
    gint n;

    /*
     * Compute transmitted, acked and video (RTP) rate over a 200ms(4*50ms) sliding window
     */
    stream->rate_transmitted_hist[stream->rate_update_hist_ptr] = stream->bytes_transmitted * 8.0f / t_delta;
    stream->rate_acked_hist[stream->rate_update_hist_ptr] = stream->bytes_acked * 8.0f / t_delta;
    stream->rate_rtp_hist_sh[stream->rate_update_hist_ptr] = stream->bytes_rtp * 8.0f / t_delta;

    stream->rate_update_hist_ptr = (stream->rate_update_hist_ptr+1) % RATE_UPDATE_SIZE;

    stream->rate_transmitted = 0.0f;
    stream->rate_acked = 0.0f;
    stream->rate_rtp = 0.0f;
    for (n = 0; n < RATE_UPDATE_SIZE; n++) {
        stream->rate_transmitted += stream->rate_transmitted_hist[n];
        stream->rate_acked += stream->rate_acked_hist[n];
        stream->rate_rtp += stream->rate_rtp_hist_sh[n];
    }
    stream->rate_transmitted /= RATE_UPDATE_SIZE;
    stream->rate_acked /= RATE_UPDATE_SIZE;
    stream->rate_rtp /= RATE_UPDATE_SIZE;

    /*
     * Generate a media RTP bitrate value, this serves to set a reasonably safe
     * upper bound to the target bitrate. This limit is useful when the then media
     * rate changes due to varaitions in input stimuli to the media coder.
     */
    stream->rate_rtp_sum += stream->rate_rtp;
    stream->rate_rtp_sum_n++;
    if (stream->rate_rtp_sum_n==10000000/RATE_UPDATE_INTERVAL) {
        /*
         * An average video bitrate is stored every ~1.0s
         */
        stream->rate_rtp_hist[stream->rate_rtp_hist_ptr] = stream->rate_rtp_sum/(10000000/RATE_UPDATE_INTERVAL);
        stream->rate_rtp_hist_ptr = (stream->rate_rtp_hist_ptr + 1) % RATE_RTP_HIST_SIZE;
        stream->rate_rtp_sum = 0;
        stream->rate_rtp_sum_n = 0;

        if (stream->rate_rtp_hist[RATE_RTP_HIST_SIZE-1] > 0.0f) {
            /*
             * Compute median rate
             */
            gboolean is_picked[RATE_RTP_HIST_SIZE];
            gfloat sorted[RATE_RTP_HIST_SIZE];
            gint i;
            /*
             * Create a sorted list
             */
            for (i=0; i < RATE_RTP_HIST_SIZE; i++)
                is_picked[i] = FALSE;
            for (i=0; i < RATE_RTP_HIST_SIZE; i++) {
                gfloat min_r = 1.0e8;
                gint min_i = 0;
                gint j;
                for (j=0; j < RATE_RTP_HIST_SIZE; j++) {
                    if (stream->rate_rtp_hist[j] < min_r && !is_picked[j]) {
                        min_r = stream->rate_rtp_hist[j];
                        min_i = j;
                    }
                }
                sorted[i] = min_r;
                is_picked[min_i] = TRUE;
            }
            /*
             * Get median value
             */
            stream->rate_rtp_median = sorted[RATE_RTP_HIST_SIZE/2];
        } else {
            stream->rate_rtp_median = 10e6;
        }
    }
    stream->bytes_acked = 0;
    stream->bytes_rtp = 0;
    stream->bytes_transmitted = 0;
}

static void update_target_stream_bitrate(GstScreamController *self, ScreamStream *stream,
    guint64 time_us)
{
    gfloat br = 0, scl_i, priority_sum, priority_scale, increment, scl, tmp, ramp_up_speed;
    guint tx_size_bits = 0;
    GList *it, *list;

    if (stream->t_start_us == 0) {
        stream->t_start_us = time_us;
    }
    /*
     * Compute a maximum bitrate
     */
    br = MAX(stream->rate_transmitted, stream->rate_acked);
    /*
     * Loss event handling
     * Rate is reduced slightly to avoid that more frames than necessary
     * queue up in the sender queue
     */
    if (stream->loss_event_flag) {
        stream->loss_event_flag = FALSE;
        if (time_us - stream->last_target_bitrate_i_adjust_us > 5000000) {
            /*
             * Avoid that target_bitrate_i is set too low in cases where a
             * congestion event is prolonged
             */
            stream->target_bitrate_i = stream->rate_acked;
            stream->last_target_bitrate_i_adjust_us = time_us;
        }
        stream->target_bitrate = MAX(stream->min_bitrate, stream->target_bitrate * LOSS_EVENT_RATE_SCALE);
        stream->last_bitrate_adjust_t_us  = time_us;
    } else {
        if (time_us - stream->last_bitrate_adjust_t_us < RATE_ADJUST_INTERVAL) {
            return;
        }

        /*
         * A scale factor that is dependent on the inflection point
         * i.e the last known highest video bitrate
         */
        scl_i = (stream->target_bitrate - stream->target_bitrate_i) / stream->target_bitrate_i;
        scl_i *= 4;
        scl_i = MAX(0.1f, MIN(1.0f, scl_i*scl_i));

        /*
         * Need a priority scale as well
         * Otherwise it will be impossible to
         *  achieve a bitrate differentiation
         */
        priority_sum = 0.0;
        list = it = g_hash_table_get_values(self->streams);
        while (it) {
            priority_sum += ((ScreamStream *)it->data)->priority;
            it = g_list_next(it);
        }
        g_list_free(list);
        priority_scale = sqrt(priority_sum / (stream->priority)) / priority_sum;
        /*
         * TODO This needs to be done differently
         * Distribute the total max throughput among the
         *  streams according to their priorities
         * The C++ implementation of SCReAM (https://github.com/EricssonResearch/scream)
         *  has a different implementation.
         */

        /*
         * Size of RTP queue [bits]
         * As this function is called immediately after a
         *  video frame is produced, we need to accept the new
         * RTP packets in the queue
         * The code below assumes that we know the framePeriod, an alternative is to
         * compute the size of the RTP packets with the highest timestamp
         */
        int last_bytes = (int)((stream->target_bitrate/8.0)*FRAME_PERIOD);
        tx_size_bits = MAX(0, ((gint)stream->bytes_in_queue - last_bytes) * 8);
        stream->tx_size_bits_avg = (tx_size_bits+stream->tx_size_bits_avg)/2;

        /*
         * Rate control agressivenes is increased if competing flows are detected
         */
        tmp = 1.0f;
        if (is_competing_flows(self))
            tmp = 0.5f;

        /*
         * Limit ramp_up_speed when bitrate is low, this should make it
         * possible to use SCReAM for low bitrate audio, with good results
         */
        ramp_up_speed = MIN(RAMP_UP_SPEED, stream->target_bitrate);
        if (stream->tx_size_bits_avg / MAX(br,stream->target_bitrate) > MAX_RTP_QUEUE_TIME &&
            time_us - stream->t_last_rtp_q_clear_us > 5 * MAX_RTP_QUEUE_TIME * 1000000) {
            GST_DEBUG("Target bitrate :  RTP queue delay ~ %f. Clear RTP queue \n",
                    stream->tx_size_bits_avg / MAX(br,stream->target_bitrate));
            stream->next_packet_size = 0;
            stream->bytes_in_queue = 0;
            stream->tx_size_bits_avg = 0;
            stream->clear_queue(stream->id, stream->user_data);
            stream->t_last_rtp_q_clear_us = time_us;
        } else if (self->in_fast_start && (tx_size_bits / stream->target_bitrate < 0.1)) {
            increment = 0.0f;
            /*
             * Compute rate increment
            */
            increment = ramp_up_speed * (RATE_ADJUST_INTERVAL/1000000.0) *
                (1.0f - MIN(1.0f, self->owd_trend /0.2f * tmp));
            /*
             * Limit increase rate near the last known highest bitrate
             */
            increment *= scl_i;

            /*
             * Add increment
             */
            stream->target_bitrate += increment;

            /*
             * Put an extra cap in case the OWD starts to increase
             */
            stream->target_bitrate *= 1.0f - OWD_GUARD * self->owd_trend * priority_scale * tmp;
            stream->was_fast_start = TRUE;
        } else {
            increment = 0.0f;
            if (stream->was_fast_start) {
                stream->was_fast_start = FALSE;
                if (time_us - stream->last_target_bitrate_i_adjust_us > 5000000) {
                    /*
                     * Avoid that target_bitrate_i is set too low in cases where a '
                     * congestion event is prolonged
                     */
                    stream->target_bitrate_i = stream->rate_acked;
                    stream->last_target_bitrate_i_adjust_us = time_us;
                }
            }
            /*
             * scl is an an adaptive scaling to prevent overshoot
             */
            scl = MIN(1.0f, MAX(0.0f, self->owd_fraction_avg - 0.3f) / 0.7f);
            scl += self->owd_trend;

            /*
             * Update target rate
             */

            increment = br*(1.0f - OWD_GUARD * scl * priority_scale * tmp)-
                TX_QUEUE_SIZE_FACTOR * stream->tx_size_bits_avg * priority_scale * tmp -
                stream->target_bitrate;


            if (increment < 0) {
                if (stream->was_fast_start) {
                    stream->was_fast_start = FALSE;
                    if (time_us - stream->last_target_bitrate_i_adjust_us > 5000000) {
                        /*
                         * Avoid that target_bitrate_i is set too low in cases where a '
                         * congestion event is prolonged
                         */
                        stream->target_bitrate_i = stream->rate_acked;
                        stream->last_target_bitrate_i_adjust_us = time_us;
                    }
                }
                /*
                 * Minimize the risk that reverse path congestion
                 * reduces target bit rate
                 */
                increment *= MIN(1.0f,self->owd_fraction_avg);
            } else {
                stream->was_fast_start = TRUE;
                if (!is_competing_flows(self)) {
                    /*
                     * Limit the bitrate increase so that it takes atleast kRampUpTime to reach
                     * from lowest to highest bitrate.
                     * This limitation is not in effect if competing flows are detected
                     */
                    increment *= scl_i;
                    increment = MIN(increment,(gfloat)(ramp_up_speed*(RATE_ADJUST_INTERVAL/1000000.0)));
                }
            }
            stream->target_bitrate += increment;
        }
        stream->last_bitrate_adjust_t_us  = time_us;
    }

    /*
     * Limit target bitrate so that it is not considerably higher than the actual bitrate,
     *  this improves stability in thorughput limited cases where video input changes a lot.
     * A median filtered value of the recent media bitrate is used for the limitation. This
     *  allows for a good performance in the cases that where the input stimuli to the media coder
     *  changes between static to varying.
     * This feature is disabled when competing (TCP) flows share the same bottleneck as it would
     *  otherwise degrade SCReAMs ability to grab a fair share of the bottleneck bandwidth
     */
    if (!is_competing_flows(self)) {
        gfloat rate_rtp_limit;
        rate_rtp_limit = MAX(br,MAX(stream->rate_rtp,stream->rate_rtp_median));
        rate_rtp_limit *= (3.0-2.0*self->owd_trend_mem);
        stream->target_bitrate = MIN(rate_rtp_limit,stream->target_bitrate);
    }
    /*
     * Limit target bitrate to min and max value
     */
    stream->target_bitrate = MIN(stream->max_bitrate, MAX(stream->min_bitrate,
        stream->target_bitrate));

{
    guint in_fl = bytes_in_flight(self);
    if (self->n_acc_bytes_in_flight_max > 0) {
        in_fl = self->acc_bytes_in_flight_max/self->n_acc_bytes_in_flight_max;
    }

    GST_INFO("Target br adj : "
            "T=%7.3fs target(actual)=%4.0f(%4.0f,%4.0f,%4.0f)k rtpQ=%4.0fms cwnd=%5u(%5u) srtt=%3.0fms "
            "owd(T)=%3.0f(%3.0f)ms fs=%u dt=%3.0f\n",
            (time_us-stream->t_start_us)/1e6f,
            stream->target_bitrate/1000.0f,
            stream->rate_rtp/1000.0f,
            stream->rate_transmitted/1000.0f,
            stream->rate_acked/1000.0f,
            (tx_size_bits/MAX(1e5f,br)*1000.0f),
            self->cwnd, in_fl,
            self->srtt_sh_us/1000.0f, self->owd*1000.0f, self->owd_target*1000.0f,
            self->in_fast_start, self->delta_t/1000.0f);
    }
    if (stream->on_bitrate_callback)
        stream->on_bitrate_callback((guint)stream->target_bitrate, stream->id, stream->user_data);
}

void gst_scream_controller_incoming_feedback(GstScreamController *self, guint stream_id,
    guint64 time_us, guint timestamp, guint highest_seq, guint n_loss, guint n_ecn, gboolean q_bit)
{
    GList *it, *list;
    TransmittedRtpPacket *packet;
    guint64 rtt_us;
    ScreamStream *stream;
    guint32 highest_seq_ext, seq_ext;
    gint n;

    SCREAM_UNUSED(n_ecn);
    SCREAM_UNUSED(q_bit);

    stream = g_hash_table_lookup(self->streams, GUINT_TO_POINTER(stream_id));
    if (!stream) {
        GST_WARNING("Received feedback for an unknown stream.");
        goto end;
    }

    self->acc_bytes_in_flight_max += bytes_in_flight(self);
    self->n_acc_bytes_in_flight_max++;
    /*
     * Remove all acked packets
     */
    for (n=0; n < MAX_TX_PACKETS; n++) {
        packet = &self->transmitted_packets[n];

        if (packet->is_used==TRUE) {
            if (stream_id == packet->stream_id) {
                if (packet->seq == highest_seq) {
                    self->acked_owd = timestamp - (guint)(packet->transmit_time_us / 1000);
                    rtt_us = time_us - packet->transmit_time_us;
                    self->srtt_sh_us = (7 * self->srtt_sh_us + rtt_us) / 8;
                    if (time_us - self->last_srtt_update_t_us > self->srtt_sh_us) {
                        self->srtt_us = (7 * self->srtt_us + self->srtt_sh_us) / 8;
                        self->last_srtt_update_t_us = time_us;
                    }
                }

                /*
                 * Wrap-around safety net
                 */
                seq_ext = packet->seq;
                highest_seq_ext = highest_seq;
                if (seq_ext < highest_seq_ext && highest_seq_ext-seq_ext > 20000)
                    seq_ext += 65536;
                else if (seq_ext > highest_seq_ext && seq_ext - highest_seq_ext > 20000)
                    highest_seq_ext += 65536;

                /*
                 * RTP packets with a sequence number lower
                 * than or equal to the highest received sequence number
                 * are treated as received even though they are not
                 * This advances the send window, similar to what
                 * SACK does in TCP
                 */
                if (seq_ext <= highest_seq_ext) {
                    self->bytes_newly_acked += packet->size;
                    stream->bytes_acked += packet->size;
                    packet->is_used = FALSE;
                }
            }
        }
    }
    self->delta_t =time_us- self->lastfb;
    self->lastfb = time_us;

    /*
     * Determine if a loss event has occurred
     */
    if (stream->n_loss < n_loss) {
        /*
         * The loss counter has increased
         */
        GST_DEBUG("Scream detected %u losses. highest seq is %u\n",
            n_loss-stream->n_loss,highest_seq);
        stream->n_loss = n_loss;
        if (time_us - self->last_loss_event_t_us > self->srtt_us) {
            /*
             * The loss counter has increased and it is more than one RTT since last
             * time loss was detected
             */
            self->loss_event = TRUE;
            self->last_loss_event_t_us = time_us;

            list = it = g_hash_table_get_values(self->streams);
            while (it) {
                ((ScreamStream *)it->data)->loss_event_flag = TRUE;
                it = g_list_next(it);
            }
            g_list_free(list);
        }
    }
    update_cwnd(self, time_us);
end:
    return;

}

static void update_cwnd(GstScreamController *self, guint64 time_us)
{
    gfloat off_target;
    guint tmp;
    guint max_bytes_in_flight_lo, max_bytes_in_flight_hi;
    guint max_bytes_in_flight;
    gfloat th;
    gint n;
    gboolean can_increase;

    tmp = estimate_owd(self, time_us) - get_base_owd(self);

    /*
     * Convert from [jiffy] OWD to an OWD in [s]
     */
    self->owd = tmp/TIMESTAMP_RATE;

    if (self->owd > self->srtt_sh_us/1e6 && time_us - self->base_owd_reset_t_us > 10000000 /* 10s */) {
        /*
         * The base OWD is likely wrong, for instance due to
         * a channel change, reset base OWD history
         */
        for (n=0; n < BASE_OWD_HIST_SIZE; n++)
            self->base_owd_hist[n] = G_MAXUINT32;
        self->base_owd = G_MAXUINT32;
        self->base_owd_reset_t_us = time_us;
    }

    /*
     * An averaged version of the OWD fraction
     * neceassary in order to make video rate control robust
     * against jitter
     */
    self->owd_fraction_avg = 0.9f*self->owd_fraction_avg + 0.1f * get_owd_fraction(self);

    /*
     * Save to OWD fraction history
     * used in GetOwdTrend()
     */
    if ((time_us - self->last_add_to_owd_fraction_hist_t_us) > OWD_FRACTION_HIST_INTERVAL) {
        self->owd_fraction_hist[self->owd_fraction_hist_ptr] = get_owd_fraction(self);
        self->owd_fraction_hist_ptr = (self->owd_fraction_hist_ptr+1) % OWD_FRACTION_HIST_SIZE;

        compute_owd_trend(self);
        self->owd_trend_mem = MAX(self->owd_trend_mem*0.99, self->owd_trend);

        if (ENABLE_SBD) {
            gfloat owd_norm = self->owd/OWD_TARGET_MIN;
            self->owd_norm_hist[self->owd_norm_hist_ptr] = owd_norm;
            self->owd_norm_hist_ptr = (self->owd_norm_hist_ptr + 1) % OWD_NORM_HIST_SIZE;

            /*
             * Compute shared bottleneck detection and update OWD target
             * if OWD variance and skew is sufficienctly low
             * TODO, this can likely be called more sparsely to save CPU if needed
             */
            compute_sbd(self);

            if (self->owd_sbd_var < 0.2 && self->owd_sbd_skew < 0.05) {
                self->owd_target = MAX(OWD_TARGET_MIN,
                    MIN(OWD_TARGET_MAX, self->owd_sbd_mean_sh * OWD_TARGET_MIN * 1.1f));
            } else if (self->owd_sbd_mean_sh * OWD_TARGET_MIN < self->owd_target) {
                self->owd_target =  MAX(OWD_TARGET_MIN, self->owd_sbd_mean_sh * OWD_TARGET_MIN);
            }
        }
        self->last_add_to_owd_fraction_hist_t_us = time_us;
    }

    /*
     * off_target is a normalized deviation from the owdTarget
     */
    off_target = (self->owd_target - self->owd) / (gfloat)self->owd_target;

    if (self->loss_event) {
        /*
         * loss event detected, decrease congestion window
         */
        self->cwnd_i = self->cwnd;
        self->cwnd = MAX(self->cwnd_min, (guint) (LOSS_BETA * self->cwnd));
        self->loss_event = FALSE;
        self->last_congestion_detected_t_us = time_us;
        self->in_fast_start = FALSE;
    }
    else {
      /*
       * Only allow CWND to increase if pipe is sufficiently filled or
       * if congestion level is low
       */
      can_increase = FALSE;
      gfloat alpha = 1.25f+2.75f*(1.0f-self->owd_trend_mem);
      can_increase = self->cwnd <= alpha*bytes_in_flight(self);
            /*
             * Compute a scaling dependent on the relation between CWND and the inflection point
             * i.e the last known max value. This helps to reduce the CWND growth close to
             * the last known congestion point
             */
            gfloat scl_i = (self->cwnd - self->cwnd_i) / ((gfloat)(self->cwnd_i));
            scl_i *= 4.0f;
            scl_i = MAX(0.1f, MIN(1.0f, scl_i * scl_i));


            if (self->in_fast_start) {
                /*
                 * In fast start, variable exit condition depending of
                 * if it is the first fast start or a later
                 */

                th = 0.2f;
                if (is_competing_flows(self))
                    th = 0.5f;
                else if (self->n_fast_start > 1)
                    th = 0.1f;
                if (self->owd_trend < th) {
                    if (can_increase)
                        self->cwnd += MIN(10 * self->mss, (guint)(self->bytes_newly_acked * scl_i));
                } else {
                    self->in_fast_start = FALSE;
                    self->last_congestion_detected_t_us = time_us;
                    self->cwnd_i = self->cwnd;
                    self->was_cwnd_increase = TRUE;
                }
            } else {
                if (off_target > 0.0f) {
                    gfloat gain;
                    /*
                     * OWD below target
                     */
                    self->was_cwnd_increase = TRUE;
                    /*
                     * Limit growth if OWD shows an increasing trend
                     */
                    gain = CWND_GAIN_UP*(1.0f + MAX(0.0f, 1.0f - self->owd_trend / 0.2f));
                    gain *= scl_i;

                    if (can_increase)
                        self->cwnd += (gint)(gain * off_target * self->bytes_newly_acked * self->mss / self->cwnd + 0.5f);
                } else {
                    /*
                     * OWD above target
                     */
                    if (self->was_cwnd_increase) {
                        self->was_cwnd_increase = FALSE;
                        self->cwnd_i = self->cwnd;
                    }
                    self->cwnd += (gint)(CWND_GAIN_DOWN * off_target * self->bytes_newly_acked * self->mss / self->cwnd);
                    self->last_congestion_detected_t_us = time_us;
                }
            }
    }

    /*
     * Congestion window validation, checks that the congestion window is
     * not considerably higher than the actual number of bytes in flight
     */
    max_bytes_in_flight_lo = MAX(bytes_in_flight(self),
        get_max_bytes_in_flight(self, self->bytes_in_flight_lo_hist));
    max_bytes_in_flight_hi = MAX(self->bytes_in_flight_hi_max,
        get_max_bytes_in_flight(self, self->bytes_in_flight_hi_hist));
    max_bytes_in_flight = (guint)(max_bytes_in_flight_hi*(1.0-self->owd_trend_mem) + max_bytes_in_flight_lo*self->owd_trend_mem);
    if (max_bytes_in_flight > INIT_CWND) {
        self->cwnd = MIN(self->cwnd, max_bytes_in_flight);
    }

    /*
     * Force CWND higher if SRTT is very low (<10ms) and little congestion detected
     * This makes it easier to reach high bitrates under there circumstances
     *  esp if the Video coder bitrate changes a lot.
     */
    if (self->srtt_us < 10000 && self->owd_trend_mem < 0.1) {
        /*
         * A min value given by the estimated transmit bitrate and an assumed RTT of 10ms
         */
        guint cwnd_min = (guint) (self->rate_transmitted*MAX(0.001f,MIN(self->srtt_us*1e-6,0.01f))/8.0f);
        /*
         * A min value given by the max_bytes_in_flight
         */
        cwnd_min = MAX(cwnd_min,(guint)(max_bytes_in_flight*1.5));
        self->cwnd = MAX(self->cwnd,cwnd_min);
    }

    self->cwnd = MAX(self->cwnd_min, self->cwnd);

    if (OPEN_CWND) {
        /*
        *
        */
        self->cwnd = 400000000;
    }
    /*
     * Make possible to enter fast start if OWD has been low for a while
     */
    th = 0.2f;
    if (is_competing_flows(self))
        th = 0.5f;
    if (self->owd_trend > th) {
        self->last_congestion_detected_t_us = time_us;
    } else if (time_us - self->last_congestion_detected_t_us > 1000000 &&
        !self->in_fast_start && ENABLE_CONSECUTIVE_FAST_START ) {
        self->in_fast_start = TRUE;
        self->last_congestion_detected_t_us = time_us;
        self->n_fast_start++;
    }
    self->bytes_newly_acked = 0;
}


static guint get_max_bytes_in_flight(GstScreamController *self, guint vec[])
{
    guint ret = 0;
    guint n;
    /*
    * All elements in the buffer must be initialized before
    * return value > 0
    */
    if (vec[self->bytes_in_flight_hist_ptr] == 0)
        return 0;
    for (n=0; n < BYTES_IN_FLIGHT_HIST_SIZE; n++) {
        ret = MAX(ret, vec[n]);
    }
    return ret;
}


static float get_owd_fraction(GstScreamController *self)
{
    return self->owd / self->owd_target;
}

static void compute_owd_trend(GstScreamController *self) {
    gint ptr = self->owd_fraction_hist_ptr;
    gfloat avg = 0.0f, x1, x2, a0, a1;
    self->owd_trend = 0.0;
    gint n;

    for (n=0; n < OWD_FRACTION_HIST_SIZE; n++) {
        avg += self->owd_fraction_hist[ptr];
        ptr = (ptr+1) % OWD_FRACTION_HIST_SIZE;
    }
    avg /= OWD_FRACTION_HIST_SIZE;

    ptr = self->owd_fraction_hist_ptr;
    x2 = 0.0f;
    a0 = 0.0f;
    a1 = 0.0f;
    for (n=0; n < OWD_FRACTION_HIST_SIZE; n++) {
        x1 = self->owd_fraction_hist[ptr] - avg;
        a0 += x1 * x1;
        a1 += x1 * x2;
        x2 = x1;
        ptr = (ptr+1) % OWD_FRACTION_HIST_SIZE;
    }
    if (a0 > 0 ) {
        self->owd_trend = MAX(0.0f, MIN(1.0f, (a1 / a0)*self->owd_fraction_avg));
    }
}


/*
 * Compute indicators of shared bottleneck
 */
static void compute_sbd(GstScreamController *self) {
    gfloat owd_norm, tmp;
    self->owd_sbd_mean = 0.0;
    self->owd_sbd_mean_sh = 0.0;
    self->owd_sbd_var = 0.0;
    self->owd_sbd_skew = 0.0;
    gint n;
    gint ptr = self->owd_norm_hist_ptr;
    for (n=0; n < OWD_NORM_HIST_SIZE; n++) {
        owd_norm = self->owd_norm_hist[ptr];
        self->owd_sbd_mean += owd_norm;
        if (n >= OWD_NORM_HIST_SIZE - 20) {
            self->owd_sbd_mean_sh += owd_norm;
        }
        ptr = (ptr+1) % OWD_NORM_HIST_SIZE;
    }
    self->owd_sbd_mean /= OWD_NORM_HIST_SIZE;
    self->owd_sbd_mean_sh /= 20;

    ptr = self->owd_norm_hist_ptr;
    for (n=0; n < OWD_NORM_HIST_SIZE; n++) {
        owd_norm = self->owd_norm_hist[ptr];
        tmp = owd_norm - self->owd_sbd_mean;
        self->owd_sbd_var += tmp * tmp;
        self->owd_sbd_skew += tmp * tmp * tmp;
        ptr = (ptr+1) % OWD_NORM_HIST_SIZE;
    }
    self->owd_sbd_var /= OWD_NORM_HIST_SIZE;
    self->owd_sbd_skew /= OWD_NORM_HIST_SIZE;
}


static guint estimate_owd(GstScreamController *self, guint64 time_us)
{
    self->base_owd = MIN(self->base_owd, self->acked_owd);
    if (time_us - self->last_base_owd_add_t_us >= 1000000) {
        self->base_owd_hist[self->base_owd_hist_ptr] = self->base_owd;
        self->base_owd_hist_ptr = (self->base_owd_hist_ptr + 1) % BASE_OWD_HIST_SIZE;
        self->last_base_owd_add_t_us = time_us;
        self->base_owd = G_MAXUINT32;
    }
    return self->acked_owd;
}

static guint get_base_owd(GstScreamController *self) {
    guint32 ret = self->base_owd;
    gint n;
    for (n=0; n < BASE_OWD_HIST_SIZE; n++)
        ret = MIN(ret, self->base_owd_hist[n]);
    return ret;
}

static gboolean is_competing_flows(GstScreamController *self) {
    return self->owd_target > OWD_TARGET_MIN;
}


static guint get_next_packet_size(ScreamStream *stream)
{
    stream->next_packet_size = stream->next_packet_size != 0 ?
        stream->next_packet_size : stream->get_next_packet_size_callback(stream->id, stream->user_data);
    return stream->next_packet_size;
}
