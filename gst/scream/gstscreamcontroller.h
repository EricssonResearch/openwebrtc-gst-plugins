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

#ifndef __GST_SCREAM_CONTROLLER_H__
#define __GST_SCREAM_CONTROLLER_H__

#include <glib-object.h>
#define INET
#define INET6


/* helper macro for unused variables */
#define SCREAM_UNUSED(x) (void)x

/*
 * Type macros.
 */
#define GST_SCREAM_TYPE_CONTROLLER                  (gst_scream_controller_get_type ())
#define GST_SCREAM_CONTROLLER(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_SCREAM_TYPE_CONTROLLER, GstScreamController))
#define GST_SCREAM_IS_CONTROLLER(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_SCREAM_TYPE_CONTROLLER))
#define GST_SCREAM_CONTROLLER_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GST_SCREAM_TYPE_CONTROLLER, GstScreamControllerClass))
#define GST_SCREAM_IS_CONTROLLER_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_SCREAM_TYPE_CONTROLLER))
#define GST_SCREAM_CONTROLLER_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_SCREAM_TYPE_CONTROLLER, GstScreamControllerClass))

typedef struct _GstScreamController        GstScreamController;
typedef struct _GstScreamControllerClass   GstScreamControllerClass;

typedef struct {
    guint stream_id;
    guint size;
    guint seq;
    guint64 transmit_time_us;
    gboolean is_used;
} TransmittedRtpPacket;

typedef void (*GstScreamQueueBitrateRequestedCb) (guint bitrate, guint stream_id, gpointer user_data);
typedef guint (*GstScreamQueueNextPacketSizeCb) (guint stream_id, gpointer user_data);
typedef void (*GstScreamQueueApproveTransmitCb) (guint stream_id, gpointer user_data);
typedef void (*GstScreamQueueClearQueueCb) (guint stream_id, gpointer user_data);

#define MAX_TX_PACKETS 1000
#define BASE_OWD_HIST_SIZE 50
#define OWD_FRACTION_HIST_SIZE 20
#define OWD_NORM_HIST_SIZE 100
#define BYTES_IN_FLIGHT_HIST_SIZE 10

struct _GstScreamController
{
    GObject parent_instance;

    GMutex lock;
    GHashTable *streams;

    gint maxTxPackets;
    gboolean approve_timer_running;
    /*guint transmitted_packets_size;*/
    TransmittedRtpPacket transmitted_packets[MAX_TX_PACKETS];

    guint64 srtt_sh_us;
    guint64 srtt_us;
    guint acked_owd; // OWD of last acked packet
    guint base_owd;
    guint32 base_owd_hist[BASE_OWD_HIST_SIZE];
    gint base_owd_hist_ptr;
    gfloat owd;
    gfloat owd_fraction_avg;
    gfloat owd_fraction_hist[OWD_FRACTION_HIST_SIZE];
    gint owd_fraction_hist_ptr;
    gfloat owd_trend;
    gfloat owd_target;
    gfloat owd_norm_hist[OWD_NORM_HIST_SIZE];
    gint owd_norm_hist_ptr;
    gfloat owd_sbd_skew;
    gfloat owd_sbd_var;
    gfloat owd_sbd_mean;
    gfloat owd_sbd_mean_sh;

    // CWND management
    guint bytes_newly_acked;
    guint mss; // Maximum Segment Size
    guint cwnd; // congestion window
    guint cwnd_min;
    guint cwnd_i;
    gboolean was_cwnd_increase;
    guint bytes_in_flight_lo_hist[BYTES_IN_FLIGHT_HIST_SIZE];
    guint bytes_in_flight_hi_hist[BYTES_IN_FLIGHT_HIST_SIZE];
    guint bytes_in_flight_hist_ptr;
    guint bytes_in_flight_hi_max;
    guint acc_bytes_in_flight_max;
    guint n_acc_bytes_in_flight_max;
    gfloat rate_transmitted;
    gfloat owd_trend_mem;
    guint64 delta_t;

    // Loss event
    gboolean loss_event;


    // Fast start
    gboolean in_fast_start;
    guint n_fast_start;

    // Transmission scheduling*/
    gfloat pacing_bitrate;

	gboolean is_initialized;
	// These need to be initialized when time_us is known
	guint64 last_srtt_update_t_us;
    guint64 last_base_owd_add_t_us;
    guint64 base_owd_reset_t_us;
    guint64 last_add_to_owd_fraction_hist_t_us;
    guint64 last_bytes_in_flight_t_us;
    guint64 last_loss_event_t_us;
    guint64 last_congestion_detected_t_us;
    guint64 last_transmit_t_us;
    guint64 next_transmit_t_us;
    guint64 last_rate_update_t_us;

    // TODO Debug variables , remove
    guint64 lastfb;
};

struct _GstScreamControllerClass {
    GObjectClass parent_class;

};

GType gst_scream_controller_get_type(void);

GstScreamController *gst_scream_controller_get(guint32 controller_id);

gboolean gst_scream_controller_register_new_stream(GstScreamController *controller,
    guint stream_id, gfloat priority, guint min_bitrate, guint max_bitrate,
    GstScreamQueueBitrateRequestedCb on_bitrate_callback,
    GstScreamQueueNextPacketSizeCb get_next_packet_size_callback,
    GstScreamQueueApproveTransmitCb approve_transmit_callback,
    GstScreamQueueClearQueueCb clear_queue,
    gpointer user_data);

guint64 gst_scream_controller_packet_transmitted(GstScreamController *self, guint stream_id,
    guint size, guint16 seq, guint64 transmit_time_us);

void gst_scream_controller_new_rtp_packet(GstScreamController *self, guint stream_id,
    guint rtp_timestamp, guint64 monotonic_time, guint bytes_in_queue, guint rtp_size);

guint64 gst_scream_controller_approve_transmits(GstScreamController *self, guint64 time_us);

void gst_scream_controller_incoming_feedback(GstScreamController *self, guint stream_id,
    guint64 time_us, guint timestamp, guint highest_seq, guint n_loss, guint n_ecn, gboolean q_bit);


#endif /* __GST_SCREAM_CONTROLLER_H__ */
