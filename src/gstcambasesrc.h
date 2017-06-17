/*
 * GStreamer
 * Copyright (C) 2015-2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_CAM_BASE_SRC_H__
#define __GST_CAM_BASE_SRC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * GstCamBaseSrcFlags:
 * @GST_CAM_BASE_SRC_FLAG_STARTING: has source is starting
 * @GST_CAM_BASE_SRC_FLAG_STARTED: has source been started
 * @GST_CAM_BASE_SRC_FLAG_LAST: offset to define more flags
 *
 * The #GstElement flags that a basesrc element may have.
 */
typedef enum {
  GST_CAM_BASE_SRC_FLAG_STARTING  =   (GST_ELEMENT_FLAG_LAST << 0),
  GST_CAM_BASE_SRC_FLAG_STARTED  =  (GST_ELEMENT_FLAG_LAST << 1),
  /* padding */
  GST_CAM_BASE_SRC_FLAG_LAST  =  (GST_ELEMENT_FLAG_LAST << 6)
} GstCamBaseSrcFlags;

#define GST_TYPE_CAM_BASE_SRC               (gst_cam_base_src_get_type())
#define GST_CAM_BASE_SRC(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CAM_BASE_SRC,GstCamBaseSrc))
#define GST_CAM_BASE_SRC_CLASS(klass)               (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CAM_BASE_SRC,GstCamBaseSrcClass))
#define GST_CAM_BASE_SRC_GET_CLASS(obj)               (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_CAM_BASE_SRC,GstCamBaseSrcClass))
#define GST_IS_CAM_BASE_SRC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAM_BASE_SRC))
#define GST_IS_CAM_BASE_SRC_CLASS(klass)               (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAM_BASE_SRC))
#define GST_CAM_BASE_SRC_CAST(obj)               ((GstCamBaseSrc *)(obj))

#define GST_CAM_BASE_SRC_IS_STARTING(obj) GST_OBJECT_FLAG_IS_SET ((obj), GST_CAM_BASE_SRC_FLAG_STARTING)
#define GST_CAM_BASE_SRC_IS_STARTED(obj)  GST_OBJECT_FLAG_IS_SET ((obj), GST_CAM_BASE_SRC_FLAG_STARTED)

#define DEFAULT_BLOCKSIZE    4096
#define DEFAULT_NUM_BUFFERS    -1
#define DEFAULT_TYPEFIND FALSE
#define DEFAULT_DO_TIMESTAMP FALSE

/**
 * GST_CAM_BASE_SRC_PAD:
 * @obj: base source instance
 *
 * Gives the pointer to the #GstPad object of the element.
 */
#define GST_CAM_BASE_SRC_PAD(obj)                 (GST_CAM_BASE_SRC_CAST (obj)->srcpad)
/**
 * GST_CAM_BASE_VIDEO_PAD:
 * @obj: video source instance
 *
 * Gives the pointer to the #GstPad object of the element.
 */
#define GST_CAM_BASE_VIDEO_PAD(obj)                 (GST_CAM_BASE_SRC_CAST (obj)->videopad)

/* Srcpads for Multi-stream feature, enable 'src' by default */
#define GST_CAM_BASE_SRC_PAD_NAME  "src"
#define GST_CAM_BASE_VIDEO_PAD_NAME  "video" //for multi-stream

typedef struct _GstCamBaseSrc GstCamBaseSrc;
typedef struct _GstCamBaseSrcClass GstCamBaseSrcClass;
typedef struct _GstCamBaseSrcPrivate GstCamBaseSrcPrivate;

struct _GstCamBaseSrc {
  GstElement element;

    /*< protected >*/
  GstPad        *srcpad;
  GstPad        *videopad; //video src pad, for multi-stream

  /* available to subclass implementations */
  /* MT-protected (with LIVE_LOCK) */
  GMutex         live_lock;
  GCond          live_cond;
  gboolean       is_live;
  gboolean       live_running;

  /* only valid when video pad is enabled */
  GMutex         vid_live_lock;

  /* MT-protected (with LOCK) */
  guint          blocksize;     /* size of buffers when operating push based */
  gboolean       can_activate_push;     /* some scheduling properties */
  gboolean       random_access;

  GstClockID     clock_id;      /* for syncing */

  /* MT-protected (with STREAM_LOCK *and* OBJECT_LOCK) */
  GstSegment     segment;
  GstSegment     vid_segment;
  /* MT-protected (with STREAM_LOCK) */
  gboolean       need_newsegment;

  gint           num_buffers;
  gint           num_buffers_left;
  gint           num_video_buffers_left;

  gboolean       typefind;
  gboolean       running;
  GstEvent      *pending_seek;

  GstCamBaseSrcPrivate *priv;

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

/**
 * GstCamBaseSrcClass:
 * @parent_class: Element parent class
 * @get_caps: Called to get the caps to report
 * @negotiate: Negotiated the caps with the peer.
 * @fixate: Called during negotiation if caps need fixating. Implement instead of
 *   setting a fixate function on the source pad.
 * @set_caps: Notify subclass of changed output caps
 * @decide_allocation: configure the allocation query
 * @start: Start processing. Subclasses should open resources and prepare
 *    to produce data. Implementation should call gst_cam_base_src_start_complete()
 *    when the operation completes, either from the current thread or any other
 *    thread that finishes the start operation asynchronously.
 * @stop: Stop processing. Subclasses should use this to close resources.
 * @get_times: Given a buffer, return the start and stop time when it
 *    should be pushed out. The base class will sync on the clock using
 *    these times.
 * @get_size: Return the total size of the resource, in the format set by
 *     gst_cam_base_src_set_format().
 * @is_seekable: Check if the source can seek
 * @prepare_seek_segment: Prepare the #GstSegment that will be passed to the
 *   #GstCamBaseSrcClass.do_seek() vmethod for executing a seek
 *   request. Sub-classes should override this if they support seeking in
 *   formats other than the configured native format. By default, it tries to
 *   convert the seek arguments to the configured native format and prepare a
 *   segment in that format.
 * @do_seek: Perform seeking on the resource to the indicated segment.
 * @unlock: Unlock any pending access to the resource. Subclasses should unblock
 *    any blocked function ASAP. In particular, any create() function in
 *    progress should be unblocked and should return GST_FLOW_FLUSHING. Any
 *    future #GstCamBaseSrcClass.create() function call should also return
 *    GST_FLOW_FLUSHING until the #GstCamBaseSrcClass.unlock_stop() function has
 *    been called.
 * @unlock_stop: Clear the previous unlock request. Subclasses should clear any
 *    state they set during #GstCamBaseSrcClass.unlock(), such as clearing command
 *    queues.
 * @query: Handle a requested query.
 * @event: Override this to implement custom event handling.
 * @create: Ask the subclass to create a buffer with offset and size.  When the
 *   subclass returns GST_FLOW_OK, it MUST return a buffer of the requested size
 *   unless fewer bytes are available because an EOS condition is near. No
 *   buffer should be returned when the return value is different from
 *   GST_FLOW_OK. A return value of GST_FLOW_EOS signifies that the end of
 *   stream is reached. The default implementation will call
 *   #GstCamBaseSrcClass.alloc() and then call #GstCamBaseSrcClass.fill().
 * @alloc: Ask the subclass to allocate a buffer with for offset and size. The
 *   default implementation will create a new buffer from the negotiated allocator.
 * @fill: Ask the subclass to fill the buffer with data for offset and size. The
 *   passed buffer is guaranteed to hold the requested amount of bytes.
 * @add_video_pad: Add video pad by request, this interface should be called
 *   from subclass
 *
 * Subclasses can override any of the available virtual methods or not, as
 * needed. At the minimum, the @create method should be overridden to produce
 * buffers.
 */
struct _GstCamBaseSrcClass {
  GstElementClass parent_class;

  /*< public >*/
  /* virtual methods for subclasses */

  /* get caps from subclass */
  GstCaps*      (*get_caps)     (GstCamBaseSrc *src, GstCaps *filter);

  /* decide on caps */
  gboolean      (*negotiate)    (GstCamBaseSrc *src, GstPad *pad);

  /* called if, in negotiation, caps need fixating */
  GstCaps *     (*fixate)       (GstCamBaseSrc *src, GstCaps *caps);

  /* notify the subclass of new caps */
  gboolean      (*set_caps)     (GstCamBaseSrc *src, GstPad *pad, GstCaps *caps);

  /* setup allocation query */
  gboolean      (*decide_allocation)   (GstCamBaseSrc *src, GstQuery *query, GstPad *pad);

  /* start and stop processing, ideal for opening/closing the resource */
  gboolean      (*start)        (GstCamBaseSrc *src);
  gboolean      (*stop)         (GstCamBaseSrc *src);

  /* given a buffer, return start and stop time when it should be pushed
   * out. The base class will sync on the clock using these times. */
  void          (*get_times)    (GstCamBaseSrc *src, GstBuffer *buffer,
                                 GstClockTime *start, GstClockTime *end);

  /* get the total size of the resource in the format set by
   * gst_cam_base_src_set_format() */
  gboolean      (*get_size)     (GstCamBaseSrc *src, guint64 *size);

  /* check if the resource is seekable */
  gboolean      (*is_seekable)  (GstCamBaseSrc *src);

  /* Prepare the segment on which to perform do_seek(), converting to the
   * current basesrc format. */
  gboolean      (*prepare_seek_segment) (GstCamBaseSrc *src, GstEvent *seek,
                                         GstSegment *segment);

  /* notify subclasses of a seek */
  gboolean      (*do_seek)      (GstCamBaseSrc *src, GstSegment *segment);

  /* unlock any pending access to the resource. subclasses should unlock
   * any function ASAP. */
  gboolean      (*unlock)       (GstCamBaseSrc *src);

  /* Clear any pending unlock request, as we succeeded in unlocking */
  gboolean      (*unlock_stop)  (GstCamBaseSrc *src);

  /* notify subclasses of a query */
  gboolean      (*query)        (GstCamBaseSrc *src, GstQuery *query);

  /* notify subclasses of an event */
  gboolean      (*event)        (GstCamBaseSrc *src, GstEvent *event);

  /* ask the subclass to create a buffer with offset and size, the default
   * implementation will call alloc and fill. */
  GstFlowReturn (*create)       (GstCamBaseSrc *src, GstPad *pad, guint64 offset, guint size,
                                 GstBuffer **buf);

  /* ask the subclass to allocate an output buffer. The default implementation
   * will use the negotiated allocator. */
  GstFlowReturn (*alloc)        (GstCamBaseSrc *src, GstPad *pad, guint64 offset, guint size,
                                 GstBuffer **buf);

  /* ask the subclass to fill the buffer with data from offset and size */
  GstFlowReturn (*fill)         (GstCamBaseSrc *src, GstPad *pad, guint64 offset, guint size,
                                 GstBuffer *buf);

  /* need to add video pad in the element */
  GstPad *     (*add_video_pad)         (GstCamBaseSrc *src, GstCamBaseSrcClass *klass);

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

struct _GstCamBaseSrcPrivate {
  gboolean discont;
  gboolean flushing;

  GstFlowReturn start_result;
  gboolean async;

  /* if a stream-start event should be sent */
  gboolean stream_start_pending;

  /* if video stream should be started */
  gboolean vid_stream_start_pending;

  /* if segment should be sent and a
   * seqnum if it was originated by a seek */
  gboolean segment_pending;
  guint32 segment_seqnum;

  /* if EOS is pending (atomic) */
  GstEvent *pending_eos;
  gint has_pending_eos;

  /* if the eos was caused by a forced eos from the application */
  gboolean forced_eos;

  /* if the src pad got EOS, request pads should also stop immediately */
  gboolean receive_eos;

  /* startup latency is the time it takes between going to PLAYING and producing
   * the first BUFFER with running_time 0. This value is included in the latency
   * reporting. */
  GstClockTime latency;
  GstClockTime vid_latency;
  /* timestamp offset, this is the offset add to the values of gst_times for
   * pseudo live sources */
  GstClockTimeDiff ts_offset;
  GstClockTimeDiff vid_ts_offset;

  gboolean do_timestamp;
  volatile gint dynamic_size;
  volatile gint automatic_eos;

  /* stream sequence number */
  guint32 seqnum;

  /* pending events (TAG, CUSTOM_BOTH, CUSTOM_DOWNSTREAM) to be
   * pushed in the data stream */
  GList *pending_events;
  volatile gint have_events;

  /* QoS *//* with LOCK */
  gboolean qos_enabled;
  gdouble proportion;
  GstClockTime earliest_time;

  /* buffer pool params of src pad */
  GstBufferPool *pool;
  GstAllocator *allocator;
  GstAllocationParams params;

  /* buffer pool params of video pad */
  GstBufferPool *vid_pool;
  GstAllocator *vid_allocator;
  GstAllocationParams vid_params;

  GCond async_cond;
};

GType gst_cam_base_src_get_type (void);

/* External interfaces*/
GstFlowReturn gst_cam_base_src_wait_playing (GstCamBaseSrc * src);

void gst_cam_base_src_set_live (GstCamBaseSrc * src, gboolean live);
gboolean gst_cam_base_src_is_live (GstCamBaseSrc * src);

void gst_cam_base_src_set_format (GstCamBaseSrc * src, GstFormat format, const char *padname);

void gst_cam_base_src_set_dynamic_size (GstCamBaseSrc * src, gboolean dynamic);

void gst_cam_base_src_set_automatic_eos (GstCamBaseSrc * src, gboolean automatic_eos);

void gst_cam_base_src_set_async (GstCamBaseSrc * src, gboolean async);

gboolean gst_cam_base_src_is_async (GstCamBaseSrc * src);

void gst_cam_base_src_start_complete (GstCamBaseSrc * basesrc, GstFlowReturn ret);

GstFlowReturn gst_cam_base_src_start_wait (GstCamBaseSrc * basesrc);

gboolean gst_cam_base_src_query_latency (GstCamBaseSrc * src, gboolean * live,
    GstClockTime * min_latency, GstClockTime * max_latency);

guint gst_cam_base_src_get_blocksize (GstCamBaseSrc * src);

void gst_cam_base_src_set_blocksize    (GstCamBaseSrc *src, guint blocksize);

void gst_cam_base_src_set_do_timestamp (GstCamBaseSrc *src, gboolean timestamp);

gboolean gst_cam_base_src_get_do_timestamp (GstCamBaseSrc *src);

//new_seamless_segment

gboolean gst_cam_base_src_set_caps (GstCamBaseSrc * src, GstCaps * caps);

GstBufferPool *gst_cam_base_src_get_buffer_pool  (GstCamBaseSrc *src);

void gst_cam_base_src_get_allocator    (GstCamBaseSrc *src,
                                               GstAllocator **allocator,
                                               GstAllocationParams *params);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstCamBaseSrc, gst_object_unref)
#endif

G_END_DECLS
#endif /* __GST_CAM_BASE_SRC_H__ */
