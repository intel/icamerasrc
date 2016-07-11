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
#include <stdlib.h>
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

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,

  PROP_CAPTURE_MODE,
  PROP_BUFFERCOUNT,
  PROP_PRINT_FPS,
  PROP_INTERLACE_MODE,
  PROP_DEINTERLACE_METHOD,
  PROP_DEVICE_ID,
  PROP_IO_MODE,
  /* Image Adjust-ment*/
  PROP_SHARPNESS,
  PROP_BRIGHTNESS,
  PROP_CONTRAST,
  PROP_HUE,
  PROP_SATURATION,
  /* Exposure Settings*/
  PROP_IRIS_MODE,
  PROP_IRIS_LEVEL,
  PROP_EXPOSURE_TIME,
  PROP_EXPOSURE_EV,
  PROP_GAIN,
  PROP_AE_MODE,
  PROP_AE_REGION,
  PROP_AE_CONVERGE_SPEED,
  /* Backlight Settings*/
  PROP_WDR_MODE,
  PROP_BLC_AREA_MODE,
  PROP_WDR_LEVEL,
  /* White Balance*/
  PROP_AWB_MODE,
  PROP_AWB_REGION,
  PROP_CCT_RANGE,
  PROP_WP,
  PROP_AWB_GAIN_R,
  PROP_AWB_GAIN_G,
  PROP_AWB_GAIN_B,
  PROP_AWB_SHIFT_R,
  PROP_AWB_SHIFT_G,
  PROP_AWB_SHIFT_B,
  PROP_AWB_COLOR_TRANSFORM,
  /* Noise Reduction*/
  PROP_NR_MODE,
  PROP_OVERALL,
  PROP_SPATIAL,
  PROP_TEMPORAL,
  /* Video Adjustment*/
  PROP_SCENE_MODE,
  PROP_SENSOR_RESOLUTION,
  PROP_FPS,

  PROP_ANTIBANDING_MODE,
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

static GType
gst_camerasrc_interlace_field_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType interlace_field_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_INTERLACE_FIELD_ANY,
        "interlace mode: ANY", "any"},
    {GST_CAMERASRC_INTERLACE_FIELD_ALTERNATE,
        "interlace mode: ALTERNATE", "alternate"},
    {0, NULL, NULL},
  };

  if (!interlace_field_type) {
    interlace_field_type =
        g_enum_register_static ("GstCamerasrcInterlacMode", method_types);
  }
  return interlace_field_type;

}

static GType
gst_camerasrc_deinterlace_method_get_type(void)
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

static GType
gst_camerasrc_io_mode_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType io_mode_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_IO_MODE_USERPTR,
        "UserPtr", "userptr"},
    {GST_CAMERASRC_IO_MODE_MMAP,
        "MMAP", "mmap"},
    {GST_CAMERASRC_IO_MODE_DMA,
        "DMA", "dma"},
    {GST_CAMERASRC_IO_MODE_DMA_IMPORT,
        "DMA import", "dma_import"},
    {0, NULL, NULL},
  };

  if (!io_mode_type) {
    io_mode_type = g_enum_register_static ("GstCamerasrcIoMode", method_types);
  }
  return io_mode_type;
}

static GType
gst_camerasrc_device_id_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType device_type = 0;

  int count = get_number_of_cameras();
  static GEnumValue *method_types = new GEnumValue[count+1];
  camera_info_t cam_info;
  int id, ret = 0;

  for (id = 0; id < count; id++) {
      ret = get_camera_info(id, cam_info);
      if (ret < 0) {
          g_print("failed to get device name.");
          return FALSE;
      }

      method_types[id].value = id;
      method_types[id].value_name = cam_info.description;
      method_types[id].value_nick = cam_info.name;
  }

  /* the last element of array should be set NULL*/
  method_types[id].value = 0;
  method_types[id].value_name = NULL;
  method_types[id].value_nick = NULL;

  if (!device_type)
    device_type = g_enum_register_static("GstCamerasrcDeviceName", method_types);

  return device_type;
}

static GType
gst_camerasrc_iris_mode_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType iris_mode_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_IRIS_MODE_AUTO,
        "Auto", "auto"},
    {GST_CAMERASRC_IRIS_MODE_MANUAL,
        "Manual", "manual"},
    {GST_CAMERASRC_IRIS_MODE_CUSTOMIZED,
        "Customized", "customized"},
    {0, NULL, NULL},
  };

  if (!iris_mode_type) {
    iris_mode_type = g_enum_register_static ("GstCamerasrcIrisMode", method_types);
   }
  return iris_mode_type;
}

static GType
gst_camerasrc_wdr_mode_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType wdr_mode_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_WDR_MODE_OFF,
          "Non-WDR mode", "off"},
    {GST_CAMERASRC_WDR_MODE_ON,
          "WDR mode", "on"},
    {GST_CAMERASRC_WDR_MODE_AUTO,
          "Auto", "auto"},
     {0, NULL, NULL},
   };

  if (!wdr_mode_type) {
    wdr_mode_type = g_enum_register_static ("GstCamerasrcWdrMode", method_types);
  }
  return wdr_mode_type;
}

static GType
gst_camerasrc_blc_area_mode_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType blc_area_mode_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_BLC_AREA_MODE_OFF,
          "Off", "off"},
    {GST_CAMERASRC_BLC_AREA_MODE_ON,
          "On", "on"},
     {0, NULL, NULL},
   };

  if (!blc_area_mode_type) {
    blc_area_mode_type = g_enum_register_static ("GstCamerasrcBlcAreaMode", method_types);
  }
  return blc_area_mode_type;
}

static GType
gst_camerasrc_awb_mode_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType awb_mode_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_AWB_MODE_AUTO,
          "Auto", "auto"},
    {GST_CAMERASRC_AWB_MODE_PARTLY_OVERCAST,
          "Partly overcast", "partly_overcast"},
    {GST_CAMERASRC_AWB_MODE_FULLY_OVERCAST,
          "Fully overcast", "fully_overcast"},
    {GST_CAMERASRC_AWB_MODE_FLUORESCENT,
          "Fluorescent", "fluorescent"},
    {GST_CAMERASRC_AWB_MODE_INCANDESCENT,
          "Incandescent", "incandescent"},
    {GST_CAMERASRC_AWB_MODE_SUNSET,
          "Sunset", "sunset"},
    {GST_CAMERASRC_AWB_MODE_VIDEO_CONFERENCING,
          "Video conferencing", "video_conferencing"},
    {GST_CAMERASRC_AWB_MODE_DAYLIGHT,
          "Daylight", "daylight"},
    {GST_CAMERASRC_AWB_MODE_CCT_RANGE,
          "CCT range", "cct_range"},
    {GST_CAMERASRC_AWB_MODE_WHITE_POINT,
          "White point", "white_point"},
    {GST_CAMERASRC_AWB_MODE_MANUAL_GAIN,
          "Manual gain", "manual_gain"},
    {GST_CAMERASRC_AWB_MODE_COLOR_TRANSFORM,
          "Color Transform", "color_transform"},
    {0, NULL, NULL},
   };

  if (!awb_mode_type) {
    awb_mode_type = g_enum_register_static ("GstCamerasrcAwbMode", method_types);
  }
  return awb_mode_type;
}

static GType
gst_camerasrc_nr_mode_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType nr_mode_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_NR_MODE_OFF,
          "Turn off noise filter", "off"},
    {GST_CAMERASRC_NR_MODE_AUTO,
          "Completely auto noise reduction", "auto"},
    {GST_CAMERASRC_NR_MODE_NORMAL,
          "Manual-Normal", "normal"},
    {GST_CAMERASRC_NR_MODE_EXPERT,
          "Manual-Expert", "expert"},
     {0, NULL, NULL},
   };

  if (!nr_mode_type) {
    nr_mode_type = g_enum_register_static ("GstCamerasrcNrMode", method_types);
  }
  return nr_mode_type;
}

static GType
gst_camerasrc_scene_mode_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType scene_mode_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_SCENE_MODE_AUTO,
          "Auto", "auto"},
    {GST_CAMERASRC_SCENE_MODE_INDOOR,
          "Indorr", "indoor"},
    {GST_CAMERASRC_SCENE_MODE_OUTOOR,
          "Outdoor", "outdoor"},
    {GST_CAMERASRC_SCENE_MODE_DISABLED,
          "Disabled", "disabled"},
     {0, NULL, NULL},
   };

  if (!scene_mode_type) {
    scene_mode_type = g_enum_register_static ("GstCamerasrcSceneMode", method_types);
  }
  return scene_mode_type;
}

static GType
gst_camerasrc_sensor_resolution_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType sensor_resolution_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_SENSOR_RESOLUTION_1080P,
          "1080P", "1080p"},
    {GST_CAMERASRC_SENSOR_RESOLUTION_720P,
          "720P", "720p"},
    {GST_CAMERASRC_SENSOR_RESOLUTION_4K,
          "4K", "4K"},
     {0, NULL, NULL},
   };

  if (!sensor_resolution_type) {
    sensor_resolution_type = g_enum_register_static ("GstCamerasrcSensorResolution", method_types);
  }
  return sensor_resolution_type;
}

static GType
gst_camerasrc_fps_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType fps_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_FPS_25,
          "25fps", "25"},
    {GST_CAMERASRC_FPS_30,
          "30fps", "30"},
    {GST_CAMERASRC_FPS_50,
          "50fps", "50"},
    {GST_CAMERASRC_FPS_60,
          "60fps", "60"},
    {0, NULL, NULL},
   };

  if (!fps_type) {
    fps_type = g_enum_register_static ("GstCamerasrcFps", method_types);
  }
  return fps_type;
}

static GType
gst_camerasrc_ae_mode_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType ae_mode_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_AE_MODE_AUTO,
          "Auto", "auto"},
    {GST_CAMERASRC_AE_MODE_MANUAL,
          "Manual", "manual"},
     {0, NULL, NULL},
   };

  if (!ae_mode_type) {
    ae_mode_type = g_enum_register_static ("GstCamerasrcAeMode", method_types);
  }
  return ae_mode_type;
}

static GType
gst_camerasrc_ae_converge_speed_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType ae_cvg_speed_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_AE_CONVERGE_SPEED_NORMAL,
          "NORMAL", "normal"},
    {GST_CAMERASRC_AE_CONVERGE_SPEED_MID,
          "MID", "mid"},
    {GST_CAMERASRC_AE_CONVERGE_SPEED_LOW,
          "LOW", "low"},
     {0, NULL, NULL},
   };

  if (!ae_cvg_speed_type) {
    ae_cvg_speed_type = g_enum_register_static ("GstCamerasrcAeConvergeSpeed", method_types);
  }
  return ae_cvg_speed_type;
}

static GType
gst_camerasrc_antibanding_mode_get_type(void)
{
  PERF_CAMERA_ATRACE();
  static GType antibanding_mode_type = 0;

  static const GEnumValue method_types[] = {
    {GST_CAMERASRC_ANTIBANDING_MODE_AUTO,
          "Auto", "auto"},
    {GST_CAMERASRC_ANTIBANDING_MODE_50HZ,
          "50Hz", "50"},
    {GST_CAMERASRC_ANTIBANDING_MODE_60HZ,
          "60HZ", "60"},
    {GST_CAMERASRC_ANTIBANDING_MODE_OFF,
          "Off", "off"},
    {0, NULL, NULL},
   };

  if (!antibanding_mode_type) {
    antibanding_mode_type = g_enum_register_static ("GstCamerasrcAntibandingMode", method_types);
  }
  return antibanding_mode_type;
}

static void
gst_camerasrc_dispose(GObject *object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_camerasrc_finalize (Gstcamerasrc *camerasrc)
{
  PERF_CAMERA_ATRACE();
  if (camerasrc->device_id >= 0 && camerasrc->camera_open) {
    camera_device_stop(camerasrc->device_id);
    camerasrc->stream_id = -1;
    camera_device_close(camerasrc->device_id);
  }

  camera_hal_deinit();
  camerasrc->camera_open = false;
  delete camerasrc->param;
  camerasrc->param = NULL;

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (camerasrc));
}

static void
gst_camerasrc_class_init (GstcamerasrcClass * klass)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
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

  g_object_class_install_property(gobject_class,PROP_CAPTURE_MODE,
      g_param_spec_int("capture-mode","capture mode","In which mode will implement preview/video/still",
        0,G_MAXINT,CAMERASRC_CAPTURE_MODE_PREVIEW,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_BUFFERCOUNT,
      g_param_spec_int("buffer-count","buffer count","The number of buffer to allocate when do the streaming",
        MIN_PROP_BUFFERCOUNT,MAX_PROP_BUFFERCOUNT,DEFAULT_PROP_BUFFERCOUNT,
        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_PRINT_FPS,
      g_param_spec_boolean("printfps","printfps","Whether print the FPS when do the streaming",
        DEFAULT_PROP_PRINT_FPS,(GParamFlags)(G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE)));

  g_object_class_install_property (gobject_class, PROP_INTERLACE_MODE,
      g_param_spec_enum ("interlace-mode", "interlace-mode", "The interlace method",
        gst_camerasrc_interlace_field_get_type(), DEFAULT_PROP_INTERLACE_MODE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_DEINTERLACE_METHOD,
      g_param_spec_enum ("deinterlace-method", "Deinterlace method", "The deinterlace method that icamerasrc run",
        gst_camerasrc_deinterlace_method_get_type(), DEFAULT_DEINTERLACE_METHOD, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

 /* DEFAULT_PROP_DEVICE_ID is defined at configure time, user can configure with custom device ID */
 g_object_class_install_property (gobject_class,PROP_DEVICE_ID,
      g_param_spec_enum("device-name","device-name","The input devices name queried from HAL",
   gst_camerasrc_device_id_get_type(), DEFAULT_PROP_DEVICE_ID, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_IO_MODE,
      g_param_spec_enum ("io-mode", "IO mode", "The memory types of the frame buffer",
          gst_camerasrc_io_mode_get_type(), DEFAULT_PROP_IO_MODE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_SHARPNESS,
      g_param_spec_int("sharpness","sharpness","sharpness",
        -128,127,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_BRIGHTNESS,
      g_param_spec_int("brightness","brightness","brightness",
        -128,127,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_CONTRAST,
      g_param_spec_int("contrast","contrast","contrast",
        -128,127,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_HUE,
      g_param_spec_int("hue","hue","hue",
        -128,127,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_SATURATION,
      g_param_spec_int("saturation","saturation","saturation",
        -128,127,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_IRIS_MODE,
      g_param_spec_enum ("iris-mode", "IRIS mode", "IRIS mode",
          gst_camerasrc_iris_mode_get_type(), DEFAULT_PROP_IRIS_MODE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_IRIS_LEVEL,
      g_param_spec_int("iris-level","IRIS level","The percentage of opening in IRIS",
        0,100,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_EXPOSURE_TIME,
      g_param_spec_int("exposure-time","Exposure time","Exposure time(only valid in manual AE mode)",
        0,1000000,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_GAIN,
      g_param_spec_int("gain","Gain","Implement total gain or maximal gain(only valid in manual AE mode).Unit: db",
        0,60,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_WDR_MODE,
      g_param_spec_enum ("wdr-mode", "WDR mode", "WDR mode",
          gst_camerasrc_wdr_mode_get_type(), DEFAULT_PROP_WDR_MODE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_BLC_AREA_MODE,
      g_param_spec_enum ("blc-area-mode", "BLC area mode", "BLC area mode",
          gst_camerasrc_blc_area_mode_get_type(), DEFAULT_PROP_BLC_AREA_MODE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_WDR_LEVEL,
      g_param_spec_int("wdr-level","WDR level","WDR level",
        0,15,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_AWB_MODE,
      g_param_spec_enum ("awb-mode", "AWB mode", "White balance mode",
          gst_camerasrc_awb_mode_get_type(), DEFAULT_PROP_AWB_MODE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_AWB_GAIN_R,
      g_param_spec_int("awb-gain-r","AWB R-gain","Manual white balance gains",
        0,255,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_AWB_GAIN_G,
      g_param_spec_int("awb-gain-g","AWB G-gain","Manual white balance gains",
        0,255,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class,PROP_AWB_GAIN_B,
      g_param_spec_int("awb-gain-b","AWB B-gain","Manual white balance gains",
        0,255,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_AWB_COLOR_TRANSFORM,
      g_param_spec_string("color-transform","AWB color transform","A 3x3 matrix for AWB color transform",
        DEFAULT_PROP_COLOR_TRANSFORM, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_NR_MODE,
      g_param_spec_enum ("nr-mode", "NR mode", "Noise reduction mode",
          gst_camerasrc_nr_mode_get_type(), DEFAULT_PROP_NR_MODE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SCENE_MODE,
      g_param_spec_enum ("scene-mode", "Scene mode", "Scene mode",
          gst_camerasrc_scene_mode_get_type(), DEFAULT_PROP_SCENE_MODE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SENSOR_RESOLUTION,
      g_param_spec_enum ("sensor-resolution", "Sensor resolution", "Sensor resolution",
          gst_camerasrc_sensor_resolution_get_type(), DEFAULT_PROP_SENSOR_RESOLUTION, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_FPS,
      g_param_spec_enum ("fps", "Framerate", "Framerate",
          gst_camerasrc_fps_get_type(), DEFAULT_PROP_FPS, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_AE_MODE,
      g_param_spec_enum ("ae-mode", "AE mode", "AE mode",
          gst_camerasrc_ae_mode_get_type(), DEFAULT_PROP_AE_MODE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_AE_CONVERGE_SPEED,
      g_param_spec_enum ("ae-converge-speed", "AE Converge Speed", "AE Converge Speed",
          gst_camerasrc_ae_converge_speed_get_type(), DEFAULT_PROP_AE_CONVERGE_SPEED, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_EXPOSURE_EV,
      g_param_spec_int("ev","Exposure Ev","Exposure Ev",
          -4,4,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_CCT_RANGE,
      g_param_spec_string("cct-range","CCT range","CCT range(only valid for manual AWB mode)",
        DEFAULT_PROP_CCT_RANGE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_WP,
      g_param_spec_string("wp-point","White point","White point coordinate(only valid for manual AWB mode)",
        DEFAULT_PROP_WP, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_AWB_SHIFT_R,
      g_param_spec_int("awb-shift-r","AWB shift-R","AWB shift-R",
        0,255,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_AWB_SHIFT_G,
      g_param_spec_int("awb-shift-g","AWB shift-G","AWB shift-G",
        0,255,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_AWB_SHIFT_B,
      g_param_spec_int("awb-shift-b","AWB shift-B","AWB shift-B",
        0,255,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_AE_REGION,
      g_param_spec_string("ae-region","AE region","AE region",
        DEFAULT_PROP_AE_REGION, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_AWB_REGION,
      g_param_spec_string("awb-region","AWB region","AWB region",
        DEFAULT_PROP_AWB_REGION, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ANTIBANDING_MODE,
      g_param_spec_enum ("antibanding-mode", "Antibanding Mode", "Antibanding Mode",
          gst_camerasrc_antibanding_mode_get_type(), DEFAULT_PROP_ANTIBANDING_MODE, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_OVERALL,
      g_param_spec_int("overall","Overall","NR level: Overall",
        0,100,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_SPATIAL,
      g_param_spec_int("spatial","Spatial","NR level: Spatial",
        0,100,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class,PROP_TEMPORAL,
      g_param_spec_int("temporal","Temporal","NR level: Temporal",
        0,100,0,(GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

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
  GST_INFO("@%s\n",__func__);

  camerasrc->pool = NULL;
  camerasrc->downstream_pool = NULL;

  /* no need to add anything to init pad*/
  gst_base_src_set_format (GST_BASE_SRC (camerasrc), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (camerasrc), TRUE);
  camerasrc->stream_id = -1;
  camerasrc->number_of_buffers = DEFAULT_PROP_BUFFERCOUNT;
  camerasrc->capture_mode = DEFAULT_DEINTERLACE_METHOD;
  camerasrc->interlace_field = DEFAULT_PROP_INTERLACE_MODE;
  camerasrc->device_id = DEFAULT_PROP_DEVICE_ID;
  camerasrc->camera_open = false;

  /* set default value for 3A manual control*/
  camerasrc->param = new Parameters;
  memset(&(camerasrc->man_ctl), 0, sizeof(camerasrc->man_ctl));
  memset(camerasrc->man_ctl.ae_region, 0, sizeof(camerasrc->man_ctl.ae_region));
  memset(camerasrc->man_ctl.awb_region, 0, sizeof(camerasrc->man_ctl.awb_region));
  memset(camerasrc->man_ctl.color_transform, 0, sizeof(camerasrc->man_ctl.color_transform));
  camerasrc->man_ctl.iris_mode = DEFAULT_PROP_IRIS_MODE;
  camerasrc->man_ctl.wdr_mode = DEFAULT_PROP_WDR_MODE;
  camerasrc->man_ctl.blc_area_mode = DEFAULT_PROP_BLC_AREA_MODE;
  camerasrc->man_ctl.awb_mode = DEFAULT_PROP_AWB_MODE;
  camerasrc->man_ctl.nr_mode = DEFAULT_PROP_NR_MODE;
  camerasrc->man_ctl.scene_mode = DEFAULT_PROP_SCENE_MODE;
  camerasrc->man_ctl.sensor_resolution = DEFAULT_PROP_SENSOR_RESOLUTION;
  camerasrc->man_ctl.fps = DEFAULT_PROP_FPS;
  camerasrc->man_ctl.ae_mode = DEFAULT_PROP_AE_MODE;
  camerasrc->man_ctl.wp = DEFAULT_PROP_WP;
  camerasrc->man_ctl.antibanding_mode = DEFAULT_PROP_ANTIBANDING_MODE;
}

/**
  * parse the region string from app, the string is similar to:
  * 161,386,186,411,1;404,192,429,217,1;246,85,271,110,1;164,271,189,296,1;418,240,443,265,1;
  */
static int
gst_camerasrc_get_region_vector(const char *reg_str, camera_window_list_t &region)
{
  if (reg_str == NULL) {
    g_print("the region string is empty\n");
    return -1;
  }

  char *attr = strdup(reg_str);
  if (attr == NULL) {
    g_print("failed to allocate attr memory\n");
    return -1;
  }
  char *str = attr;
  camera_window_t window;
  char *tmpStr = NULL, *token = NULL;

  while ((tmpStr = strstr(str, ";")) != NULL) {
      *tmpStr = '\0';

      token = strtok(str, ",");
      if (NULL == token) {
          g_print("failed to parse the left string.\n");
          break;
      }
      window.left = atoi(token);
      token = strtok(NULL, ",");
      if (NULL == token) {
          g_print("failed to parse the top string.\n");
          break;
      }
      window.top = atoi(token);
      token = strtok(NULL, ",");
      if (NULL == token) {
          g_print("failed to parse the right string.\n");
          break;
      }
      window.right = atoi(token);
      token = strtok(NULL, ",");
      if (NULL == token) {
          g_print("failed to parse the bottom string.\n");
          break;
      }
      window.bottom = atoi(token);
      token = strtok(NULL, ",");
      if (NULL == token) {
          g_print("failed to parse the weight string.\n");
          break;
      }
      window.weight = atoi(token);
      region.push_back(window);
      str = tmpStr + 1;
  };
  free(attr);

  return (NULL == token) ? -1 : 0;
}


/**
  * parse the string to two-dimensional matrix with float type, the string is similar to:
  * -1.5, -1.5, 0, -2.0, -2.0, 1.0, 1.5, 2.0, 0.5....
  */
static int
gst_camerasrc_parse_string_to_matrix(const char *tra_str, float **array, int row, int cul)
{
  if (tra_str == NULL) {
    g_print("the color transform string is empty\n");
    return -1;
  }

  char *attr = strdup(tra_str);
  if (attr == NULL) {
    g_print("failed to allocate attr memory\n");
    return -1;
  }
  float value = 0.0;
  int i = 0, j = 0;
  char *str = attr;
  char *token = strtok(str, ", ");

  for (i = 0; i < row; i++) {
    for (j = 0; j < cul; j++) {
      if (NULL == token) {
        g_print("failed to parse the color transform string.\n");
        free(attr);
        return -1;
      }
      value = atof(token);
      if (value < -2.0)
        value = -2.0;
      if (value > 2.0)
        value = 2.0;
      *((float*)array + row*i + j) = value;
      token = strtok(NULL, ", ");
    }
  }

  free(attr);
  return 0;
}

/**
  * parse cct_range property, assign max and min value to  camera_range_t
  */
static int
gst_camerasrc_parse_cct_range(Gstcamerasrc *src, gchar *cct_range_str, camera_range_t &cct_range)
{
  char *token = NULL;
  char cct_range_array[64]={'\0'};

  strncpy(cct_range_array, cct_range_str, 64);
  cct_range_array[63] = '\0';
  token = strtok(cct_range_array,"~");
  if (token == NULL) {
      GST_ERROR_OBJECT(src, "failed to acquire cct range.");
      return -1;
  }
  cct_range.min = atoi(token);
  cct_range.max = atoi(src->man_ctl.cct_range+strlen(token)+1);
  if (cct_range.min < 1800)
      cct_range.min = 1800;
  if (cct_range.max > 15000)
      cct_range.max = 15000;

  return 0;
}


/**
  * parse white point property, assign x, y to camera_coordinate_t
  */
static int
gst_camerasrc_parse_white_point(Gstcamerasrc *src, gchar *wp_str, camera_coordinate_t &white_point)
{
  char *token = NULL;
  char white_point_array[64]={'\0'};

  strncpy(white_point_array, wp_str, 64);
  white_point_array[63] = '\0';
  token = strtok(white_point_array,",");
  if (token == NULL) {
      GST_ERROR_OBJECT(src, "failed to acquire white point.");
      return -1;
  }
  white_point.x = atoi(token);
  white_point.y = atoi(src->man_ctl.wp+strlen(token)+1);

  return 0;
}

static void
gst_camerasrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  PERF_CAMERA_ATRACE();
  Gstcamerasrc *src = GST_CAMERASRC (object);
  int ret = 0;

  gboolean manual_setting = true;
  camera_awb_gains_t awb_gain;
  camera_image_enhancement_t img_enhancement;
  camera_window_list_t region;
  camera_color_transform_t transform;
  camera_range_t cct_range;
  camera_coordinate_t white_point;

  memset(&img_enhancement, 0, sizeof(camera_image_enhancement_t));
  memset(&awb_gain, 0, sizeof(camera_awb_gains_t));

  switch (prop_id) {
    case PROP_CAPTURE_MODE:
      manual_setting = false;
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
      manual_setting = false;
      src->number_of_buffers = g_value_get_int (value);
      break;
    case PROP_PRINT_FPS:
      manual_setting = false;
      src->print_fps = g_value_get_boolean(value);
      break;
    case PROP_INTERLACE_MODE:
      manual_setting = false;
      src->interlace_field = g_value_get_enum (value);
      break;
    case PROP_DEINTERLACE_METHOD:
      manual_setting = false;
      src->deinterlace_method = g_value_get_enum (value);
      break;
    case PROP_IO_MODE:
      manual_setting = false;
      src->io_mode = g_value_get_enum (value);
      break;
    case PROP_DEVICE_ID:
      manual_setting = false;
      src->device_id = g_value_get_enum (value);
      ret   = get_camera_info(src->device_id, src->cam_info);
      if (ret < 0) {
          GST_ERROR_OBJECT(src, "failed to get device name.");
          return;
      }
      break;
    case PROP_SHARPNESS:
      src->param->getImageEnhancement(img_enhancement);
      img_enhancement.sharpness = g_value_get_int (value);
      src->param->setImageEnhancement(img_enhancement);
      src->man_ctl.sharpness = img_enhancement.sharpness;
      break;
    case PROP_BRIGHTNESS:
      src->param->getImageEnhancement(img_enhancement);
      img_enhancement.brightness = g_value_get_int (value);
      src->param->setImageEnhancement(img_enhancement);
      src->man_ctl.brightness = img_enhancement.brightness;
      break;
    case PROP_CONTRAST:
      src->param->getImageEnhancement(img_enhancement);
      img_enhancement.contrast = g_value_get_int (value);
      src->param->setImageEnhancement(img_enhancement);
      src->man_ctl.contrast = img_enhancement.contrast;
      break;
    case PROP_HUE:
      src->param->getImageEnhancement(img_enhancement);
      img_enhancement.hue = g_value_get_int (value);
      src->param->setImageEnhancement(img_enhancement);
      src->man_ctl.hue = img_enhancement.hue;
      break;
    case PROP_SATURATION:
      src->param->getImageEnhancement(img_enhancement);
      img_enhancement.saturation = g_value_get_int (value);
      src->param->setImageEnhancement(img_enhancement);
      src->man_ctl.saturation = img_enhancement.saturation;
      break;
    case PROP_IRIS_MODE:
      src->param->setIrisMode((camera_iris_mode_t)g_value_get_enum(value));
      src->man_ctl.iris_mode = g_value_get_enum (value);
      break;
    case PROP_IRIS_LEVEL:
      src->param->setIrisLevel(g_value_get_int(value));
      src->man_ctl.iris_level = g_value_get_int (value);
      break;
    case PROP_EXPOSURE_TIME:
      src->param->setExposureTime((int64_t)g_value_get_int (value));
      src->man_ctl.exposure_time = g_value_get_int (value);
      break;
    case PROP_GAIN:
      src->param->setSensitivityGain((float)g_value_get_int (value));
      src->man_ctl.gain = g_value_get_int (value);
      break;
    case PROP_WDR_MODE:
      src->param->setWdrMode((camera_wdr_mode_t)g_value_get_enum(value));
      src->man_ctl.wdr_mode = g_value_get_enum (value);
      /* W/A: When wdr-mode is on and specific camera is selected,
       *      switch to another camera ID and pass on to HAL.
       */
      if (src->man_ctl.wdr_mode == GST_CAMERASRC_WDR_MODE_ON && src->device_id == 2)
        src->device_id = 14;
      break;
    case PROP_BLC_AREA_MODE:
      src->param->setBlcAreaMode((camera_blc_area_mode_t)g_value_get_enum(value));
      src->man_ctl.blc_area_mode = g_value_get_enum (value);
      break;
    case PROP_WDR_LEVEL:
      src->param->setWdrLevel(g_value_get_int (value));
      src->man_ctl.wdr_level = g_value_get_int (value);
      break;
    case PROP_AWB_MODE:
      src->param->setAwbMode((camera_awb_mode_t)g_value_get_enum(value));
      src->man_ctl.awb_mode = g_value_get_enum (value);
      break;
    case PROP_AWB_GAIN_R:
      src->param->getAwbGains(awb_gain);
      awb_gain.r_gain = g_value_get_int (value);
      src->param->setAwbGains(awb_gain);
      src->man_ctl.awb_gain_r = awb_gain.r_gain;
      break;
    case PROP_AWB_GAIN_G:
      src->param->getAwbGains(awb_gain);
      awb_gain.g_gain = g_value_get_int (value);
      src->param->setAwbGains(awb_gain);
      src->man_ctl.awb_gain_g = awb_gain.g_gain;
      break;
    case PROP_AWB_GAIN_B:
      src->param->getAwbGains(awb_gain);
      awb_gain.b_gain = g_value_get_int (value);
      src->param->setAwbGains(awb_gain);
      src->man_ctl.awb_gain_b = awb_gain.b_gain;
      break;
    case PROP_NR_MODE:
      src->param->setNrMode((camera_nr_mode_t)g_value_get_enum(value));
      src->man_ctl.nr_mode = g_value_get_enum (value);
      break;
    case PROP_SCENE_MODE:
      src->param->setSceneMode((camera_scene_mode_t)g_value_get_enum(value));
      src->man_ctl.scene_mode = g_value_get_enum (value);
      break;
    case PROP_SENSOR_RESOLUTION:
      //implement this in the future.
      src->man_ctl.sensor_resolution = g_value_get_enum (value);
      break;
    case PROP_FPS:
      //didn't implement in hal
      src->man_ctl.fps = g_value_get_enum (value);
      break;
    case PROP_AE_MODE:
      src->param->setAeMode((camera_ae_mode_t)g_value_get_enum(value));
      src->man_ctl.ae_mode = g_value_get_enum(value);
      break;
    case PROP_AE_CONVERGE_SPEED:
      src->param->setAeConvergeSpeed((camera_converge_speed_t)g_value_get_enum(value));
      src->param->setAwbConvergeSpeed((camera_converge_speed_t)g_value_get_enum(value));
      break;
    case PROP_EXPOSURE_EV:
      src->param->setAeCompensation(g_value_get_int(value));
      src->man_ctl.exposure_ev = g_value_get_int (value);
      break;
    case PROP_CCT_RANGE:
      g_free(src->man_ctl.cct_range);
      src->man_ctl.cct_range = g_strdup(g_value_get_string (value));
      src->param->getAwbCctRange(cct_range);
      ret = gst_camerasrc_parse_cct_range(src, src->man_ctl.cct_range, cct_range);
      if (ret == 0)
        src->param->setAwbCctRange(cct_range);
      break;
    case PROP_WP:
      g_free(src->man_ctl.wp);
      src->man_ctl.wp = g_strdup(g_value_get_string (value));
      src->param->getAwbWhitePoint(white_point);
      ret = gst_camerasrc_parse_white_point(src, src->man_ctl.wp, white_point);
      if (ret == 0)
        src->param->setAwbWhitePoint(white_point);
      break;
    case PROP_AWB_SHIFT_R:
      src->param->getAwbGainShift(awb_gain);
      awb_gain.r_gain = g_value_get_int (value);
      src->param->setAwbGainShift(awb_gain);
      src->man_ctl.awb_shift_r = awb_gain.r_gain;
      break;
    case PROP_AWB_SHIFT_G:
      src->param->getAwbGainShift(awb_gain);
      awb_gain.g_gain = g_value_get_int (value);
      src->param->setAwbGainShift(awb_gain);
      src->man_ctl.awb_shift_g = awb_gain.g_gain;
      break;
    case PROP_AWB_SHIFT_B:
      src->param->getAwbGainShift(awb_gain);
      awb_gain.b_gain = g_value_get_int (value);
      src->param->setAwbGainShift(awb_gain);
      src->man_ctl.awb_shift_b = awb_gain.b_gain;
      break;
    case PROP_AE_REGION:
      if (gst_camerasrc_get_region_vector(g_value_get_string (value), region) == 0) {
        src->param->setAeRegions(region);
        strncpy(src->man_ctl.ae_region, g_value_get_string(value), (sizeof(src->man_ctl.ae_region)-1));
      }
      break;
    case PROP_AWB_REGION:
      if (gst_camerasrc_get_region_vector(g_value_get_string (value), region) == 0) {
        src->param->setAwbRegions(region);
        strncpy(src->man_ctl.awb_region, g_value_get_string(value), (sizeof(src->man_ctl.awb_region)-1));
      }
      break;
    case PROP_AWB_COLOR_TRANSFORM:
      ret = gst_camerasrc_parse_string_to_matrix(g_value_get_string (value),
                                    (float**)(transform.color_transform), 3, 3);
      if (ret == 0) {
        src->param->setColorTransform(transform);
        strncpy(src->man_ctl.color_transform, g_value_get_string(value), (sizeof(src->man_ctl.color_transform)-1));
      }
      break;
    case PROP_ANTIBANDING_MODE:
      src->param->setAntiBandingMode((camera_antibanding_mode_t)g_value_get_enum(value));
      src->man_ctl.antibanding_mode = g_value_get_enum (value);
      break;
    case PROP_OVERALL:
      src->man_ctl.overall = g_value_get_int (value);
      break;
    case PROP_SPATIAL:
      src->man_ctl.spatial = g_value_get_int (value);
      break;
    case PROP_TEMPORAL:
      src->man_ctl.temporal = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  if (manual_setting && src->camera_open) {
      camera_set_parameters(src->device_id, *(src->param));
  }

}

static void
gst_camerasrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstcamerasrc *src = GST_CAMERASRC (object);

  switch (prop_id) {
    case PROP_CAPTURE_MODE:
      g_value_set_int (value, src->capture_mode);
      break;
    case PROP_BUFFERCOUNT:
      g_value_set_int (value, src->number_of_buffers);
      break;
    case PROP_PRINT_FPS:
      g_value_set_boolean(value,src->print_fps);
      break;
    case PROP_INTERLACE_MODE:
      g_value_set_enum (value, src->interlace_field);
      break;
    case PROP_DEINTERLACE_METHOD:
      g_value_set_enum (value, src->deinterlace_method);
      break;
    case PROP_IO_MODE:
      g_value_set_enum (value, src->io_mode);
      break;
    case PROP_DEVICE_ID:
      g_value_set_enum (value, src->device_id);
      break;
    case PROP_SHARPNESS:
      g_value_set_int (value, src->man_ctl.sharpness);
      break;
    case PROP_BRIGHTNESS:
      g_value_set_int (value, src->man_ctl.brightness);
      break;
    case PROP_CONTRAST:
      g_value_set_int (value, src->man_ctl.contrast);
      break;
    case PROP_HUE:
      g_value_set_int (value, src->man_ctl.hue);
      break;
    case PROP_SATURATION:
      g_value_set_int (value, src->man_ctl.saturation);
      break;
    case PROP_IRIS_MODE:
      g_value_set_enum (value, src->man_ctl.iris_mode);
      break;
    case PROP_IRIS_LEVEL:
      g_value_set_int (value, src->man_ctl.iris_level);
      break;
    case PROP_EXPOSURE_TIME:
      g_value_set_int (value, src->man_ctl.exposure_time);
      break;
    case PROP_GAIN:
      g_value_set_int (value, src->man_ctl.gain);
      break;
    case PROP_WDR_MODE:
      g_value_set_enum (value, src->man_ctl.wdr_mode);
      break;
    case PROP_BLC_AREA_MODE:
      g_value_set_enum (value, src->man_ctl.blc_area_mode);
      break;
    case PROP_WDR_LEVEL:
      g_value_set_int (value, src->man_ctl.wdr_level);
      break;
    case PROP_AWB_MODE:
      g_value_set_enum (value, src->man_ctl.awb_mode);
      break;
    case PROP_AWB_GAIN_R:
      g_value_set_int (value, src->man_ctl.awb_gain_r);
      break;
    case PROP_AWB_GAIN_G:
      g_value_set_int (value, src->man_ctl.awb_gain_g);
      break;
    case PROP_AWB_GAIN_B:
      g_value_set_int (value, src->man_ctl.awb_gain_b);
      break;
    case PROP_NR_MODE:
      g_value_set_enum (value, src->man_ctl.nr_mode);
      break;
    case PROP_SCENE_MODE:
      g_value_set_enum (value, src->man_ctl.scene_mode);
      break;
   case PROP_SENSOR_RESOLUTION:
      g_value_set_enum (value, src->man_ctl.sensor_resolution);
      break;
    case PROP_FPS:
      g_value_set_enum (value, src->man_ctl.fps);
      break;
    case PROP_AE_MODE:
      g_value_set_enum (value, src->man_ctl.ae_mode);
      break;
    case PROP_AE_CONVERGE_SPEED:
      g_value_set_enum (value, src->man_ctl.ae_converge_speed);
      break;
    case PROP_EXPOSURE_EV:
      g_value_set_int (value, src->man_ctl.exposure_ev);
      break;
    case PROP_CCT_RANGE:
      g_value_set_string (value, src->man_ctl.cct_range);
      break;
    case PROP_WP:
      g_value_set_string (value, src->man_ctl.wp);
      break;
    case PROP_AWB_SHIFT_R:
      g_value_set_int (value, src->man_ctl.awb_shift_r);
      break;
    case PROP_AWB_SHIFT_G:
      g_value_set_int (value, src->man_ctl.awb_shift_g);
      break;
    case PROP_AWB_SHIFT_B:
      g_value_set_int (value, src->man_ctl.awb_shift_b);
      break;
    case PROP_AE_REGION:
      g_value_set_string (value, src->man_ctl.ae_region);
      break;
    case PROP_AWB_REGION:
      g_value_set_string (value, src->man_ctl.awb_region);
      break;
    case PROP_AWB_COLOR_TRANSFORM:
      g_value_set_string(value, src->man_ctl.color_transform);
      break;
    case PROP_ANTIBANDING_MODE:
      g_value_set_enum (value, src->man_ctl.antibanding_mode);
      break;
    case PROP_OVERALL:
      g_value_set_int (value, src->man_ctl.overall);
      break;
    case PROP_SPATIAL:
      g_value_set_int (value, src->man_ctl.spatial);
      break;
    case PROP_TEMPORAL:
      g_value_set_int (value, src->man_ctl.temporal);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
  * Find match stream from the support list in cam_info
  * if not found, return false
  */
static gboolean
gst_camerasrc_find_match_stream(Gstcamerasrc* camerasrc,
                                int format, int width, int height, int field)
{
    stream_array_t configs;
    int ret = FALSE;
    camerasrc->cam_info.capability->getSupportedStreamConfig(configs);

    for (unsigned int i = 0; i < configs.size(); i++) {
        if (width == configs[i].width && height == configs[i].height &&
            format == configs[i].format && field == configs[i].field) {
            camerasrc->streams[0] = configs[i];
            return TRUE;
        }
    }

    //return the first one as default
    GST_ERROR_OBJECT(camerasrc, "failed to find a match resolutions from HAL.");
    return ret;
}

/**
  * Init the HAL and return the camera information in the cam_info for the match device
  */
static gboolean
gst_camerasrc_device_probe(Gstcamerasrc* camerasrc)
{
    int ret = camera_hal_init();
    if (ret < 0) {
        GST_ERROR_OBJECT(camerasrc, "failed to init libcamhal device.");
        return FALSE;
    }

    ret = get_camera_info(camerasrc->device_id, camerasrc->cam_info);
    if (ret < 0) {
        GST_ERROR_OBJECT(camerasrc, "failed to get device name.");
        camera_hal_deinit();
        return FALSE;
    }

    return TRUE;
}

static gboolean
gst_camerasrc_get_caps_info (Gstcamerasrc* camerasrc, GstCaps * caps, stream_config_t *stream_list)
{
  PERF_CAMERA_ATRACE();
  GstStructure *structure;
  guint32 fourcc;
  const gchar *mimetype;
  GstVideoInfo info;

  fourcc = 0;
  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  /* parse format,width,height from caps */
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

  GST_DEBUG_OBJECT(camerasrc, "format %d width %d height %d", fourcc, info.width, info.height);

  int ret = gst_camerasrc_find_match_stream(camerasrc, fourcc,
                            info.width, info.height, camerasrc->interlace_field);
  if (!ret) {
    GST_ERROR_OBJECT(camerasrc, "no match stream found from HAL");
    return ret;
  }

  /* set memtype for stream*/
  switch(camerasrc->io_mode) {
    case GST_CAMERASRC_IO_MODE_USERPTR:
      camerasrc->streams[0].memType = V4L2_MEMORY_USERPTR;
      break;
    case GST_CAMERASRC_IO_MODE_DMA_IMPORT:
      camerasrc->streams[0].memType = V4L2_MEMORY_DMABUF;
      break;
    case GST_CAMERASRC_IO_MODE_DMA:
    case GST_CAMERASRC_IO_MODE_MMAP:
      camerasrc->streams[0].memType = V4L2_MEMORY_MMAP;
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

static gboolean
gst_camerasrc_set_caps(GstBaseSrc *src, GstCaps *caps)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
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

static GstCaps *
gst_camerasrc_get_caps(GstBaseSrc *src,GstCaps *filter)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
  return gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (GST_CAMERASRC(src)));
}

static gboolean
gst_camerasrc_start(GstBaseSrc *basesrc)
{
  Gstcamerasrc *camerasrc;
  GST_INFO("@%s\n",__func__);
  camerasrc = GST_CAMERASRC (basesrc);
  int ret;

  if (!gst_camerasrc_device_probe(camerasrc)) {
    GST_ERROR_OBJECT(camerasrc, "device proble failed ");
    return FALSE;
  }

  ret = camera_device_open(camerasrc->device_id);
  if (ret < 0) {
     GST_ERROR_OBJECT(camerasrc, "incorrect device_id, failed to open libcamhal device.");
     camerasrc->camera_open = false;
     camera_hal_deinit();
     return FALSE;
  } else {
     camerasrc->camera_open = true;
  }

  //set all the params first time.
  camera_set_parameters(camerasrc->device_id, *(camerasrc->param));

  return TRUE;
}

static gboolean
gst_camerasrc_stop(GstBaseSrc *basesrc)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
  return TRUE;
}

static GstStateChangeReturn
gst_camerasrc_change_state (GstElement * element, GstStateChange transition)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static GstCaps *
gst_camerasrc_fixate(GstBaseSrc * basesrc, GstCaps * caps)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
  GstStructure *structure;
  GST_DEBUG_OBJECT (basesrc, "fixated caps %" GST_PTR_FORMAT, caps);

  caps = gst_caps_make_writable(caps);

  for (guint i = 0; i < gst_caps_get_size (caps); ++i) {
    structure = gst_caps_get_structure (caps, i);
    gst_structure_fixate_field_nearest_int (structure, "width", DEFAULT_FRAME_WIDTH);
    gst_structure_fixate_field_nearest_int (structure, "height", DEFAULT_FRAME_HEIGHT);
    gst_structure_fixate_field_nearest_fraction (structure, "framerate", DEFAULT_FRAMERATE, 1);
  }
  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (basesrc, caps);

  return caps;
}

static gboolean
gst_camerasrc_query(GstBaseSrc * bsrc, GstQuery * query )
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
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

static gboolean
gst_camerasrc_negotiate(GstBaseSrc *basesrc)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
  return GST_BASE_SRC_CLASS (parent_class)->negotiate (basesrc);
}

static gboolean
gst_camerasrc_decide_allocation(GstBaseSrc *bsrc,GstQuery *query)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
  Gstcamerasrc * camerasrc = GST_CAMERASRC(bsrc);
  GstStructure *config;
  GstCaps *caps;
  GstAllocationParams params;
  GstBufferPool *pool = NULL;
  GstAllocator *allocator = NULL;
  guint size = 0, min = 0, max = 0;
  gboolean update;

  switch (camerasrc->io_mode) {
    case GST_CAMERASRC_IO_MODE_DMA:
    case GST_CAMERASRC_IO_MODE_MMAP:
    case GST_CAMERASRC_IO_MODE_USERPTR: {
        if(gst_query_get_n_allocation_pools(query)>0){
          gst_query_parse_nth_allocation_pool(query,0,&pool,&size,&min,&max);
          update=TRUE;
        } else {
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

      if (gst_query_get_n_allocation_pools (query) > 0)
        gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

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

      /* config bufferpool */
      gst_query_set_nth_allocation_pool (query, 0, pool, size, camerasrc->number_of_buffers, max);

      if (allocator)
        gst_object_unref (allocator);

      break;
    default:
      break;
 }

 return GST_BASE_SRC_CLASS (parent_class)->decide_allocation (bsrc, query);
}

static GstFlowReturn
gst_camerasrc_fill(GstPushSrc *src, GstBuffer *buf)
{
  PERF_CAMERA_ATRACE();
  GST_INFO("@%s\n",__func__);
  GstClock *clock;
  GstClockTime delay;
  GstClockTime abs_time, base_time, timestamp, duration;
  Gstcamerasrc *camerasrc = GST_CAMERASRC(src);
  GstCamerasrcBufferPool *bpool = GST_CAMERASRC_BUFFER_POOL(camerasrc->pool);
  struct timespec now;
  GstClockTime gstnow;

  if (G_LIKELY(camerasrc->time_start == 0))
    /* time_start is 0 after dqbuf at the first time */
    /* use base time as starting point*/
    camerasrc->time_start = GST_ELEMENT (camerasrc)->base_time;

  clock_gettime (CLOCK_MONOTONIC, &now);
  gstnow = GST_TIMESPEC_TO_TIME (now);
  camerasrc->time_end = gstnow;

  duration = (GstClockTime) (camerasrc->time_end - camerasrc->time_start);

  GST_DEBUG_OBJECT(camerasrc,"@%s duration=%lu\n",__func__,duration);

  timestamp = GST_BUFFER_TIMESTAMP (buf);

  /* timestamps, LOCK to get clock and base time. */
  GST_OBJECT_LOCK (camerasrc);
  if ((clock = GST_ELEMENT_CLOCK (camerasrc))) {
      /* we have a clock, get base time and ref clock */
      base_time = GST_ELEMENT(camerasrc)->base_time;
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
      GTimeVal now;
      g_get_current_time(&now);
      gstnow = GST_TIMEVAL_TO_TIME(now);
      if (gstnow > timestamp)
          delay = abs_time - camerasrc->time_end;
      else
          delay = 0;
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
  clock_gettime (CLOCK_MONOTONIC, &now);
  gstnow = GST_TIMESPEC_TO_TIME (now);
  camerasrc->time_start = gstnow;

  return GST_FLOW_OK;
}

static gboolean
gst_camerasrc_unlock(GstBaseSrc *src)
{
  return TRUE;
}

static gboolean
gst_camerasrc_unlock_stop(GstBaseSrc *src)
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
