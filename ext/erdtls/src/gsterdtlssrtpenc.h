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

#ifndef gsterdtlssrtpenc_h
#define gsterdtlssrtpenc_h

#include "gsterdtlssrtpbin.h"

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_ER_DTLS_SRTP_ENC (gst_er_dtls_srtp_enc_get_type())
#define GST_ER_DTLS_SRTP_ENC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ER_DTLS_SRTP_ENC, GstErDtlsSrtpEnc))
#define GST_ER_DTLS_SRTP_ENC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ER_DTLS_SRTP_ENC, GstErDtlsSrtpEncClass))
#define GST_IS_ER_DTLS_SRTP_ENC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ER_DTLS_SRTP_ENC))
#define GST_IS_ER_DTLS_SRTP_ENC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ER_DTLS_SRTP_ENC))

typedef struct _GstErDtlsSrtpEnc GstErDtlsSrtpEnc;
typedef struct _GstErDtlsSrtpEncClass GstErDtlsSrtpEncClass;

struct _GstErDtlsSrtpEnc {
    GstErDtlsSrtpBin bin;

    GstElement *srtp_enc;
    GstElement *funnel;
};

struct _GstErDtlsSrtpEncClass {
    GstErDtlsSrtpBinClass parent_class;
};

GType gst_er_dtls_srtp_enc_get_type(void);

gboolean gst_er_dtls_srtp_enc_plugin_init(GstPlugin *);

guint gst_er_dtls_srtp_enc_get_cipher_value_by_nick(const gchar *cipher_nick);
guint gst_er_dtls_srtp_enc_get_auth_value_by_nick(const gchar *auth_nick);

G_END_DECLS

#endif /* gsterdtlssrtpenc_h */
