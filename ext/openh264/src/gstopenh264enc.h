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

/*/
\*\ GstOpenh264Enc
/*/

#ifndef _GST_OPENH264ENC_H_
#define _GST_OPENH264ENC_H_

#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>

G_BEGIN_DECLS
#define GST_TYPE_OPENH264ENC          (gst_openh264enc_get_type())
#define GST_OPENH264ENC(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OPENH264ENC,GstOpenh264Enc))
#define GST_OPENH264ENC_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OPENH264ENC,GstOpenh264EncClass))
#define GST_IS_OPENH264ENC(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OPENH264ENC))
#define GST_IS_OPENH264ENC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OPENH264ENC))

typedef struct _GstOpenh264Enc GstOpenh264Enc;
typedef struct _GstOpenh264EncClass GstOpenh264EncClass;
typedef struct _GstOpenh264EncPrivate GstOpenh264EncPrivate;

struct _GstOpenh264Enc
{
    GstVideoEncoder base_openh264enc;

    /*< private >*/
    GstOpenh264EncPrivate *priv;
};

struct _GstOpenh264EncClass
{
    GstVideoEncoderClass base_openh264enc_class;
};

GType gst_openh264enc_get_type(void);

G_END_DECLS
#endif
