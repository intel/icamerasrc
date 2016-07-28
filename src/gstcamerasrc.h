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

#ifndef __GST_CAMERASRC_H__
#define __GST_CAMERASRC_H__
#include <sys/types.h>
#include <gst/gst.h>
#include "Parameters.h"
#include <linux/videodev2.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS
#define ALIGN(val, alignment) (((val)+(alignment)-1) & ~((alignment)-1))
#define ALIGN_64(val) ALIGN(val, 64)

#define DEFAULT_FRAME_WIDTH 1920
#define DEFAULT_FRAME_HEIGHT 1080
#define DEFAULT_FRAMERATE 30

/* Default value of int type properties */
#define DEFAULT_PROP_BUFFERCOUNT 6
#define MAX_PROP_BUFFERCOUNT 10
#define MIN_PROP_BUFFERCOUNT 2
#define DEFAULT_PROP_PRINT_FPS false

/* Default value of enum type property 'io-mode':userptr */
#define DEFAULT_PROP_IO_MODE GST_CAMERASRC_IO_MODE_USERPTR
/* Default value of enum type property 'interlace-mode':any */
#define DEFAULT_PROP_INTERLACE_MODE GST_CAMERASRC_INTERLACE_FIELD_ANY
/* Default value of enum type property 'deinterlace-method':none */
#define DEFAULT_DEINTERLACE_METHOD GST_CAMERASRC_DEINTERLACE_METHOD_NONE
/* Default value of enum type property 'iris-mode':auto */
#define DEFAULT_PROP_IRIS_MODE GST_CAMERASRC_IRIS_MODE_AUTO
/* Default value of enum type property 'wdr-mode':off */
#define DEFAULT_PROP_WDR_MODE GST_CAMERASRC_WDR_MODE_OFF
/* Default value of enum type property 'blc-area-mode':off */
#define DEFAULT_PROP_BLC_AREA_MODE GST_CAMERASRC_BLC_AREA_MODE_OFF
/* Default value of enum type property 'awb-mode':auto */
#define DEFAULT_PROP_AWB_MODE GST_CAMERASRC_AWB_MODE_AUTO
/* Default value of enum type property 'nr-mode':off */
#define DEFAULT_PROP_NR_MODE GST_CAMERASRC_NR_MODE_OFF
/* Default value of enum type property 'scene-mode':auto */
#define DEFAULT_PROP_SCENE_MODE GST_CAMERASRC_SCENE_MODE_AUTO
/* Default value of enum type property 'sensor-resolution':1080p */
#define DEFAULT_PROP_SENSOR_RESOLUTION GST_CAMERASRC_SENSOR_RESOLUTION_1080P
/* Default value of enum type property 'fps':25 fps */
#define DEFAULT_PROP_FPS GST_CAMERASRC_FPS_25
/* Default value of enum type property 'ae-mode':auto */
#define DEFAULT_PROP_AE_MODE GST_CAMERASRC_AE_MODE_AUTO
/* Default value of enum type property 'ae-converge-speed':normal */
#define DEFAULT_PROP_CONVERGE_SPEED GST_CAMERASRC_CONVERGE_SPEED_NORMAL
#define DEFAULT_PROP_CONVERGE_SPEED_MODE GST_CAMERASRC_CONVERGE_SPEED_MODE_AIQ
/* Default value of enum type property 'antibanding':auto*/
#define DEFAULT_PROP_ANTIBANDING_MODE GST_CAMERASRC_ANTIBANDING_MODE_AUTO

/* Default value of string type properties */
#define DEFAULT_PROP_WP NULL
#define DEFAULT_PROP_AE_REGION NULL
#define DEFAULT_PROP_AWB_REGION NULL
#define DEFAULT_PROP_CCT_RANGE NULL
#define DEFAULT_PROP_COLOR_TRANSFORM NULL

// Macro for memcpy
#define MEMCPY_S(dest, dmax, src, smax) \
  memcpy((dest), (src), std::min((size_t)(dmax), (size_t)(smax)))

// Macro for strncpy
#define STRNCPY_S(dest, dmax, src, slen) \
   strncpy((dest), (src), std::min((size_t)(dmax), (size_t)(slen)))
enum
{
    CAMERASRC_CAPTURE_MODE_STILL = 0,
    CAMERASRC_CAPTURE_MODE_VIDEO = 1,
    CAMERASRC_CAPTURE_MODE_PREVIEW = 2,
};

typedef enum
{
  GST_CAMERASRC_DEINTERLACE_METHOD_NONE = 0,
  GST_CAMERASRC_DEINTERLACE_METHOD_SOFTWARE_BOB = 1,
  GST_CAMERASRC_DEINTERLACE_METHOD_HARDWARE_BOB = 2,
} GstCamerasrcDeinterlaceMethod;

typedef enum
{
  GST_CAMERASRC_IO_MODE_USERPTR = 0,
  GST_CAMERASRC_IO_MODE_MMAP = 1,
  GST_CAMERASRC_IO_MODE_DMA = 2,
  GST_CAMERASRC_IO_MODE_DMA_IMPORT = 3,
} GstCamerasrcIoMode;

typedef enum
{
  GST_CAMERASRC_INTERLACE_FIELD_ANY = V4L2_FIELD_ANY,
  GST_CAMERASRC_INTERLACE_FIELD_ALTERNATE = V4L2_FIELD_ALTERNATE,
} GstCamerasrcInterlaceField;

typedef enum
{
  GST_CAMERASRC_IRIS_MODE_AUTO = 0,
  GST_CAMERASRC_IRIS_MODE_MANUAL = 1,
  GST_CAMERASRC_IRIS_MODE_CUSTOMIZED = 2,
} GstCamerasrcIrisMode;

typedef enum
{
  GST_CAMERASRC_WDR_MODE_OFF = 0,
  GST_CAMERASRC_WDR_MODE_ON = 1,
  GST_CAMERASRC_WDR_MODE_AUTO = 2,
} GstCamerasrcWdrMode;

typedef enum
{
  GST_CAMERASRC_BLC_AREA_MODE_OFF = 0,
  GST_CAMERASRC_BLC_AREA_MODE_ON = 1,
} GstCamerasrcBlcAreaMode;

typedef enum
{
  GST_CAMERASRC_AWB_MODE_AUTO = 0,
  GST_CAMERASRC_AWB_MODE_PARTLY_OVERCAST = 1,
  GST_CAMERASRC_AWB_MODE_FULLY_OVERCAST = 2,
  GST_CAMERASRC_AWB_MODE_FLUORESCENT = 3,
  GST_CAMERASRC_AWB_MODE_INCANDESCENT = 4,
  GST_CAMERASRC_AWB_MODE_SUNSET = 5,
  GST_CAMERASRC_AWB_MODE_VIDEO_CONFERENCING = 6,
  GST_CAMERASRC_AWB_MODE_DAYLIGHT = 7,
  GST_CAMERASRC_AWB_MODE_CCT_RANGE = 8,
  GST_CAMERASRC_AWB_MODE_WHITE_POINT = 9,
  GST_CAMERASRC_AWB_MODE_MANUAL_GAIN = 10,
  GST_CAMERASRC_AWB_MODE_COLOR_TRANSFORM = 11,
} GstCamerasrcAwbMode;

typedef enum
{
  GST_CAMERASRC_NR_MODE_OFF = 0,
  GST_CAMERASRC_NR_MODE_AUTO = 1,
  GST_CAMERASRC_NR_MODE_NORMAL = 2,
  GST_CAMERASRC_NR_MODE_EXPERT = 3,
} GstCamerasrcNrMode;

typedef enum
{
  GST_CAMERASRC_SCENE_MODE_AUTO = 0,
  GST_CAMERASRC_SCENE_MODE_INDOOR = 1,
  GST_CAMERASRC_SCENE_MODE_OUTOOR = 2,
  GST_CAMERASRC_SCENE_MODE_DISABLED = 3,
} GstCamerasrcSceneMode;

typedef enum
{
  GST_CAMERASRC_SENSOR_RESOLUTION_1080P = 0,
  GST_CAMERASRC_SENSOR_RESOLUTION_720P = 1,
  GST_CAMERASRC_SENSOR_RESOLUTION_4K = 2,
} GstCanerasrcSensorResolution;

typedef enum
{
  GST_CAMERASRC_FPS_25 = 0,
  GST_CAMERASRC_FPS_30 = 1,
  GST_CAMERASRC_FPS_50 = 2,
  GST_CAMERASRC_FPS_60 = 3,
} GstCamerasrcFps;

typedef enum
{
  GST_CAMERASRC_AE_MODE_AUTO = 0,
  GST_CAMERASRC_AE_MODE_MANUAL = 1,
} GstCamerasrcAeMode;

typedef enum
{
  GST_CAMERASRC_CONVERGE_SPEED_NORMAL = 0,
  GST_CAMERASRC_CONVERGE_SPEED_MID = 1,
  GST_CAMERASRC_CONVERGE_SPEED_LOW = 2,
} GstCamerasrcConvergeSpeed;

typedef enum
{
  GST_CAMERASRC_CONVERGE_SPEED_MODE_AIQ = 0,
  GST_CAMERASRC_CONVERGE_SPEED_MODE_HAL = 1,
} GstCamerasrcConvergeSpeedMode;

typedef enum
{
  GST_CAMERASRC_ANTIBANDING_MODE_AUTO = 0,
  GST_CAMERASRC_ANTIBANDING_MODE_50HZ = 1,
  GST_CAMERASRC_ANTIBANDING_MODE_60HZ = 2,
  GST_CAMERASRC_ANTIBANDING_MODE_OFF = 3,
} GstCamerasrcAntibandingMode;

#define GST_TYPE_CAMERASRC \
  (gst_camerasrc_get_type())
#define GST_CAMERASRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CAMERASRC,Gstcamerasrc))
#define GST_CAMERASRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CAMERASRC,GstcamerasrcClass))
#define GST_IS_CAMERASRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CAMERASRC))
#define GST_IS_CAMERASRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CAMERASRC))

typedef struct _Gstcamerasrc Gstcamerasrc;
typedef struct _GstcamerasrcClass GstcamerasrcClass;
typedef struct _GstFpsDebug GstFpsDebug;
typedef struct _Gst3AManualControl Gst3AManualControl;

/* Used to update fps*/
struct _GstFpsDebug
{
  struct timeval dqbuf_start_tm_count,dqbuf_tm_start,qbuf_tm_end;
  double buf_count;
  double last_buf_count;
  double max_fps;
  double min_fps;
  double av_fps;
  double sum_time;
  double tm_interval;
  bool init_max_min_fps;
};

struct _Gst3AManualControl
{
  /* Image Adjustment*/
  guint sharpness;
  guint brightness;
  guint contrast;
  guint hue;
  guint saturation;
  /* Exposure Settings*/
  int iris_mode;
  guint iris_level;
  guint exposure_time;
  guint exposure_ev;
  guint gain;
  int ae_mode;
  char ae_region[128];
  int converge_speed;
  int converge_speed_mode;
  /* Backlight Settings*/
  int wdr_mode;
  int blc_area_mode;
  guint wdr_level;
  /* White Balance*/
  int awb_mode;
  char awb_region[128];
  gchar *cct_range;
  gchar *wp;
  char color_transform[64];
  guint awb_shift_r;
  guint awb_shift_g;
  guint awb_shift_b;
  guint awb_gain_r;
  guint awb_gain_g;
  guint awb_gain_b;
  /* Noise Reduction*/
  int nr_mode;
  guint overall;
  guint spatial;
  guint temporal;
  /* Video Adjustment*/
  int scene_mode;
  int sensor_resolution;
  int fps;

  int antibanding_mode;
};

using namespace icamera;
struct _Gstcamerasrc
{
  GstPushSrc element;

  GstPad *srcpad, *sinkpad;

  GstBufferPool *pool;
  /* This is used for down stream plugin buffer pool, in
   * dma-import mode, icamerasrc will get the down stream
   * buffer pool to allocate buffers */
  GstBufferPool *downstream_pool;

  /* Buffer configuration*/
  guint64 offset;
  int stream_id;
  int interlace_field;
  guint bpl;
  guint number_of_buffers;
  GstVideoInfo info;
  stream_config_t  stream_list;
  stream_t      streams[1]; //FIXME: Support only one stream now.
  camera_info_t cam_info;

  /*non-3A properties*/
  int device_id;
  int deinterlace_method;
  int io_mode;
  guint capture_mode;
  guint print_fps;
  GstFpsDebug fps_debug;

  /*3A properties */
  gboolean camera_open;
  Parameters *param;
  Gst3AManualControl man_ctl;

  /* Calculate Gstbuffer timestamp*/
  uint64_t time_end;
  uint64_t time_start;
};

struct _GstcamerasrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_camerasrc_get_type (void);

G_END_DECLS

#endif /* __GST_CAMERASRC_H__ */
