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

static gboolean gst_camerasrc_buffer_pool_set_config (GstBufferPool * bpool, GstStructure * config);
static gboolean gst_camerasrc_buffer_pool_start(GstBufferPool * bpool);
static gboolean gst_camerasrc_buffer_pool_stop(GstBufferPool *bpool);
static GstFlowReturn gst_camerasrc_buffer_pool_alloc_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params);
static void gst_camerasrc_buffer_pool_release_buffer (GstBufferPool * bpool, GstBuffer * buffer);
static GstFlowReturn gst_camerasrc_buffer_pool_acquire_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params);
static void gst_camerasrc_buffer_pool_free_buffer (GstBufferPool * bpool, GstBuffer * buffer);
static void gst_camerasrc_free_weave_buffer (Gstcamerasrc *src);

static void
gst_camerasrc_buffer_pool_finalize (GObject * object)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_camerasrc_buffer_pool_init (GstCamerasrcBufferPool * pool)
{
}

static void
gst_camerasrc_buffer_pool_class_init(GstCamerasrcBufferPoolClass *klass)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
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
  GST_INFO("@%s\n",__func__);
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL(bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstCaps *caps;
  guint size, min_buffers, max_buffers;

  // parse the config and keep around
  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
        &max_buffers)) {
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params)) {
    return FALSE;
  }

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  if (camerasrc->io_mode == GST_CAMERASRC_IO_MODE_DMA_EXPORT) {
    pool->allocator = gst_dmabuf_allocator_new ();
  } else {
    pool->allocator = allocator;
  }

  pool->params = params;
  pool->size = size;
  pool->number_of_buffers = camerasrc->number_of_buffers;

  GST_DEBUG_OBJECT(camerasrc, "size %d min_buffers %d max_buffers %d\n", size, min_buffers, max_buffers);
  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (bpool, config);
}

static gboolean
gst_camerasrc_buffer_pool_start (GstBufferPool * bpool)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
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

  camerasrc->first_frame = true;

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

static gboolean
gst_camerasrc_buffer_pool_stop(GstBufferPool *bpool)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL(bpool);
  Gstcamerasrc *camerasrc = pool->src;
  gboolean ret;

  /* Calculate max/min/average fps */
  if (camerasrc->print_fps) {
     camerasrc->fps_debug.av_fps = (camerasrc->fps_debug.buf_count-FPS_BUF_COUNT_START)/(camerasrc->fps_debug.sum_time/1000000);
     if (camerasrc->fps_debug.buf_count <= FPS_BUF_COUNT_START) {
        g_print("\nCamera name:%s(Id:%d)\nnum-buffers value is too low, should be at least %d\n",
                      camerasrc->cam_info_name,
                      camerasrc->device_id,
                      FPS_BUF_COUNT_START);
     } else if (camerasrc->fps_debug.max_fps == 0 || camerasrc->fps_debug.min_fps == 0) {
        // This means that pipeline runtime is less than 2 seconds,no update of max_fps and min_fps
        g_print("\nCamera name:%s(Id:%d)\nAverage fps is:%.4f\n",
                      camerasrc->cam_info_name,
                      camerasrc->device_id,
                      camerasrc->fps_debug.av_fps);
     } else {
        g_print("\nTotal frame is:%g  Camera name:%s(Id:%d)\n",
                     camerasrc->fps_debug.buf_count,
                     camerasrc->cam_info_name,
                     camerasrc->device_id);
        g_print("Max fps is:%.4f,Minimum fps is:%.4f,Average fps is:%.4f\n\n",
                     camerasrc->fps_debug.max_fps,
                     camerasrc->fps_debug.min_fps,
                     camerasrc->fps_debug.av_fps);
     }
     camerasrc->print_fps=false;
  }

  if (camerasrc->camera_open) {
    camera_device_stop(camerasrc->device_id);
    camera_device_close(camerasrc->device_id);
    camerasrc->stream_id = -1;

    if (camerasrc->downstream_pool)
      gst_object_unref(camerasrc->downstream_pool);

    camerasrc->camera_open = false;
  }

  if (pool->allocator)
    gst_object_unref(pool->allocator);

  /* free buffers in the queue */
  ret = GST_BUFFER_POOL_CLASS(parent_class)->stop(bpool);

  /* free topfield buffer and bottomfield buffer, both for temp storage */
  if (camerasrc->interlace_field == GST_CAMERASRC_INTERLACE_FIELD_ALTERNATE &&
        camerasrc->deinterlace_method == GST_CAMERASRC_DEINTERLACE_METHOD_SOFTWARE_WEAVE)
    gst_camerasrc_free_weave_buffer(camerasrc);

  /* free the remaining buffers */
  for (int n = 0; n < pool->number_allocated; n++)
    gst_camerasrc_buffer_pool_free_buffer (bpool, pool->buffers[n]);

  pool->number_allocated = 0;
  g_free(pool->buffers);
  pool->buffers = NULL;
  return ret;
}

static GstVideoFormat
gst_camerasrc_get_video_format(int format)
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
    case V4L2_PIX_FMT_RGB565:
      videoFmt = GST_VIDEO_FORMAT_RGB16;
      break;
    case V4L2_PIX_FMT_NV16:
      videoFmt = GST_VIDEO_FORMAT_NV16;
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

static void
gst_camerasrc_set_meta(Gstcamerasrc *camerasrc, GstBuffer *alloc_buffer)
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

  if (camerasrc->interlace_field == GST_CAMERASRC_INTERLACE_FIELD_ALTERNATE)
      videoFlags = GST_VIDEO_FRAME_FLAG_INTERLACED;

  GST_DEBUG_OBJECT(camerasrc, "set Buffer meta: videoFlage: %d, videofmt: %d, width: %d, \
          heigh: %d, n_planes: %d, offset: %d, stride: %d\n", videoFlags, videoFmt,
          camerasrc->streams[0].width,camerasrc->streams[0].height, n_planes, (int)offset[0], (int)stride[0]);

  /* add metadata to raw video buffers */
  gst_buffer_add_video_meta_full(alloc_buffer, videoFlags, videoFmt,
          camerasrc->streams[0].width, camerasrc->streams[0].height, n_planes, offset, stride);
}

static int
gst_camerasrc_alloc_weave_buffer(Gstcamerasrc *camerasrc, gint size)
{
  int ret = 0;

  if (camerasrc->top == NULL && camerasrc->bottom == NULL &&
        camerasrc->interlace_field == GST_CAMERASRC_INTERLACE_FIELD_ALTERNATE &&
        camerasrc->deinterlace_method == GST_CAMERASRC_DEINTERLACE_METHOD_SOFTWARE_WEAVE) {
    camerasrc->top= (camera_buffer_t *)calloc(1, sizeof(camera_buffer_t));
    camerasrc->bottom = (camera_buffer_t *)calloc(1, sizeof(camera_buffer_t));
    ret = posix_memalign(&camerasrc->top->addr, getpagesize(), size);
    if (ret < 0) {
      GST_ERROR_OBJECT(camerasrc, "failed to alloc topfield buffer for weave method");
      return GST_FLOW_ERROR;
    }
    ret = posix_memalign(&camerasrc->bottom->addr, getpagesize(), size);
    if (ret < 0) {
      GST_ERROR_OBJECT(camerasrc, "failed to alloc bottomfield buffer for weave method");
      return GST_FLOW_ERROR;
    }
  }

  return ret;
}

static GstFlowReturn
gst_camerasrc_buffer_pool_alloc_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL(bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GstBuffer *alloc_buffer = NULL;
  GstCamerasrcMeta *meta;
  GstMemory *mem = NULL;
  int ret = 0;

  switch (camerasrc->io_mode) {
    case GST_CAMERASRC_IO_MODE_MMAP:
    case GST_CAMERASRC_IO_MODE_USERPTR:
      alloc_buffer = gst_buffer_new();
      meta = GST_CAMERASRC_META_ADD(alloc_buffer);
      meta->buffer = (camera_buffer_t *)calloc(1, sizeof(camera_buffer_t));
      if (meta->buffer == NULL)
        return GST_FLOW_ERROR;

      /* Allocate temp buffers: top(only top field), bottom(only bottom field)
       * in order to do deinterlace weaving and fill in metadata */
      ret = gst_camerasrc_alloc_weave_buffer(camerasrc, pool->size);
      if (ret < 0) {
        GST_ERROR_OBJECT(camerasrc, "failed to alloc buffer io-mode %d", camerasrc->io_mode);
        return GST_FLOW_ERROR;
      }

      meta->buffer->s = camerasrc->streams[0];

      if (camerasrc->io_mode == GST_CAMERASRC_IO_MODE_MMAP) {
        meta->buffer->s.memType = V4L2_MEMORY_MMAP;
        meta->buffer->flags = 0;
        ret = camera_device_allocate_memory(camerasrc->device_id, meta->buffer);
      } else {
        meta->buffer->s.memType = V4L2_MEMORY_USERPTR;
        ret = posix_memalign(&meta->buffer->addr, getpagesize(), pool->size);
      }

      if (ret < 0) {
          GST_ERROR_OBJECT(camerasrc, "failed to alloc buffer io-mode %d", camerasrc->io_mode);
          return GST_FLOW_ERROR;
      }

      meta->mem = meta->buffer->addr;
      gst_buffer_append_memory (alloc_buffer, gst_memory_new_wrapped (GST_MEMORY_FLAG_NO_SHARE, meta->mem, pool->size, 0, pool->size, NULL, NULL));
      break;
    case GST_CAMERASRC_IO_MODE_DMA_EXPORT:
      int dmafd;
      alloc_buffer = gst_buffer_new();
      meta = GST_CAMERASRC_META_ADD(alloc_buffer);
      meta->buffer = (camera_buffer_t *)calloc(1, sizeof(camera_buffer_t));
      if (meta->buffer == NULL) {
        return GST_FLOW_ERROR;
      }

      meta->buffer->s.memType = V4L2_MEMORY_MMAP;
      meta->buffer->flags = BUFFER_FLAG_DMA_EXPORT;
      ret = camera_device_allocate_memory(camerasrc->device_id, meta->buffer);
      if (ret < 0) {
        GST_ERROR_OBJECT(camerasrc, "failed to alloc dma buffer from libcamhal. io-mode %d", camerasrc->io_mode);
        return GST_FLOW_ERROR;
      }

      if ((dmafd = dup (meta->buffer->dmafd)) < 0) {
        return GST_FLOW_ERROR;
      }

      mem = gst_dmabuf_allocator_alloc (pool->allocator, dmafd, pool->size);
      gst_buffer_append_memory (alloc_buffer, mem);
      break;
    case GST_CAMERASRC_IO_MODE_DMA_IMPORT:
      ret = gst_buffer_pool_acquire_buffer(camerasrc->downstream_pool, &alloc_buffer, NULL);
      if (ret != GST_FLOW_OK) {
        GST_ERROR_OBJECT(camerasrc, "failed to acquire buffer from downstream buffer pool");
        goto err_acquire_buffer;
      }
      meta = GST_CAMERASRC_META_ADD(alloc_buffer);

      mem = gst_buffer_peek_memory(alloc_buffer, 0);
      meta->buffer = (camera_buffer_t *)calloc(1, sizeof(camera_buffer_t));
      if (meta->buffer == NULL) {
        return GST_FLOW_ERROR;
      }

      meta->buffer->dmafd = dup(gst_dmabuf_memory_get_fd(mem));
      meta->buffer->s.memType = V4L2_MEMORY_DMABUF;
      meta->buffer->flags = 0;
      break;
    default:
      GST_ERROR_OBJECT(camerasrc, "Cannot find corresponding io-mode in %s, please verify if this mode is valid.",__FUNCTION__);
      goto err_io_mode;
  }

  meta->buffer->s = camerasrc->streams[0];
  meta->index = pool->number_allocated;
  pool->buffers[meta->index] = alloc_buffer;
  pool->number_allocated++;

  //need to set meta to allocated buffer.
  gst_camerasrc_set_meta(camerasrc, alloc_buffer);
  *buffer = alloc_buffer;
  GST_DEBUG_OBJECT(camerasrc, "alloc_buffer buffer %p\n", *buffer);

  return GST_FLOW_OK;

err_acquire_buffer:
  {
    gst_buffer_unref (alloc_buffer);
    return GST_FLOW_ERROR;
  }
err_io_mode:
  {
    alloc_buffer = NULL;
    g_assert_not_reached ();
    return GST_FLOW_ERROR;
  }
}

static void
gst_camerasrc_free_weave_buffer (Gstcamerasrc *src)
{
  if (src->top->addr)
    free(src->top->addr);

  if (src->bottom->addr)
    free(src->bottom->addr);

  free(src->top);
  free(src->bottom);
  src->top = NULL;
  src->bottom = NULL;

  src->deinterlace_method = DEFAULT_DEINTERLACE_METHOD;
}

static void
gst_camerasrc_buffer_pool_free_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL (bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GstCamerasrcMeta *meta;

  meta = GST_CAMERASRC_META_GET(buffer);

  switch (camerasrc->io_mode) {
    case GST_CAMERASRC_IO_MODE_USERPTR:
      if (meta->buffer->addr)
        free(meta->buffer->addr);
      break;
    case GST_CAMERASRC_IO_MODE_DMA_EXPORT:
      if (meta->buffer->dmafd)
        close(meta->buffer->dmafd);
      break;
    default:
      break;
  }

  free(meta->buffer);
  pool->buffers[meta->index] = NULL;
  GST_DEBUG_OBJECT(camerasrc, "free_buffer buffer %p\n", buffer);
  gst_buffer_unref (buffer);
}

/**
  *Use system time to update fps when dqbuf for every 2 seconds
  */
void gst_camerasrc_update_fps(Gstcamerasrc *camerasrc)
{
  PERF_CAMERA_ATRACE();
  camerasrc->fps_debug.buf_count++;
  if (camerasrc->fps_debug.buf_count == FPS_BUF_COUNT_START) {
      gettimeofday(&camerasrc->fps_debug.dqbuf_start_tm_count,NULL);
      camerasrc->fps_debug.dqbuf_tm_start = camerasrc->fps_debug.dqbuf_start_tm_count;
      camerasrc->fps_debug.last_buf_count = camerasrc->fps_debug.buf_count;
  } else if (camerasrc->fps_debug.buf_count > FPS_BUF_COUNT_START) {
      gettimeofday(&camerasrc->fps_debug.qbuf_tm_end,NULL);
      double duration = (camerasrc->fps_debug.qbuf_tm_end.tv_sec-camerasrc->fps_debug.dqbuf_tm_start.tv_sec)*1000000+ \
                         camerasrc->fps_debug.qbuf_tm_end.tv_usec-camerasrc->fps_debug.dqbuf_tm_start.tv_usec;
      //calculate pipeline runtime
      camerasrc->fps_debug.sum_time += duration;
      camerasrc->fps_debug.tm_interval += duration;
      camerasrc->fps_debug.dqbuf_tm_start = camerasrc->fps_debug.qbuf_tm_end;

      if (camerasrc->fps_debug.tm_interval >= FPS_TIME_INTERVAL) {
          double interval_fps = (camerasrc->fps_debug.buf_count-camerasrc->fps_debug.last_buf_count)/ \
                                (camerasrc->fps_debug.tm_interval/1000000);
          g_print("fps:%.4f   Camera name: %s\n",interval_fps, camerasrc->cam_info_name);

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

/**
 * Dequeue a buffer from a stream
 */
static GstFlowReturn
gst_camerasrc_buffer_pool_acquire_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL(bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GstBuffer *gbuffer;
  GstCamerasrcMeta *meta;
  GstClockTime timestamp;
  int ret;

  gbuffer = pool->buffers[pool->acquire_buffer_index%pool->number_allocated];
  meta = GST_CAMERASRC_META_GET(gbuffer);

  if (camerasrc->print_fps)
      gst_camerasrc_update_fps(camerasrc);

  ret = camera_stream_dqbuf(camerasrc->device_id, camerasrc->stream_id, &meta->buffer);
  if (ret != 0) {
    GST_ERROR_OBJECT(camerasrc, "dqbuf failed ret %d.\n", ret);
    return GST_FLOW_ERROR;
  }

  timestamp = meta->buffer->timestamp;
  camerasrc->time_end = meta->buffer->timestamp;

  switch(meta->buffer->s.field) {
    case V4L2_FIELD_ANY:
        break;
    case V4L2_FIELD_TOP:
        GST_BUFFER_FLAG_SET (gbuffer, GST_VIDEO_BUFFER_FLAG_TFF);

        gst_camerasrc_copy_field(camerasrc, meta->buffer, camerasrc->top);
        if (camerasrc->first_frame) {
          gst_camerasrc_copy_field(camerasrc, meta->buffer, camerasrc->bottom);
        }
        break;
    case V4L2_FIELD_BOTTOM:
        GST_BUFFER_FLAG_UNSET (gbuffer, GST_VIDEO_BUFFER_FLAG_TFF);
        GST_BUFFER_FLAG_SET (gbuffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);

        gst_camerasrc_copy_field(camerasrc, meta->buffer, camerasrc->bottom);
        if (camerasrc->first_frame) {
          gst_camerasrc_copy_field(camerasrc, meta->buffer, camerasrc->top);
        }

        break;
    default:
        GST_BUFFER_FLAG_UNSET (gbuffer, GST_VIDEO_BUFFER_FLAG_TFF);
        GST_BUFFER_FLAG_UNSET (gbuffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
        break;
  }

  ret = gst_camerasrc_deinterlace_frame(camerasrc, meta->buffer);
  if (ret != 0) {
    GST_ERROR_OBJECT(camerasrc, "deinterlace frame failed ret %d.\n", ret);
    return GST_FLOW_ERROR;
  }

  camerasrc->first_frame = false;

  GST_BUFFER_TIMESTAMP(gbuffer) = timestamp;
  *buffer = gbuffer;
  pool->acquire_buffer_index++;
  GST_DEBUG_OBJECT(camerasrc, "acquire_buffer buffer %p\n", *buffer);
  {
    PERF_CAMERA_ATRACE_PARAM1("sof.sequence", meta->buffer->sequence);
  }

  return GST_FLOW_OK;
}

/**
 * Queue a buffer from a stream
 */
static void
gst_camerasrc_buffer_pool_release_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
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

static int
gst_camerasrc_get_bpl(int format, int bytes_per_line, int bpp)
{
  /* Check if is a planar format */
  if (gst_camerasrc_isPlanarFormat(format))
    return ALIGN_64(bytes_per_line)/(bpp/8);
  else
    return ALIGN_64(bytes_per_line);
}

GstBufferPool *
gst_camerasrc_buffer_pool_new (Gstcamerasrc *camerasrc, GstCaps *caps)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
  GstCamerasrcBufferPool *pool;
  GstStructure *s;
  int bpp = 0;
  int bytes_per_line;

  pool = (GstCamerasrcBufferPool *) g_object_new (GST_TYPE_CAMERASRC_BUFFER_POOL, NULL);

  /* Get format bpp */
  get_frame_size(camerasrc->streams[0].format, camerasrc->streams[0].width,
                   camerasrc->streams[0].height, camerasrc->streams[0].field, &bpp);

  /* In Gstreamer, image bpl and image stride share the same value */
  bytes_per_line = camerasrc->streams[0].width * (bpp/8);
  camerasrc->bpl = gst_camerasrc_get_bpl(camerasrc->streams[0].format, bytes_per_line, bpp);

  pool->src = camerasrc;
  camerasrc->pool = GST_BUFFER_POOL(pool);

  s = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));
  gst_buffer_pool_config_set_params (s, caps, camerasrc->streams[0].size,
                   MIN_PROP_BUFFERCOUNT, MAX_PROP_BUFFERCOUNT);
  gst_buffer_pool_set_config (GST_BUFFER_POOL_CAST (pool), s);

  return GST_BUFFER_POOL (pool);
}
