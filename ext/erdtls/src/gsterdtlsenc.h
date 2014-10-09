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

#ifndef gsterdtlsenc_h
#define gsterdtlsenc_h

#include "erdtlsagent.h"
#include "erdtlsconnection.h"

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_ER_DTLS_ENC (gst_er_dtls_enc_get_type())
#define GST_ER_DTLS_ENC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ER_DTLS_ENC, GstErDtlsEnc))
#define GST_ER_DTLS_ENC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ER_DTLS_ENC, GstErDtlsEncClass))
#define GST_IS_ER_DTLS_ENC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ER_DTLS_ENC))
#define GST_IS_ER_DTLS_ENC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ER_DTLS_ENC))

typedef struct _GstErDtlsEnc GstErDtlsEnc;
typedef struct _GstErDtlsEncClass GstErDtlsEncClass;

struct _GstErDtlsEnc {
    GstElement element;

    GstPad *src;

    GPtrArray *queue;
    GMutex queue_lock;
    GCond queue_cond_add;

    ErDtlsConnection *connection;
    gchar *connection_id;

    gboolean is_client;

    GstBuffer *encoder_key;
    guint srtp_cipher;
    guint srtp_auth;

    gboolean send_initial_events;
};

struct _GstErDtlsEncClass {
    GstElementClass parent_class;
};

GType gst_er_dtls_enc_get_type(void);

gboolean gst_er_dtls_enc_plugin_init(GstPlugin *);

G_END_DECLS

#endif /* gsterdtlsenc_h */
