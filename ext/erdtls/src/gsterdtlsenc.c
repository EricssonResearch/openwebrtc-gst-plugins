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

#include "gsterdtlsenc.h"

#include "gsterdtlsdec.h"

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS_ANY
    );

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS("application/x-dtls")
    );

GST_DEBUG_CATEGORY_STATIC(er_dtls_enc_debug);
#define GST_CAT_DEFAULT er_dtls_enc_debug

#define gst_er_dtls_enc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstErDtlsEnc, gst_er_dtls_enc, GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT(er_dtls_enc_debug, "erdtlsenc", 0, "Ericsson DTLS Encoder"));

enum {
    SIGNAL_ON_KEY_RECEIVED,
    NUM_SIGNALS
};

static guint signals[NUM_SIGNALS];

enum {
    PROP_0,
    PROP_CONNECTION_ID,
    PROP_IS_CLIENT,

    PROP_ENCODER_KEY,
    PROP_SRTP_CIPHER,
    PROP_SRTP_AUTH,
    NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES];

#define DEFAULT_CONNECTION_ID NULL
#define DEFAULT_IS_CLIENT FALSE

#define DEFAULT_ENCODER_KEY NULL
#define DEFAULT_SRTP_CIPHER 0
#define DEFAULT_SRTP_AUTH 0

#define INITIAL_QUEUE_SIZE 64

static void gst_er_dtls_enc_finalize(GObject *);
static void gst_er_dtls_enc_set_property(GObject *, guint prop_id, const GValue *, GParamSpec *);
static void gst_er_dtls_enc_get_property(GObject *, guint prop_id, GValue *, GParamSpec *);

static GstStateChangeReturn gst_er_dtls_enc_change_state(GstElement *, GstStateChange);
static GstPad *gst_er_dtls_enc_request_new_pad(GstElement *, GstPadTemplate *, const gchar *name, const GstCaps *);

static gboolean src_activate_mode(GstPad *, GstErDtlsEnc *, GstPadMode, gboolean active);
static void src_task_loop(GstPad *);

static GstFlowReturn sink_chain(GstPad *, GstErDtlsEnc *, GstBuffer *);

static void on_key_received(ErDtlsConnection *, gpointer key, guint cipher, guint auth, GstErDtlsEnc *);
static void on_send_data(ErDtlsConnection *, gconstpointer data, gint length, GstErDtlsEnc *);

static void gst_er_dtls_enc_class_init(GstErDtlsEncClass *klass)
{
    GObjectClass *gobject_class;
    GstElementClass *element_class;

    gobject_class = (GObjectClass *) klass;
    element_class = (GstElementClass *) klass;

    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_er_dtls_enc_finalize);
    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_er_dtls_enc_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_er_dtls_enc_get_property);

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_er_dtls_enc_change_state);
    element_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_er_dtls_enc_request_new_pad);

    signals[SIGNAL_ON_KEY_RECEIVED] =
        g_signal_new("on-key-received", G_TYPE_FROM_CLASS(klass),
            G_SIGNAL_RUN_LAST, 0, NULL, NULL,
            g_cclosure_marshal_generic, G_TYPE_NONE, 0);

    properties[PROP_CONNECTION_ID] =
        g_param_spec_string("connection-id",
            "Connection id",
            "Every encoder/decoder pair should have the same, unique, connection-id",
            DEFAULT_CONNECTION_ID,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_IS_CLIENT] =
        g_param_spec_boolean("is-client",
            "Is client",
            "Set to true if the decoder should act as"
            "client and initiate the handshake",
            DEFAULT_IS_CLIENT,
            GST_PARAM_MUTABLE_READY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ENCODER_KEY] =
        g_param_spec_boxed("encoder-key",
            "Encoder key",
            "Master key that should be used by the SRTP encoder",
            GST_TYPE_BUFFER,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SRTP_CIPHER] =
        g_param_spec_uint("srtp-cipher",
            "SRTP cipher",
            "The SRTP cipher selected in the DTLS handshake. "
            "The value will be set to an ErDtlsSrtpCipher.",
            0, ER_DTLS_SRTP_CIPHER_AES_128_ICM, DEFAULT_SRTP_CIPHER,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SRTP_AUTH] =
        g_param_spec_uint("srtp-auth",
            "SRTP authentication",
            "The SRTP authentication selected in the DTLS handshake. "
            "The value will be set to an ErDtlsSrtpAuth.",
            0, ER_DTLS_SRTP_AUTH_HMAC_SHA1_80, DEFAULT_SRTP_AUTH,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, NUM_PROPERTIES, properties);

    gst_element_class_add_pad_template(element_class,
        gst_static_pad_template_get(&src_template));
    gst_element_class_add_pad_template(element_class,
        gst_static_pad_template_get(&sink_template));

    gst_element_class_set_static_metadata(element_class,
        "DTLS Encoder",
        "Encoder/Network/DTLS",
        "Encodes packets with DTLS",
        "Patrik Oldsberg patrik.oldsberg@ericsson.com");
}

static void gst_er_dtls_enc_init(GstErDtlsEnc *self)
{
    self->connection_id = NULL;
    self->connection = NULL;

    self->is_client = DEFAULT_IS_CLIENT;

    self->encoder_key = NULL;
    self->srtp_cipher = DEFAULT_SRTP_CIPHER;
    self->srtp_auth = DEFAULT_SRTP_AUTH;

    self->queue = g_ptr_array_sized_new(INITIAL_QUEUE_SIZE);

    g_mutex_init(&self->queue_lock);
    g_cond_init(&self->queue_cond_add);

    self->src = gst_pad_new_from_static_template(&src_template, "src");
    g_return_if_fail(self->src);

    gst_pad_set_activatemode_function(self->src, GST_DEBUG_FUNCPTR((GstPadActivateModeFunction) src_activate_mode));

    gst_element_add_pad(GST_ELEMENT(self), self->src);
}

static void gst_er_dtls_enc_finalize(GObject *object)
{
    GstErDtlsEnc *self = GST_ER_DTLS_ENC(object);

    if (self->encoder_key) {
        gst_buffer_unref(self->encoder_key);
        self->encoder_key = NULL;
    }

    g_mutex_lock(&self->queue_lock);

    g_ptr_array_set_free_func(self->queue, (GDestroyNotify) gst_buffer_unref);
    g_ptr_array_unref(self->queue);
    self->queue = NULL;

    g_mutex_unlock(&self->queue_lock);

    g_mutex_clear(&self->queue_lock);
    g_cond_clear(&self->queue_cond_add);

    GST_LOG_OBJECT(self, "finalized");

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_er_dtls_enc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GstErDtlsEnc *self = GST_ER_DTLS_ENC(object);

    switch (prop_id) {
    case PROP_CONNECTION_ID:
        self->connection_id = g_value_dup_string(value);
        break;
    case PROP_IS_CLIENT:
        self->is_client = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(self, prop_id, pspec);
    }
}

static void gst_er_dtls_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    GstErDtlsEnc *self = GST_ER_DTLS_ENC(object);

    switch (prop_id) {
    case PROP_CONNECTION_ID:
        g_value_set_string(value, self->connection_id);
        break;
    case PROP_IS_CLIENT:
        g_value_set_boolean(value, self->is_client);
        break;
    case PROP_ENCODER_KEY:
        g_value_set_boxed(value, self->encoder_key);
        break;
    case PROP_SRTP_CIPHER:
        g_value_set_uint(value, self->srtp_cipher);
        break;
    case PROP_SRTP_AUTH:
        g_value_set_uint(value, self->srtp_auth);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(self, prop_id, pspec);
    }
}

static GstStateChangeReturn gst_er_dtls_enc_change_state(GstElement *element, GstStateChange transition)
{
    GstErDtlsEnc *self = GST_ER_DTLS_ENC(element);
    GstStateChangeReturn ret;

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        if (self->connection_id) {
            self->connection = gst_er_dtls_dec_fetch_connection(self->connection_id);

            if (!self->connection) {
                GST_WARNING_OBJECT(self,
                    "invalid connection id: '%s', connection not found or already in use",
                    self->connection_id);
                return GST_STATE_CHANGE_FAILURE;
            }

            g_signal_connect_object(self->connection,
                "on-encoder-key", G_CALLBACK(on_key_received), self, 0);

            er_dtls_connection_set_send_callback(self->connection,
                g_cclosure_new(G_CALLBACK(on_send_data), self, NULL));
        } else {
            GST_WARNING_OBJECT(self, "trying to change state to ready without connection id");
            return GST_STATE_CHANGE_FAILURE;
        }
        break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        GST_DEBUG_OBJECT(self, "starting connection %s", self->connection_id);
        er_dtls_connection_start(self->connection, self->is_client);

        gst_pad_set_active(self->src, TRUE);
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        GST_DEBUG_OBJECT(self, "stopping connection %s", self->connection_id);
        gst_pad_set_active(self->src, FALSE);

        er_dtls_connection_stop(self->connection);
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
        GST_DEBUG_OBJECT(self, "closing connection %s", self->connection_id);

        if (self->connection) {
            er_dtls_connection_close(self->connection);
            er_dtls_connection_set_send_callback(self->connection, NULL);
            g_object_unref(self->connection);
            self->connection = NULL;
        }
        break;
    default:
        break;
    }

    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

    return ret;
}

static GstPad *gst_er_dtls_enc_request_new_pad(GstElement *element,
    GstPadTemplate *templ, const gchar *name, const GstCaps *caps)
{
    GstPad *sink;
    gboolean ret;

    GST_DEBUG_OBJECT(element, "sink pad requested");

    g_return_val_if_fail(templ->direction == GST_PAD_SINK, NULL);

    sink = gst_pad_new_from_template(templ, name);
    g_return_val_if_fail(sink, NULL);

    if (caps) {
        g_object_set(sink, "caps", caps, NULL);
    }

    gst_pad_set_chain_function(sink, GST_DEBUG_FUNCPTR((GstPadChainFunction) sink_chain));

    ret = gst_pad_set_active(sink, TRUE);
    g_warn_if_fail(ret);

    gst_element_add_pad(element, sink);

    return sink;
}

static gboolean src_activate_mode(GstPad *pad, GstErDtlsEnc *self, GstPadMode mode, gboolean active)
{
    gboolean success = TRUE;
    g_return_val_if_fail(mode == GST_PAD_MODE_PUSH, FALSE);

    if (active) {
        GST_DEBUG_OBJECT(self, "src pad activating in push mode");

        self->send_initial_events = TRUE;
        success = gst_pad_start_task(pad, (GstTaskFunction) src_task_loop, self->src, NULL);
        if (!success) {
            GST_WARNING_OBJECT(self, "failed to activate pad task");
        }
    } else {
        GST_DEBUG_OBJECT(self, "deactivating src pad");

        g_cond_signal(&self->queue_cond_add);

        success = gst_pad_stop_task(pad);
        if (!success) {
            GST_WARNING_OBJECT(self, "failed to deactivate pad task");
        }
    }

    return success;
}

static void src_task_loop(GstPad *pad)
{
    GstErDtlsEnc *self = GST_ER_DTLS_ENC(GST_PAD_PARENT(pad));
    GstFlowReturn ret;
    GstPad *peer;
    gboolean peer_is_active;

    if (!gst_pad_is_active(pad)) {
        GST_LOG_OBJECT(self, "src task loop entered on inactive pad");
        return;
    }

    GST_TRACE_OBJECT(self, "src loop: acquiring lock");
    g_mutex_lock(&self->queue_lock);
    GST_TRACE_OBJECT(self, "src loop: acquired lock");

    while (!self->queue->len) {
        GST_TRACE_OBJECT(self, "src loop: queue empty, waiting for add");
        g_cond_wait(&self->queue_cond_add, &self->queue_lock);
        GST_TRACE_OBJECT(self, "src loop: add signaled");

        if (!gst_pad_is_active(pad)) {
            GST_LOG_OBJECT(self, "pad inactive, task returning");
            GST_TRACE_OBJECT(self, "src loop: releasing lock");
            g_mutex_unlock(&self->queue_lock);
            return;
        }
    }
    GST_TRACE_OBJECT(self, "src loop: queue has element");

    peer = gst_pad_get_peer(pad);
    peer_is_active = gst_pad_is_active(peer);
    gst_object_unref(peer);

    if (peer_is_active) {
        GstBuffer *buffer;
        gboolean start_connection_timeout = FALSE;

        if (self->send_initial_events) {
          GstSegment segment;
          gchar s_id[32];
          GstCaps *caps;

          g_snprintf (s_id, sizeof (s_id), "erdtlsenc-%08x", g_random_int ());
          gst_pad_push_event (self->src, gst_event_new_stream_start (s_id));
          caps = gst_caps_new_empty_simple ("application/x-dtls");
          gst_pad_push_event (self->src, gst_event_new_caps (caps));
          gst_caps_unref (caps);
          gst_segment_init (&segment, GST_FORMAT_BYTES);
          gst_pad_push_event (self->src, gst_event_new_segment (&segment));
          self->send_initial_events = FALSE;
          start_connection_timeout = TRUE;
        }

        buffer = g_ptr_array_remove_index(self->queue, 0);

        GST_TRACE_OBJECT(self, "src loop: releasing lock");
        g_mutex_unlock(&self->queue_lock);

        ret = gst_pad_push(self->src, buffer);
        if (start_connection_timeout)
          er_dtls_connection_start_timeout (self->connection);

        if (G_UNLIKELY(ret != GST_FLOW_OK)) {
            GST_WARNING_OBJECT(self, "failed to push buffer on src pad: %s", gst_flow_get_name(ret));
        }
    } else {
        g_warn_if_reached();
        GST_TRACE_OBJECT(self, "src loop: releasing lock");
        g_mutex_unlock(&self->queue_lock);
    }
}

static GstFlowReturn sink_chain(GstPad *pad, GstErDtlsEnc *self, GstBuffer *buffer)
{
    GstMapInfo map_info;
    gint ret;

    gst_buffer_map(buffer, &map_info, GST_MAP_READ);

    if (map_info.size) {
        ret = er_dtls_connection_send(self->connection, map_info.data, map_info.size);
        if (ret != map_info.size) {
            GST_WARNING_OBJECT(self, "error sending data: %d B were written, expected value was %zd B",
                ret, map_info.size);
        }
    }

    gst_buffer_unmap(buffer, &map_info);

    gst_buffer_unref(buffer);

    return GST_FLOW_OK;
}

static void on_key_received(ErDtlsConnection *connection, gpointer key, guint cipher, guint auth, GstErDtlsEnc *self)
{
    gpointer key_dup;
    gchar *key_str;

    g_return_if_fail(GST_IS_ER_DTLS_ENC(self));
    g_return_if_fail(ER_IS_DTLS_CONNECTION(connection));

    self->srtp_cipher = cipher;
    self->srtp_auth = auth;

    key_dup = g_memdup(key, ER_DTLS_SRTP_MASTER_KEY_LENGTH);
    self->encoder_key = gst_buffer_new_wrapped(key_dup, ER_DTLS_SRTP_MASTER_KEY_LENGTH);

    key_str = g_base64_encode(key, ER_DTLS_SRTP_MASTER_KEY_LENGTH);
    GST_INFO_OBJECT(self, "received key: %s", key_str);
    g_free(key_str);

    g_signal_emit(self, signals[SIGNAL_ON_KEY_RECEIVED], 0);
}

static void on_send_data(ErDtlsConnection *connection, gconstpointer data, gint length, GstErDtlsEnc *self)
{
    GstBuffer *buffer;

    GST_DEBUG_OBJECT(self, "sending data from %s with length %d", self->connection_id, length);

    buffer = gst_buffer_new_wrapped(g_memdup(data, length), length);

    GST_TRACE_OBJECT(self, "send data: acquiring lock");
    g_mutex_lock(&self->queue_lock);
    GST_TRACE_OBJECT(self, "send data: acquired lock");

    g_ptr_array_add(self->queue, buffer);

    GST_TRACE_OBJECT(self, "send data: signaling add");
    g_cond_signal(&self->queue_cond_add);

    GST_TRACE_OBJECT(self, "send data: releasing lock");
    g_mutex_unlock(&self->queue_lock);
}
