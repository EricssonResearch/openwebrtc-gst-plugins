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

#include "codec_api.h"
#include "codec_app_def.h"
#include "codec_def.h"

#include "gstopenh264dec.h"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/codecparsers/gsth264parser.h>
#include <string.h> /* for memcpy */


#define GST_OPENH264DEC_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ( \
                                     (obj), GST_TYPE_OPENH264DEC, \
                                     GstOpenh264DecPrivate))

GST_DEBUG_CATEGORY_STATIC(gst_openh264dec_debug_category);
#define GST_CAT_DEFAULT gst_openh264dec_debug_category

/* prototypes */


static void gst_openh264dec_set_property(GObject *object,
    guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_openh264dec_get_property(GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec);

static gboolean gst_openh264dec_start(GstVideoDecoder *decoder);
static gboolean gst_openh264dec_stop(GstVideoDecoder *decoder);

static gboolean gst_openh264dec_set_format(GstVideoDecoder *decoder, GstVideoCodecState *state);
static gboolean gst_openh264dec_reset(GstVideoDecoder *decoder, gboolean hard);
static GstFlowReturn gst_openh264dec_finish(GstVideoDecoder *decoder);
static GstFlowReturn gst_openh264dec_handle_frame(GstVideoDecoder *decoder,
    GstVideoCodecFrame *frame);
static gboolean gst_openh264dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query);

enum
{
    PROP_0,
    N_PROPERTIES
};

struct _GstOpenh264DecPrivate
{
    ISVCDecoder *decoder;
    GstH264NalParser *nal_parser;
    GstVideoCodecState *input_state;
    guint width, height;
};

/* pad templates */

static GstStaticPadTemplate gst_openh264dec_sink_template = GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-h264, stream-format=(string)\"byte-stream\", alignment=(string)\"nal\""));

static GstStaticPadTemplate gst_openh264dec_src_template = GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("I420")));

/* class initialization */

G_DEFINE_TYPE_WITH_CODE(GstOpenh264Dec, gst_openh264dec, GST_TYPE_VIDEO_DECODER,
    GST_DEBUG_CATEGORY_INIT(gst_openh264dec_debug_category,
        "openh264dec", 0, "debug category for openh264dec element"));

static void gst_openh264dec_class_init(GstOpenh264DecClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstOpenh264DecPrivate));

    /* Setting up pads and setting metadata should be moved to
       base_class_init if you intend to subclass this class. */
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
        gst_static_pad_template_get(&gst_openh264dec_sink_template));
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
        gst_static_pad_template_get(&gst_openh264dec_src_template));

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass), "OpenH264 video decoder", "Decoder/Video", "OpenH264 video decoder", "Ericsson AB, http://www.ericsson.com");
    gobject_class->set_property = gst_openh264dec_set_property;
    gobject_class->get_property = gst_openh264dec_get_property;

    video_decoder_class->start = GST_DEBUG_FUNCPTR(gst_openh264dec_start);
    video_decoder_class->stop = GST_DEBUG_FUNCPTR(gst_openh264dec_stop);

    video_decoder_class->set_format = GST_DEBUG_FUNCPTR(gst_openh264dec_set_format);
    video_decoder_class->reset = GST_DEBUG_FUNCPTR(gst_openh264dec_reset);
    video_decoder_class->finish = GST_DEBUG_FUNCPTR(gst_openh264dec_finish);
    video_decoder_class->handle_frame = GST_DEBUG_FUNCPTR(gst_openh264dec_handle_frame);
    video_decoder_class->decide_allocation = GST_DEBUG_FUNCPTR(gst_openh264dec_decide_allocation);
}

static void gst_openh264dec_init(GstOpenh264Dec *openh264dec)
{
    openh264dec->priv = GST_OPENH264DEC_GET_PRIVATE(openh264dec);
    openh264dec->priv->nal_parser = NULL;
    openh264dec->priv->decoder = NULL;

    gst_video_decoder_set_packetized(GST_VIDEO_DECODER(openh264dec), TRUE);
    gst_video_decoder_set_needs_format(GST_VIDEO_DECODER(openh264dec), TRUE);
}

void gst_openh264dec_set_property(GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
    GstOpenh264Dec *openh264dec = GST_OPENH264DEC(object);

    GST_DEBUG_OBJECT(openh264dec, "set_property");

    switch (property_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_openh264dec_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    GstOpenh264Dec *openh264dec = GST_OPENH264DEC(object);

    GST_DEBUG_OBJECT(openh264dec, "get_property");

    switch (property_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gboolean gst_openh264dec_start(GstVideoDecoder *decoder)
{
    GstOpenh264Dec *openh264dec = GST_OPENH264DEC(decoder);
    gint ret;
    SDecodingParam dec_param = {0};
    openh264dec->priv->nal_parser = gst_h264_nal_parser_new();

    if (openh264dec->priv->decoder != NULL)
    {
        openh264dec->priv->decoder->Uninitialize();
        WelsDestroyDecoder(openh264dec->priv->decoder);
        openh264dec->priv->decoder = NULL;
    }
    WelsCreateDecoder(&(openh264dec->priv->decoder));

    dec_param.uiTargetDqLayer = 255;
    dec_param.uiEcActiveFlag = 1;
    dec_param.iOutputColorFormat = videoFormatI420;
    dec_param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;

    ret = openh264dec->priv->decoder->Initialize(&dec_param);

    GST_DEBUG_OBJECT(openh264dec, "openh264_dec_start called, openh264dec %sinitialized OK!", (ret != cmResultSuccess) ? "NOT " : "");

    return (ret == cmResultSuccess);
}

static gboolean gst_openh264dec_stop(GstVideoDecoder *decoder)
{
    GstOpenh264Dec *openh264dec = GST_OPENH264DEC(decoder);
    gint ret = TRUE;

    if (openh264dec->priv->nal_parser) {
      gst_h264_nal_parser_free(openh264dec->priv->nal_parser);
      openh264dec->priv->nal_parser = NULL;
    }

    if (openh264dec->priv->decoder) {
      ret = openh264dec->priv->decoder->Uninitialize();
      WelsDestroyDecoder(openh264dec->priv->decoder);
      openh264dec->priv->decoder = NULL;
    }

    if (openh264dec->priv->input_state) {
      gst_video_codec_state_unref (openh264dec->priv->input_state);
      openh264dec->priv->input_state = NULL;
    }
    openh264dec->priv->width = openh264dec->priv->height = 0;

    return ret;
}

static gboolean gst_openh264dec_set_format(GstVideoDecoder *decoder, GstVideoCodecState *state)
{
    GstOpenh264Dec *openh264dec = GST_OPENH264DEC(decoder);

    GST_DEBUG_OBJECT(openh264dec, "openh264_dec_set_format called, caps: %" GST_PTR_FORMAT, state->caps);

    if (openh264dec->priv->input_state) {
      gst_video_codec_state_unref (openh264dec->priv->input_state);
      openh264dec->priv->input_state = NULL;
    }
    openh264dec->priv->input_state = gst_video_codec_state_ref (state);

    return TRUE;
}

static gboolean gst_openh264dec_reset(GstVideoDecoder *decoder, gboolean hard)
{
    GstOpenh264Dec *openh264dec = GST_OPENH264DEC(decoder);

    GST_DEBUG_OBJECT(openh264dec, "reset");

    return TRUE;
}

static GstFlowReturn gst_openh264dec_handle_frame(GstVideoDecoder *decoder, GstVideoCodecFrame *frame)
{
    GstOpenh264Dec *openh264dec = GST_OPENH264DEC(decoder);
    GstMapInfo map_info;
    GstVideoCodecState *state;
    SBufferInfo dst_buf_info;
    guint offset = 0;
    GstH264ParserResult parser_result;
    GstH264NalUnit nalu;
    GstH264SPS sps;
    DECODING_STATE ret;
    guint8 *yuvdata[3];
    GstFlowReturn flow_status;
    GstVideoFrame video_frame;
    guint actual_width, actual_height;
    guint i;
    guint8 *p;
    guint row_stride, component_width, component_height, src_width, row;

    if (frame) {
        if (!gst_buffer_map(frame->input_buffer, &map_info, GST_MAP_READ)) {
            GST_ERROR_OBJECT(openh264dec, "Cannot map input buffer!");
            return GST_FLOW_ERROR;
        }

        GST_LOG_OBJECT(openh264dec, "handle frame, %d", map_info.size > 4 ? map_info.data[4] & 0x1f : -1);

        while (offset < map_info.size) {
            memset (&dst_buf_info, 0, sizeof (SBufferInfo));
            parser_result = gst_h264_parser_identify_nalu(openh264dec->priv->nal_parser,
                map_info.data, offset, map_info.size, &nalu);
            offset = nalu.offset + nalu.size;

            if (parser_result != GST_H264_PARSER_OK && parser_result != GST_H264_PARSER_NO_NAL_END) {
                GST_WARNING_OBJECT(openh264dec, "Failed to identify nalu, parser result: %u", parser_result);
                break;
            }

            memset (&dst_buf_info, 0, sizeof (SBufferInfo));

            ret = openh264dec->priv->decoder->DecodeFrame2(nalu.data, nalu.size + 4, yuvdata, &dst_buf_info);

            if (ret == dsNoParamSets) {
                GST_DEBUG_OBJECT(openh264dec, "Requesting a key unit");
                gst_pad_push_event(GST_VIDEO_DECODER_SINK_PAD(decoder),
                    gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, FALSE, 0));
            }

            if (ret != dsErrorFree && ret != dsNoParamSets) {
                GST_DEBUG_OBJECT(openh264dec, "Requesting a key unit");
                gst_pad_push_event(GST_VIDEO_DECODER_SINK_PAD(decoder),
                                   gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, FALSE, 0));
                GST_LOG_OBJECT(openh264dec, "error decoding nal, return code: %d", ret);
                GST_LOG_OBJECT(openh264dec, "nal first byte: %u", (guint) nalu.data[0]);
                GST_LOG_OBJECT(openh264dec, "nal size: %u", nalu.size);
            }

            if (nalu.type == GST_H264_NAL_SPS) {
                parser_result = gst_h264_parser_parse_sps(openh264dec->priv->nal_parser, &nalu, &sps, TRUE);
                if (parser_result == GST_H264_PARSER_OK) {
                    GST_DEBUG_OBJECT(openh264dec, "Got SPS, fps_n: %u fps_d: %u", sps.fps_num, sps.fps_den);
                    openh264dec->priv->input_state->info.fps_n = sps.fps_num ? sps.fps_num : 30;
                    openh264dec->priv->input_state->info.fps_d = sps.fps_num ? sps.fps_den : 1;
                } else {
                    GST_WARNING_OBJECT(openh264dec, "Failed to parse SPS, parser result: %u", parser_result);
                }
            } else {
                parser_result = gst_h264_parser_parse_nal(openh264dec->priv->nal_parser, &nalu);
                if (parser_result == GST_H264_PARSER_OK) {
                    if (nalu.type == GST_H264_NAL_SLICE_IDR) {
                        GST_DEBUG_OBJECT(openh264dec, "Got an intra picture");
                        GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT(frame);
                    }
                } else {
                    GST_WARNING_OBJECT(openh264dec, "Failed to parse nal, parser result: %u", parser_result);
                }
            }
        }

        gst_buffer_unmap(frame->input_buffer, &map_info);
        gst_video_codec_frame_unref (frame);
        frame = NULL;
    } else {
        memset (&dst_buf_info, 0, sizeof (SBufferInfo));
        ret = openh264dec->priv->decoder->DecodeFrame2(NULL, 0, yuvdata, &dst_buf_info);
        if (ret != dsErrorFree)
            return GST_FLOW_EOS;
    }

    frame = gst_video_decoder_get_oldest_frame (decoder);
    if (!frame) {
      /* Can only happen in finish() */
      return GST_FLOW_EOS;
    }

    if (dst_buf_info.iBufferStatus != 1) {
        GST_VIDEO_CODEC_FRAME_SET_DECODE_ONLY(frame);
        goto finish;
    }

    actual_width  = dst_buf_info.UsrData.sSystemBuffer.iWidth;
    actual_height = dst_buf_info.UsrData.sSystemBuffer.iHeight;

    if (!gst_pad_has_current_caps (GST_VIDEO_DECODER_SRC_PAD (openh264dec)) || actual_width != openh264dec->priv->width || actual_height != openh264dec->priv->height) {
        state = gst_video_decoder_set_output_state(decoder,
            GST_VIDEO_FORMAT_I420,
            actual_width,
            actual_height,
            openh264dec->priv->input_state);
        openh264dec->priv->width = actual_width;
        openh264dec->priv->height = actual_height;

        if (!gst_video_decoder_negotiate(decoder)) {
            GST_ERROR_OBJECT(openh264dec, "Failed to negotiate with downstream elements");
            return GST_FLOW_NOT_NEGOTIATED;
        }
    } else {
        state = gst_video_decoder_get_output_state(decoder);
    }

    flow_status = gst_video_decoder_allocate_output_frame(decoder, frame);
    if (flow_status != GST_FLOW_OK) {
        gst_video_codec_state_unref (state);
        return flow_status;
    }

    if (!gst_video_frame_map(&video_frame, &state->info, frame->output_buffer, GST_MAP_WRITE)) {
        GST_ERROR_OBJECT(openh264dec, "Cannot map output buffer!");
        gst_video_codec_state_unref (state);
        return GST_FLOW_ERROR;
    }

    for (i = 0; i < 3; i++) {
        p = GST_VIDEO_FRAME_COMP_DATA(&video_frame, i);
        row_stride = GST_VIDEO_FRAME_COMP_STRIDE(&video_frame, i);
        component_width = GST_VIDEO_FRAME_COMP_WIDTH(&video_frame, i);
        component_height = GST_VIDEO_FRAME_COMP_HEIGHT(&video_frame, i);
        src_width = i < 1 ? dst_buf_info.UsrData.sSystemBuffer.iStride[0] : dst_buf_info.UsrData.sSystemBuffer.iStride[1];
        for (row = 0; row < component_height; row++) {
            memcpy(p, yuvdata[i], component_width);
            p += row_stride;
            yuvdata[i] += src_width;
        }
    }
    gst_video_codec_state_unref (state);

    gst_video_frame_unmap(&video_frame);


finish:
    return gst_video_decoder_finish_frame(decoder, frame);
}

static GstFlowReturn gst_openh264dec_finish(GstVideoDecoder *decoder)
{
    GstOpenh264Dec *openh264dec = GST_OPENH264DEC(decoder);

    GST_DEBUG_OBJECT(openh264dec, "finish");

    /* Decoder not negotiated yet */
    if (openh264dec->priv->width == 0)
      return GST_FLOW_OK;

    /* Drain all pending frames */
    while ((gst_openh264dec_handle_frame (decoder, NULL)) == GST_FLOW_OK);

    return GST_FLOW_OK;
}

static gboolean gst_openh264dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstVideoCodecState *state;
  GstBufferPool *pool;
  guint size, min, max;
  GstStructure *config;

  if (!GST_VIDEO_DECODER_CLASS (gst_openh264dec_parent_class)->decide_allocation (decoder,
          query))
    return FALSE;

  state = gst_video_decoder_get_output_state (decoder);

  gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  config = gst_buffer_pool_get_config (pool);
  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }

  gst_buffer_pool_set_config (pool, config);

  gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);

  gst_object_unref (pool);
  gst_video_codec_state_unref (state);

  return TRUE;
}

