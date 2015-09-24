/*
* Copyright (c) 2015, Ericsson AB. All rights reserved.
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
#ifndef gstscreamqueue_h
#define gstscreamqueue_h

#include "gstscreamcontroller.h"

#include <gst/gst.h>
#include <gst/base/base.h>

G_BEGIN_DECLS

#define GST_TYPE_SCREAM_QUEUE (gst_scream_queue_get_type())
#define GST_SCREAM_QUEUE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SCREAM_QUEUE, GstScreamQueue))
#define GST_SCREAM_QUEUE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SCREAM_QUEUE, GstScreamQueueClass))
#define GST_IS_SCREAM_QUEUE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SCREAM_QUEUE))
#define GST_IS_SCREAM_QUEUE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_SCREAM_QUEUE))

typedef struct _GstScreamQueue GstScreamQueue;
typedef struct _GstScreamQueueClass GstScreamQueueClass;
typedef struct _GstScreamQueuePrivate GstScreamQueuePrivate;

struct _GstScreamQueue {
    GstElement element;

    GstPad *sink_pad;
    GstPad *src_pad;
    gboolean pass_through;

    guint scream_controller_id;
    GstScreamController *scream_controller;
    guint priority;

    GRWLock lock;
    GHashTable *streams;
    GHashTable *adapted_stream_ids;
    GHashTable *ignored_stream_ids;

    /*GstDataQueue *incoming_packets;*/
    GAsyncQueue *incoming_packets;
    GstDataQueue *approved_packets;
    guint64 next_approve_time;
};

struct _GstScreamQueueClass {
    GstElementClass parent_class;

    void (*gst_scream_queue_on_bitrate_change)(GstElement *element, guint bitrate, guint ssrc);
    gboolean (*gst_scream_queue_on_adaptation_request)(GstElement *element, guint pt);
    void (*gst_scream_queue_incoming_feedback)(GstScreamQueue *self, guint ssrc, guint timestamp,
        guint highestSeqNr, guint n_loss, guint n_ecn, gboolean qBit);
};

GType gst_scream_queue_get_type(void);

G_END_DECLS

#endif /* gstscreamqueue_h */
