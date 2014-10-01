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

#include "gsterdtlsdec.h"

#include "erdtlscertificate.h"

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS("application/x-dtls")
    );

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS_ANY
    );

GST_DEBUG_CATEGORY_STATIC(er_dtls_dec_debug);
#define GST_CAT_DEFAULT er_dtls_dec_debug

#define gst_er_dtls_dec_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstErDtlsDec, gst_er_dtls_dec, GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT(er_dtls_dec_debug, "erdtlsdec", 0, "Ericsson DTLS Decoder"));

#define UNUSED(param) while (0) { (void)(param); }

enum {
    SIGNAL_ON_KEY_RECEIVED,
    NUM_SIGNALS
};

static guint signals[NUM_SIGNALS];

enum {
    PROP_0,
    PROP_CONNECTION_ID,
    PROP_PEM,
    PROP_PEER_PEM,

    PROP_DECODER_KEY,
    PROP_SRTP_CIPHER,
    PROP_SRTP_AUTH,
    NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES];

#define DEFAULT_CONNECTION_ID NULL
#define DEFAULT_PEM NULL
#define DEFAULT_PEER_PEM NULL

#define DEFAULT_DECODER_KEY NULL
#define DEFAULT_SRTP_CIPHER 0
#define DEFAULT_SRTP_AUTH 0


static void gst_er_dtls_dec_finalize(GObject *);
static void gst_er_dtls_dec_dispose(GObject *);
static void gst_er_dtls_dec_set_property(GObject *, guint prop_id, const GValue *, GParamSpec *);
static void gst_er_dtls_dec_get_property(GObject *, guint prop_id, GValue *, GParamSpec *);

static GstStateChangeReturn gst_er_dtls_dec_change_state(GstElement *, GstStateChange);
static GstPad *gst_er_dtls_dec_request_new_pad(GstElement *, GstPadTemplate *, const gchar *name, const GstCaps *);
static void gst_er_dtls_dec_release_pad(GstElement *, GstPad *);

static void on_key_received(ErDtlsConnection *, gpointer key, guint cipher, guint auth, GstErDtlsDec *);
static gboolean on_peer_certificate_received(ErDtlsConnection *, gchar *pem, GstErDtlsDec *);
static GstFlowReturn sink_chain(GstPad *, GstObject *parent, GstBuffer *);

static ErDtlsAgent *get_agent_by_pem(const gchar *pem);
static void agent_weak_ref_notify(gchar *pem, ErDtlsAgent *);
static void create_connection(GstErDtlsDec *, gchar *id);
static void connection_weak_ref_notify(gchar *id, ErDtlsConnection *);

static void gst_er_dtls_dec_class_init(GstErDtlsDecClass *klass)
{
    GObjectClass *gobject_class;
    GstElementClass *element_class;

    gobject_class = (GObjectClass *) klass;
    element_class = (GstElementClass *) klass;

    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_er_dtls_dec_finalize);
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_er_dtls_dec_dispose);
    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_er_dtls_dec_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_er_dtls_dec_get_property);

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_er_dtls_dec_change_state);
    element_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_er_dtls_dec_request_new_pad);
    element_class->release_pad = GST_DEBUG_FUNCPTR(gst_er_dtls_dec_release_pad);

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

    properties[PROP_PEM] =
        g_param_spec_string("pem",
            "PEM string",
            "A string containing a X509 certificate and RSA private key in PEM format",
            DEFAULT_PEM,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PEER_PEM] =
        g_param_spec_string("peer-pem",
            "Peer PEM string",
            "The X509 certificate received in the DTLS handshake, in PEM format",
            DEFAULT_PEER_PEM,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DECODER_KEY] =
        g_param_spec_boxed("decoder-key",
            "Decoder key",
            "SRTP key that should be used by the decider",
            GST_TYPE_CAPS,
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
        "DTLS Decoder",
        "Decoder/Network/DTLS",
        "Decodes DTLS packets",
        "Patrik Oldsberg patrik.oldsberg@ericsson.com");
}

static void gst_er_dtls_dec_init(GstErDtlsDec *self)
{
    GstPad *sink;
    self->agent = get_agent_by_pem(NULL);
    self->connection_id = NULL;
    self->connection = NULL;
    self->peer_pem = NULL;

    self->decoder_key = NULL;
    self->srtp_cipher = DEFAULT_SRTP_CIPHER;
    self->srtp_auth = DEFAULT_SRTP_AUTH;

    g_mutex_init(&self->src_mutex);

    self->src = NULL;
    sink = gst_pad_new_from_static_template(&sink_template, "sink");
    g_return_if_fail(sink);

    gst_pad_set_chain_function(sink, GST_DEBUG_FUNCPTR(sink_chain));

    gst_element_add_pad(GST_ELEMENT(self), sink);
}

static void gst_er_dtls_dec_finalize(GObject *object)
{
    GstErDtlsDec *self = GST_ER_DTLS_DEC(object);

    if (self->decoder_key) {
        gst_buffer_unref(self->decoder_key);
        self->decoder_key = NULL;
    }

    g_free(self->connection_id);
    self->connection_id = NULL;

    g_free(self->peer_pem);
    self->peer_pem = NULL;

    g_mutex_clear(&self->src_mutex);

    GST_LOG_OBJECT(self, "finalized");

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_er_dtls_dec_dispose(GObject *object)
{
    GstErDtlsDec *self = GST_ER_DTLS_DEC(object);

    if (self->agent) {
        g_object_unref(self->agent);
        self->agent = NULL;
    }

    if (self->connection) {
        g_object_unref(self->connection);
        self->connection = NULL;
    }
}

static void gst_er_dtls_dec_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GstErDtlsDec *self = GST_ER_DTLS_DEC(object);

    switch (prop_id) {
    case PROP_CONNECTION_ID:
        g_free(self->connection_id);
        self->connection_id = g_value_dup_string(value);
        g_return_if_fail(self->agent);
        create_connection(self, self->connection_id);
        break;
    case PROP_PEM:
        if (self->agent) {
            g_object_unref(self->agent);
        }
        self->agent = get_agent_by_pem(g_value_get_string(value));
        if (self->connection_id) {
            create_connection(self, self->connection_id);
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(self, prop_id, pspec);
    }
}

static void gst_er_dtls_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    GstErDtlsDec *self = GST_ER_DTLS_DEC(object);

    switch (prop_id) {
    case PROP_CONNECTION_ID:
        g_value_set_string(value, self->connection_id);
        break;
    case PROP_PEM:
        g_value_take_string(value, er_dtls_agent_get_certificate_pem(self->agent));
        break;
    case PROP_PEER_PEM:
        g_value_set_string(value, self->peer_pem);
        break;
    case PROP_DECODER_KEY:
        g_value_set_boxed(value, self->decoder_key);
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

static GstStateChangeReturn gst_er_dtls_dec_change_state(GstElement *element, GstStateChange transition)
{
    GstErDtlsDec *self = GST_ER_DTLS_DEC(element);
    GstStateChangeReturn ret;

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        if (self->connection) {
            g_signal_connect_object(self->connection,
                "on-decoder-key", G_CALLBACK(on_key_received), self, 0);
            g_signal_connect_object(self->connection,
                "on-peer-certificate", G_CALLBACK(on_peer_certificate_received), self, 0);
        } else {
            GST_WARNING_OBJECT(self, "trying to change state to ready without connection id and pem");
            return GST_STATE_CHANGE_FAILURE;
        }
        break;
    default:
        break;
    }

    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

    return ret;
}

static GstPad *gst_er_dtls_dec_request_new_pad(GstElement *element,
    GstPadTemplate *tmpl, const gchar *name, const GstCaps *caps)
{
    GstErDtlsDec *self = GST_ER_DTLS_DEC(element);

    GST_DEBUG_OBJECT(element, "requesting pad");

    g_return_val_if_fail(!self->src, NULL);
    g_return_val_if_fail(tmpl->direction == GST_PAD_SRC, NULL);

    g_mutex_lock(&self->src_mutex);

    self->src = gst_pad_new_from_template(tmpl, name);
    g_return_val_if_fail(self->src, NULL);

    if (caps) {
        g_object_set(self->src, "caps", caps, NULL);
    }

    gst_pad_set_active(self->src, TRUE);
    gst_element_add_pad(element, self->src);

    g_mutex_unlock(&self->src_mutex);

    return self->src;
}

static void gst_er_dtls_dec_release_pad(GstElement *element, GstPad *pad)
{
    GstErDtlsDec *self = GST_ER_DTLS_DEC(element);

    g_mutex_lock(&self->src_mutex);

    g_return_if_fail(self->src == pad);
    gst_element_remove_pad(element, self->src);
    self->src = NULL;

    GST_DEBUG_OBJECT(self, "releasing src pad");

    g_mutex_unlock(&self->src_mutex);

    GST_ELEMENT_GET_CLASS(element)->release_pad(element, pad);
}

static void on_key_received(ErDtlsConnection *connection, gpointer key, guint cipher, guint auth, GstErDtlsDec *self)
{
    gpointer key_dup;
    gchar *key_str;

    UNUSED(connection);
    g_return_if_fail(GST_IS_ER_DTLS_DEC(self));

    self->srtp_cipher = cipher;
    self->srtp_auth = auth;

    key_dup = g_memdup(key, ER_DTLS_SRTP_MASTER_KEY_LENGTH);
    self->decoder_key = gst_buffer_new_wrapped(key_dup, ER_DTLS_SRTP_MASTER_KEY_LENGTH);

    key_str = g_base64_encode(key, ER_DTLS_SRTP_MASTER_KEY_LENGTH);
    GST_INFO_OBJECT(self, "received key: %s", key_str);
    g_free(key_str);

    g_signal_emit(self, signals[SIGNAL_ON_KEY_RECEIVED], 0);
}

static gboolean signal_peer_certificate_received(GWeakRef *ref)
{
    GstErDtlsDec *self;

    self = g_weak_ref_get(ref);
    g_weak_ref_clear(ref);
    g_free(ref);
    ref = NULL;

    if (self) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PEER_PEM]);
        g_object_unref(self);
        self = NULL;
    }

    return FALSE;
}

static gboolean on_peer_certificate_received(ErDtlsConnection *connection, gchar *pem, GstErDtlsDec *self)
{
    GWeakRef *ref;

    UNUSED(connection);
    g_return_val_if_fail(GST_IS_ER_DTLS_DEC(self), TRUE);

    GST_DEBUG_OBJECT(self, "Received peer certificate PEM: \n%s", pem);

    self->peer_pem = g_strdup(pem);

    ref = g_new(GWeakRef, 1);
    g_weak_ref_init(ref, self);

    g_idle_add((GSourceFunc) signal_peer_certificate_received, ref);

    return TRUE;
}

static GstFlowReturn sink_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
    GstErDtlsDec *self = GST_ER_DTLS_DEC(parent);
    GstFlowReturn ret = GST_FLOW_OK;
    GstMapInfo map_info = GST_MAP_INFO_INIT;
    gint size;

    if (!self->agent) {
        gst_buffer_unref(buffer);
        return GST_FLOW_OK;
    }

    GST_DEBUG_OBJECT(self, "received buffer from %s with length %zd",
        self->connection_id, gst_buffer_get_size(buffer));

    gst_buffer_map(buffer, &map_info, GST_MAP_READWRITE);

    if (!map_info.size) {
        gst_buffer_unmap(buffer, &map_info);
        return GST_FLOW_OK;
    }

    size = er_dtls_connection_process(self->connection, map_info.data, map_info.size);
    gst_buffer_unmap(buffer, &map_info);

    if (size <= 0) {
        gst_buffer_unref(buffer);

        return GST_FLOW_OK;
    }

    g_mutex_lock(&self->src_mutex);

    if (self->src) {
        gst_buffer_set_size(buffer, size);
        GST_LOG_OBJECT(self, "decoded buffer with length %d, pushing", size);
        ret = gst_pad_push(self->src, buffer);
    } else {
        GST_LOG_OBJECT(self, "dropped buffer with length %d, not linked", size);
        gst_buffer_unref(buffer);
    }

    g_mutex_unlock(&self->src_mutex);

    return ret;
}

static GHashTable *agent_table = NULL;
G_LOCK_DEFINE_STATIC(agent_table);

static ErDtlsAgent *generated_cert_agent = NULL;

static ErDtlsAgent *get_agent_by_pem(const gchar *pem)
{
    ErDtlsAgent *agent;

    if (!pem) {
        if (g_once_init_enter (&generated_cert_agent)) {
            ErDtlsAgent *new_agent;

            new_agent = g_object_new(ER_TYPE_DTLS_AGENT, "certificate",
                g_object_new(ER_TYPE_DTLS_CERTIFICATE, NULL), NULL);

            GST_DEBUG_OBJECT(generated_cert_agent, "no agent with generated cert found, creating new");
            g_once_init_leave (&generated_cert_agent, new_agent);
        } else {
            GST_DEBUG_OBJECT(generated_cert_agent, "using agent with generated cert");
        }

        agent = generated_cert_agent;
        g_object_ref(agent);
    } else {
        G_LOCK(agent_table);

        if (!agent_table) {
            agent_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        }

        agent =  ER_DTLS_AGENT(g_hash_table_lookup(agent_table, pem));

        if (!agent) {
            agent = g_object_new(ER_TYPE_DTLS_AGENT,
                "certificate", g_object_new(ER_TYPE_DTLS_CERTIFICATE, "pem", pem, NULL), NULL);

            g_object_weak_ref(G_OBJECT(agent), (GWeakNotify) agent_weak_ref_notify, (gpointer) g_strdup(pem));

            g_hash_table_insert(agent_table, g_strdup(pem), agent);

            GST_DEBUG_OBJECT(agent, "no agent found, created new");
        } else {
            g_object_ref(agent);
            GST_DEBUG_OBJECT(agent, "agent found");
        }

        G_UNLOCK(agent_table);
    }


    return agent;
}

static void agent_weak_ref_notify(gchar *pem, ErDtlsAgent *agent)
{
    UNUSED(agent);

    G_LOCK(agent_table);
    g_hash_table_remove(agent_table, pem);
    G_UNLOCK(agent_table);

    g_free(pem);
    pem = NULL;
}

static GHashTable *connection_table = NULL;
G_LOCK_DEFINE_STATIC(connection_table);

ErDtlsConnection *gst_er_dtls_dec_fetch_connection(gchar *id)
{
    ErDtlsConnection *connection;
    g_return_val_if_fail(id, NULL);

    GST_DEBUG("fetching '%s' from connection table, size is %d",
        id, g_hash_table_size(connection_table));

    G_LOCK(connection_table);

    connection = g_hash_table_lookup(connection_table, id);

    if (connection) {
        g_object_ref(connection);
        g_hash_table_remove(connection_table, id);
    } else {
        GST_WARNING("no connection with id '%s' found", id);
    }

    G_UNLOCK(connection_table);

    return connection;
}

static void create_connection(GstErDtlsDec *self, gchar *id)
{
    g_return_if_fail(GST_IS_ER_DTLS_DEC(self));
    g_return_if_fail(ER_IS_DTLS_AGENT(self->agent));

    if (self->connection) {
        g_object_unref(self->connection);
        self->connection = NULL;
    }

    G_LOCK(connection_table);

    if (!connection_table) {
        connection_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    }

    if (g_hash_table_contains(connection_table, id)) {
        G_UNLOCK(connection_table);

        g_return_if_reached();
    }

    self->connection = g_object_new(ER_TYPE_DTLS_CONNECTION, "agent", self->agent, NULL);

    g_object_weak_ref(G_OBJECT(self->connection), (GWeakNotify) connection_weak_ref_notify, g_strdup(id));

    g_hash_table_insert(connection_table, g_strdup(id), self->connection);

    G_UNLOCK(connection_table);
}

static void connection_weak_ref_notify(gchar *id, ErDtlsConnection *connection)
{
    UNUSED(connection);

    G_LOCK(connection_table);
    g_hash_table_remove(connection_table, id);
    G_UNLOCK(connection_table);

    g_free(id);
    id = NULL;
}
