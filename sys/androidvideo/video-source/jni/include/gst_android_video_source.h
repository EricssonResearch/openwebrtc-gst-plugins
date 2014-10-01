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

#ifndef gst_android_video_source_h
#define gst_android_video_source_h

#include "android_video_capture_device.h"

#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_ANDROIDVIDEOSOURCE (gst_android_video_source_get_type())
#define GST_ANDROIDVIDEOSOURCE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ANDROIDVIDEOSOURCE, GstAndroidVideoSource))
#define GST_ANDROIDVIDEOSOURCE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ANDROIDVIDEOSOURCE, GstAndroidVideoSourceClass))
#define GST_IS_ANDROIDVIDEOSOURCE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ANDROIDVIDEOSOURCE))
#define GST_IS_ANDROIDVIDEOSOURCE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ANDROIDVIDEOSOURCE))

typedef struct _GstAndroidVideoSource      GstAndroidVideoSource;
typedef struct _GstAndroidVideoSourceClass GstAndroidVideoSourceClass;

struct _GstAndroidVideoSource {
    GstPushSrc element;
    VCD_handle m_devHandle;
    GstClockTime m_duration;
    GstClockTime m_prev_timestamp;
    gint m_bufSize;
    gint log_interval;
    gint log_level_android;
    gboolean vcdStarted;
};

struct _GstAndroidVideoSourceClass {
    GstPushSrcClass parent_class;
};

GType gst_android_video_source_get_type(void);

G_END_DECLS

#endif /* gst_android_video_source_h */
