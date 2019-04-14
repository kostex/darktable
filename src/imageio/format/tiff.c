/*
    This file is part of darktable,
    copyright (c) 2010--2011 Henrik Andersson and johannes hanika
    copyright (c) 2014 LebedevRI.

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
#include "bauhaus/bauhaus.h"
#include "common/colorspaces.h"
#include "common/darktable.h"
#include "common/exif.h"
#include "common/imageio.h"
#include "common/imageio_module.h"
#include "control/conf.h"
#include "imageio/format/imageio_format_api.h"
#include <inttypes.h>
#include <memory.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <tiffio.h>

DT_MODULE(3)

typedef struct dt_imageio_tiff_t
{
  dt_imageio_module_data_t global;
  int bpp;
  int compress;
  int compresslevel;
  TIFF *handle;
} dt_imageio_tiff_t;

typedef struct dt_imageio_tiff_gui_t
{
  GtkWidget *bpp;
  GtkWidget *compress;
  GtkWidget *compresslevel;
} dt_imageio_tiff_gui_t;


int write_image(dt_imageio_module_data_t *d_tmp, const char *filename, const void *in_void,
                dt_colorspaces_color_profile_type_t over_type, const char *over_filename,
                void *exif, int exif_len, int imgid, int num, int total, dt_dev_pixelpipe_t *pipe)
{
  const dt_imageio_tiff_t *d = (dt_imageio_tiff_t *)d_tmp;

  uint8_t *profile = NULL;
  uint32_t profile_len = 0;

  TIFF *tif = NULL;

  void *rowdata = NULL;

  int rc = 1; // default to error

  if(imgid > 0)
  {
    cmsHPROFILE out_profile = dt_colorspaces_get_output_profile(imgid, over_type, over_filename)->profile;
    cmsSaveProfileToMem(out_profile, 0, &profile_len);
    if(profile_len > 0)
    {
      profile = malloc(profile_len);
      if(!profile)
      {
        rc = 1;
        goto exit;
      }
      cmsSaveProfileToMem(out_profile, profile, &profile_len);
    }
  }

  // Create little endian tiff image
#ifdef _WIN32
  wchar_t *wfilename = g_utf8_to_utf16(filename, -1, NULL, NULL, NULL);
  tif = TIFFOpenW(wfilename, "wl");
  g_free(wfilename);
#else
  tif = TIFFOpen(filename, "wl");
#endif
  if(!tif)
  {
    rc = 1;
    goto exit;
  }

  // http://partners.adobe.com/public/developer/en/tiff/TIFFphotoshop.pdf (dated 2002)
  // "A proprietary ZIP/Flate compression code (0x80b2) has been used by some"
  // "software vendors. This code should be considered obsolete. We recommend"
  // "that TIFF implementations recognize and read the obsolete code but only"
  // "write the official compression code (0x0008)."
  // http://www.awaresystems.be/imaging/tiff/tifftags/compression.html
  // http://www.awaresystems.be/imaging/tiff/tifftags/predictor.html
  if(d->compress == 1)
  {
    TIFFSetField(tif, TIFFTAG_COMPRESSION, (uint16_t)COMPRESSION_ADOBE_DEFLATE);
    TIFFSetField(tif, TIFFTAG_PREDICTOR, (uint16_t)PREDICTOR_NONE);
    TIFFSetField(tif, TIFFTAG_ZIPQUALITY, (uint16_t)d->compresslevel);
  }
  else if(d->compress == 2)
  {
    TIFFSetField(tif, TIFFTAG_COMPRESSION, (uint16_t)COMPRESSION_ADOBE_DEFLATE);
    TIFFSetField(tif, TIFFTAG_PREDICTOR, (uint16_t)PREDICTOR_HORIZONTAL);
    TIFFSetField(tif, TIFFTAG_ZIPQUALITY, (uint16_t)d->compresslevel);
  }
  else if(d->compress == 3)
  {
    TIFFSetField(tif, TIFFTAG_COMPRESSION, (uint16_t)COMPRESSION_ADOBE_DEFLATE);
    if(d->bpp == 32)
      TIFFSetField(tif, TIFFTAG_PREDICTOR, (uint16_t)PREDICTOR_FLOATINGPOINT);
    else
      TIFFSetField(tif, TIFFTAG_PREDICTOR, (uint16_t)PREDICTOR_HORIZONTAL);
    TIFFSetField(tif, TIFFTAG_ZIPQUALITY, (uint16_t)d->compresslevel);
  }
  else // (d->compress == 0)
  {
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
  }

  TIFFSetField(tif, TIFFTAG_FILLORDER, (uint16_t)FILLORDER_MSB2LSB);
  if(profile != NULL)
  {
    TIFFSetField(tif, TIFFTAG_ICCPROFILE, (uint32_t)profile_len, profile);
  }
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, (uint16_t)3);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, (uint16_t)d->bpp);
  TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, (uint16_t)(d->bpp == 32 ? SAMPLEFORMAT_IEEEFP : SAMPLEFORMAT_UINT));
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, (uint32_t)d->global.width);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, (uint32_t)d->global.height);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, (uint16_t)PHOTOMETRIC_RGB);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, (uint16_t)PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, (uint32_t)1);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, (uint16_t)ORIENTATION_TOPLEFT);

  int resolution = dt_conf_get_int("metadata/resolution");
  if(resolution > 0)
  {
    TIFFSetField(tif, TIFFTAG_XRESOLUTION, (float)resolution);
    TIFFSetField(tif, TIFFTAG_YRESOLUTION, (float)resolution);
    TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, (uint16_t)RESUNIT_INCH);
  }

  const size_t rowsize = (d->global.width * 3) * d->bpp / 8;
  if((rowdata = malloc(rowsize)) == NULL)
  {
    rc = 1;
    goto exit;
  }

  if(d->bpp == 32)
  {
    for(int y = 0; y < d->global.height; y++)
    {
      float *in = (float *)in_void + (size_t)4 * y * d->global.width;
      float *out = (float *)rowdata;

      for(int x = 0; x < d->global.width; x++, in += 4, out += 3)
      {
        memcpy(out, in, 3 * sizeof(float));
      }

      if(TIFFWriteScanline(tif, rowdata, y, 0) == -1)
      {
        rc = 1;
        goto exit;
      }
    }
  }
  else if(d->bpp == 16)
  {
    for(int y = 0; y < d->global.height; y++)
    {
      uint16_t *in = (uint16_t *)in_void + (size_t)4 * y * d->global.width;
      uint16_t *out = (uint16_t *)rowdata;

      for(int x = 0; x < d->global.width; x++, in += 4, out += 3)
      {
        memcpy(out, in, 3 * sizeof(uint16_t));
      }

      if(TIFFWriteScanline(tif, rowdata, y, 0) == -1)
      {
        rc = 1;
        goto exit;
      }
    }
  }
  else
  {
    for(int y = 0; y < d->global.height; y++)
    {
      uint8_t *in = (uint8_t *)in_void + (size_t)4 * y * d->global.width;
      uint8_t *out = (uint8_t *)rowdata;

      for(int x = 0; x < d->global.width; x++, in += 4, out += 3)
      {
        memcpy(out, in, 3 * sizeof(uint8_t));
      }

      if(TIFFWriteScanline(tif, rowdata, y, 0) == -1)
      {
        rc = 1;
        goto exit;
      }
    }
  }

  // success
  rc = 0;

exit:
  // close the file before adding exif data
  if(tif)
  {
    TIFFClose(tif);
    tif = NULL;
  }
  if(!rc && exif)
  {
    rc = dt_exif_write_blob(exif, exif_len, filename, d->compress > 0);
    // Until we get symbolic error status codes, if rc is 1, return 0
    rc = (rc == 1) ? 0 : 1;
  }
  free(profile);
  profile = NULL;
  free(rowdata);
  rowdata = NULL;

  return rc;
}

#if 0
int dt_imageio_tiff_read_header(const char *filename, dt_imageio_tiff_t *tiff)
{
  tiff->handle = TIFFOpen(filename, "rl");
  if( tiff->handle )
  {
    TIFFGetField(tiff->handle, TIFFTAG_IMAGEWIDTH, &tiff->width);
    TIFFGetField(tiff->handle, TIFFTAG_IMAGELENGTH, &tiff->height);
  }
  return 1;
}

int dt_imageio_tiff_read(dt_imageio_tiff_t *tiff, uint8_t *out)
{
  TIFFClose(tiff->handle);
  return 1;
}
#endif

size_t params_size(dt_imageio_module_format_t *self)
{
  return sizeof(dt_imageio_tiff_t) - sizeof(TIFF *);
}

void *legacy_params(dt_imageio_module_format_t *self, const void *const old_params,
                    const size_t old_params_size, const int old_version, const int new_version,
                    size_t *new_size)
{
  if(old_version == 1 && new_version == 3)
  {
    typedef struct dt_imageio_tiff_v1_t
    {
      int max_width, max_height;
      int width, height;
      char style[128];
      int bpp;
      int compress;
      TIFF *handle;
    } dt_imageio_tiff_v1_t;

    const dt_imageio_tiff_v1_t *o = (dt_imageio_tiff_v1_t *)old_params;
    dt_imageio_tiff_t *n = (dt_imageio_tiff_t *)calloc(1, sizeof(dt_imageio_tiff_t));

    n->global.max_width = o->max_width;
    n->global.max_height = o->max_height;
    n->global.width = o->width;
    n->global.height = o->height;
    g_strlcpy(n->global.style, o->style, sizeof(o->style));
    n->global.style_append = FALSE;
    n->bpp = o->bpp;
    n->compress = o->compress;
    n->compresslevel = 9;
    n->handle = o->handle;
    *new_size = self->params_size(self);
    return n;
  }
  else if(old_version == 2 && new_version == 3)
  {
    typedef struct dt_imageio_tiff_v2_t
    {
      int max_width, max_height;
      int width, height;
      char style[128];
      gboolean style_append;
      int bpp;
      int compress;
      TIFF *handle;
    } dt_imageio_tiff_v2_t;

    const dt_imageio_tiff_v2_t *o = (dt_imageio_tiff_v2_t *)old_params;
    dt_imageio_tiff_t *n = (dt_imageio_tiff_t *)calloc(1, sizeof(dt_imageio_tiff_t));

    n->global.max_width = o->max_width;
    n->global.max_height = o->max_height;
    n->global.width = o->width;
    n->global.height = o->height;
    g_strlcpy(n->global.style, o->style, sizeof(o->style));
    n->global.style_append = o->style_append;
    n->bpp = o->bpp;
    n->compress = o->compress;
    n->compresslevel = 9;
    n->handle = o->handle;
    *new_size = self->params_size(self);
    return n;
  }
  return NULL;
}

void *get_params(dt_imageio_module_format_t *self)
{
  dt_imageio_tiff_t *d = (dt_imageio_tiff_t *)calloc(1, sizeof(dt_imageio_tiff_t));
  d->bpp = dt_conf_get_int("plugins/imageio/format/tiff/bpp");
  if(d->bpp == 16)
    d->bpp = 16;
  else if(d->bpp == 32)
    d->bpp = 32;
  else
    d->bpp = 8;
  d->compress = dt_conf_get_int("plugins/imageio/format/tiff/compress");

  // TIFF compression level might actually be zero, handle this
  if(!dt_conf_key_exists("plugins/imageio/format/tiff/compresslevel"))
    d->compresslevel = 5;
  else
  {
    d->compresslevel = dt_conf_get_int("plugins/imageio/format/tiff/compresslevel");
    if(d->compresslevel < 0 || d->compresslevel > 9) d->compresslevel = 5;
  }

  return d;
}

void free_params(dt_imageio_module_format_t *self, dt_imageio_module_data_t *params)
{
  free(params);
}

int set_params(dt_imageio_module_format_t *self, const void *params, const int size)
{
  if(size != self->params_size(self)) return 1;
  const dt_imageio_tiff_t *d = (dt_imageio_tiff_t *)params;
  const dt_imageio_tiff_gui_t *g = (dt_imageio_tiff_gui_t *)self->gui_data;

  if(d->bpp == 16)
    dt_bauhaus_combobox_set(g->bpp, 1);
  else if(d->bpp == 32)
    dt_bauhaus_combobox_set(g->bpp, 2);
  else // (d->bpp == 8)
    dt_bauhaus_combobox_set(g->bpp, 0);

  dt_bauhaus_combobox_set(g->compress, d->compress);

  dt_bauhaus_slider_set(g->compresslevel, d->compresslevel);

  return 0;
}

int bpp(dt_imageio_module_data_t *p)
{
  return ((dt_imageio_tiff_t *)p)->bpp;
}

int levels(dt_imageio_module_data_t *p)
{
  int ret = IMAGEIO_RGB;

  if(((dt_imageio_tiff_t *)p)->bpp == 8)
    ret |= IMAGEIO_INT8;
  else if(((dt_imageio_tiff_t *)p)->bpp == 16)
    ret |= IMAGEIO_INT16;
  else if(((dt_imageio_tiff_t *)p)->bpp == 32)
    ret |= IMAGEIO_FLOAT;

  return ret;
}

const char *mime(dt_imageio_module_data_t *data)
{
  return "image/tiff";
}

const char *extension(dt_imageio_module_data_t *data)
{
  return "tif";
}

const char *name()
{
  return _("TIFF (8/16/32-bit)");
}

static void bpp_combobox_changed(GtkWidget *widget, gpointer user_data)
{
  const int bpp = dt_bauhaus_combobox_get(widget);

  if(bpp == 1)
    dt_conf_set_int("plugins/imageio/format/tiff/bpp", 16);
  else if(bpp == 2)
    dt_conf_set_int("plugins/imageio/format/tiff/bpp", 32);
  else // (bpp == 0)
    dt_conf_set_int("plugins/imageio/format/tiff/bpp", 8);
}

static void compress_combobox_changed(GtkWidget *widget, gpointer user_data)
{
  const int compress = dt_bauhaus_combobox_get(widget);
  dt_conf_set_int("plugins/imageio/format/tiff/compress", compress);
}

static void compress_level_changed(GtkWidget *slider, gpointer user_data)
{
  const int compresslevel = (int)dt_bauhaus_slider_get(slider);
  dt_conf_set_int("plugins/imageio/format/tiff/compresslevel", compresslevel);
}

void init(dt_imageio_module_format_t *self)
{
#ifdef USE_LUA
  dt_lua_register_module_member(darktable.lua_state.state, self, dt_imageio_tiff_t, bpp, int);
#endif
}
void cleanup(dt_imageio_module_format_t *self)
{
}

void gui_init(dt_imageio_module_format_t *self)
{
  dt_imageio_tiff_gui_t *gui = (dt_imageio_tiff_gui_t *)malloc(sizeof(dt_imageio_tiff_gui_t));
  self->gui_data = (void *)gui;

  const int bpp = dt_conf_get_int("plugins/imageio/format/tiff/bpp");

  const int compress = dt_conf_get_int("plugins/imageio/format/tiff/compress");

  // TIFF compression level might actually be zero!
  int compresslevel = 5;
  if(dt_conf_key_exists("plugins/imageio/format/tiff/compresslevel"))
    compresslevel = dt_conf_get_int("plugins/imageio/format/tiff/compresslevel");

  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  // Bit depth combo box
  gui->bpp = dt_bauhaus_combobox_new(NULL);
  dt_bauhaus_widget_set_label(gui->bpp, NULL, _("bit depth"));
  dt_bauhaus_combobox_add(gui->bpp, _("8 bit"));
  dt_bauhaus_combobox_add(gui->bpp, _("16 bit"));
  dt_bauhaus_combobox_add(gui->bpp, _("32 bit (float)"));
  if(bpp == 16)
    dt_bauhaus_combobox_set(gui->bpp, 1);
  else if(bpp == 32)
    dt_bauhaus_combobox_set(gui->bpp, 2);
  else // (bpp == 8)
    dt_bauhaus_combobox_set(gui->bpp, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), gui->bpp, TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(gui->bpp), "value-changed", G_CALLBACK(bpp_combobox_changed), NULL);

  // Compression method combo box
  gui->compress = dt_bauhaus_combobox_new(NULL);
  dt_bauhaus_widget_set_label(gui->compress, NULL, _("compression"));
  dt_bauhaus_combobox_add(gui->compress, _("uncompressed"));
  dt_bauhaus_combobox_add(gui->compress, _("deflate"));
  dt_bauhaus_combobox_add(gui->compress, _("deflate with predictor"));
  dt_bauhaus_combobox_add(gui->compress, _("deflate with predictor (float)"));
  dt_bauhaus_combobox_set(gui->compress, compress);
  gtk_box_pack_start(GTK_BOX(self->widget), gui->compress, TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(gui->compress), "value-changed", G_CALLBACK(compress_combobox_changed), NULL);

  // Compression level slider
  gui->compresslevel = dt_bauhaus_slider_new_with_range(NULL, 0, 9, 1, 5, 0);
  dt_bauhaus_widget_set_label(gui->compresslevel, NULL, _("compression level"));
  dt_bauhaus_slider_set(gui->compresslevel, compresslevel);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(gui->compresslevel), TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(gui->compresslevel), "value-changed", G_CALLBACK(compress_level_changed), NULL);
}

void gui_cleanup(dt_imageio_module_format_t *self)
{
  free(self->gui_data);
}

void gui_reset(dt_imageio_module_format_t *self)
{
  // TODO: reset to conf? reset to factory defaults?
}

int flags(dt_imageio_module_data_t *data)
{
  return FORMAT_FLAGS_SUPPORT_XMP;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
