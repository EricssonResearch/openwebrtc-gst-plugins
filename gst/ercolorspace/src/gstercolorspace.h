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

#ifndef __GST_ERCOLORSPACE_H__
#define __GST_ERCOLORSPACE_H__

#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>
#include <glib.h>

G_BEGIN_DECLS

#define GST_TYPE_ERCOLORSPACE         (gst_ercolorspace_get_type())
#define GST_ERCOLORSPACE(obj)         (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ERCOLORSPACE,GstERColorspace))
#define GST_ERCOLORSPACE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ERCOLORSPACE,GstERColorspaceClass))
#define GST_IS_ERCOLORSPACE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ERCOLORSPACE))
#define GST_IS_ERCOLORSPACE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ERCOLORSPACE))

// Style: We prefer to have the Gst namespace only used by code from the
//        GStreamer project
typedef struct _GstERColorspace      GstERColorspace;
typedef struct _GstERColorspaceClass GstERColorspaceClass;


struct _GstERColorspace {
  GstVideoFilter parent;

  GstVideoInfo from_info;
  GstVideoFormat to_format;
};

struct _GstERColorspaceClass
{
  GstVideoFilterClass parent_class;
};


G_END_DECLS

#endif /* __GST_ERCOLORSPACE_H__ */
