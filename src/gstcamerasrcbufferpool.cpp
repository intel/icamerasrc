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
#include <sys/syscall.h>

using namespace icamera;

#define gettid() syscall(SYS_gettid)

#define PRINT_FIELD(a, f) \
                     do { \
                            if (a == 2)  {f = "top";} \
                            else if (a == 3) {f = "bottom";} \
                            else if (a == 7) {f = "alternate";} \
                            else {f = "none";} \
                     } while(0)

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
gst_camerasrc_buffer_pool_class_init(GstCamerasrcBufferPoolClass *klass)
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

static void
gst_camerasrc_buffer_pool_init (GstCamerasrcBufferPool * pool)
{
  /* Restore for extension functions */
}

GstBufferPool *
gst_camerasrc_buffer_pool_new (Gstcamerasrc *camerasrc, GstCaps *caps)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("CameraId=%d.", camerasrc->device_id);
  int bpp = 0;

  GstCamerasrcBufferPool *pool = (GstCamerasrcBufferPool *) g_object_new (GST_TYPE_CAMERASRC_BUFFER_POOL, NULL);

  /* Get format bpp */
  get_frame_size(camerasrc->streams[0].format, camerasrc->streams[0].width,
                   camerasrc->streams[0].height, camerasrc->streams[0].field, &bpp);

  pool->src = camerasrc;
  camerasrc->pool = GST_BUFFER_POOL(pool);

  GstStructure *s = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));
  gst_buffer_pool_config_set_params (s, caps, camerasrc->streams[0].size,
                   MIN_PROP_BUFFERCOUNT, MAX_PROP_BUFFERCOUNT);
  gst_buffer_pool_set_config (GST_BUFFER_POOL_CAST (pool), s);

  GST_INFO("CameraId=%d Buffer pool config: min buffers=%d, max buffers=%d, buffer bpl=%d, bpp=%d, size=%d",
                   camerasrc->device_id, MIN_PROP_BUFFERCOUNT, MAX_PROP_BUFFERCOUNT,
                   camerasrc->bpl, bpp, camerasrc->streams[0].size);
  return GST_BUFFER_POOL (pool);
}

static gboolean
gst_camerasrc_buffer_pool_set_config (GstBufferPool * bpool, GstStructure * config)
{
  PERF_CAMERA_ATRACE();
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL(bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GST_INFO("CameraId=%d.", camerasrc->device_id);

  GstAllocator *allocator;
  GstAllocationParams params;
  GstCaps *caps;
  guint size, min_buffers, max_buffers;

  // parse the config and keep around
  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
        &max_buffers)) {
    GST_ERROR("CameraId=%d failed to parse buffer pool config.", camerasrc->device_id);
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params)) {
    GST_ERROR("CameraId=%d failed to get buffer pool allocator.", camerasrc->device_id);
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

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (bpool, config);
}

static gboolean
gst_camerasrc_buffer_pool_start (GstBufferPool * bpool)
{
  PERF_CAMERA_ATRACE();
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL(bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GST_INFO("CameraId=%d.", camerasrc->device_id);

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
  GST_INFO("CameraId=%d start pool %p, Thread ID=%ld, number of buffers in pool=%d.",
    camerasrc->device_id, pool, gettid(), pool->number_of_buffers);

  pool->number_allocated = 0;
  pool->acquire_buffer_index = 0;

  if (camerasrc->downstream_pool) {
    if (!gst_buffer_pool_set_active(camerasrc->downstream_pool, TRUE)) {
      GST_ERROR("CameraId=%d failed to active the other pool %p." GST_PTR_FORMAT,
        camerasrc->device_id, camerasrc->downstream_pool);
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

static int
gst_camerasrc_alloc_weave_buffer(Gstcamerasrc *camerasrc, gint size)
{
  GST_DEBUG("CameraId=%d allocate top and bottom buffers to do buffer weaving.", camerasrc->device_id);
  int ret = 0;

  if (camerasrc->top == NULL && camerasrc->bottom == NULL &&
        camerasrc->interlace_field == GST_CAMERASRC_INTERLACE_FIELD_ALTERNATE &&
        camerasrc->deinterlace_method == GST_CAMERASRC_DEINTERLACE_METHOD_SOFTWARE_WEAVE) {
    camerasrc->top = (camera_buffer_t *)calloc(1, sizeof(camera_buffer_t));
    camerasrc->bottom = (camera_buffer_t *)calloc(1, sizeof(camera_buffer_t));
    ret = posix_memalign(&camerasrc->top->addr, getpagesize(), size);
    if (ret < 0) {
      GST_ERROR_OBJECT(camerasrc, "failed to alloc topfield buffer for weave method");
      return GST_FLOW_ERROR;
    }
    ret = posix_memalign(&camerasrc->bottom->addr, getpagesize(), size);
    if (ret < 0) {
      GST_ERROR("CameraId=%d failed to alloc bottomfield buffer for weave method.", camerasrc->device_id);
      return GST_FLOW_ERROR;
    }
  }

  return ret;
}

static int
gst_camerasrc_alloc_userptr(GstCamerasrcBufferPool *pool,
      GstBuffer **alloc_buffer, GstCamerasrcMeta **meta)
{
  Gstcamerasrc *src = pool->src;
  GST_DEBUG("CameraId=%d allocate userptr buffer.", src->device_id);

  *alloc_buffer = gst_buffer_new();
  *meta = GST_CAMERASRC_META_ADD(*alloc_buffer);
  (*meta)->buffer = (camera_buffer_t *)calloc(1, sizeof(camera_buffer_t));
  if ((*meta)->buffer == NULL)
    return GST_FLOW_ERROR;

  /* Allocate temp buffers: top(only top field), bottom(only bottom field)
   * in order to do deinterlace weaving and fill in metadata */
  int ret = gst_camerasrc_alloc_weave_buffer(src, pool->size);
  if (ret < 0)
    return GST_FLOW_ERROR;

  (*meta)->buffer->s = src->streams[0];
  (*meta)->buffer->s.memType = V4L2_MEMORY_USERPTR;
  ret = posix_memalign(&(*meta)->buffer->addr, getpagesize(), pool->size);

  if (ret < 0) {
    GST_ERROR("CameraId=%d failed to alloc userptr buffer.", src->device_id);
    return GST_FLOW_ERROR;
  }

  (*meta)->mem = (*meta)->buffer->addr;
  gst_buffer_append_memory (*alloc_buffer,
           gst_memory_new_wrapped (GST_MEMORY_FLAG_NO_SHARE, (*meta)->mem, pool->size, 0, pool->size, NULL, NULL));

  return GST_FLOW_OK;
}

static int
gst_camerasrc_alloc_mmap(GstCamerasrcBufferPool *pool,
      GstBuffer **alloc_buffer, GstCamerasrcMeta **meta)
{
  Gstcamerasrc *src = pool->src;
  GST_DEBUG("CameraId=%d allocate mmap buffer.", src->device_id);

  *alloc_buffer = gst_buffer_new();
  *meta = GST_CAMERASRC_META_ADD(*alloc_buffer);
  (*meta)->buffer = (camera_buffer_t *)calloc(1, sizeof(camera_buffer_t));
  if ((*meta)->buffer == NULL)
    return GST_FLOW_ERROR;

  /* Allocate temp buffers: top(only top field), bottom(only bottom field)
   * in order to do deinterlace weaving and fill in metadata */
  int ret = gst_camerasrc_alloc_weave_buffer(src, pool->size);
  if (ret < 0) {
    GST_ERROR("CameraId=%d failed to alloc mmap buffer.", src->device_id);
    return GST_FLOW_ERROR;
  }

  (*meta)->buffer->s = src->streams[0];
  (*meta)->buffer->s.memType = V4L2_MEMORY_MMAP;
  (*meta)->buffer->flags = 0;

  ret = camera_device_allocate_memory(src->device_id, (*meta)->buffer);
  if (ret < 0) {
    GST_ERROR("CameraId=%d failed to alloc memory for mmap buffer.", src->device_id);
    return GST_FLOW_ERROR;
  }

  (*meta)->mem = (*meta)->buffer->addr;
  gst_buffer_append_memory (*alloc_buffer,
           gst_memory_new_wrapped (GST_MEMORY_FLAG_NO_SHARE, (*meta)->mem, pool->size, 0, pool->size, NULL, NULL));

  return GST_FLOW_OK;
}

static gboolean
gst_camerasrc_is_dma_buffer (GstBuffer *buf)
{
  GstMemory *mem;

  if (gst_buffer_n_memory (buf) < 1) {
    GST_ERROR("the amount of memory blocks is smaller than 1.");
    return FALSE;
  }

  mem = gst_buffer_peek_memory (buf, 0);
  if (!mem || !gst_is_dmabuf_memory (mem))
    return FALSE;

  return TRUE;
}

static int
gst_camerasrc_alloc_dma_export(GstCamerasrcBufferPool *pool,
      GstBuffer **alloc_buffer, GstCamerasrcMeta **meta)
{
  Gstcamerasrc *src = pool->src;
  GST_DEBUG("CameraId=%d allocate DMA export buffer.", src->device_id);

  GstMemory *mem = NULL;
  *alloc_buffer = gst_buffer_new();
  *meta = GST_CAMERASRC_META_ADD(*alloc_buffer);
  (*meta)->buffer = (camera_buffer_t *)calloc(1, sizeof(camera_buffer_t));
  if ((*meta)->buffer == NULL)
    return GST_FLOW_ERROR;

  (*meta)->buffer->s = src->streams[0];
  (*meta)->buffer->s.memType = V4L2_MEMORY_MMAP;
  (*meta)->buffer->flags = BUFFER_FLAG_DMA_EXPORT;
  int ret = camera_device_allocate_memory(src->device_id, (*meta)->buffer);
  if (ret < 0) {
    GST_ERROR("CameraId=%d failed to alloc memory for dma export buffer.", src->device_id);
    return GST_FLOW_ERROR;
  }

  int dmafd = dup ((*meta)->buffer->dmafd);
  if (dmafd < 0)
    goto err_get_fd;

  GST_DEBUG("CameraId=%d DMA export buffer fd=%d.", src->device_id, dmafd);

  mem = gst_dmabuf_allocator_alloc (pool->allocator, dmafd, pool->size);
  gst_buffer_append_memory (*alloc_buffer, mem);
  if (!gst_camerasrc_is_dma_buffer(*alloc_buffer))
    goto err_not_dmabuf;

  return GST_FLOW_OK;

err_get_fd:
  {
    GST_ERROR("CameraId=%d failed to get fd of DMA export buffer.", src->device_id);
    gst_buffer_unref (*alloc_buffer);
    return GST_FLOW_ERROR;
  }
err_not_dmabuf:
  {
    GST_ERROR("CameraId=%d not a dma buffer.", src->device_id);
    gst_buffer_unref (*alloc_buffer);
    return GST_FLOW_ERROR;
  }
}

static int
gst_camerasrc_alloc_dma_import(GstCamerasrcBufferPool *pool,
      GstBuffer **alloc_buffer, GstCamerasrcMeta **meta)
{
  Gstcamerasrc *src = pool->src;
  GST_DEBUG("CameraId=%d allocate DMA import buffer.", src->device_id);

  GstMemory *mem = NULL;
  int ret = gst_buffer_pool_acquire_buffer(src->downstream_pool, alloc_buffer, NULL);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT(src, "failed to acquire buffer from downstream buffer pool");
    goto err_acquire_buffer;
  }

  *meta = GST_CAMERASRC_META_ADD(*alloc_buffer);

  (*meta)->buffer = (camera_buffer_t *)calloc(1, sizeof(camera_buffer_t));
  if ((*meta)->buffer == NULL) {
    return GST_FLOW_ERROR;
  }

  mem = gst_buffer_peek_memory(*alloc_buffer, 0);
  (*meta)->buffer->dmafd = dup(gst_dmabuf_memory_get_fd(mem));
  if ((*meta)->buffer->dmafd < 0)
    goto err_get_fd;

  if (!gst_camerasrc_is_dma_buffer(*alloc_buffer))
    goto err_not_dmabuf;

  GST_DEBUG("CameraId=%d DMA import buffer fd=%d.", src->device_id, (*meta)->buffer->dmafd);

  (*meta)->buffer->s = src->streams[0];
  (*meta)->buffer->s.memType = V4L2_MEMORY_DMABUF;
  (*meta)->buffer->flags = 0;

  return GST_FLOW_OK;

  err_acquire_buffer:
  {
    gst_buffer_unref (*alloc_buffer);
    return GST_FLOW_ERROR;
  }
  err_get_fd:
  {
    GST_ERROR("CameraId=%d failed to get fd of DMA import buffer.", src->device_id);
    gst_buffer_unref (*alloc_buffer);
    return GST_FLOW_ERROR;
  }
  err_not_dmabuf:
  {
    GST_ERROR("CameraId=%d not a dma buffer.", src->device_id);
    gst_buffer_unref (*alloc_buffer);
    return GST_FLOW_ERROR;
  }
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

  GST_DEBUG("CameraId=%d set Buffer meta: videoFlage: %d, videofmt: %d, width: %d, \
          heigh: %d, n_planes: %d, offset: %d, stride: %d.",
          camerasrc->device_id, videoFlags, videoFmt, camerasrc->streams[0].width,
          camerasrc->streams[0].height, n_planes, (int)offset[0], (int)stride[0]);

  /* add metadata to raw video buffers */
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
  GST_INFO("CameraId=%d io-mode=%d.", camerasrc->device_id, camerasrc->io_mode);

  GstBuffer *alloc_buffer = NULL;
  GstCamerasrcMeta *meta = NULL;

  switch (camerasrc->io_mode) {
    case GST_CAMERASRC_IO_MODE_USERPTR:
      if (gst_camerasrc_alloc_userptr(pool, &alloc_buffer, &meta) < 0)
        goto err_alloc_buffer;
      break;
    case GST_CAMERASRC_IO_MODE_MMAP:
      if (gst_camerasrc_alloc_mmap(pool, &alloc_buffer, &meta) < 0)
        goto err_alloc_buffer;
      break;
    case GST_CAMERASRC_IO_MODE_DMA_EXPORT:
      if (gst_camerasrc_alloc_dma_export(pool, &alloc_buffer, &meta) < 0)
        goto err_alloc_buffer;
      break;
    case GST_CAMERASRC_IO_MODE_DMA_IMPORT:
      if (gst_camerasrc_alloc_dma_import(pool, &alloc_buffer, &meta) < 0)
        goto err_alloc_buffer;
      break;
    default:
      break;
  }

  meta->index = pool->number_allocated;
  pool->buffers[meta->index] = alloc_buffer;
  pool->number_allocated++;

  //need to set meta to allocated buffer.
  gst_camerasrc_set_meta(camerasrc, alloc_buffer);
  *buffer = alloc_buffer;
  GST_DEBUG("CameraId=%d alloc_buffer buffer %p\n", camerasrc->device_id, *buffer);

  return GST_FLOW_OK;

  err_alloc_buffer:
  {
    return GST_FLOW_ERROR;
  }
}

/**
  *Use system time to update fps when dqbuf for every 2 seconds
  */
void gst_camerasrc_update_fps(Gstcamerasrc *camerasrc)
{
  PERF_CAMERA_ATRACE();
  camerasrc->fps_debug.buf_count++;

  /* Don't start counting fps after first few buffers due to they're not stable */
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
          g_print("fps:%.4f   Camera name: %s\n",interval_fps, camerasrc->cam_info.name);

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
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL(bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GST_INFO("CameraId=%d.", camerasrc->device_id);

  GstBuffer *gbuffer = pool->buffers[pool->acquire_buffer_index%pool->number_allocated];
  GstCamerasrcMeta *meta = GST_CAMERASRC_META_GET(gbuffer);
  const char *buffer_field;

  if (camerasrc->print_fps)
      gst_camerasrc_update_fps(camerasrc);

  int ret = camera_stream_dqbuf(camerasrc->device_id, camerasrc->stream_id, &meta->buffer);
  if (ret != 0) {
    GST_ERROR("CameraId=%d dqbuf failed ret %d.", camerasrc->device_id, ret);
    return GST_FLOW_ERROR;
  }

  GstClockTime timestamp = meta->buffer->timestamp;
  camerasrc->time_end = meta->buffer->timestamp;

  if (camerasrc->print_field)
    g_print("buffer field: %d    Camera Id: %d    buffer sequence: %d\n",
      meta->buffer->s.field, camerasrc->device_id, meta->buffer->sequence);

  PRINT_FIELD(meta->buffer->s.field, buffer_field);
  GST_INFO("CameraId=%d DQ buffer done, GstBuffer=%p, UserBuffer=%p, buffer index=%d, Thread ID=%ld  ts=%lu, buffer field=%s.",
    camerasrc->device_id, gbuffer, meta->buffer, meta->buffer->index, gettid(), meta->buffer->timestamp, buffer_field);

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
    GST_ERROR("CameraId=%d deinterlace frame failed.", camerasrc->device_id);
    return GST_FLOW_ERROR;
  }

  camerasrc->first_frame = false;

  GST_BUFFER_TIMESTAMP(gbuffer) = timestamp;
  *buffer = gbuffer;
  pool->acquire_buffer_index++;
  GST_DEBUG("CameraId=%d acquire_buffer buffer %p.", camerasrc->device_id, *buffer);
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
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL (bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GstCamerasrcMeta *meta = GST_CAMERASRC_META_GET(buffer);
  GST_INFO("CameraId=%d", camerasrc->device_id);

  int ret = camera_stream_qbuf(camerasrc->device_id, camerasrc->stream_id, meta->buffer);
  if (ret < 0) {
    GST_ERROR("CameraId=%d failed to qbuf back to stream.", camerasrc->device_id);
    return;
  }
  GST_INFO("CameraId=%d Q buffer done, GstBuffer=%p, UserBuffer=%p, Buffer index=%d, Thread ID=%ld,  ts=%lu",
    camerasrc->device_id, buffer, meta->buffer, meta->buffer->index, gettid(), meta->buffer->timestamp);

  GST_DEBUG("CameraId=%d release_buffer buffer %p.", camerasrc->device_id, buffer);
  {
    PERF_CAMERA_ATRACE_PARAM1("sof.sequence", meta->buffer->sequence);
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
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL (bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GstCamerasrcMeta *meta = GST_CAMERASRC_META_GET(buffer);
  GST_INFO("CameraId=%d.", camerasrc->device_id);

  switch (camerasrc->io_mode) {
    case GST_CAMERASRC_IO_MODE_USERPTR:
      if (meta->buffer->addr)
        free(meta->buffer->addr);
      break;
    case GST_CAMERASRC_IO_MODE_MMAP:
      if (meta->mem)
        meta->mem = NULL;
      break;
    case GST_CAMERASRC_IO_MODE_DMA_EXPORT:
    case GST_CAMERASRC_IO_MODE_DMA_IMPORT:
      if (meta->buffer->dmafd)
        close(meta->buffer->dmafd);
      break;
    default:
      break;
  }

  free(meta->buffer);
  pool->buffers[meta->index] = NULL;
  GST_DEBUG("CameraId=%d free_buffer buffer %p.", camerasrc->device_id, buffer);
  gst_buffer_unref (buffer);
}

static void
gst_camerasrc_print_framerate_analysis(Gstcamerasrc *camerasrc)
{
  double total_stream_buffers = camerasrc->fps_debug.buf_count-FPS_BUF_COUNT_START; //valid number of stream buffers
  double  total_stream_duration = camerasrc->fps_debug.sum_time/1000000; //valid time of counting stream buffers
  camerasrc->fps_debug.av_fps = total_stream_buffers/total_stream_duration;

  if (total_stream_duration < FPS_TIME_INTERVAL/1000000) {
     /* This case means that pipeline runtime is less than 2 seconds(we count fps every 2 seconds),
        * no updates from max_fps and min_fps, only average fps is available */
     g_print("\nCamera name:%s(Id:%d)\nAverage fps is:%.4f\n",
                      camerasrc->cam_info.name,
                      camerasrc->device_id,
                      camerasrc->fps_debug.av_fps);
  } else {
     //This case means that pipeline runtime is longer than 2 seconds
     g_print("\nTotal frame is:%g  Camera name:%s(Id:%d)\n",
                      camerasrc->fps_debug.buf_count,
                      camerasrc->cam_info.name,
                      camerasrc->device_id);
     g_print("Max fps is:%.4f,Minimum fps is:%.4f,Average fps is:%.4f\n\n",
                      camerasrc->fps_debug.max_fps,
                      camerasrc->fps_debug.min_fps,
                      camerasrc->fps_debug.av_fps);
  }
}

static gboolean
gst_camerasrc_buffer_pool_stop(GstBufferPool *bpool)
{
  PERF_CAMERA_ATRACE();
  GstCamerasrcBufferPool *pool = GST_CAMERASRC_BUFFER_POOL(bpool);
  Gstcamerasrc *camerasrc = pool->src;
  GST_INFO("CameraId=%d.", camerasrc->device_id);

  /* Calculate max/min/average fps */
  if (camerasrc->print_fps) {
     gst_camerasrc_print_framerate_analysis(camerasrc);
     camerasrc->print_fps=false;
  }

  if (camerasrc->camera_open) {
    camera_device_stop(camerasrc->device_id);
    camera_device_close(camerasrc->device_id);
    camerasrc->stream_id = -1;

    if (camerasrc->downstream_pool)
      gst_object_unref(camerasrc->downstream_pool);

    if (camerasrc->pool)
      gst_object_unref(camerasrc->pool);

    camerasrc->camera_open = false;
  }

  if (pool->allocator)
    gst_object_unref(pool->allocator);

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

  return TRUE;
}
