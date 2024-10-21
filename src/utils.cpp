/*
 * GStreamer
 * Copyright (C) 2018-2024 Intel Corporation
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

#define LOG_TAG "GstCameraUtils"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "utils.h"

struct FormatCvt {
  const char *gst_fmt_string;
  GstVideoFormat gst_pixel;
  int v4l2_pixel;
};

static const FormatCvt gFormatMapping[] = {
  { "YUY2", GST_VIDEO_FORMAT_YUY2, V4L2_PIX_FMT_YUYV },
  { "UYVY", GST_VIDEO_FORMAT_UYVY, V4L2_PIX_FMT_UYVY },
  { "NV12", GST_VIDEO_FORMAT_NV12, V4L2_PIX_FMT_NV12 },
  { "RGBx", GST_VIDEO_FORMAT_RGBx, V4L2_PIX_FMT_XRGB32 },
  { "BGRA", GST_VIDEO_FORMAT_BGRA, V4L2_PIX_FMT_BGR32 },
  { "BGR", GST_VIDEO_FORMAT_BGR, V4L2_PIX_FMT_BGR24 },
  { "RGB16", GST_VIDEO_FORMAT_RGB16, V4L2_PIX_FMT_RGB565 },
  { "NV16", GST_VIDEO_FORMAT_NV16, V4L2_PIX_FMT_NV16 },
  { "BGRx", GST_VIDEO_FORMAT_BGRx, V4L2_PIX_FMT_XBGR32 },
  { "P010", GST_VIDEO_FORMAT_P010_10BE, V4L2_PIX_FMT_P010 },
  { "P01L", GST_VIDEO_FORMAT_P010_10LE, V4L2_PIX_FMT_P010_LE },
};

int num_of_format = ARRAY_SIZE(gFormatMapping);

int CameraSrcUtils::gst_fmt_2_fourcc(GstVideoFormat gst_fmt)
{
  for (int i = 0; i < num_of_format; i++) {
    if (gFormatMapping[i].gst_pixel == gst_fmt)
      return gFormatMapping[i].v4l2_pixel;
  }

  return -1;
}

GstVideoFormat CameraSrcUtils::fourcc_2_gst_fmt(int fourcc)
{
  for (int j = 0; j < num_of_format; j++) {
    if (gFormatMapping[j].v4l2_pixel == fourcc)
      return gFormatMapping[j].gst_pixel;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

gboolean CameraSrcUtils::check_format_by_name(const char *name)
{
  for (int k = 0; k < num_of_format; k++) {
    if (strcmp (gFormatMapping[k].gst_fmt_string, name) == 0)
      return TRUE;
  }

  return FALSE;
}

int CameraSrcUtils::string_2_fourcc(const char *fmt_string)
{
  for (int m = 0; m < num_of_format; m++) {
    if (strcmp(gFormatMapping[m].gst_fmt_string, fmt_string) == 0)
      return gFormatMapping[m].v4l2_pixel;
  }

  return -1;
}


/* This function is used for interlaced frame
 * It will return the number of lines that contains valid data
 * For packed format, 'Y' and 'UY' conponents are stored in a single array
 * For planar format, 'Y' and 'UY' conponents are stored separately */
int CameraSrcUtils::get_number_of_valid_lines(int format, int height)
{
  switch(format) {
    /* Planar formats */
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_P010:
    case V4L2_PIX_FMT_P010_LE:
      return height*3/2;
    case V4L2_PIX_FMT_NV16:
      return height*2;
    /* Packed formats */
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_XRGB32:
    case V4L2_PIX_FMT_BGR24:
    case V4L2_PIX_FMT_RGB565:
    case V4L2_PIX_FMT_XBGR32:
    case V4L2_PIX_FMT_BGR32:
      return height;
    default:
      break;
  }

  return 0;
}

void CameraSrcUtils::get_stream_info_by_caps(GstCaps *caps, const char **format, int *width, int *height)
{
  const GstStructure *structure = gst_caps_get_structure(caps, 0);
  *format = gst_structure_get_string(structure, "format");
  gst_structure_get_int(structure, "width", width);
  gst_structure_get_int(structure, "height", height);
}

int CameraSrcUtils::get_stream_id_by_pad(map<string, int> &streamMap, GstPad *pad)
{
  int stream_id = -1;
  gchar *padname = gst_pad_get_name(pad);

  auto iter = streamMap.find(padname);
  if (iter != streamMap.end())
    stream_id = iter->second;
  else {
    GST_ERROR("failed to find StreamId by pad: %s", padname);
  }

  g_free(padname);
  return stream_id;
}

#ifdef GST_DRM_FORMAT

#define VA_NSB_FIRST 0 /* No Significant Bit  */

/* *INDENT-OFF* */
static struct FormatMap {
  GstVideoFormat format;
  guint va_rtformat;
  VAImageFormat va_format;
  /* The drm fourcc may have different definition from VA */
  guint drm_fourcc;
} format_map[] = {
#ifndef G_OS_WIN32
#define F(format, drm, fourcc, rtformat, order, bpp, depth, r, g, b, a)        \
  {                                                                            \
    G_PASTE(GST_VIDEO_FORMAT_, format), G_PASTE(VA_RT_FORMAT_, rtformat),      \
        {VA_FOURCC fourcc,                                                     \
         G_PASTE(G_PASTE(VA_, order), _FIRST),                                 \
         bpp,                                                                  \
         depth,                                                                \
         r,                                                                    \
         g,                                                                    \
         b,                                                                    \
         a},                                                                   \
        G_PASTE(DRM_FORMAT_, drm)                                              \
  }
#else
#define F(format, drm, fourcc, rtformat, order, bpp, depth, r, g, b, a)        \
  {                                                                            \
    G_PASTE(GST_VIDEO_FORMAT_, format), G_PASTE(VA_RT_FORMAT_, rtformat),      \
        {VA_FOURCC fourcc,                                                     \
         G_PASTE(G_PASTE(VA_, order), _FIRST),                                 \
         bpp,                                                                  \
         depth,                                                                \
         r,                                                                    \
         g,                                                                    \
         b,                                                                    \
         a},                                                                   \
        0 /* DRM_FORMAT_INVALID */                                             \
  }
#endif
#define G(format, drm, fourcc, rtformat, order, bpp)                           \
  F(format, drm, fourcc, rtformat, order, bpp, 0, 0, 0, 0, 0)
    G(NV12, NV12, ('N', 'V', '1', '2'), YUV420, NSB, 12),
    G(NV21, NV21, ('N', 'V', '2', '1'), YUV420, NSB, 21),
    G(VUYA, AYUV, ('A', 'Y', 'U', 'V'), YUV444, LSB, 32),
    F(RGBA, RGBA8888, ('R', 'G', 'B', 'A'), RGB32, LSB, 32, 32, 0x000000ff,
      0x0000ff00, 0x00ff0000, 0xff000000),
    F(RGBx, RGBX8888, ('R', 'G', 'B', 'X'), RGB32, LSB, 32, 24, 0x000000ff,
      0x0000ff00, 0x00ff0000, 0x00000000),
    F(BGRA, BGRA8888, ('B', 'G', 'R', 'A'), RGB32, LSB, 32, 32, 0x00ff0000,
      0x0000ff00, 0x000000ff, 0xff000000),
    F(ARGB, ARGB8888, ('A', 'R', 'G', 'B'), RGB32, LSB, 32, 32, 0x0000ff00,
      0x00ff0000, 0xff000000, 0x000000ff),
    F(xRGB, XRGB8888, ('X', 'R', 'G', 'B'), RGB32, LSB, 32, 24, 0x0000ff00,
      0x00ff0000, 0xff000000, 0x00000000),
    F(ABGR, ABGR8888, ('A', 'B', 'G', 'R'), RGB32, LSB, 32, 32, 0xff000000,
      0x00ff0000, 0x0000ff00, 0x000000ff),
    F(xBGR, XBGR8888, ('X', 'B', 'G', 'R'), RGB32, LSB, 32, 24, 0xff000000,
      0x00ff0000, 0x0000ff00, 0x00000000),
    F(BGRx, BGRX8888, ('B', 'G', 'R', 'X'), RGB32, LSB, 32, 24, 0x00ff0000,
      0x0000ff00, 0x000000ff, 0x00000000),
    G(UYVY, UYVY, ('U', 'Y', 'V', 'Y'), YUV422, NSB, 16),
    G(YUY2, YUYV, ('Y', 'U', 'Y', '2'), YUV422, NSB, 16),
    G(AYUV, AYUV, ('A', 'Y', 'U', 'V'), YUV444, LSB, 32),
    /* F (????, NV11), */
    G(YV12, YVU420, ('Y', 'V', '1', '2'), YUV420, NSB, 12),
    /* F (????, P208), */
    G(I420, YUV420, ('I', '4', '2', '0'), YUV420, NSB, 12),
    /* F (????, YV24), */
    /* F (????, YV32), */
    /* F (????, Y800), */
    /* F (????, IMC3), */
    /* F (????, 411P), */
    /* F (????, 411R), */
    G(Y42B, YUV422, ('4', '2', '2', 'H'), YUV422, LSB, 16),
    /* F (????, 422V), */
    /* F (????, 444P), */
    /* No RGBP support in drm fourcc */
    G(RGBP, INVALID, ('R', 'G', 'B', 'P'), RGBP, LSB, 8),
    /* F (????, BGRP), */
    /* F (????, RGB565), */
    /* F (????, BGR565), */
    G(Y210, Y210, ('Y', '2', '1', '0'), YUV422_10, NSB, 32),
    /* F (????, Y216), */
    G(Y410, Y410, ('Y', '4', '1', '0'), YUV444_10, NSB, 32),
    G(Y212_LE, Y212, ('Y', '2', '1', '2'), YUV422_12, NSB, 32),
    G(Y412_LE, Y412, ('Y', '4', '1', '2'), YUV444_12, NSB, 32),
    /* F (????, Y416), */
    /* F (????, YV16), */
    G(P010_10LE, P010, ('P', '0', '1', '0'), YUV420_10, NSB, 24),
    G(P012_LE, P012, ('P', '0', '1', '2'), YUV420_12, NSB, 24),
    /* F (P016_LE, P016, ????), */
    /* F (????, I010), */
    /* F (????, IYUV), */
    /* F (????, A2R10G10B10), */
    /* F (????, A2B10G10R10), */
    /* F (????, X2R10G10B10), */
    /* F (????, X2B10G10R10), */
    /* No GRAY8 support in drm fourcc */
    G(GRAY8, INVALID, ('Y', '8', '0', '0'), YUV400, NSB, 8),
    G(Y444, YUV444, ('4', '4', '4', 'P'), YUV444, NSB, 24),
    /* F (????, Y16), */
    /* G (VYUY, VYUY, YUV422), */
    /* G (YVYU, YVYU, YUV422), */
    /* F (ARGB64, ARGB64, ????), */
    /* F (????, ABGR64), */
    F(RGB16, RGB565, ('R', 'G', '1', '6'), RGB16, NSB, 16, 16, 0x0000f800,
      0x000007e0, 0x0000001f, 0x00000000),
    F(RGB, RGB888, ('R', 'G', '2', '4'), RGB32, NSB, 32, 24, 0x00ff0000,
      0x0000ff00, 0x000000ff, 0x00000000),
    F(BGR10A2_LE, ARGB2101010, ('A', 'R', '3', '0'), RGB32, LSB, 32, 30,
      0x3ff00000, 0x000ffc00, 0x000003ff, 0x30000000),
#undef F
#undef G
};

static const struct RBG32FormatMap {
  GstVideoFormat format;
  guint drm_fourcc;
  VAImageFormat va_format[2];
} rgb32_format_map[] = {
#define F(fourcc, order, bpp, depth, r, g, b, a)                               \
  {                                                                            \
    VA_FOURCC fourcc, G_PASTE(G_PASTE(VA_, order), _FIRST), bpp, depth, r, g,  \
        b, a                                                                   \
  }
#define A(fourcc, order, r, g, b, a) F(fourcc, order, 32, 32, r, g, b, a)
#define X(fourcc, order, r, g, b) F(fourcc, order, 32, 24, r, g, b, 0x0)
#ifndef G_OS_WIN32
#define D(format, drm_fourcc)                                                  \
  G_PASTE(GST_VIDEO_FORMAT_, format), G_PASTE(DRM_FORMAT_, drm_fourcc)
#else
#define D(format, drm_fourcc)                                                  \
  G_PASTE(GST_VIDEO_FORMAT_, format), 0 /* DRM_FORMAT_INVALID */
#endif
    {D(ARGB, BGRA8888),
     {
         A(('B', 'G', 'R', 'A'), LSB, 0x0000ff00, 0x00ff0000, 0xff000000,
           0x000000ff),
         A(('A', 'R', 'G', 'B'), MSB, 0x00ff0000, 0x0000ff00, 0x000000ff,
           0xff000000),
     }},
    {D(RGBA, ABGR8888),
     {
         A(('A', 'B', 'G', 'R'), LSB, 0x000000ff, 0x0000ff00, 0x00ff0000,
           0xff000000),
         A(('R', 'G', 'B', 'A'), MSB, 0xff000000, 0x00ff0000, 0x0000ff00,
           0x000000ff),
     }},
    {D(ABGR, RGBA8888),
     {
         A(('R', 'G', 'B', 'A'), LSB, 0xff000000, 0x00ff0000, 0x0000ff00,
           0x000000ff),
         A(('A', 'B', 'G', 'R'), MSB, 0x000000ff, 0x0000ff00, 0x00ff0000,
           0xff000000),
     }},
    {D(BGRA, ARGB8888),
     {
         A(('A', 'R', 'G', 'B'), LSB, 0x00ff0000, 0x0000ff00, 0x000000ff,
           0xff000000),
         A(('B', 'G', 'R', 'A'), MSB, 0x0000ff00, 0x00ff0000, 0xff000000,
           0x000000ff),
     }},
    {D(xRGB, BGRX8888),
     {
         X(('B', 'G', 'R', 'X'), LSB, 0x0000ff00, 0x00ff0000, 0xff000000),
         X(('X', 'R', 'G', 'B'), MSB, 0x00ff0000, 0x0000ff00, 0x000000ff),
     }},
    {D(RGBx, XBGR8888),
     {
         X(('X', 'B', 'G', 'R'), LSB, 0x000000ff, 0x0000ff00, 0x00ff0000),
         X(('R', 'G', 'B', 'X'), MSB, 0xff000000, 0x00ff0000, 0x0000ff00),
     }},
    {D(xBGR, RGBX8888),
     {
         X(('R', 'G', 'B', 'X'), LSB, 0xff000000, 0x00ff0000, 0x0000ff00),
         X(('X', 'B', 'G', 'R'), MSB, 0x000000ff, 0x0000ff00, 0x00ff0000),
     }},
    {D(BGRx, XRGB8888),
     {
         X(('X', 'R', 'G', 'B'), LSB, 0x00ff0000, 0x0000ff00, 0x000000ff),
         X(('B', 'G', 'R', 'X'), MSB, 0x0000ff00, 0x00ff0000, 0xff000000),
     }},
#undef X
#undef A
#undef F
#undef D
};
/* *INDENT-ON* */

gboolean CameraSrcUtils::gst_video_info_from_dma_drm_caps(GstVideoInfo *info,
                                                          const GstCaps *caps) {
  if (!gst_video_is_dma_drm_caps(caps)) {
    if (!gst_video_info_from_caps(info, caps)) {
      return FALSE;
    }
  } else {
    GstVideoInfoDmaDrm drm_info;
    if (!gst_video_info_dma_drm_from_caps(&drm_info, caps)) {
      return FALSE;
    }
    if (!gst_video_info_dma_drm_to_video_info(&drm_info, info)) {
      return FALSE;
    }
  }
  return TRUE;
}

static struct FormatMap *
get_format_map_from_video_format(GstVideoFormat format) {
  long unsigned int i;

  for (i = 0; i < G_N_ELEMENTS(format_map); i++) {
    if (format_map[i].format == format)
      return &format_map[i];
  }

  return NULL;
}

static guint gst_va_drm_fourcc_from_video_format(GstVideoFormat format) {
  const struct FormatMap *map = get_format_map_from_video_format(format);

  return map ? map->drm_fourcc : 0;
}

static guint gst_va_chroma_from_video_format(GstVideoFormat format) {
  const struct FormatMap *map = get_format_map_from_video_format(format);

  return map ? map->va_rtformat : 0;
}

static guint gst_va_fourcc_from_video_format(GstVideoFormat format) {
  const struct FormatMap *map = get_format_map_from_video_format(format);

  return map ? map->va_format.fourcc : 0;
}

static gboolean va_destroy_surfaces(GstVaDisplay *display,
                                    VASurfaceID *surfaces, gint num_surfaces) {
  VADisplay dpy = gst_va_display_get_va_dpy(display);
  VAStatus status;

  g_return_val_if_fail(num_surfaces > 0, FALSE);

  status = vaDestroySurfaces(dpy, surfaces, num_surfaces);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR("vaDestroySurfaces: %s", vaErrorStr(status));
    return FALSE;
  }

  return TRUE;
}

static guint64 gst_va_dmabuf_get_modifier_for_format_for_icamerasrc(
    GstVaDisplay *display, GstVideoFormat format, guint usage_hint) {
  VADRMPRIMESurfaceDescriptor desc = {
      0,
  };
  VASurfaceID surface;
  GstVideoInfo info;

  gst_video_info_init(&info);
  gst_video_info_set_format(&info, format, 64, 64);

  if (!CameraSrcUtils::_va_create_surface_and_export_to_dmabuf(
          display, usage_hint, NULL, 0, &info, &surface, &desc))
    return DRM_FORMAT_MOD_INVALID;

  va_destroy_surfaces(display, &surface, 1);

  return desc.objects[0].drm_format_modifier;
}

static void gst_icamerasrc_get_supported_modifiers(GstVaDisplay *display,
                                                   GstVideoFormat format,
                                                   GValue *modifiers) {
  guint64 mod = DRM_FORMAT_MOD_INVALID;
  GValue gmod = G_VALUE_INIT;
  guint usage_hint = VA_SURFACE_ATTRIB_USAGE_HINT_GENERIC;

  g_value_init(&gmod, G_TYPE_UINT64);

  mod = gst_va_dmabuf_get_modifier_for_format_for_icamerasrc(display, format,
                                                             usage_hint);
  if (mod != DRM_FORMAT_MOD_INVALID) {
    g_value_set_uint64(&gmod, mod);
    gst_value_list_append_value(modifiers, &gmod);
  } else {
    GST_WARNING("Failed to get modifier %s:0x%016llx",
                gst_video_format_to_string(format), DRM_FORMAT_MOD_INVALID);
  }

  g_value_unset(&gmod);
}

static inline gboolean va_format_is_rgb(const VAImageFormat *va_format) {
  return va_format->depth != 0;
}

static inline gboolean va_format_is_same_rgb(const VAImageFormat *fmt1,
                                             const VAImageFormat *fmt2) {
  return (fmt1->red_mask == fmt2->red_mask &&
          fmt1->green_mask == fmt2->green_mask &&
          fmt1->blue_mask == fmt2->blue_mask &&
          fmt1->alpha_mask == fmt2->alpha_mask);
}

static inline gboolean va_format_is_same(const VAImageFormat *fmt1,
                                         const VAImageFormat *fmt2) {
  if (fmt1->fourcc != fmt2->fourcc)
    return FALSE;
  if (fmt1->byte_order != VA_NSB_FIRST && fmt2->byte_order != VA_NSB_FIRST &&
      fmt1->byte_order != fmt2->byte_order)
    return FALSE;
  return va_format_is_rgb(fmt1) ? va_format_is_same_rgb(fmt1, fmt2) : TRUE;
}

static GstVideoFormat
find_gst_video_format_in_rgb32_map(VAImageFormat *image_format,
                                   guint *drm_fourcc) {
  guint i, j;

  for (i = 0; i < G_N_ELEMENTS(rgb32_format_map); i++) {
    for (j = 0; j < G_N_ELEMENTS(rgb32_format_map[i].va_format); j++) {
      if (va_format_is_same(&rgb32_format_map[i].va_format[j], image_format)) {
        *drm_fourcc = rgb32_format_map[i].drm_fourcc;
        return rgb32_format_map[i].format;
      }
    }
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

struct ImageFormatArray {
  VAImageFormat *image_formats;
  gint len;
};

static gpointer fix_map(gpointer data) {
  struct ImageFormatArray *args = (struct ImageFormatArray *)data;
  GstVideoFormat format;
  VAImageFormat *image_format;
  struct FormatMap *map;
  guint drm_fourcc = 0;
  gint i;

  for (i = 0; i < args->len; i++) {
    image_format = &args->image_formats[i];
    if (!va_format_is_rgb(image_format))
      continue;
    format = find_gst_video_format_in_rgb32_map(image_format, &drm_fourcc);
    if (format == GST_VIDEO_FORMAT_UNKNOWN)
      continue;
    map = get_format_map_from_video_format(format);
    if (!map)
      continue;
    if (va_format_is_same(&map->va_format, image_format))
      continue;

    map->va_format = *image_format;
    map->drm_fourcc = drm_fourcc;

    GST_INFO("GST_VIDEO_FORMAT_%s => { fourcc %" GST_FOURCC_FORMAT ", "
             "drm fourcc %" GST_FOURCC_FORMAT ", %s, bpp %d, depth %d, "
             "R %#010x, G %#010x, B %#010x, A %#010x }",
             gst_video_format_to_string(map->format),
             GST_FOURCC_ARGS(map->va_format.fourcc),
             GST_FOURCC_ARGS(map->drm_fourcc),
             (map->va_format.byte_order == 1) ? "LSB" : "MSB",
             map->va_format.bits_per_pixel, map->va_format.depth,
             map->va_format.red_mask, map->va_format.green_mask,
             map->va_format.blue_mask, map->va_format.alpha_mask);
  }

  return NULL;
}

static void gst_va_video_format_fix_map(VAImageFormat *image_formats,
                                        gint num) {
  static GOnce once = G_ONCE_INIT;
  struct ImageFormatArray args = {image_formats, num};

  g_once(&once, fix_map, &args);
}

static void _fix_map(GstVaDisplay *display) {
  VAImageFormat *va_formats;
  VADisplay dpy;
  VAStatus status;
  int max, num = 0;

  dpy = gst_va_display_get_va_dpy(display);

  max = vaMaxNumImageFormats(dpy);
  if (max == 0)
    return;

  va_formats = g_new(VAImageFormat, max);
  status = vaQueryImageFormats(dpy, va_formats, &num);
  gst_va_video_format_fix_map(va_formats, num);

  if (status != VA_STATUS_SUCCESS)
    GST_WARNING("vaQueryImageFormats: %s", vaErrorStr(status));

  g_free(va_formats);
  return;
}

gboolean CameraSrcUtils::_dma_fmt_to_dma_drm_fmts(GstVaDisplay *display,
                                                  const GstVideoFormat fmt,
                                                  GValue *dma_drm_fmts) {
  gchar *drm_fmt_str;
  guint32 drm_fourcc;
  guint64 modifier;
  GValue gval = G_VALUE_INIT;
  GValue mods = G_VALUE_INIT;

  g_return_val_if_fail(fmt != GST_VIDEO_FORMAT_UNKNOWN, FALSE);

  _fix_map(display);

  drm_fourcc = gst_va_drm_fourcc_from_video_format(fmt);
  if (drm_fourcc == DRM_FORMAT_INVALID)
    return FALSE;

  g_value_init(&mods, GST_TYPE_LIST);
  g_value_init(&gval, G_TYPE_STRING);

  gst_icamerasrc_get_supported_modifiers(display, fmt, &mods);

  for (guint m = 0; m < gst_value_list_get_size(&mods); m++) {
    const GValue *gmod = gst_value_list_get_value(&mods, m);
    modifier = g_value_get_uint64(gmod);

    drm_fmt_str = gst_video_dma_drm_fourcc_to_string(drm_fourcc, modifier);
    if (!drm_fmt_str)
      continue;

    g_value_set_string(&gval, drm_fmt_str);
    gst_value_list_append_value(dma_drm_fmts, &gval);

    GST_DEBUG("Got modifier: %s", drm_fmt_str);
    g_free(drm_fmt_str);
  }
  g_value_unset(&mods);
  g_value_unset(&gval);

  return TRUE;
}

static gboolean _modifier_found(guint64 modifier, guint64 *modifiers,
                                guint num_modifiers) {
  guint i;

  /* user doesn't care the returned modifier */
  if (num_modifiers == 0)
    return TRUE;

  for (i = 0; i < num_modifiers; i++)
    if (modifier == modifiers[i])
      return TRUE;
  return FALSE;
}

/*
* @brief use libva to create surfaces
* @param is_only_linear_modifier If true,
          only create linear dma buffer surfaces, or create others.
* @details If you want to support modifiers instead of linear modifier,
            you can just drop is_only_linear_modifier
*/
static gboolean va_create_surfaces(GstVaDisplay *display, guint rt_format,
                                   guint fourcc, guint width, guint height,
                                   gint usage_hint, guint64 *modifiers,
                                   guint num_modifiers,
                                   VADRMPRIMESurfaceDescriptor *desc,
                                   VASurfaceID *surfaces, guint num_surfaces, gboolean is_only_linear_modifier = TRUE) {
  VADisplay dpy = gst_va_display_get_va_dpy(display);
  /* *INDENT-OFF* */
  VASurfaceAttrib attrs[6];
  memset(attrs, 0, sizeof(attrs));
  attrs[0].type = VASurfaceAttribUsageHint;
  attrs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
  attrs[0].value.type = VAGenericValueTypeInteger;
  attrs[0].value.value.i = usage_hint;
  attrs[1].type = VASurfaceAttribMemoryType;
  attrs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
  attrs[1].value.type = VAGenericValueTypeInteger;
  attrs[1].value.value.i = (desc && desc->num_objects > 0)
                               ? VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2
                               : VA_SURFACE_ATTRIB_MEM_TYPE_VA;
  VADRMFormatModifierList modifier_list;
  modifier_list.num_modifiers = num_modifiers;
  modifier_list.modifiers = modifiers;
  VASurfaceAttribExternalBuffers extbuf = {0};
  extbuf.width = width;
  extbuf.height = height;
  extbuf.num_planes = 1;
  extbuf.pixel_format = fourcc;
  /* *INDENT-ON* */
  VAStatus status;
  guint num_attrs = 2;

  g_return_val_if_fail(num_surfaces > 0, FALSE);
  /* must have modifiers when num_modifiers > 0 */
  g_return_val_if_fail(num_modifiers == 0 || modifiers, FALSE);

  if (fourcc > 0) {
    /* *INDENT-OFF* */
    attrs[num_attrs].type = VASurfaceAttribPixelFormat;
    attrs[num_attrs].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attrs[num_attrs].value.type = VAGenericValueTypeInteger;
    attrs[num_attrs].value.value.i = fourcc;
    num_attrs++;
    /* *INDENT-ON* */
  }

  if (desc && desc->num_objects > 0) {
    /* *INDENT-OFF* */
    attrs[num_attrs].type = VASurfaceAttribExternalBufferDescriptor;
    attrs[num_attrs].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attrs[num_attrs].value.type = VAGenericValueTypePointer;
    attrs[num_attrs].value.value.p = desc;
    num_attrs++;
    /* *INDENT-ON* */
  } else if (is_only_linear_modifier) {
    /* HACK(victor): disable tiling for i965 driver for RGB formats */
    /* *INDENT-OFF* */
    attrs[num_attrs].type = VASurfaceAttribExternalBufferDescriptor;
    attrs[num_attrs].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attrs[num_attrs].value.type = VAGenericValueTypePointer;
    attrs[num_attrs].value.value.p = &extbuf;
    num_attrs++;
    /* *INDENT-ON* */
  }

  if (num_modifiers > 0 && modifiers) {
    /* *INDENT-OFF* */
    attrs[num_attrs].type = VASurfaceAttribDRMFormatModifiers;
    attrs[num_attrs].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attrs[num_attrs].value.type = VAGenericValueTypePointer;
    attrs[num_attrs].value.value.p = &modifier_list;
    num_attrs++;
    /* *INDENT-ON* */
  }

retry:
  status = vaCreateSurfaces(dpy, rt_format, width, height, surfaces,
                            num_surfaces, attrs, num_attrs);

  if (status == VA_STATUS_ERROR_ATTR_NOT_SUPPORTED &&
      attrs[num_attrs - 1].type == VASurfaceAttribDRMFormatModifiers) {
    guint i;

    /* if requested modifiers contain linear, let's remove the attribute and
     * "hope" the driver will create linear dmabufs */
    for (i = 0; i < num_modifiers; ++i) {
      if (modifiers[i] == DRM_FORMAT_MOD_LINEAR) {
        num_attrs--;
        goto retry;
      }
    }
  }

  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR("vaCreateSurfaces: %s", vaErrorStr(status));
    return FALSE;
  }

  return TRUE;
}

static gboolean va_export_surface_to_dmabuf(GstVaDisplay *display,
                                            VASurfaceID surface, guint32 flags,
                                            VADRMPRIMESurfaceDescriptor *desc) {
  VADisplay dpy = gst_va_display_get_va_dpy(display);
  VAStatus status;

  status = vaExportSurfaceHandle(
      dpy, surface, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2, flags, desc);
  if (status != VA_STATUS_SUCCESS) {
    GST_INFO("vaExportSurfaceHandle: %s", vaErrorStr(status));
    return FALSE;
  }

  return TRUE;
}

gboolean CameraSrcUtils::_va_create_surface_and_export_to_dmabuf(
    GstVaDisplay *display, guint usage_hint, guint64 *modifiers,
    guint num_modifiers, GstVideoInfo *info, VASurfaceID *ret_surface,
    VADRMPRIMESurfaceDescriptor *ret_desc) {
  VADRMPRIMESurfaceDescriptor desc = {
      0,
  };
  guint32 i, fourcc, rt_format, export_flags;
  GstVideoFormat format;
  VASurfaceID surface;
  guint64 prev_modifier = DRM_FORMAT_MOD_INVALID;

  format = GST_VIDEO_INFO_FORMAT(info);

  fourcc = gst_va_fourcc_from_video_format(format);
  rt_format = gst_va_chroma_from_video_format(format);
  if (fourcc == 0 || rt_format == 0)
    return FALSE;

  if (!va_create_surfaces(display, rt_format, fourcc,
                          GST_VIDEO_INFO_WIDTH(info),
                          GST_VIDEO_INFO_HEIGHT(info), usage_hint, modifiers,
                          num_modifiers, NULL, &surface, 1))
    return FALSE;

  /* workaround for missing layered dmabuf formats in i965 */
  if (GST_VA_DISPLAY_IS_IMPLEMENTATION(display, INTEL_I965) &&
      (fourcc == VA_FOURCC_YUY2 || fourcc == VA_FOURCC_UYVY)) {
    /* These are not representable as separate planes */
    export_flags = VA_EXPORT_SURFACE_COMPOSED_LAYERS;
  } else {
    /* Each layer will contain exactly one plane.  For example, an NV12
     * surface will be exported as two layers */
    export_flags = VA_EXPORT_SURFACE_SEPARATE_LAYERS;
  }

  export_flags |= VA_EXPORT_SURFACE_READ_WRITE;

  if (!va_export_surface_to_dmabuf(display, surface, export_flags, &desc))
    goto failed;

  if (GST_VIDEO_INFO_N_PLANES(info) != desc.num_layers)
    goto failed;

  /* YUY2 and YUYV are the same. radeonsi returns always YUYV.
   * There's no reason to fail if the different fourcc if there're dups.
   * https://fourcc.org/pixel-format/yuv-yuy2/ */
  if (fourcc != desc.fourcc) {
    GST_INFO("Different fourcc: requested %" GST_FOURCC_FORMAT
             " - returned %" GST_FOURCC_FORMAT,
             GST_FOURCC_ARGS(fourcc), GST_FOURCC_ARGS(desc.fourcc));
  }

  if (desc.num_objects == 0) {
    GST_ERROR("Failed to export surface to dmabuf");
    goto failed;
  }

  for (i = 0; i < desc.num_objects; i++) {
    guint64 modifier = desc.objects[i].drm_format_modifier;

    if (!_modifier_found(modifier, modifiers, num_modifiers)) {
      GST_ERROR("driver set a modifier different from allowed list: "
                "0x%016" G_GINT64_MODIFIER "x",
                modifier);
      goto failed;
    }
    /* XXX: all dmabufs in buffer have to have the same modifier, otherwise the
     * drm-format field in caps is ill-designed */
    if (i > 0 && modifier != prev_modifier) {
      GST_ERROR("Different objects have different modifier");
      goto failed;
    }

    prev_modifier = modifier;
  }

  *ret_surface = surface;
  if (ret_desc)
    *ret_desc = desc;

  return TRUE;

failed : {
  va_destroy_surfaces(display, &surface, 1);
  return FALSE;
}
}

goffset CameraSrcUtils::_get_fd_size(gint fd) {
#ifndef G_OS_WIN32
  return lseek(fd, 0, SEEK_END);
#else
  return 0;
#endif
}

#endif
