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

#ifndef gsterdtlssrtpbin_h
#define gsterdtlssrtpbin_h

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_ER_DTLS_SRTP_BIN (gst_er_dtls_srtp_bin_get_type())
#define GST_ER_DTLS_SRTP_BIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ER_DTLS_SRTP_BIN, GstErDtlsSrtpBin))
#define GST_ER_DTLS_SRTP_BIN_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ER_DTLS_SRTP_BIN, GstErDtlsSrtpBinClass))
#define GST_ER_DTLS_SRTP_BIN_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_ER_DTLS_SRTP_BIN, GstErDtlsSrtpBinClass))
#define GST_IS_ER_DTLS_SRTP_BIN(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ER_DTLS_SRTP_BIN))
#define GST_IS_ER_DTLS_SRTP_BIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ER_DTLS_SRTP_BIN))

typedef struct _GstErDtlsSrtpBin GstErDtlsSrtpBin;
typedef struct _GstErDtlsSrtpBinClass GstErDtlsSrtpBinClass;

struct _GstErDtlsSrtpBin {
    GstBin bin;

    GstElement *dtls_element;

    gboolean key_is_set;
    GstBuffer *key;
    gchar *srtp_cipher;
    gchar *srtp_auth;
    gchar *srtcp_cipher;
    gchar *srtcp_auth;
};

struct _GstErDtlsSrtpBinClass {
    GstBinClass parent_class;

    void (*remove_dtls_element)(GstErDtlsSrtpBin *);
};

GType gst_er_dtls_srtp_bin_get_type(void);

G_END_DECLS

#endif /* gsterdtlssrtpbin_h */
