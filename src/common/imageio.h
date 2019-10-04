/*
    This file is part of darktable,
    copyright (c) 2009--2011 johannes hanika.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "common/image.h"
#include "common/imageio_module.h"
#include "common/mipmap_cache.h"
#include <glib.h>
#include <stdio.h>

#include <inttypes.h>

#define FILTERS_ARE_CYGM(filters)                                                                                 \
  ((filters) == 0xb4b4b4b4 || (filters) == 0x4b4b4b4b || (filters) == 0x1e1e1e1e || (filters) == 0xe1e1e1e1)

#define FILTERS_ARE_RGBE(filters)                                                                                 \
  ((filters) == 0x63636363 || (filters) == 0x36363636 || (filters) == 0x9c9c9c9c || (filters) == 0xc9c9c9c9)

// FIXME: kill this pls.
#define FILTERS_ARE_4BAYER(filters) (FILTERS_ARE_CYGM(filters) || FILTERS_ARE_RGBE(filters))

typedef enum dt_imageio_levels_t
{
  IMAGEIO_INT8 = 0x0,
  IMAGEIO_INT12 = 0x1,
  IMAGEIO_INT16 = 0x2,
  IMAGEIO_INT32 = 0x3,
  IMAGEIO_FLOAT = 0x4,
  IMAGEIO_BW = 0x5,
  IMAGEIO_PREC_MASK = 0xFF,

  IMAGEIO_RGB = 0x100,
  IMAGEIO_GRAY = 0x200,
  IMAGEIO_CHANNEL_MASK = 0xFF00
} dt_imageio_levels_t;

// Checks that the image is indeed an ldr image
gboolean dt_imageio_is_ldr(const char *filename);

// opens the file using pfm, hdr, exr.
dt_imageio_retval_t dt_imageio_open_hdr(dt_image_t *img, const char *filename, dt_mipmap_buffer_t *buf);
// opens file using imagemagick
dt_imageio_retval_t dt_imageio_open_ldr(dt_image_t *img, const char *filename, dt_mipmap_buffer_t *buf);
// try all the options in sequence
dt_imageio_retval_t dt_imageio_open(dt_image_t *img, const char *filename, dt_mipmap_buffer_t *buf);
// tries to open the files not opened by the other routines using GraphicsMagick (if supported)
dt_imageio_retval_t dt_imageio_open_exotic(dt_image_t *img, const char *filename,
                                           dt_mipmap_buffer_t *buf);

struct dt_imageio_module_format_t;
struct dt_imageio_module_data_t;
int dt_imageio_export(const uint32_t imgid, const char *filename, struct dt_imageio_module_format_t *format,
                      struct dt_imageio_module_data_t *format_params, const gboolean high_quality, const gboolean upscale,
                      const gboolean copy_metadata, dt_colorspaces_color_profile_type_t icc_type,
                      const gchar *icc_filename, dt_iop_color_intent_t icc_intent, dt_imageio_module_storage_t *storage,
                      dt_imageio_module_data_t *storage_params, int num, int total, dt_export_metadata_t *metadata);

int dt_imageio_export_with_flags(const uint32_t imgid, const char *filename,
                                 struct dt_imageio_module_format_t *format,
                                 struct dt_imageio_module_data_t *format_params, const gboolean ignore_exif,
                                 const gboolean display_byteorder, const gboolean high_quality, const gboolean upscale,
                                 const gboolean thumbnail_export, const char *filter, const gboolean copy_metadata,
                                 dt_colorspaces_color_profile_type_t icc_type, const gchar *icc_filename,
                                 dt_iop_color_intent_t icc_intent, dt_imageio_module_storage_t *storage,
                                 dt_imageio_module_data_t *storage_params, int num, int total, dt_export_metadata_t *metadata);

size_t dt_imageio_write_pos(int i, int j, int wd, int ht, float fwd, float fht,
                            dt_image_orientation_t orientation);

// general, efficient buffer flipping function using memcopies
void dt_imageio_flip_buffers(char *out, const char *in,
                             const size_t bpp, // bytes per pixel
                             const int wd, const int ht, const int fwd, const int fht, const int stride,
                             const dt_image_orientation_t orientation);

void dt_imageio_flip_buffers_ui16_to_float(float *out, const uint16_t *in, const float black,
                                           const float white, const int ch, const int wd, const int ht,
                                           const int fwd, const int fht, const int stride,
                                           const dt_image_orientation_t orientation);
void dt_imageio_flip_buffers_ui8_to_float(float *out, const uint8_t *in, const float black, const float white,
                                          const int ch, const int wd, const int ht, const int fwd,
                                          const int fht, const int stride,
                                          const dt_image_orientation_t orientation);

// allocate buffer and return 0 on success along with largest jpg thumbnail from raw.
int dt_imageio_large_thumbnail(const char *filename, uint8_t **buffer, int32_t *width, int32_t *height,
                               dt_colorspaces_color_profile_type_t *color_space);

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
