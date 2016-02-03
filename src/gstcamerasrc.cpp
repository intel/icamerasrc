/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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

#define LOG_TAG "GstCameraSrc"

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <stdio.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include <gst/gst.h>

#include "ICamera.h"
#include "ScopedAtrace.h"

#include "gstcamerasrcbufferpool.h"
#include "gstcamerasrc.h"

#include "gstcameraformat.h"

using namespace icamera;

GST_DEBUG_CATEGORY (gst_camerasrc_debug);
#define GST_CAT_DEFAULT gst_camerasrc_debug

static struct timeval time_start, time_end;
/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT,

  PROP_CAPTURE_MODE,
  PROP_BUFFERCOUNT,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_PIXELFORMAT,
  PROP_PRINT_FPS,
  PROP_INTERLACE_MODE,
  PROP_DEINTERLACE_METHOD,
  PROP_CAMERA_NAME,
  PROP_IO_MODE,
};

#define gst_camerasrc_parent_class parent_class
G_DEFINE_TYPE (Gstcamerasrc, gst_camerasrc, GST_TYPE_PUSH_SRC);

static void gst_camerasrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_camerasrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_camerasrc_set_caps(GstBaseSrc *pad, GstCaps * caps);
static GstCaps* gst_camerasrc_get_caps(GstBaseSrc *src, GstCaps * filter);
static gboolean gst_camerasrc_start(GstBaseSrc *basesrc);
static gboolean gst_camerasrc_stop(GstBaseSrc *basesrc);
static GstStateChangeReturn gst_camerasrc_change_state(GstElement * element,GstStateChange transition);
static GstCaps *gst_camerasrc_fixate (GstBaseSrc * basesrc, GstCaps * caps);
static gboolean gst_camerasrc_negotiate(GstBaseSrc *basesrc);
static gboolean gst_camerasrc_query(GstBaseSrc * bsrc, GstQuery * query );
static gboolean gst_camerasrc_decide_allocation(GstBaseSrc *bsrc,GstQuery *query);
static GstFlowReturn gst_camerasrc_fill(GstPushSrc *src,GstBuffer *buf);
static void gst_camerasrc_dispose(GObject *object);
static gboolean gst_camerasrc_unlock(GstBaseSrc *src);
static gboolean gst_camerasrc_unlock_stop(GstBaseSrc *src);

#if 0
static gboolean gst_camerasrc_sink_event(GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn gst_camerasrc_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);
#endif

static GType gst_camerasrc_deinterlace_method_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType deinterlace_method_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_DEINTERLACE_METHOD_NONE,
        "don't do deinterlace", "none"},
    {GST_CAMERASRC_DEINTERLACE_METHOD_SOFTWARE_BOB,
        "software bob", "sw_bob"},
    {GST_CAMERASRC_DEINTERLACE_METHOD_HARDWARE_BOB,
        "hardware", "hw_bob"},
    {0, NULL, NULL},
  };

  if (!deinterlace_method_type) {
    deinterlace_method_type =
        g_enum_register_static ("GstCamerasrcDeinterlaceMode", method_types);
  }
  return deinterlace_method_type;
}

static GType gst_camerasrc_io_mode_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType io_mode_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_IO_MODE_USERPTR,
        "GST_CAMERASRC_IO_MODE_USERPTR", "USERPTR"},
    {GST_CAMERASRC_IO_MODE_MMAP,
        "GST_CAMERASRC_IO_MODE_MMAP", "MMAP"},
    {GST_CAMERASRC_IO_MODE_DMA,
        "GST_CAMERASRC_IO_MODE_DMA", "DMA"},
    {GST_CAMERASRC_IO_MODE_DMA_IMPORT,
        "GST_CAMERASRC_IO_MODE_DMA_IMPORT", "DMA_IMPORT"},
    {0, NULL, NULL},
  };

  if (!io_mode_type) {
    io_mode_type = g_enum_register_static ("GstCamerasrcIoMode", method_types);
  }
  return io_mode_type;
}

static void gst_camerasrc_dispose(GObject *object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_camerasrc_finalize (Gstcamerasrc *camerasrc)
{
  PERF_CAMERA_ATRACE();
  if (camerasrc->device_id >= 0) {
    camera_device_stop(camerasrc->device_id);
    camerasrc->stream_id = -1;
    camera_device_close(camerasrc->device_id);
  }

  camera_hal_deinit();

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (camerasrc));
}

static void
gst_camerasrc_class_init (GstcamerasrcClass * klass)
{
  PERF_CAMERA_ATRACE();
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *basesrc_class;
  GstPushSrcClass * pushsrc_class;
  GstCaps *cap_camsrc;

  gobject_class = G_OBJECT_CLASS(klass);
  gstelement_class = GST_ELEMENT_CLASS(klass);
  basesrc_class = GST_BASE_SRC_CLASS(klass);
  pushsrc_class = GST_PUSH_SRC_CLASS(klass);

  gobject_class->set_property = gst_camerasrc_set_property;
  gobject_class->get_property = gst_camerasrc_get_property;
  gobject_class->finalize = (GObjectFinalizeFunc) gst_camerasrc_finalize;
  gobject_class->dispose = gst_camerasrc_dispose;

  gstelement_class->change_state = gst_camerasrc_change_state;

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
        FALSE, (GParamFlags)G_PARAM_READWRITE));

  g_object_class_install_property(gobject_class,PROP_CAPTURE_MODE,
      g_param_spec_int("capture-mode","capture mode","capture mode",
        0,G_MAXINT,CAMERASRC_CAPTURE_MODE_PREVIEW,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_BUFFERCOUNT,
      g_param_spec_int("buffer-count","buffer count","buffer count",
        MIN_PROP_BUFFERCOUNT,MAX_PROP_BUFFERCOUNT,DEFAULT_PROP_BUFFERCOUNT,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_WIDTH,
      g_param_spec_int("width","width","width",
        0,G_MAXINT,DEFAULT_PROP_WIDTH,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_HEIGHT,
      g_param_spec_int("height","height","height",
        0,G_MAXINT,DEFAULT_PROP_HEIGHT,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_PIXELFORMAT,
      g_param_spec_int("pixelformat","pixelformat","pixelformat",
        0,G_MAXINT,DEFAULT_PROP_PIXELFORMAT,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_PRINT_FPS,
      g_param_spec_boolean("printfps","printfps","printfps",
        DEFAULT_PROP_PRINT_FPS,(GParamFlags)(G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE)));

  g_object_class_install_property (gobject_class, PROP_INTERLACE_MODE,
      g_param_spec_boolean ("interlace-mode", "interlace-mode", "interlaced output mode",
        FALSE, (GParamFlags)G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DEINTERLACE_METHOD,
      g_param_spec_enum ("deinterlace-method", "Deinterlace method", "Deinterlace method to use",
        gst_camerasrc_deinterlace_method_get_type(), DEFAULT_DEINTERLACE_METHOD, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_CAMERA_NAME,
      g_param_spec_string("device-name","device-name","device-name",
        DEFAULT_PROP_CAMERA_NAME,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_IO_MODE,
      g_param_spec_enum ("io-mode", "IO mode", "I/O mode",
          gst_camerasrc_io_mode_get_type(), DEFAULT_PROP_IO_MODE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata(gstelement_class,
      "icamerasrc",
      "Source/Video",
      "CameraSource Element",
      "Intel");

  cap_camsrc = gst_camerasrc_get_all_caps(klass);
  gst_element_class_add_pad_template
    (gstelement_class, gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, cap_camsrc));
  gst_caps_unref(cap_camsrc);

  basesrc_class->get_caps = GST_DEBUG_FUNCPTR(gst_camerasrc_get_caps);
  basesrc_class->set_caps = GST_DEBUG_FUNCPTR(gst_camerasrc_set_caps);
  basesrc_class->start = GST_DEBUG_FUNCPTR(gst_camerasrc_start);
  basesrc_class->unlock = GST_DEBUG_FUNCPTR(gst_camerasrc_unlock);
  basesrc_class->unlock_stop = GST_DEBUG_FUNCPTR(gst_camerasrc_unlock_stop);
  basesrc_class->fixate = GST_DEBUG_FUNCPTR(gst_camerasrc_fixate);
  basesrc_class->stop = GST_DEBUG_FUNCPTR(gst_camerasrc_stop);
  basesrc_class->query = GST_DEBUG_FUNCPTR(gst_camerasrc_query);
  basesrc_class->negotiate = GST_DEBUG_FUNCPTR(gst_camerasrc_negotiate);
  basesrc_class->decide_allocation = GST_DEBUG_FUNCPTR(gst_camerasrc_decide_allocation);

  pushsrc_class->fill = GST_DEBUG_FUNCPTR(gst_camerasrc_fill);

  GST_DEBUG_CATEGORY_INIT (gst_camerasrc_debug, "icamerasrc", 0, "camerasrc source element");
}

static void
gst_camerasrc_init (Gstcamerasrc * camerasrc)
{
  PERF_CAMERA_ATRACE();

  /* no need to add anything to init pad*/
  gst_base_src_set_format (GST_BASE_SRC (camerasrc), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (camerasrc), TRUE);
  camerasrc->device_id = -1;
  camerasrc->stream_id = -1;
  camerasrc->width = DEFAULT_PROP_WIDTH;
  camerasrc->height = DEFAULT_PROP_HEIGHT;
  camerasrc->number_of_buffers = DEFAULT_PROP_BUFFERCOUNT;
  camerasrc->pixelformat = V4L2_PIX_FMT_NV12;
  camerasrc->capture_mode = GST_CAMERASRC_DEINTERLACE_METHOD_NONE;
  camerasrc->device_name = NULL;

  camerasrc->interlace_mode = false;
  camerasrc->pool = NULL;
  camerasrc->downstream_pool = NULL;
}

static void
gst_camerasrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  PERF_CAMERA_ATRACE();
  Gstcamerasrc *src = GST_CAMERASRC (object);
  const char* devicename;

  switch (prop_id) {
    case PROP_SILENT:
      src->silent = g_value_get_boolean (value);
      break;
    case PROP_CAPTURE_MODE:
      switch (g_value_get_int (value)) {
        case 0:
          src->capture_mode = CAMERASRC_CAPTURE_MODE_STILL;
          break;
        case 1:
          src->capture_mode = CAMERASRC_CAPTURE_MODE_VIDEO;
          break;
        case 2:
          src->capture_mode = CAMERASRC_CAPTURE_MODE_PREVIEW;
          break;
        default:
          g_print ("Invalid capure mode");
          break;
      }
      break;
    case PROP_BUFFERCOUNT:
      src->number_of_buffers = g_value_get_int (value);
      break;
    case PROP_WIDTH:
      src->width = g_value_get_int (value);
      break;
    case PROP_HEIGHT:
      src->height = g_value_get_int (value);
      break;
    case PROP_PIXELFORMAT:
      src->pixelformat = g_value_get_int (value);
      break;
    case PROP_PRINT_FPS:
      src->print_fps = g_value_get_boolean(value);
      break;
    case PROP_INTERLACE_MODE:
      src->interlace_mode = g_value_get_boolean (value);
      break;
    case PROP_DEINTERLACE_METHOD:
      src->deinterlace_method = g_value_get_enum (value);
      break;
    case PROP_IO_MODE:
      src->io_mode = g_value_get_enum (value);
      break;
    case PROP_CAMERA_NAME:
      devicename = g_value_get_string (value);
      src->device_name = new char[strlen(devicename)+1];
      strncpy(src->device_name, devicename, strlen(devicename)+1);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_camerasrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstcamerasrc *src = GST_CAMERASRC (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, src->silent);
      break;
    case PROP_CAPTURE_MODE:
      g_value_set_int (value, src->capture_mode);
      break;
    case PROP_BUFFERCOUNT:
      g_value_set_int (value, src->number_of_buffers);
      break;
    case PROP_WIDTH:
      g_value_set_int (value, src->width);
      break;
    case PROP_HEIGHT:
      g_value_set_int (value, src->height);
      break;
    case PROP_PIXELFORMAT:
      g_value_set_int (value, src->pixelformat);
      break;
    case PROP_PRINT_FPS:
      g_value_set_boolean(value,src->print_fps);
      break;
    case PROP_INTERLACE_MODE:
      g_value_set_boolean (value, src->interlace_mode);
      break;
    case PROP_DEINTERLACE_METHOD:
      g_value_set_enum (value, src->deinterlace_method);
      break;
    case PROP_IO_MODE:
      g_value_set_enum (value, src->io_mode);
      break;
    case PROP_CAMERA_NAME:
      g_value_set_string (value, src->device_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_camerasrc_get_caps_info (Gstcamerasrc* camerasrc, GstCaps * caps, stream_config_t *stream_list)
{
  PERF_CAMERA_ATRACE();
  GstStructure *structure;
  guint32 fourcc;
  const gchar *mimetype;
  GstVideoInfo info;

  /* default unknown values */
  fourcc = 0;
  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  if (g_str_equal (mimetype, "video/x-raw")) {
    /* raw caps, parse into video info */
    if (!gst_video_info_from_caps (&info, caps)) {
      GST_ERROR_OBJECT (camerasrc, "invalid format");
      return FALSE;
    }

    switch (GST_VIDEO_INFO_FORMAT (&info)) {
      case GST_VIDEO_FORMAT_NV12:
        fourcc = V4L2_PIX_FMT_NV12;
        break;
      case GST_VIDEO_FORMAT_YUY2:
        fourcc = V4L2_PIX_FMT_YUYV;
        break;
      case GST_VIDEO_FORMAT_UYVY:
        fourcc = V4L2_PIX_FMT_UYVY;
        break;
      case GST_VIDEO_FORMAT_RGBx:
        fourcc = V4L2_PIX_FMT_XRGB32;
        break;
      case GST_VIDEO_FORMAT_BGR:
        fourcc = V4L2_PIX_FMT_BGR24;
        break;
      case GST_VIDEO_FORMAT_BGRx:
        fourcc = V4L2_PIX_FMT_XBGR32;
        break;
      default:
        break;
    }
  } else if (g_str_equal (mimetype, "video/x-bayer")) {
    fourcc = V4L2_PIX_FMT_SGRBG8;
  } else {
    GST_ERROR_OBJECT(camerasrc, "unsupported type %s", mimetype);
    return FALSE;
  }

  if (!gst_structure_get_int (structure, "width", &info.width)) {
    GST_ERROR_OBJECT(camerasrc, "failed to get width");
    return FALSE;
  }

  if (!gst_structure_get_int (structure, "height", &info.height)) {
    GST_ERROR_OBJECT(camerasrc, "failed to get height");
    return FALSE;
  }

  if (fourcc == 0) {
    GST_ERROR_OBJECT(camerasrc, "unsupported format");
    return FALSE;
  }

  GST_DEBUG_OBJECT(camerasrc, "format %d width %d height %d", fourcc, info.width, info.height);

  camerasrc->streams[0].format = fourcc;
  camerasrc->streams[0].width = info.width;
  camerasrc->streams[0].height = info.height;
  camerasrc->streams[0].interlaced_video = camerasrc->interlace_mode ? INTERLACED_ENABLED: INTERLACED_DISABLED;
  switch(camerasrc->io_mode) {
    case GST_CAMERASRC_IO_MODE_USERPTR:
      camerasrc->streams[0].memType = V4L2_MEMORY_USERPTR;
      break;
    case GST_CAMERASRC_IO_MODE_DMA_IMPORT:
      camerasrc->streams[0].memType = V4L2_MEMORY_DMABUF;
      break;
    default:
      GST_ERROR_OBJECT(camerasrc, "iomode %d is not supported yet.", camerasrc->io_mode);
      break;
  }

  stream_list->num_streams = 1;
  stream_list->streams = camerasrc->streams;
  camerasrc->info = info;

  return TRUE;
}

static gboolean gst_camerasrc_set_caps(GstBaseSrc *src, GstCaps *caps)
{
  PERF_CAMERA_ATRACE();
  Gstcamerasrc *camerasrc;
  int ret;

  camerasrc = GST_CAMERASRC (src);

  if (!gst_camerasrc_get_caps_info (camerasrc, caps, &camerasrc->stream_list)) {
    GST_ERROR_OBJECT(camerasrc, "failed to get caps info.");
    return FALSE;
  }

  /* Create buffer pool */
  camerasrc->pool = gst_camerasrc_buffer_pool_new(camerasrc, caps);
  if (!camerasrc->pool) {
    GST_ERROR_OBJECT(camerasrc, "new buffer pool failed.");
    return FALSE;
  }

  ret = camera_device_config_streams(camerasrc->device_id,  &camerasrc->stream_list);
  if(ret < 0) {
    GST_ERROR_OBJECT(camerasrc, "failed to add stream for format %d %dx%d.", camerasrc->streams[0].format,
                     camerasrc->streams[0].width, camerasrc->streams[0].height);
    return FALSE;
  }

  camerasrc->stream_id = camerasrc->streams[0].id;

  return TRUE;
}

static GstCaps *gst_camerasrc_get_caps(GstBaseSrc *src,GstCaps *filter)
{
  PERF_CAMERA_ATRACE();
  return gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (GST_CAMERASRC(src)));
}

static gboolean gst_camerasrc_start(GstBaseSrc *basesrc)
{
  Gstcamerasrc *camerasrc;
  camerasrc = GST_CAMERASRC (basesrc);
  gettimeofday(&time_start, NULL);
  gettimeofday(&time_end, NULL);
  int ret;
  int count;
  gboolean device_probe = FALSE;

  ret = camera_hal_init();

  if (ret < 0) {
    GST_ERROR_OBJECT(camerasrc, "failed to init libcamhal device.");
    return FALSE;
  }
  count = get_number_of_cameras();
  if (camerasrc->device_name != NULL ) {
    for (int id = 0; id < count; id++) {
       camera_info_t info;
       ret = get_camera_info(id, info);
       if (ret < 0) {
          GST_ERROR_OBJECT(camerasrc, "failed to get device name.");
          camera_hal_deinit();
          return FALSE;
       }

       if(strcmp(info.name, camerasrc->device_name) == 0) {
          camerasrc->device_id = id;
          device_probe = TRUE;
          break;
       }
    }

    if (!device_probe) {
       g_print("Failed to get correct device name from property:'device-name',please set cameraInput first\n");
       return FALSE;
    }
    delete camerasrc->device_name;
    camerasrc->device_name = NULL;
  } else
    camerasrc->device_id = 0;

  ret = camera_device_open(camerasrc->device_id);
  if (ret < 0) {
     GST_ERROR_OBJECT(camerasrc, "incorrect device_id, failed to open libcamhal device.");
     camera_hal_deinit();
     return FALSE;
  }
  return TRUE;
}

static gboolean gst_camerasrc_stop(GstBaseSrc *basesrc)
{
  memset(&time_start, 0, sizeof(struct timeval));
  memset(&time_end, 0, sizeof(struct timeval));
  return TRUE;
}

static GstStateChangeReturn
gst_camerasrc_change_state (GstElement * element, GstStateChange transition)
{
  PERF_CAMERA_ATRACE();
  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static GstCaps *gst_camerasrc_fixate(GstBaseSrc * basesrc, GstCaps * caps)
{
  PERF_CAMERA_ATRACE();
  GstStructure *structure;
  GST_DEBUG_OBJECT (basesrc, "fixated caps %" GST_PTR_FORMAT, caps);

  caps = gst_caps_make_writable(caps);

  for (guint i = 0; i < gst_caps_get_size (caps); ++i) {
    structure = gst_caps_get_structure (caps, i);
    gst_structure_fixate_field_nearest_int (structure, "width", DEFAULT_PROP_WIDTH);
    gst_structure_fixate_field_nearest_int (structure, "height", DEFAULT_PROP_HEIGHT);
  }
  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (basesrc, caps);

  return caps;
}

static gboolean gst_camerasrc_query(GstBaseSrc * bsrc, GstQuery * query )
{
  PERF_CAMERA_ATRACE();
  gboolean res;
  res = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);

  switch (GST_QUERY_TYPE (query)){
    case GST_QUERY_LATENCY:
      res = TRUE;
      break;
    default:
      res = GST_BASE_SRC_CLASS(parent_class)->query(bsrc,query);
      break;
  }
  return res;
}

static gboolean gst_camerasrc_negotiate(GstBaseSrc *basesrc)
{
  PERF_CAMERA_ATRACE();
  return GST_BASE_SRC_CLASS (parent_class)->negotiate (basesrc);
}

static gboolean gst_camerasrc_decide_allocation(GstBaseSrc *bsrc,GstQuery *query)
{
  PERF_CAMERA_ATRACE();
  Gstcamerasrc * camerasrc = GST_CAMERASRC(bsrc);
  GstBufferPool *pool = NULL;
  guint size = 0, min = 0, max = 0;
  GstAllocator *allocator = NULL;
  GstStructure *config;
  GstCaps *caps;
  GstAllocationParams params;

  switch (camerasrc->io_mode) {
    case GST_CAMERASRC_IO_MODE_USERPTR: {
        gboolean update;

        if(gst_query_get_n_allocation_pools(query)>0){
          gst_query_parse_nth_allocation_pool(query,0,&pool,&size,&min,&max);
          update=TRUE;
        }else{
          pool = NULL;
          max=min=0;
          size = 0;
          update = FALSE;
        }
        pool = GST_BUFFER_POOL_CAST(camerasrc->pool);
        size = (GST_CAMERASRC_BUFFER_POOL(camerasrc->pool))->size;

        if (update)
          gst_query_set_nth_allocation_pool (query, 0, pool, size, camerasrc->number_of_buffers, MAX_PROP_BUFFERCOUNT);
        else
          gst_query_add_allocation_pool (query, pool, size, camerasrc->number_of_buffers, MAX_PROP_BUFFERCOUNT);
      }
      break;
    case GST_CAMERASRC_IO_MODE_DMA_IMPORT:
      memset(&params, 0, sizeof(GstAllocationParams));
      gst_query_parse_allocation (query, &caps, NULL);

      if (gst_query_get_n_allocation_params (query) > 0)
        gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);

      if (gst_query_get_n_allocation_pools (query) > 0) {
        gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
      }

      if (camerasrc->downstream_pool)
        gst_object_unref (camerasrc->downstream_pool);
      camerasrc->downstream_pool = (GstBufferPool*)gst_object_ref(pool);
      gst_object_unref (pool);
      pool = (GstBufferPool*)gst_object_ref(camerasrc->pool);

      if (pool) {
        config = gst_buffer_pool_get_config (pool);
        gst_buffer_pool_config_get_params (config, NULL, &size, &min, &max);
        gst_structure_free (config);
      }

      gst_query_set_nth_allocation_pool (query, 0, pool, size, camerasrc->number_of_buffers, max);

      if (allocator)
        gst_object_unref (allocator);
      break;

    default:
      break;
 }

 return GST_BASE_SRC_CLASS (parent_class)->decide_allocation (bsrc, query);
}

static GstFlowReturn gst_camerasrc_fill(GstPushSrc *src, GstBuffer *buf)
{
  GstClock *clock;
  GstClockTime delay;
  GstClockTime abs_time, base_time, timestamp, duration;
  Gstcamerasrc *camerasrc = GST_CAMERASRC(src);
  GstCamerasrcBufferPool *bpool = GST_CAMERASRC_BUFFER_POOL(camerasrc->pool);

  gettimeofday(&time_end, NULL);
  //TODO: the duration will be get from hal in the future.
  //the unit of duration is ns.
  duration = (GstClockTime) (((time_end.tv_sec - time_start.tv_sec) * 1000000 +
                                    (time_end.tv_usec - time_start.tv_usec))*1000);

  timestamp = GST_BUFFER_TIMESTAMP (buf);
  /* timestamps, LOCK to get clock and base time. */
  /* FIXME: element clock and base_time is rarely changing */
  GST_OBJECT_LOCK (camerasrc);
  if ((clock = GST_ELEMENT_CLOCK (camerasrc))) {
      /* we have a clock, get base time and ref clock */
      base_time = GST_ELEMENT (camerasrc)->base_time;
      gst_object_ref (clock);
  } else {
      /* no clock, can't set timestamps */
      base_time = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (camerasrc);

  /* sample pipeline clock */
  if (clock) {
      abs_time = gst_clock_get_time (clock);
      gst_object_unref (clock);
  } else {
      abs_time = GST_CLOCK_TIME_NONE;
  }

  if (timestamp != GST_CLOCK_TIME_NONE) {
      struct timespec now;
      GstClockTime gstnow;
      clock_gettime (CLOCK_MONOTONIC, &now);
      gstnow = GST_TIMESPEC_TO_TIME (now);
      if (gstnow < timestamp && (timestamp - gstnow) > (10 * GST_SECOND)) {
          GTimeVal now;
          /* very large diff, fall back to system time */
          g_get_current_time (&now);
          gstnow = GST_TIMEVAL_TO_TIME (now);
      }
      if (gstnow > timestamp) {
          delay = gstnow - timestamp;
      } else {
          delay = 0;
      }
      GST_DEBUG_OBJECT (camerasrc, "ts: %" GST_TIME_FORMAT " now %" GST_TIME_FORMAT
              " delay %" GST_TIME_FORMAT, GST_TIME_ARGS (timestamp),
              GST_TIME_ARGS (gstnow), GST_TIME_ARGS (delay));
  } else {
      /* we assume 1 frame latency otherwise */
      if (GST_CLOCK_TIME_IS_VALID (duration))
          delay = duration;
      else
          delay = 0;
  }

  if (G_LIKELY (abs_time != GST_CLOCK_TIME_NONE)) {
      /* the time now is the time of the clock minus the base time */
      timestamp = abs_time - base_time;

      /* adjust for delay in the device */
      if (timestamp > delay)
          timestamp -= delay;
      else
          timestamp = 0;
  } else {
      timestamp = GST_CLOCK_TIME_NONE;
  }

  //set buffer metadata.
  GST_BUFFER_TIMESTAMP(buf) = timestamp;
  GST_BUFFER_OFFSET(buf) = bpool->acquire_buffer_index;
  GST_BUFFER_OFFSET_END(buf) = bpool->acquire_buffer_index + 1;
  GST_BUFFER_DURATION(buf) = duration;
  gettimeofday(&time_start, NULL);

  return GST_FLOW_OK;
}

static gboolean gst_camerasrc_unlock(GstBaseSrc *src)
{
  return TRUE;
}

static gboolean gst_camerasrc_unlock_stop(GstBaseSrc *src)
{
  return TRUE;
}

#if 0
static gboolean
gst_camerasrc_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
      {
        GstCaps * caps;

        gst_event_parse_caps (event, &caps);
        /* do something with the caps */

        /* and forward */
        ret = gst_pad_event_default (pad, parent, event);
        break;
      }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

static GstFlowReturn
gst_camerasrc_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  Gstcamerasrc *src;

  src = GST_CAMERASRC (parent);

  if (src->silent == FALSE)
    g_print ("I'm plugged, therefore I'm in.\n");

  /* just push out the incoming buffer without touching it */
  return gst_pad_push (src->srcpad, buf);
}
#endif

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 * then GST_PLUGIN_DEFINE will work
 */
static gboolean
camerasrc_init (GstPlugin * Plugin)
{
  PERF_CAMERA_ATRACE();
  if(!gst_element_register (Plugin, "icamerasrc", GST_RANK_NONE,
        GST_TYPE_CAMERASRC)){
    return FALSE;
  }
  return TRUE;
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstcamerasrc"
#endif

/* gstreamer looks for this structure to register camerasrcs
 *
 * exchange the string 'Template camerasrc' with your camerasrc description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    icamerasrc,
    "Template icamerasrc",
    camerasrc_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
    )
