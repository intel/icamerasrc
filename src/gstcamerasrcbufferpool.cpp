/*
 * GStreamer
 * Copyright (C) 2015-2016 Intel Corporation
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

#define LOG_TAG "GstCameraSrcBufferPool"

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include "gst/allocators/gstdmabuf.h"
#include "gst/video/video.h"
#include "gst/video/gstvideometa.h"
#include "gst/video/gstvideopool.h"

#include "ICamera.h"
#include "ScopedAtrace.h"

#include "gstcameradeinterlace.h"
#include "gstcamerasrcbufferpool.h"
#include "gstcamerasrc.h"
#include <iostream>
#include <time.h>

using namespace icamera;

GST_DEBUG_CATEGORY_EXTERN(gst_camerasrc_debug);
#define GST_CAT_DEFAULT gst_camerasrc_debug

GType
gst_camerasrc_meta_api_get_type (void)
{
  PERF_CAMERA_ATRACE();
  static volatile GType type;
  static const gchar *tags[] = { "memory", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstCamerasrcMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_camerasrc_meta_get_info (void)
{
  PERF_CAMERA_ATRACE();
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (gst_camerasrc_meta_api_get_type (), "GstCamerasrcMeta",
        sizeof (GstCamerasrcMeta), (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) NULL, (GstMetaTransformFunction) NULL);
    g_once_init_leave (&meta_info, meta);
  }
  return meta_info;
}

/*
 * GstICGCAMBufferPool:
 */
#define gst_camerasrc_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstCamerasrcBufferPool, gst_camerasrc_buffer_pool, GST_TYPE_BUFFER_POOL);


static gboolean
gst_camerasrc_buffer_pool_set_config (GstBufferPool * bpool, GstStructure * config);

static gboolean gst_camerasrc_buffer_pool_start(GstBufferPool * bpool);
static gboolean gst_camerasrc_buffer_pool_stop(GstBufferPool *bpool);

//static GstFlowReturn gst_camerasrc_bufferpool_qbuf(GstCamerasrcBufferPool *pool, GstBuffer *buf);
//static GstFlowReturn gst_camerasrc_buffer_pool_dqbuf(GstCamerasrcBufferPool *pool,GstBuffer *buf);
static GstFlowReturn
gst_camerasrc_buffer_pool_alloc_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params);
static void
gst_camerasrc_buffer_pool_release_buffer (GstBufferPool * bpool, GstBuffer * buffer);
static GstFlowReturn
gst_camerasrc_buffer_pool_acquire_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params);
static void gst_camerasrc_buffer_pool_free_buffer (GstBufferPool * bpool, GstBuffer * buffer);


static void gst_camerasrc_buffer_pool_finalize (GObject * object)
{
  PERF_CAMERA_ATRACE();
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void gst_camerasrc_buffer_pool_init (GstCamerasrcBufferPool * pool)
{
}

static void gst_camerasrc_buffer_pool_class_init(GstCamerasrcBufferPoolClass *klass)
{
  PERF_CAMERA_ATRACE();
   GObjectClass *object_class = G_OBJECT_CLASS (klass);
   GstBufferPoolClass *bufferpool_class = GST_BUFFER_POOL_CLASS (klass);

   object_class->finalize = gst_camerasrc_buffer_pool_finalize;

   bufferpool_class->start = gst_camerasrc_buffer_pool_start;
   bufferpool_class->stop = gst_camerasrc_buffer_pool_stop;
   bufferpool_class->set_config = gst_camerasrc_buffer_pool_set_config;
   bufferpool_class->alloc_buffer = gst_camerasrc_buffer_pool_alloc_buffer;
   bufferpool_class->acquire_buffer = gst_camerasrc_buffer_pool_acquire_buffer;
   bufferpool_class->release_buffer = gst_camerasrc_buffer_pool_release_buffer;
   bufferpool_class->free_buffer = gst_camerasrc_buffer_pool_free_buffer;

}

static gboolean
gst_camerasrc_buffer_pool_set_config (GstBufferPool * bpool, GstStructure * config)
{
  PERF_CAMERA_ATRACE();
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL(bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstCaps *caps;
  guint size, min_buffers, max_buffers;

  /* parse the config and keep around */
  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
        &max_buffers)) {
    return FALSE;
  }

  /*TODO Get the allocator and params from config */
  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params)) {
    return FALSE;
  }

  pool->allocator = allocator;
  pool->params = params;
  pool->size = size;
  pool->number_of_buffers = camerasrc->number_of_buffers;

  if (camerasrc->number_of_buffers < min_buffers)
    pool->number_of_buffers = min_buffers;
  if (camerasrc->number_of_buffers > max_buffers)
    pool->number_of_buffers = max_buffers;

  GST_DEBUG_OBJECT(camerasrc, "size %d min_buffers %d max_buffers %d\n", size, min_buffers, max_buffers);
  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (bpool, config);
}

static gboolean
gst_camerasrc_buffer_pool_start (GstBufferPool * bpool)
{
  PERF_CAMERA_ATRACE();
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL(bpool);
  Gstcamerasrc *camerasrc = pool->src;

  if (camerasrc->print_fps){
    camerasrc->fps_debug.buf_count = 0;
    camerasrc->fps_debug.last_buf_count = 0;
    camerasrc->fps_debug.sum_time = 0;
    camerasrc->fps_debug.tm_interval = 0;
    camerasrc->fps_debug.max_fps = 0;
    camerasrc->fps_debug.min_fps = 0;
    camerasrc->fps_debug.av_fps = 0;
    camerasrc->fps_debug.init_max_min_fps = true;
  }

  pool->buffers = g_new0 (GstBuffer *, pool->number_of_buffers);
  GST_DEBUG_OBJECT(camerasrc, "start pool %p pool->buffers %p %d\n", pool, pool->buffers, pool->number_of_buffers);

  pool->number_allocated = 0;
  pool->acquire_buffer_index = 0;

  if (camerasrc->downstream_pool) {
    if (!gst_buffer_pool_set_active(camerasrc->downstream_pool, TRUE)) {
      GST_ERROR_OBJECT(camerasrc, "failed to active the other pool %" GST_PTR_FORMAT, camerasrc->downstream_pool);
      return FALSE;
    }
  }
  /* now, allocate the buffers: */
  if (!GST_BUFFER_POOL_CLASS (parent_class)->start (bpool)) {
    return FALSE;
  }

  camera_device_start(camerasrc->device_id);
  return TRUE;
}

static gboolean gst_camerasrc_buffer_pool_stop(GstBufferPool *bpool)
{
  PERF_CAMERA_ATRACE();
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL(bpool);
  Gstcamerasrc *camerasrc = pool->src;
  camerasrc->fps_debug.av_fps = (camerasrc->fps_debug.buf_count-FPS_BUF_COUNT_START)/(camerasrc->fps_debug.sum_time/1000000);

  if (camerasrc->print_fps) {
     if (camerasrc->fps_debug.buf_count <= FPS_BUF_COUNT_START) {
        g_print("num-buffers value is too low, should be at least %d\n\n",FPS_BUF_COUNT_START);
        camerasrc->print_fps=false;
     } else if (camerasrc->fps_debug.max_fps == 0 || camerasrc->fps_debug.min_fps == 0) {
        g_print("Average fps is:%.4f\n\n",camerasrc->fps_debug.av_fps);
        camerasrc->print_fps=false;
     } else {
        g_print("\nMax fps is:%.4f,Minimum fps is:%.4f,Average fps is:%.4f\n\n",
                     camerasrc->fps_debug.max_fps,
                     camerasrc->fps_debug.min_fps,
                     camerasrc->fps_debug.av_fps);
        camerasrc->print_fps=false;
     }
  }

  gboolean ret;
  ret = GST_BUFFER_POOL_CLASS(parent_class)->stop(bpool);
  return ret;
}

static GstVideoFormat gst_camerasrc_get_video_format(int format)
{
  GstVideoFormat videoFmt = GST_VIDEO_FORMAT_UNKNOWN;
  switch (format) {
    case V4L2_PIX_FMT_NV12:
      videoFmt = GST_VIDEO_FORMAT_NV12;
      break;
    case V4L2_PIX_FMT_YUYV:
      videoFmt = GST_VIDEO_FORMAT_YUY2;
      break;
    case V4L2_PIX_FMT_UYVY:
      videoFmt = GST_VIDEO_FORMAT_UYVY;
      break;
    case V4L2_PIX_FMT_XRGB32:
      videoFmt = GST_VIDEO_FORMAT_RGBx;
      break;
    case V4L2_PIX_FMT_BGR24:
      videoFmt = GST_VIDEO_FORMAT_BGR;
      break;
    case V4L2_PIX_FMT_XBGR32:
      videoFmt = GST_VIDEO_FORMAT_BGRx;
      break;
    default:
      g_print("not support this format: %d\n", format);
      break;
  }

  return videoFmt;
}

static void gst_camerasrc_set_meta(Gstcamerasrc *camerasrc, GstBuffer *alloc_buffer)
{
  gsize offset[GST_VIDEO_MAX_PLANES];
  gint n_planes, i, offs, stride[GST_VIDEO_MAX_PLANES];
  GstVideoFrameFlags videoFlags = GST_VIDEO_FRAME_FLAG_NONE;
  GstVideoFormat videoFmt = gst_camerasrc_get_video_format(camerasrc->streams[0].format);
  memset(offset,0,sizeof(offset));
  memset(stride,0,sizeof(stride));

  offs = 0;
  n_planes = GST_VIDEO_INFO_N_PLANES (&camerasrc->info);
  for (i = 0; i < n_planes; i++) {
      offset[i] = offs;
      stride[i] = camerasrc->bpl;
      offs = stride[i] * camerasrc->streams[0].height;
  }
  if (camerasrc->interlace_mode)
      videoFlags = GST_VIDEO_FRAME_FLAG_INTERLACED;
  GST_DEBUG_OBJECT(camerasrc, "set Buffer meta: videoFlage: %d, videofmt: %d, width: %d, \
          heigh: %d, n_planes: %d, offset: %d, stride: %d\n", videoFlags, videoFmt,
          camerasrc->streams[0].width,camerasrc->streams[0].height, n_planes, (int)offset[0], (int)stride[0]);

  gst_buffer_add_video_meta_full(alloc_buffer, videoFlags, videoFmt,
          camerasrc->streams[0].width, camerasrc->streams[0].height, n_planes, offset, stride);
}

static GstFlowReturn
gst_camerasrc_buffer_pool_alloc_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  PERF_CAMERA_ATRACE();
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL(bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GstBuffer *alloc_buffer = NULL;
  GstCamerasrcMeta *meta;
  GstMemory *mem = NULL;
  int ret;

  switch (camerasrc->io_mode) {
    case GST_CAMERASRC_IO_MODE_USERPTR:
      alloc_buffer = gst_buffer_new();
      meta = GST_CAMERASRC_META_ADD(alloc_buffer);

      meta->buffer = (camera_buffer_t *)calloc(1, sizeof(camera_buffer_t));
      if (meta->buffer == NULL) {
        return GST_FLOW_ERROR;
      }
      meta->buffer->length = pool->size;
      meta->buffer->conf = &camerasrc->streams[0];
      ret = posix_memalign(&meta->buffer->addr, getpagesize(), pool->size);
      if (ret < 0) {
        return GST_FLOW_ERROR;
      }
      meta->mem = meta->buffer->addr;
      meta->buffer->memType = V4L2_MEMORY_USERPTR;

      gst_buffer_append_memory (alloc_buffer, gst_memory_new_wrapped (GST_MEMORY_FLAG_NO_SHARE, meta->mem, pool->size, 0, pool->size, NULL, NULL));
    break;

    case GST_CAMERASRC_IO_MODE_DMA_IMPORT:
      ret = gst_buffer_pool_acquire_buffer(camerasrc->downstream_pool, &alloc_buffer, NULL);
      if (ret != GST_FLOW_OK) {
        GST_ERROR_OBJECT(camerasrc, "failed to acquire buffer from downstream pool");
        goto err_acquire_buffer;
      }
      meta = GST_CAMERASRC_META_ADD(alloc_buffer);

      mem = gst_buffer_peek_memory(alloc_buffer, 0);
      meta->buffer = (camera_buffer_t *)calloc(1, sizeof(camera_buffer_t));
      if (meta->buffer == NULL) {
        return GST_FLOW_ERROR;
      }
      meta->buffer->dmafd = dup(gst_dmabuf_memory_get_fd(mem));
      meta->buffer->memType = V4L2_MEMORY_DMABUF;

    break;
    default:
      GST_ERROR_OBJECT(camerasrc, "io-mode %d is not supported yet.", camerasrc->io_mode);
    goto err_io_mode;
  }

  meta->buffer->length = pool->size;
  meta->buffer->conf = &camerasrc->streams[0];
  meta->index = pool->number_allocated;
  pool->buffers[meta->index] = alloc_buffer;
  pool->number_allocated++;

  //need to set meta to allocated buffer.
  gst_camerasrc_set_meta(camerasrc, alloc_buffer);
  *buffer = alloc_buffer;
  GST_DEBUG_OBJECT(camerasrc, "alloc_buffer buffer %p\n", *buffer);

  return GST_FLOW_OK;

err_acquire_buffer:
err_io_mode:
  return GST_FLOW_ERROR;
}

static void gst_camerasrc_buffer_pool_free_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  PERF_CAMERA_ATRACE();
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL (bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GstCamerasrcMeta *meta;

  meta = GST_CAMERASRC_META_GET(buffer);
  free(meta->buffer->addr);
  free(meta->buffer);
  pool->buffers[meta->index] = NULL;
  GST_DEBUG_OBJECT(camerasrc, "free_buffer buffer %p\n", buffer);
  gst_buffer_unref (buffer);

}

static GstFlowReturn
gst_camerasrc_buffer_pool_acquire_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  PERF_CAMERA_ATRACE();
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL(bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GstBuffer *gbuffer;
  GstCamerasrcMeta *meta;
  int ret;

  gbuffer = pool->buffers[pool->acquire_buffer_index%pool->number_allocated];
  meta = GST_CAMERASRC_META_GET(gbuffer);

  if (camerasrc->print_fps) {
     camerasrc->fps_debug.buf_count++;
     if (camerasrc->fps_debug.buf_count == FPS_BUF_COUNT_START) {
        gettimeofday(&camerasrc->fps_debug.dqbuf_start_tm_count,NULL);
        camerasrc->fps_debug.dqbuf_tm_start = camerasrc->fps_debug.dqbuf_start_tm_count;
        camerasrc->fps_debug.last_buf_count = camerasrc->fps_debug.buf_count;
      } else if (camerasrc->fps_debug.buf_count > FPS_BUF_COUNT_START) {
        gettimeofday(&camerasrc->fps_debug.qbuf_tm_end,NULL);
        double div = (camerasrc->fps_debug.qbuf_tm_end.tv_sec-camerasrc->fps_debug.dqbuf_tm_start.tv_sec)*1000000+camerasrc->fps_debug.qbuf_tm_end.tv_usec-camerasrc->fps_debug.dqbuf_tm_start.tv_usec;
        camerasrc->fps_debug.sum_time += div;
        camerasrc->fps_debug.tm_interval += div;
        camerasrc->fps_debug.dqbuf_tm_start = camerasrc->fps_debug.qbuf_tm_end;

        if (camerasrc->fps_debug.tm_interval >= (FPS_TIME_INTERVAL*1000000)) {
          double interval_fps = (camerasrc->fps_debug.buf_count-camerasrc->fps_debug.last_buf_count)/(camerasrc->fps_debug.tm_interval/1000000);
          g_print("fps:%.4f\n",interval_fps);

          if (camerasrc->fps_debug.init_max_min_fps) {
             camerasrc->fps_debug.max_fps = interval_fps;
             camerasrc->fps_debug.min_fps = interval_fps;
             camerasrc->fps_debug.init_max_min_fps = false;
          }

          if (interval_fps >= camerasrc->fps_debug.max_fps) {
             camerasrc->fps_debug.max_fps = interval_fps;
          } else if (interval_fps < camerasrc->fps_debug.min_fps) {
             camerasrc->fps_debug.min_fps = interval_fps;
          }

          camerasrc->fps_debug.tm_interval = 0;
          camerasrc->fps_debug.last_buf_count = camerasrc->fps_debug.buf_count;
         }
      }
  }
  ret = camera_stream_dqbuf(camerasrc->device_id, camerasrc->stream_id, &meta->buffer);
  if (ret != 0) {
    GST_ERROR_OBJECT(camerasrc, "dqbuf failed ret %d.\n", ret);
    return GST_FLOW_ERROR;
  }

  if ((camerasrc->interlace_mode == true) && camerasrc->deinterlace_method != GST_CAMERASRC_DEINTERLACE_METHOD_NONE) {
    ret = gst_camerasrc_deinterlace_frame(camerasrc, meta->buffer);
    if (ret != 0) {
      GST_ERROR_OBJECT(camerasrc, "deinterlace frame failed ret %d.\n", ret);
      return GST_FLOW_ERROR;
    }
  } else if (camerasrc->interlace_mode == true) {
    if ((meta->buffer->interlaced_field == CAMERA_INTERLACED_FIELD_TOP) || (meta->buffer->interlaced_field == CAMERA_INTERLACED_FIELD_BOTTOM)) {
      GST_BUFFER_FLAG_SET (gbuffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
      GST_BUFFER_FLAG_SET (gbuffer, GST_VIDEO_BUFFER_FLAG_ONEFIELD);
    }
  }

  *buffer = gbuffer;
  pool->acquire_buffer_index++;
  GST_DEBUG_OBJECT(camerasrc, "acquire_buffer buffer %p\n", *buffer);
  {
    PERF_CAMERA_ATRACE_PARAM1("sof.sequence", meta->buffer->sequence);
  }
  return GST_FLOW_OK;
}

static void
gst_camerasrc_buffer_pool_release_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  PERF_CAMERA_ATRACE();
  GstCamerasrcMeta *meta;
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL (bpool);
  Gstcamerasrc *camerasrc = pool->src;
  int ret = 0;

  meta = GST_CAMERASRC_META_GET(buffer);

  ret = camera_stream_qbuf(camerasrc->device_id, camerasrc->stream_id, meta->buffer);

  if (ret < 0) {
    GST_ERROR_OBJECT(camerasrc, "failed to qbuf back to stream.");
    return ;
  }
  GST_DEBUG_OBJECT(camerasrc, "release_buffer buffer %p\n", buffer);
  {
    PERF_CAMERA_ATRACE_PARAM1("sof.sequence", meta->buffer->sequence);
  }
}

#if 0
static GstFlowReturn
gst_camerasrc_buffer_pool_qbuf (GstCamerasrcBufferPool * pool, GstBuffer * buf)
{
   return GST_FLOW_OK;

}

static GstFlowReturn gst_camerasrc_buffer_pool_dqbuf(GstCamerasrcBufferPool *pool,GstBuffer *buf)
{
   return GST_FLOW_OK;
}
#endif

static int gst_camerasrc_stride_to_bpl(int format, int stride)
{
  int bpp, bpl = 0;
  switch (format) {
    case V4L2_PIX_FMT_SRGGB8:
    case V4L2_PIX_FMT_SGRBG8:
    case V4L2_PIX_FMT_SGBRG8:
    case V4L2_PIX_FMT_SBGGR8:
      bpp = 8;
      break;
    case V4L2_PIX_FMT_NV12:
      bpp = 12;
      break;
    case V4L2_PIX_FMT_SRGGB10:
    case V4L2_PIX_FMT_SGRBG10:
    case V4L2_PIX_FMT_SGBRG10:
    case V4L2_PIX_FMT_SBGGR10:
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_UYVY:
    default:
      bpp = 16;
      break;
  }
  bpl = (stride * bpp) / 8;

  return bpl;
}

GstBufferPool *gst_camerasrc_buffer_pool_new (Gstcamerasrc *camerasrc, GstCaps *caps)
{
  PERF_CAMERA_ATRACE();
  GstCamerasrcBufferPool *pool;
  GstStructure *s;

  camera_frame_info_t frame_info;
  frame_info.format = camerasrc->streams[0].format;
  frame_info.width = camerasrc->streams[0].width;
  frame_info.height = camerasrc->streams[0].height;
  frame_info.interlaced_video = camerasrc->interlace_mode ? INTERLACED_ENABLED : INTERLACED_DISABLED;

  int ret = query_frame_info(camerasrc->device_id, frame_info);
  if (ret != 0) {
    GST_ERROR_OBJECT(camerasrc, "failed to query frame info for format %d %dx%d"
       ,camerasrc->streams[0].format, camerasrc->streams[0].width, camerasrc->streams[0].height);
    return NULL;
  }

  pool = (GstCamerasrcBufferPool *) g_object_new (GST_TYPE_CAMERASRC_BUFFER_POOL, NULL);

  /* if interlace_mode and deinterlace_method are set both, camerasrc will do internal deinterlace, so the buffer is double. */
  if ((camerasrc->interlace_mode == true) && camerasrc->deinterlace_method != GST_CAMERASRC_DEINTERLACE_METHOD_NONE) {
    frame_info.size *= 2;
  }

  camerasrc->bpl = gst_camerasrc_stride_to_bpl(camerasrc->streams[0].format, frame_info.stride);
  pool->src = camerasrc;
  camerasrc->pool = GST_BUFFER_POOL(pool);

  s = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));
  gst_buffer_pool_config_set_params (s, caps, frame_info.size, MIN_PROP_BUFFERCOUNT, MAX_PROP_BUFFERCOUNT);
  gst_buffer_pool_set_config (GST_BUFFER_POOL_CAST (pool), s);

  return GST_BUFFER_POOL (pool);
}
