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

#include "gstercolorspace.h"
#include "gstercolorspace_neon.h"
#include <stdio.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_ercolorspace_debug);
#define GST_CAT_DEFAULT gst_ercolorspace_debug

G_DEFINE_TYPE_WITH_CODE (GstERColorspace, gst_ercolorspace, GST_TYPE_VIDEO_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_ercolorspace_debug, "ercolorspace", 0,
    "debug category for ercolorspace element"));

/* the capabilities of the inputs and outputs.
 */
static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE("{ BGRA, RGBA, I420, NV12, NV21 }"))
);

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE("{ BGRA, RGBA, I420 }"))
);


GType gst_ercolorspace_get_type (void);

static gboolean gst_ercolorspace_set_info (GstVideoFilter *filter,
                           GstCaps *incaps, GstVideoInfo *in_info,
                           GstCaps *outcaps, GstVideoInfo *out_info);
static GstFlowReturn gst_ercolorspace_transform_frame (GstVideoFilter * filter, GstVideoFrame *in_frame, GstVideoFrame *out_frame);

static GstCaps *
gst_ercolorspace_transform_caps (GstBaseTransform * trans, GstPadDirection direction, GstCaps * query_caps, GstCaps * filter_caps)
{
    GstCaps *result_caps;
    int caps_size, i;
    GstCaps *copy_caps;

    copy_caps = gst_caps_copy (query_caps);
    caps_size = gst_caps_get_size(copy_caps);

    for (i = 0; i < caps_size; ++i) {
        GstStructure *str = gst_caps_get_structure(copy_caps, i);
        /* FIXME: Probably want to remove the colorimetry, chroma-siting
         * and other fields too, which are not relevant for RGB and can
         * cause negotiation errors */
        gst_structure_remove_field(str, "format");
    }

    /* Prefer passthrough */
    result_caps = gst_caps_merge (gst_caps_ref (query_caps), copy_caps);

    /* basetransform will filter against our template caps */

    if (filter_caps) {
        GstCaps *tmp = gst_caps_intersect_full (filter_caps, result_caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (result_caps);
        return tmp;
    } else {
        return result_caps;
    }
}


static gboolean
gst_ercolorspace_set_info (GstVideoFilter *filter,
                           GstCaps *incaps, GstVideoInfo *in_info,
                           GstCaps *outcaps, GstVideoInfo *out_info)
{
    GstERColorspace *space;

    space = GST_ERCOLORSPACE (filter);

    space->from_info = *in_info;
    space->to_format = out_info->finfo->format;

    return TRUE;
}

void
gst_ercolorspace_dispose (GObject * obj)
{
    G_OBJECT_CLASS (gst_ercolorspace_parent_class)->dispose (obj);
}


static void
gst_ercolorspace_finalize (GObject * obj)
{
    G_OBJECT_CLASS (gst_ercolorspace_parent_class)->finalize (obj);
}


/* initialize the ercolorspace's class */
static void
gst_ercolorspace_class_init (GstERColorspaceClass * klass)
{
    GObjectClass *gobject_class = (GObjectClass *) klass;
    GstElementClass *element_class = (GstElementClass *) klass;
    GstBaseTransformClass *gstbasetransform_class = (GstBaseTransformClass *) klass;
    GstVideoFilterClass *gstvideofilter_class = (GstVideoFilterClass *) klass;

    gobject_class->dispose = gst_ercolorspace_dispose;
    gobject_class->finalize = gst_ercolorspace_finalize;

    gstbasetransform_class->transform_caps = GST_DEBUG_FUNCPTR (gst_ercolorspace_transform_caps);
    gstbasetransform_class->passthrough_on_same_caps = TRUE;

    gstvideofilter_class->set_info = GST_DEBUG_FUNCPTR (gst_ercolorspace_set_info);
    gstvideofilter_class->transform_frame = GST_DEBUG_FUNCPTR (gst_ercolorspace_transform_frame);

    gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&src_template));
    gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&sink_template));

    gst_element_class_set_static_metadata (element_class,
      "OpenWebRTC colorspace converter",
      "Filter/Converter/Video",
      "Converts video from one colorspace to another",
      "Ericsson AB, http://www.ericsson.com/");
}


static void
gst_ercolorspace_init (GstERColorspace * space)
{
    gst_video_info_init (&space->from_info);
    space->to_format = GST_VIDEO_FORMAT_UNKNOWN;
}


/* this function does the actual processing
 */
static GstFlowReturn
gst_ercolorspace_transform_frame (GstVideoFilter * filter, GstVideoFrame *in_frame, GstVideoFrame *out_frame)
{
    GstERColorspace *space = GST_ERCOLORSPACE (filter);

    if (G_UNLIKELY (space->from_info.finfo->format == GST_VIDEO_FORMAT_UNKNOWN
        || space->to_format == GST_VIDEO_FORMAT_UNKNOWN))
    {
        GST_ELEMENT_ERROR (space, CORE, NOT_IMPLEMENTED, (NULL),
                           ("colorspace conversion failed: unknown formats"));
        return GST_FLOW_NOT_NEGOTIATED;
    }

    switch (space->from_info.finfo->format) {
    case GST_VIDEO_FORMAT_NV12: {
        guint8 * yarr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 0);
        guint8 * uvarr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 1);
        gint width, height;
        gint y_stride, uv_stride;
        gint row;

        width = GST_VIDEO_FRAME_WIDTH (in_frame);
        height = GST_VIDEO_FRAME_HEIGHT (in_frame);
        y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 0);
        uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 1);

        if (space->to_format != GST_VIDEO_FORMAT_I420) {
            guint8 * argbarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);
            gint argb_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);

            for (row = 0; row < height; row++)
            {
                if (space->to_format == GST_VIDEO_FORMAT_BGRA) {
                    gst_ercolorspace_transform_nv12_to_bgra_neon (yarr, uvarr, argbarr, width);
                } else {
                    gst_ercolorspace_transform_nv12_to_rgba_neon (yarr, uvarr, argbarr, width);
                }
                yarr += y_stride;
                if (row % 2 == 1)
                  uvarr += uv_stride;
                argbarr += argb_stride;
            }
        } else /* I420 */ {
          guint8 *out_yarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);
          guint8 *out_uarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 1);
          guint8 *out_varr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 2);
          gint out_y_stride, out_u_stride, out_v_stride;

          out_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);
          out_u_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 1);
          out_v_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 2);

          for (row = 0; row < height; row++)
          {
            memcpy (out_yarr, yarr, width);
            yarr += y_stride;
            out_yarr += out_y_stride;

            if (row % 2 == 1) {
              gst_ercolorspace_transform_nv12_to_i420_neon (uvarr, width, out_uarr, out_varr);
              uvarr += uv_stride;
              out_uarr += out_u_stride;
              out_varr += out_v_stride;
            }
          }
        }
        return GST_FLOW_OK;
        break;
      }
    case GST_VIDEO_FORMAT_NV21: {
        guint8 * yarr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 0);
        guint8 * uvarr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 1);
        gint width, height;
        gint y_stride, uv_stride;
        gint row;

        width = GST_VIDEO_FRAME_WIDTH (in_frame);
        height = GST_VIDEO_FRAME_HEIGHT (in_frame);
        y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 0);
        uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 1);

        if (space->to_format != GST_VIDEO_FORMAT_I420) {
            guint8 * argbarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);
            gint argb_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);

            for (row = 0; row < height; row++)
            {
                if (space->to_format == GST_VIDEO_FORMAT_BGRA) {
                    gst_ercolorspace_transform_nv21_to_bgra_neon (yarr, uvarr, argbarr, width);
                } else {
                    gst_ercolorspace_transform_nv21_to_rgba_neon (yarr, uvarr, argbarr, width);
                }
                yarr += y_stride;

                if (row % 2 == 1)
                  uvarr += uv_stride;
                argbarr += argb_stride;
            }
        } else /* I420 */ {
          guint8 *out_yarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);
          guint8 *out_uarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 1);
          guint8 *out_varr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 2);
          gint out_y_stride, out_u_stride, out_v_stride;

          out_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);
          out_u_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 1);
          out_v_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 2);

          for (row = 0; row < height; row++)
          {
            memcpy (out_yarr, yarr, width);
            yarr += y_stride;
            out_yarr += out_y_stride;

            if (row % 2 == 1) {
              gst_ercolorspace_transform_nv21_to_i420_neon (uvarr, width, out_uarr, out_varr);
              uvarr += uv_stride;
              out_uarr += out_u_stride;
              out_varr += out_v_stride;
            }
          }
        }
        return GST_FLOW_OK;
        break;
      }
    case GST_VIDEO_FORMAT_I420: {
        guint8 * yarr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 0);
        guint8 * uarr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 1);
        guint8 * varr = GST_VIDEO_FRAME_PLANE_DATA (in_frame, 2);
        guint8 * argbarr = GST_VIDEO_FRAME_PLANE_DATA (out_frame, 0);
        gint width, height;
        gint y_stride, u_stride, v_stride, argb_stride;
        gint row;

        width = GST_VIDEO_FRAME_WIDTH (in_frame);
        height = GST_VIDEO_FRAME_HEIGHT (in_frame);

        y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 0);
        u_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 1);
        v_stride = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, 2);
        argb_stride = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, 0);

        for (row = 0; row < height; row++)
        {
            if (space->to_format == GST_VIDEO_FORMAT_BGRA) {
                gst_ercolorspace_transform_i420_to_bgra_neon (yarr, uarr, varr, width, argbarr);
            } else {
                gst_ercolorspace_transform_i420_to_rgba_neon (yarr, uarr, varr, width, argbarr);
            }

            yarr += y_stride;
            argbarr += argb_stride;

            if (row % 2 == 1) {
              uarr += u_stride;
              varr += v_stride;
            }
        }
        return GST_FLOW_OK;
        break;
      }
    default:
        break;
    }

    GST_ELEMENT_ERROR (space, CORE, NOT_IMPLEMENTED, (NULL),
                       ("colorspace conversion failed: unsupported formats"));
    return GST_FLOW_NOT_NEGOTIATED;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
ercolorspace_init (GstPlugin * ercolorspace)
{
  return gst_element_register (ercolorspace, "ercolorspace", GST_RANK_NONE,
      GST_TYPE_ERCOLORSPACE);
}

/* gstreamer looks for this structure to register ercolorspaces
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ercolorspace,
    "OpenWebRTC colorspace converter",
    ercolorspace_init,
    VERSION,
    "BSD",
    "OpenWebRTC GStreamer plugins",
    "http://www.ericsson.com/"
)
