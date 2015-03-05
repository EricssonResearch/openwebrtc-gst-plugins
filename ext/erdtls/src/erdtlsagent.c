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

#include "erdtlsagent.h"

#include "erdtlscommon.h"

#ifdef __APPLE__
# define __AVAILABILITYMACROS__
# define DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER
#endif

#include <openssl/err.h>
#include <openssl/ssl.h>

#if ER_DTLS_USE_GST_LOG
    GST_DEBUG_CATEGORY_STATIC(er_dtls_agent_debug);
#   define GST_CAT_DEFAULT er_dtls_agent_debug
    G_DEFINE_TYPE_WITH_CODE(ErDtlsAgent, er_dtls_agent, G_TYPE_OBJECT,
        GST_DEBUG_CATEGORY_INIT(er_dtls_agent_debug, "erdtlsagent", 0, "Ericsson DTLS Agent"));
#else
    G_DEFINE_TYPE(ErDtlsAgent, er_dtls_agent, G_TYPE_OBJECT);
#endif

#define ER_DTLS_AGENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), ER_TYPE_DTLS_AGENT, ErDtlsAgentPrivate))

enum {
    PROP_0,
    PROP_CERTIFICATE,
    NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES];

struct _ErDtlsAgentPrivate {
    SSL_CTX *ssl_context;

    ErDtlsCertificate *certificate;
};

static void er_dtls_agent_finalize(GObject *gobject);
static void er_dtls_agent_set_property(GObject *, guint prop_id, const GValue *, GParamSpec *);
const gchar *er_dtls_agent_peek_id(ErDtlsAgent *);

static GRWLock *ssl_locks;

static void ssl_locking_function(gint mode, gint lock_num, const gchar *file, gint line)
{
    gboolean locking;
    gboolean reading;
    GRWLock *lock;

    locking = mode & CRYPTO_LOCK;
    reading = mode & CRYPTO_READ;
    lock = &ssl_locks[lock_num];

    LOG_LOG(NULL, "%s SSL lock for %s, thread=%p location=%s:%d",
        locking ? "locking" : "unlocking", reading ? "reading" : "writing",
        g_thread_self(), file, line);

    if (locking) {
        if (reading) {
            g_rw_lock_reader_lock(lock);
        } else {
            g_rw_lock_writer_lock(lock);
        }
    } else {
        if (reading) {
            g_rw_lock_reader_unlock(lock);
        } else {
            g_rw_lock_writer_unlock(lock);
        }
    }
}

static gulong ssl_thread_id_function(void)
{
    return (gulong) g_thread_self();
}

void _er_dtls_init_openssl()
{
    static gsize is_init = 0;
    gint i;
    gint num_locks;

    if (g_once_init_enter(&is_init)) {
        if (OPENSSL_VERSION_NUMBER < 0x1000100fL) {
            LOG_WARNING(NULL, "Incorrect OpenSSL version, should be >= 1.0.1, is %s", OPENSSL_VERSION_TEXT);
            g_assert_not_reached();
        }

        LOG_INFO(NULL, "initializing openssl %lx", OPENSSL_VERSION_NUMBER);
        SSL_library_init();
        SSL_load_error_strings();
        ERR_load_BIO_strings();

        num_locks = CRYPTO_num_locks();
        ssl_locks = g_new(GRWLock, num_locks);
        for (i = 0; i < num_locks; ++i) {
            g_rw_lock_init(&ssl_locks[i]);
        }
        CRYPTO_set_locking_callback(ssl_locking_function);
        CRYPTO_set_id_callback(ssl_thread_id_function);

        g_once_init_leave(&is_init, 1);
    }
}

static void er_dtls_agent_class_init(ErDtlsAgentClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(ErDtlsAgentPrivate));

    gobject_class->set_property = er_dtls_agent_set_property;
    gobject_class->finalize = er_dtls_agent_finalize;

    properties[PROP_CERTIFICATE] =
        g_param_spec_object("certificate",
            "ErDtlsCertificate",
            "Sets the certificate of the agent",
            ER_TYPE_DTLS_CERTIFICATE,
            G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(gobject_class, NUM_PROPERTIES, properties);

    _er_dtls_init_openssl();
}

static void er_dtls_agent_init(ErDtlsAgent *self)
{
    ErDtlsAgentPrivate *priv = ER_DTLS_AGENT_GET_PRIVATE(self);
    self->priv = priv;

    ERR_clear_error();

    priv->ssl_context = SSL_CTX_new(DTLSv1_method());
    if (ERR_peek_error() || !priv->ssl_context) {
        char buf[512];

        priv->ssl_context = NULL;

        LOG_WARNING(self, "Error creating SSL Context: %s", ERR_error_string(ERR_get_error(), buf));

        g_return_if_reached();
    }

    SSL_CTX_set_verify_depth(priv->ssl_context, 2);
    SSL_CTX_set_tlsext_use_srtp(priv->ssl_context, "SRTP_AES128_CM_SHA1_80");
    SSL_CTX_set_cipher_list(priv->ssl_context, "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
    SSL_CTX_set_read_ahead(priv->ssl_context, 1);
#if OPENSSL_VERSION_NUMBER >= 0x1000200fL
    SSL_CTX_set_ecdh_auto(priv->ssl_context, 1);
#endif
}

static void er_dtls_agent_finalize(GObject *gobject)
{
    ErDtlsAgentPrivate *priv = ER_DTLS_AGENT(gobject)->priv;

    SSL_CTX_free(priv->ssl_context);
    priv->ssl_context = NULL;

    LOG_DEBUG(gobject, "finalized");

    G_OBJECT_CLASS(er_dtls_agent_parent_class)->finalize(gobject);
}

static void er_dtls_agent_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    ErDtlsAgent *self = ER_DTLS_AGENT(object);
    ErDtlsCertificate *certificate;

    switch (prop_id) {
    case PROP_CERTIFICATE:
        certificate = ER_DTLS_CERTIFICATE(g_value_get_object(value));
        g_return_if_fail(ER_IS_DTLS_CERTIFICATE(certificate));
        g_return_if_fail(self->priv->ssl_context);

        self->priv->certificate = certificate;
        g_object_ref(certificate);

        if (!SSL_CTX_use_certificate(self->priv->ssl_context, _er_dtls_certificate_get_internal_certificate(certificate))) {
            LOG_WARNING(self, "could not use certificate");
            g_return_if_reached();
        }

        if (!SSL_CTX_use_PrivateKey(self->priv->ssl_context, _er_dtls_certificate_get_internal_key(certificate))) {
            LOG_WARNING(self, "could not use private key");
            g_return_if_reached();
        }

        if (!SSL_CTX_check_private_key(self->priv->ssl_context)) {
            LOG_WARNING(self, "invalid private key");
            g_return_if_reached();
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(self, prop_id, pspec);
    }
}

ErDtlsCertificate *er_dtls_agent_get_certificate(ErDtlsAgent *self)
{
    g_return_val_if_fail(ER_IS_DTLS_AGENT(self), NULL);
    if (self->priv->certificate) {
        g_object_ref(self->priv->certificate);
    }
    return self->priv->certificate;
}

gchar *er_dtls_agent_get_certificate_pem(ErDtlsAgent *self)
{
    gchar *pem;
    g_return_val_if_fail(ER_IS_DTLS_AGENT(self), NULL);
    g_return_val_if_fail(ER_IS_DTLS_CERTIFICATE(self->priv->certificate), NULL);

    g_object_get(self->priv->certificate, "pem", &pem, NULL);

    return pem;
}

const ErDtlsAgentContext _er_dtls_agent_peek_context(ErDtlsAgent *self)
{
    g_return_val_if_fail(ER_IS_DTLS_AGENT(self), NULL);
    return self->priv->ssl_context;
}
