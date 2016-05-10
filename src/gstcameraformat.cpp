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


#define LOG_TAG "GstCameraFormat"

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <stdio.h>
#include <gst/gst.h>
#include <linux/videodev2.h>
#include <gst/video/video.h>

#include "ICamera.h"
#include "ScopedAtrace.h"
#include "gstcamerasrc.h"
#include "gstcameraformat.h"
#include "Parameters.h"

using namespace icamera;

GST_DEBUG_CATEGORY_EXTERN(gst_camerasrc_debug);
#define GST_CAT_DEFAULT gst_camerasrc_debug

typedef struct
{
  guint32 format;
  gboolean dimensions;
} cameraSrcFormatDesc;

static GstStructure *
gst_camerasrc_format_to_structure (guint32 fourcc)
{
  PERF_CAMERA_ATRACE();
  GstStructure *structure = NULL;

  switch (fourcc) {
    case V4L2_PIX_FMT_NV12:{   /* 12  Y/CbCr 4:2:0  */
      structure = gst_structure_new ("video/x-raw",
          "format", G_TYPE_STRING, gst_video_format_to_string (GST_VIDEO_FORMAT_NV12), (void *)NULL);
    }
    break;
    case V4L2_PIX_FMT_UYVY:{
      structure = gst_structure_new ("video/x-raw",
          "format", G_TYPE_STRING, gst_video_format_to_string (GST_VIDEO_FORMAT_UYVY), (void *)NULL);
    }
    break;
    case V4L2_PIX_FMT_YUYV:{
      structure = gst_structure_new ("video/x-raw",
          "format", G_TYPE_STRING, gst_video_format_to_string (GST_VIDEO_FORMAT_YUY2), (void *)NULL);
    }
    break;
    case V4L2_PIX_FMT_SGRBG8:{
      structure = gst_structure_new_empty ("video/x-bayer");
    }
    break;
    case V4L2_PIX_FMT_XRGB32:{
       structure = gst_structure_new ("video/x-raw",
           "format", G_TYPE_STRING, gst_video_format_to_string (GST_VIDEO_FORMAT_RGBx), (void *)NULL);
    }
    break;
    case V4L2_PIX_FMT_BGR24:{
          structure = gst_structure_new ("video/x-raw",
           "format", G_TYPE_STRING, gst_video_format_to_string (GST_VIDEO_FORMAT_BGR), (void *)NULL);
    }
    break;
    case V4L2_PIX_FMT_XBGR32:{
           structure = gst_structure_new ("video/x-raw",
            "format", G_TYPE_STRING, gst_video_format_to_string (GST_VIDEO_FORMAT_BGRx), (void *)NULL);
    }
    break;
    default:
    break;
  }

  return structure;
}

/**
  * Read all supported formats, width, height from Camera info
  * if no availble formats, return -1
  */
int get_format_and_resolution(const camera_info_t info, stream_array_t &configs, int &numberOfFormat, int *formats, camera_resolution_t *tmp_res)
{
    PERF_CAMERA_ATRACE();

    int previousFormat = -1;
    info.capability->getSupportedStreamConfig(configs);

    for (size_t j = 0; j < configs.size(); j++) {
      // Find all non-interlaced format
      if (configs[j].field == GST_CAMERASRC_INTERLACE_FIELD_ANY
            && configs[j].format != previousFormat) {
        formats[numberOfFormat] = configs[j].format;
        numberOfFormat++;
        previousFormat = configs[j].format;
      }
      tmp_res[j].width = configs[j].width;
      tmp_res[j].height = configs[j].height;
    }

    if (previousFormat == -1 || numberOfFormat == 0)
        return -1;
    else
        return 0;
}

/**
  * Parse all supported resolutions, set values of max width/height and min width/height
  * from all supported resolutions, used to generate width/height range
  */
void get_max_and_min_resolution(camera_resolution_t *r, int r_count, int *max_w, int *max_h, int *min_w, int *min_h)
{
  PERF_CAMERA_ATRACE();

  if (r) {
    camera_resolution_t *rz = r;
    *max_w = rz->width;
    *max_h = rz->height;
    *min_w = rz->width;
    *min_h = rz->height;
    for (int j = 0; j < r_count; j++, rz++) {
      if (rz->width > *max_w) {
        *max_w = rz->width;
      }
      if (rz->height > *max_h) {
        *max_h = rz->height;
      }
      if (rz->width < *min_w) {
        *min_w = rz->width;
      }
      if (rz->height < *min_h) {
        *min_h = rz->height;
      }
    }
  }
}

GstCaps *gst_camerasrc_get_all_caps (GstcamerasrcClass *camerasrc_class)
{
  PERF_CAMERA_ATRACE();

  static GstCaps *caps = NULL;
  GstStructure *structure;

  int formats[20];
  camera_resolution_t tmp_res[20];
  int ret = 0;
  int count;

  caps = gst_caps_new_empty ();
  count = get_number_of_cameras();

  for(int i = 0; i < count; i++) {
    stream_array_t configs;
    camera_info_t info;
    int max_w, max_h, min_w, min_h;
    int numberOfFormat = 0;

    ret = get_camera_info(i, info);
    if (ret != 0) {
      GST_ERROR_OBJECT(camerasrc_class, "failed to get_camera_info from libcamhal %d\n", ret);
      gst_caps_unref(caps);
      return NULL;
    }

    ret = get_format_and_resolution(info, configs, numberOfFormat, formats, tmp_res);
    if (ret != 0) {
        GST_ERROR_OBJECT(camerasrc_class, "failed to get format info from libcamhal %d\n", ret);
        gst_caps_unref(caps);
        return NULL;
    }

    get_max_and_min_resolution(tmp_res, configs.size(), &max_w, &max_h, &min_w, &min_h);

    /* Merge resolutions */
    for (int j = 0; j < numberOfFormat; j++) {
      structure = gst_camerasrc_format_to_structure (formats[j]);
      if (structure) {
        if ( max_w == min_w && max_h == min_h )
          gst_structure_set (structure,
            "width", GST_TYPE_INT_RANGE, min_w-1, max_w,
            "height", GST_TYPE_INT_RANGE, min_h-1, max_h,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 60, 1,
            "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
            (void *)NULL);
        else
          gst_structure_set (structure,
            "width", GST_TYPE_INT_RANGE, min_w, max_w,
            "height", GST_TYPE_INT_RANGE, min_h, max_h,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 60, 1,
            "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
            (void *)NULL);
      }
      caps = gst_caps_merge_structure (caps, structure);
    }
    memset(formats, 0, sizeof(formats));
    memset(tmp_res, 0, sizeof(tmp_res));
  }

  return caps;
}

