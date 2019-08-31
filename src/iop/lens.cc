/*
    This file is part of darktable,
    copyright (c) 2009--2012 johannes hanika.
    copyright (c) 2014-2015 LebedevRI.

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

extern "C" {

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "bauhaus/bauhaus.h"
#include "common/interpolation.h"
#include "common/file_location.h"
#include "common/opencl.h"
#include "control/control.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "develop/tiling.h"
#include "dtgtk/button.h"
#include "dtgtk/resetlabel.h"
#include "gui/accelerators.h"
#include "gui/draw.h"
#include "gui/gtk.h"
#include "iop/iop_api.h"
#include <assert.h>
#include <ctype.h>
#include <gtk/gtk.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
}

#include <lensfun.h>

extern "C" {

#if LF_VERSION < ((0 << 24) | (2 << 16) | (9 << 8) | 0)
#define LF_SEARCH_SORT_AND_UNIQUIFY 2
#endif

#if LF_VERSION == ((0 << 24) | (3 << 16) | (95 << 8) | 0)
#define LF_0395
#endif

DT_MODULE_INTROSPECTION(5, dt_iop_lensfun_params_t)

typedef enum dt_iop_lensfun_modflag_t
{
  LENSFUN_MODFLAG_NONE = 0,
  LENSFUN_MODFLAG_ALL = LF_MODIFY_DISTORTION | LF_MODIFY_TCA | LF_MODIFY_VIGNETTING,
  LENSFUN_MODFLAG_DIST_TCA = LF_MODIFY_DISTORTION | LF_MODIFY_TCA,
  LENSFUN_MODFLAG_DIST_VIGN = LF_MODIFY_DISTORTION | LF_MODIFY_VIGNETTING,
  LENSFUN_MODFLAG_TCA_VIGN = LF_MODIFY_TCA | LF_MODIFY_VIGNETTING,
  LENSFUN_MODFLAG_DIST = LF_MODIFY_DISTORTION,
  LENSFUN_MODFLAG_TCA = LF_MODIFY_TCA,
  LENSFUN_MODFLAG_VIGN = LF_MODIFY_VIGNETTING,
  LENSFUN_MODFLAG_MASK = LF_MODIFY_DISTORTION | LF_MODIFY_TCA | LF_MODIFY_VIGNETTING
} dt_iop_lensfun_modflag_t;

typedef struct dt_iop_lensfun_modifier_t
{
  char name[40];
  int pos; // position in combo box
  int modflag;
} dt_iop_lensfun_modifier_t;

typedef struct dt_iop_lensfun_params_t
{
  int modify_flags;
  int inverse;
  float scale;
  float crop;
  float focal;
  float aperture;
  float distance;
  lfLensType target_geom;
  char camera[128];
  char lens[128];
  int tca_override;
  float tca_r, tca_b;
  int modified; // did user changed anything from automatically detected?
} dt_iop_lensfun_params_t;

typedef struct dt_iop_lensfun_gui_data_t
{
  const lfCamera *camera;
  GtkWidget *lens_param_box;
  GtkWidget *detection_warning;
  GtkWidget *cbe[3];
  GtkButton *camera_model;
  GtkMenu *camera_menu;
  GtkButton *lens_model;
  GtkMenu *lens_menu;
  GtkWidget *modflags, *target_geom, *reverse, *tca_r, *tca_b, *scale;
  GtkWidget *find_lens_button;
  GtkWidget *find_camera_button;
  GList *modifiers;
  GtkLabel *message;
  int corrections_done;
  dt_pthread_mutex_t lock;
} dt_iop_lensfun_gui_data_t;

typedef struct dt_iop_lensfun_global_data_t
{
  lfDatabase *db;
  int kernel_lens_distort_bilinear;
  int kernel_lens_distort_bicubic;
  int kernel_lens_distort_lanczos2;
  int kernel_lens_distort_lanczos3;
  int kernel_lens_vignette;
} dt_iop_lensfun_global_data_t;

typedef struct dt_iop_lensfun_data_t
{
  lfLens *lens;
  int modify_flags;
  int inverse;
  float scale;
  float crop;
  float focal;
  float aperture;
  float distance;
  lfLensType target_geom;
  gboolean do_nan_checks;
  gboolean tca_override;
  lfLensCalibTCA custom_tca;
} dt_iop_lensfun_data_t;


const char *name()
{
  return _("lens correction");
}

int default_group()
{
  return IOP_GROUP_CORRECT;
}

int operation_tags()
{
  return IOP_TAG_DISTORT;
}

int flags()
{
  return IOP_FLAGS_ALLOW_TILING | IOP_FLAGS_TILING_FULL_ROI | IOP_FLAGS_ONE_INSTANCE;
}

int default_colorspace(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  return iop_cs_rgb;
}

void init_key_accels(dt_iop_module_so_t *self)
{
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "scale"));
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "TCA R"));
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "TCA B"));

  dt_accel_register_iop(self, FALSE, NC_("accel", "find camera"), 0, (GdkModifierType)0);
  dt_accel_register_iop(self, FALSE, NC_("accel", "find lens"), 0, (GdkModifierType)0);
  dt_accel_register_iop(self, FALSE, NC_("accel", "auto scale"), 0, (GdkModifierType)0);
  dt_accel_register_iop(self, FALSE, NC_("accel", "camera model"), 0, (GdkModifierType)0);
  dt_accel_register_iop(self, FALSE, NC_("accel", "lens model"), 0, (GdkModifierType)0);
  dt_accel_register_iop(self, FALSE, NC_("accel", "select corrections"), 0, (GdkModifierType)0);
}

void connect_key_accels(dt_iop_module_t *self)
{
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;

  dt_accel_connect_button_iop(self, "find lens", GTK_WIDGET(g->find_lens_button));
  dt_accel_connect_button_iop(self, "lens model", GTK_WIDGET(g->lens_model));
  dt_accel_connect_button_iop(self, "camera model", GTK_WIDGET(g->camera_model));
  dt_accel_connect_button_iop(self, "find camera", GTK_WIDGET(g->find_camera_button));
  dt_accel_connect_button_iop(self, "select corrections", GTK_WIDGET(g->modflags));

  dt_accel_connect_slider_iop(self, "scale", GTK_WIDGET(g->scale));
  dt_accel_connect_slider_iop(self, "tca R", GTK_WIDGET(g->tca_r));
  dt_accel_connect_slider_iop(self, "tca B", GTK_WIDGET(g->tca_b));
}

int legacy_params(dt_iop_module_t *self, const void *const old_params, const int old_version,
                  void *new_params, const int new_version)
{
  if(old_version == 2 && new_version == 5)
  {
    // legacy params of version 2; version 1 comes from ancient times and seems to be forgotten by now
    typedef struct
    {
      int modify_flags;
      int inverse;
      float scale;
      float crop;
      float focal;
      float aperture;
      float distance;
      lfLensType target_geom;
      char camera[52];
      char lens[52];
      int tca_override;
      float tca_r, tca_b;
    } dt_iop_lensfun_params_v2_t;

    const dt_iop_lensfun_params_v2_t *o = (dt_iop_lensfun_params_v2_t *)old_params;
    dt_iop_lensfun_params_t *n = (dt_iop_lensfun_params_t *)new_params;
    dt_iop_lensfun_params_t *d = (dt_iop_lensfun_params_t *)self->default_params;

    *n = *d; // start with a fresh copy of default parameters

    n->modify_flags = o->modify_flags;
    n->inverse = o->inverse;
    n->scale = o->scale;
    n->crop = o->crop;
    n->focal = o->focal;
    n->aperture = o->aperture;
    n->distance = o->distance;
    n->target_geom = o->target_geom;
    n->tca_override = o->tca_override;
    g_strlcpy(n->camera, o->camera, sizeof(n->camera));
    g_strlcpy(n->lens, o->lens, sizeof(n->lens));
    n->modified = 1;

    // old versions had R and B swapped
    n->tca_r = o->tca_b;
    n->tca_b = o->tca_r;

    return 0;
  }
  if(old_version == 3 && new_version == 5)
  {
    typedef struct
    {
      int modify_flags;
      int inverse;
      float scale;
      float crop;
      float focal;
      float aperture;
      float distance;
      lfLensType target_geom;
      char camera[128];
      char lens[128];
      int tca_override;
      float tca_r, tca_b;
    } dt_iop_lensfun_params_v3_t;

    const dt_iop_lensfun_params_v3_t *o = (dt_iop_lensfun_params_v3_t *)old_params;
    dt_iop_lensfun_params_t *n = (dt_iop_lensfun_params_t *)new_params;
    dt_iop_lensfun_params_t *d = (dt_iop_lensfun_params_t *)self->default_params;

    *n = *d; // start with a fresh copy of default parameters

    memcpy(n, o, sizeof(dt_iop_lensfun_params_t) - sizeof(int));

    // one more parameter and changed parameters in case we autodetect
    n->modified = 1;

    // old versions had R and B swapped
    n->tca_r = o->tca_b;
    n->tca_b = o->tca_r;

    return 0;
  }

  if(old_version == 4 && new_version == 5)
  {
    typedef struct
    {
      int modify_flags;
      int inverse;
      float scale;
      float crop;
      float focal;
      float aperture;
      float distance;
      lfLensType target_geom;
      char camera[128];
      char lens[128];
      int tca_override;
      float tca_r, tca_b;
      int modified;
    } dt_iop_lensfun_params_v4_t;

    const dt_iop_lensfun_params_v4_t *o = (dt_iop_lensfun_params_v4_t *)old_params;
    dt_iop_lensfun_params_t *n = (dt_iop_lensfun_params_t *)new_params;
    dt_iop_lensfun_params_t *d = (dt_iop_lensfun_params_t *)self->default_params;

    *n = *d; // start with a fresh copy of default parameters

    memcpy(n, o, sizeof(dt_iop_lensfun_params_t));

    // old versions had R and B swapped
    n->tca_r = o->tca_b;
    n->tca_b = o->tca_r;

    return 0;
  }

  return 1;
}

static char *_lens_sanitize(const char *orig_lens)
{
  const char *found_or = strstr(orig_lens, " or ");
  const char *found_parenthesis = strstr(orig_lens, " (");

  if(found_or || found_parenthesis)
  {
    size_t pos_or = (size_t)(found_or - orig_lens);
    size_t pos_parenthesis = (size_t)(found_parenthesis - orig_lens);
    size_t pos = pos_or < pos_parenthesis ? pos_or : pos_parenthesis;

    if(pos > 0)
    {
      char *new_lens = (char *)malloc(pos + 1);

      strncpy(new_lens, orig_lens, pos);
      new_lens[pos] = '\0';

      return new_lens;
    }
    else
    {
      char *new_lens = strdup(orig_lens);
      return new_lens;
    }
  }
  else
  {
    char *new_lens = strdup(orig_lens);
    return new_lens;
  }
}

static lfModifier * get_modifier(int *mods_done, int w, int h, const dt_iop_lensfun_data_t *d, int mods_filter)
{
  lfModifier *mod;
  int mods_todo = d->modify_flags & mods_filter;
  int mods_done_tmp = 0;

#ifdef LF_0395
  mod = new lfModifier(d->crop, w, h, LF_PF_F32, d->inverse);
  if(mods_todo & LF_MODIFY_DISTORTION)
    mods_done_tmp |= mod->EnableDistortionCorrection(d->lens, d->focal);
  if((mods_todo & LF_MODIFY_GEOMETRY) && (d->lens->Type != d->target_geom))
    mods_done_tmp |= mod->EnableProjectionTransform(d->lens, d->focal, d->target_geom);
  if((mods_todo & LF_MODIFY_SCALE) && (d->scale != 1.0))
    mods_done_tmp |= mod->EnableScaling(d->scale);
  if(mods_todo & LF_MODIFY_TCA)
  {
    if(d->tca_override) mods_done_tmp |= mod->EnableTCACorrection(d->custom_tca);
    else mods_done_tmp |= mod->EnableTCACorrection(d->lens, d->focal);
  }
  if(mods_todo & LF_MODIFY_VIGNETTING)
    mods_done_tmp |= mod->EnableVignettingCorrection(d->lens, d->focal, d->aperture, d->distance);
#else
  mod = new lfModifier(d->lens, d->crop, w, h);
  mods_done_tmp = mod->Initialize(d->lens, LF_PF_F32, d->focal, d->aperture, d->distance, d->scale, d->target_geom, mods_todo, d->inverse);
#endif

  if(mods_done) *mods_done = mods_done_tmp;
  return mod;
}

void process(dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const void *const ivoid, void *const ovoid,
             const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  const dt_iop_lensfun_data_t *const d = (dt_iop_lensfun_data_t *)piece->data;
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;

  const int ch = piece->colors;
  const int ch_width = ch * roi_in->width;
  const int mask_display = piece->pipe->mask_display;

  const unsigned int pixelformat = ch == 3 ? LF_CR_3(RED, GREEN, BLUE) : LF_CR_4(RED, GREEN, BLUE, UNKNOWN);

  if(!d->lens || !d->lens->Maker || d->crop <= 0.0f)
  {
    memcpy(ovoid, ivoid, (size_t)ch * sizeof(float) * roi_out->width * roi_out->height);
    return;
  }

  const float orig_w = roi_in->scale * piece->buf_in.width, orig_h = roi_in->scale * piece->buf_in.height;

  dt_pthread_mutex_lock(&darktable.plugin_threadsafe);

  int modflags;
  lfModifier *modifier = get_modifier(&modflags, orig_w, orig_h, d, LF_MODIFY_ALL);

  dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);

  const struct dt_interpolation *const interpolation = dt_interpolation_new(DT_INTERPOLATION_USERPREF);

  if(d->inverse)
  {
    // reverse direction (useful for renderings)
    if(modflags & (LF_MODIFY_TCA | LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY | LF_MODIFY_SCALE))
    {
      // acquire temp memory for distorted pixel coords
      const size_t bufsize = (size_t)roi_out->width * 2 * 3;
      void *buf = dt_alloc_align(64, bufsize * dt_get_num_threads() * sizeof(float));

#ifdef _OPENMP
#pragma omp parallel for default(none) \
      dt_omp_firstprivate(bufsize, ch, ch_width, d, interpolation, ivoid, \
                          mask_display, ovoid, roi_in, roi_out) \
      shared(buf, modifier) \
      schedule(static)
#endif
      for(int y = 0; y < roi_out->height; y++)
      {
        float *bufptr = ((float *)buf) + (size_t)bufsize * dt_get_thread_num();
        modifier->ApplySubpixelGeometryDistortion(roi_out->x, roi_out->y + y, roi_out->width, 1, bufptr);

        // reverse transform the global coords from lf to our buffer
        float *out = ((float *)ovoid) + (size_t)y * roi_out->width * ch;
        for(int x = 0; x < roi_out->width; x++, bufptr += 6, out += ch)
        {
          for(int c = 0; c < 3; c++)
          {
            if(d->do_nan_checks && (!isfinite(bufptr[c * 2]) || !isfinite(bufptr[c * 2 + 1])))
            {
              out[c] = 0.0f;
              continue;
            }

            const float *const inptr = (const float *const)ivoid + (size_t)c;
            const float pi0 = bufptr[c * 2] - roi_in->x;
            const float pi1 = bufptr[c * 2 + 1] - roi_in->y;
            out[c] = dt_interpolation_compute_sample(interpolation, inptr, pi0, pi1, roi_in->width,
                                                     roi_in->height, ch, ch_width);
          }

          if(mask_display & DT_DEV_PIXELPIPE_DISPLAY_MASK)
          {
            if(d->do_nan_checks && (!isfinite(bufptr[2]) || !isfinite(bufptr[3])))
            {
              out[3] = 0.0f;
              continue;
            }

            // take green channel distortion also for alpha channel
            const float *const inptr = (const float *const)ivoid + (size_t)3;
            const float pi0 = bufptr[2] - roi_in->x;
            const float pi1 = bufptr[3] - roi_in->y;
            out[3] = dt_interpolation_compute_sample(interpolation, inptr, pi0, pi1, roi_in->width,
                                                     roi_in->height, ch, ch_width);
          }
        }
      }
      dt_free_align(buf);
    }
    else
    {
      memcpy(ovoid, ivoid, (size_t)ch * sizeof(float) * roi_out->width * roi_out->height);
    }

    if(modflags & LF_MODIFY_VIGNETTING)
    {
#ifdef _OPENMP
#pragma omp parallel for default(none) \
      dt_omp_firstprivate(ch, pixelformat, roi_out, ovoid) \
      shared(modifier) \
      schedule(static)
#endif
      for(int y = 0; y < roi_out->height; y++)
      {
        /* Colour correction: vignetting */
        // actually this way row stride does not matter.
        float *out = ((float *)ovoid) + (size_t)y * roi_out->width * ch;
        modifier->ApplyColorModification(out, roi_out->x, roi_out->y + y, roi_out->width, 1,
                                         pixelformat, ch * roi_out->width);
      }
    }
  }
  else // correct distortions:
  {
    // acquire temp memory for image buffer
    const size_t bufsize = (size_t)roi_in->width * roi_in->height * ch * sizeof(float);
    void *buf = dt_alloc_align(64, bufsize);
    memcpy(buf, ivoid, bufsize);

    if(modflags & LF_MODIFY_VIGNETTING)
    {
#ifdef _OPENMP
#pragma omp parallel for default(none) \
      dt_omp_firstprivate(ch, pixelformat, roi_in) \
      shared(buf, modifier) \
      schedule(static)
#endif
      for(int y = 0; y < roi_in->height; y++)
      {
        /* Colour correction: vignetting */
        // actually this way row stride does not matter.
        float *bufptr = ((float *)buf) + (size_t)ch * roi_in->width * y;
        modifier->ApplyColorModification(bufptr, roi_in->x, roi_in->y + y, roi_in->width, 1,
                                         pixelformat, ch * roi_in->width);
      }
    }

    if(modflags & (LF_MODIFY_TCA | LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY | LF_MODIFY_SCALE))
    {
      // acquire temp memory for distorted pixel coords
      const size_t buf2size = (size_t)roi_out->width * 2 * 3;
      void *buf2 = dt_alloc_align(64, buf2size * sizeof(float) * dt_get_num_threads());

#ifdef _OPENMP
#pragma omp parallel for default(none) \
      dt_omp_firstprivate(buf2size, ch, ch_width, d, interpolation, mask_display, ovoid, roi_in, roi_out) \
      shared(buf2, buf, modifier) \
      schedule(static)
#endif
      for(int y = 0; y < roi_out->height; y++)
      {
        float *buf2ptr = ((float *)buf2) + (size_t)buf2size * dt_get_thread_num();
        modifier->ApplySubpixelGeometryDistortion(roi_out->x, roi_out->y + y, roi_out->width,
                                                  1, buf2ptr);
        // reverse transform the global coords from lf to our buffer
        float *out = ((float *)ovoid) + (size_t)y * roi_out->width * ch;
        for(int x = 0; x < roi_out->width; x++, buf2ptr += 6, out += ch)
        {
          for(int c = 0; c < 3; c++)
          {
            if(d->do_nan_checks && (!isfinite(buf2ptr[c * 2]) || !isfinite(buf2ptr[c * 2 + 1])))
            {
              out[c] = 0.0f;
              continue;
            }

            float *bufptr = ((float *)buf) + c;
            const float pi0 = buf2ptr[c * 2] - roi_in->x;
            const float pi1 = buf2ptr[c * 2 + 1] - roi_in->y;
            out[c] = dt_interpolation_compute_sample(interpolation, bufptr, pi0, pi1, roi_in->width,
                                                     roi_in->height, ch, ch_width);
          }

          if(mask_display & DT_DEV_PIXELPIPE_DISPLAY_MASK)
          {
            if(d->do_nan_checks && (!isfinite(buf2ptr[2]) || !isfinite(buf2ptr[3])))
            {
              out[3] = 0.0f;
              continue;
            }

            // take green channel distortion also for alpha channel
            float *bufptr = ((float *)buf) + 3;
            const float pi0 = buf2ptr[2] - roi_in->x;
            const float pi1 = buf2ptr[3] - roi_in->y;
            out[3] = dt_interpolation_compute_sample(interpolation, bufptr, pi0, pi1, roi_in->width,
                                                     roi_in->height, ch, ch_width);
          }
        }
      }
      dt_free_align(buf2);
    }
    else
    {
      memcpy(ovoid, buf, bufsize);
    }
    dt_free_align(buf);
  }
  delete modifier;

  if(self->dev->gui_attached && g && piece->pipe->type == DT_DEV_PIXELPIPE_PREVIEW)
  {
    dt_pthread_mutex_lock(&g->lock);
    g->corrections_done = (modflags & LENSFUN_MODFLAG_MASK);
    dt_pthread_mutex_unlock(&g->lock);
  }
}

#ifdef HAVE_OPENCL
int process_cl(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, cl_mem dev_in, cl_mem dev_out,
               const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  dt_iop_lensfun_data_t *d = (dt_iop_lensfun_data_t *)piece->data;
  dt_iop_lensfun_global_data_t *gd = (dt_iop_lensfun_global_data_t *)self->global_data;
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;

  cl_mem dev_tmpbuf = NULL;
  cl_mem dev_tmp = NULL;
  cl_int err = -999;

  float *tmpbuf = NULL;
  lfModifier *modifier = NULL;

  const int devid = piece->pipe->devid;
  const int iwidth = roi_in->width;
  const int iheight = roi_in->height;
  const int owidth = roi_out->width;
  const int oheight = roi_out->height;
  const int roi_in_x = roi_in->x;
  const int roi_in_y = roi_in->y;
  const int width = MAX(iwidth, owidth);
  const int height = MAX(iheight, oheight);
  const int ch = piece->colors;
  const int tmpbufwidth = owidth * 2 * 3;
  const size_t tmpbuflen = d->inverse ? (size_t)oheight * owidth * 2 * 3 * sizeof(float)
                                      : MAX((size_t)oheight * owidth * 2 * 3, (size_t)iheight * iwidth * ch)
                                        * sizeof(float);
  const unsigned int pixelformat = ch == 3 ? LF_CR_3(RED, GREEN, BLUE) : LF_CR_4(RED, GREEN, BLUE, UNKNOWN);

  const float orig_w = roi_in->scale * piece->buf_in.width, orig_h = roi_in->scale * piece->buf_in.height;

  size_t origin[] = { 0, 0, 0 };
  size_t iregion[] = { (size_t)iwidth, (size_t)iheight, 1 };
  size_t oregion[] = { (size_t)owidth, (size_t)oheight, 1 };
  size_t isizes[] = { (size_t)ROUNDUPWD(iwidth), (size_t)ROUNDUPHT(iheight), 1 };
  size_t osizes[] = { (size_t)ROUNDUPWD(owidth), (size_t)ROUNDUPHT(oheight), 1 };

  int modflags;
  int ldkernel = -1;
  const struct dt_interpolation *interpolation = dt_interpolation_new(DT_INTERPOLATION_USERPREF);

  if(!d->lens || !d->lens->Maker || d->crop <= 0.0f)
  {
    err = dt_opencl_enqueue_copy_image(devid, dev_in, dev_out, origin, origin, oregion);
    if(err != CL_SUCCESS) goto error;
    return TRUE;
  }

  switch(interpolation->id)
  {
    case DT_INTERPOLATION_BILINEAR:
      ldkernel = gd->kernel_lens_distort_bilinear;
      break;
    case DT_INTERPOLATION_BICUBIC:
      ldkernel = gd->kernel_lens_distort_bicubic;
      break;
    case DT_INTERPOLATION_LANCZOS2:
      ldkernel = gd->kernel_lens_distort_lanczos2;
      break;
    case DT_INTERPOLATION_LANCZOS3:
      ldkernel = gd->kernel_lens_distort_lanczos3;
      break;
    default:
      return FALSE;
  }

  tmpbuf = (float *)dt_alloc_align(64, tmpbuflen);
  if(tmpbuf == NULL) goto error;

  dev_tmp = (cl_mem)dt_opencl_alloc_device(devid, width, height, 4 * sizeof(float));
  if(dev_tmp == NULL) goto error;

  dev_tmpbuf = (cl_mem)dt_opencl_alloc_device_buffer(devid, tmpbuflen);
  if(dev_tmpbuf == NULL) goto error;

  dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
  modifier = get_modifier(&modflags, orig_w, orig_h, d, LF_MODIFY_ALL);
  dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);

  if(d->inverse)
  {
    // reverse direction (useful for renderings)
    if(modflags & (LF_MODIFY_TCA | LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY | LF_MODIFY_SCALE))
    {
#ifdef _OPENMP
#pragma omp parallel for default(none) \
      dt_omp_firstprivate(tmpbufwidth, roi_out) \
      shared(tmpbuf, d, modifier) \
      schedule(static)
#endif
      for(int y = 0; y < roi_out->height; y++)
      {
        float *pi = tmpbuf + (size_t)y * tmpbufwidth;
        modifier->ApplySubpixelGeometryDistortion(roi_out->x, roi_out->y + y, roi_out->width, 1, pi);
      }

      /* _blocking_ memory transfer: host tmpbuf buffer -> opencl dev_tmpbuf */
      err = dt_opencl_write_buffer_to_device(devid, tmpbuf, dev_tmpbuf, 0,
                                             (size_t)owidth * oheight * 2 * 3 * sizeof(float), CL_TRUE);
      if(err != CL_SUCCESS) goto error;

      dt_opencl_set_kernel_arg(devid, ldkernel, 0, sizeof(cl_mem), (void *)&dev_in);
      dt_opencl_set_kernel_arg(devid, ldkernel, 1, sizeof(cl_mem), (void *)&dev_tmp);
      dt_opencl_set_kernel_arg(devid, ldkernel, 2, sizeof(int), (void *)&owidth);
      dt_opencl_set_kernel_arg(devid, ldkernel, 3, sizeof(int), (void *)&oheight);
      dt_opencl_set_kernel_arg(devid, ldkernel, 4, sizeof(int), (void *)&iwidth);
      dt_opencl_set_kernel_arg(devid, ldkernel, 5, sizeof(int), (void *)&iheight);
      dt_opencl_set_kernel_arg(devid, ldkernel, 6, sizeof(int), (void *)&roi_in_x);
      dt_opencl_set_kernel_arg(devid, ldkernel, 7, sizeof(int), (void *)&roi_in_y);
      dt_opencl_set_kernel_arg(devid, ldkernel, 8, sizeof(cl_mem), (void *)&dev_tmpbuf);
      dt_opencl_set_kernel_arg(devid, ldkernel, 9, sizeof(int), (void *)&(d->do_nan_checks));
      err = dt_opencl_enqueue_kernel_2d(devid, ldkernel, osizes);
      if(err != CL_SUCCESS) goto error;
    }
    else
    {
      err = dt_opencl_enqueue_copy_image(devid, dev_in, dev_tmp, origin, origin, oregion);
      if(err != CL_SUCCESS) goto error;
    }

    if(modflags & LF_MODIFY_VIGNETTING)
    {
#ifdef _OPENMP
#pragma omp parallel for default(none) \
      dt_omp_firstprivate(ch, pixelformat, roi_out) \
      shared(tmpbuf, modifier, d) \
      schedule(static)
#endif
      for(int y = 0; y < roi_out->height; y++)
      {
        /* Colour correction: vignetting */
        // actually this way row stride does not matter.
        float *buf = tmpbuf + (size_t)y * ch * roi_out->width;
        for(int k = 0; k < ch * roi_out->width; k++) buf[k] = 0.5f;
        modifier->ApplyColorModification(buf, roi_out->x, roi_out->y + y, roi_out->width, 1,
                                         pixelformat, ch * roi_out->width);
      }

      /* _blocking_ memory transfer: host tmpbuf buffer -> opencl dev_tmpbuf */
      err = dt_opencl_write_buffer_to_device(devid, tmpbuf, dev_tmpbuf, 0,
                                             (size_t)ch * roi_out->width * roi_out->height * sizeof(float),
                                             CL_TRUE);
      if(err != CL_SUCCESS) goto error;

      dt_opencl_set_kernel_arg(devid, gd->kernel_lens_vignette, 0, sizeof(cl_mem), (void *)&dev_tmp);
      dt_opencl_set_kernel_arg(devid, gd->kernel_lens_vignette, 1, sizeof(cl_mem), (void *)&dev_out);
      dt_opencl_set_kernel_arg(devid, gd->kernel_lens_vignette, 2, sizeof(int), (void *)&owidth);
      dt_opencl_set_kernel_arg(devid, gd->kernel_lens_vignette, 3, sizeof(int), (void *)&oheight);
      dt_opencl_set_kernel_arg(devid, gd->kernel_lens_vignette, 4, sizeof(cl_mem), (void *)&dev_tmpbuf);
      err = dt_opencl_enqueue_kernel_2d(devid, gd->kernel_lens_vignette, osizes);
      if(err != CL_SUCCESS) goto error;
    }
    else
    {
      err = dt_opencl_enqueue_copy_image(devid, dev_tmp, dev_out, origin, origin, oregion);
      if(err != CL_SUCCESS) goto error;
    }
  }

  else // correct distortions:
  {

    if(modflags & LF_MODIFY_VIGNETTING)
    {
#ifdef _OPENMP
#pragma omp parallel for default(none) \
      dt_omp_firstprivate(ch, pixelformat, roi_in) \
      shared(tmpbuf, modifier, d) \
      schedule(static)
#endif
      for(int y = 0; y < roi_in->height; y++)
      {
        /* Colour correction: vignetting */
        // actually this way row stride does not matter.
        float *buf = tmpbuf + (size_t)y * ch * roi_in->width;
        for(int k = 0; k < ch * roi_in->width; k++) buf[k] = 0.5f;
        modifier->ApplyColorModification(buf, roi_in->x, roi_in->y + y, roi_in->width, 1,
                                         pixelformat, ch * roi_in->width);
      }

      /* _blocking_ memory transfer: host tmpbuf buffer -> opencl dev_tmpbuf */
      err = dt_opencl_write_buffer_to_device(
          devid, tmpbuf, dev_tmpbuf, 0, (size_t)ch * roi_in->width * roi_in->height * sizeof(float), CL_TRUE);
      if(err != CL_SUCCESS) goto error;

      dt_opencl_set_kernel_arg(devid, gd->kernel_lens_vignette, 0, sizeof(cl_mem), (void *)&dev_in);
      dt_opencl_set_kernel_arg(devid, gd->kernel_lens_vignette, 1, sizeof(cl_mem), (void *)&dev_tmp);
      dt_opencl_set_kernel_arg(devid, gd->kernel_lens_vignette, 2, sizeof(int), (void *)&iwidth);
      dt_opencl_set_kernel_arg(devid, gd->kernel_lens_vignette, 3, sizeof(int), (void *)&iheight);
      dt_opencl_set_kernel_arg(devid, gd->kernel_lens_vignette, 4, sizeof(cl_mem), (void *)&dev_tmpbuf);
      err = dt_opencl_enqueue_kernel_2d(devid, gd->kernel_lens_vignette, isizes);
      if(err != CL_SUCCESS) goto error;
    }
    else
    {
      err = dt_opencl_enqueue_copy_image(devid, dev_in, dev_tmp, origin, origin, iregion);
      if(err != CL_SUCCESS) goto error;
    }

    if(modflags & (LF_MODIFY_TCA | LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY | LF_MODIFY_SCALE))
    {
#ifdef _OPENMP
#pragma omp parallel for default(none) \
      dt_omp_firstprivate(tmpbufwidth, roi_out) \
      shared(tmpbuf, d, modifier) \
      schedule(static)
#endif
      for(int y = 0; y < roi_out->height; y++)
      {
        float *pi = tmpbuf + (size_t)y * tmpbufwidth;
        modifier->ApplySubpixelGeometryDistortion(roi_out->x, roi_out->y + y, roi_out->width, 1, pi);
      }

      /* _blocking_ memory transfer: host tmpbuf buffer -> opencl dev_tmpbuf */
      err = dt_opencl_write_buffer_to_device(devid, tmpbuf, dev_tmpbuf, 0,
                                             (size_t)owidth * oheight * 2 * 3 * sizeof(float), CL_TRUE);
      if(err != CL_SUCCESS) goto error;

      dt_opencl_set_kernel_arg(devid, ldkernel, 0, sizeof(cl_mem), (void *)&dev_tmp);
      dt_opencl_set_kernel_arg(devid, ldkernel, 1, sizeof(cl_mem), (void *)&dev_out);
      dt_opencl_set_kernel_arg(devid, ldkernel, 2, sizeof(int), (void *)&owidth);
      dt_opencl_set_kernel_arg(devid, ldkernel, 3, sizeof(int), (void *)&oheight);
      dt_opencl_set_kernel_arg(devid, ldkernel, 4, sizeof(int), (void *)&iwidth);
      dt_opencl_set_kernel_arg(devid, ldkernel, 5, sizeof(int), (void *)&iheight);
      dt_opencl_set_kernel_arg(devid, ldkernel, 6, sizeof(int), (void *)&roi_in_x);
      dt_opencl_set_kernel_arg(devid, ldkernel, 7, sizeof(int), (void *)&roi_in_y);
      dt_opencl_set_kernel_arg(devid, ldkernel, 8, sizeof(cl_mem), (void *)&dev_tmpbuf);
      dt_opencl_set_kernel_arg(devid, ldkernel, 9, sizeof(int), (void *)&(d->do_nan_checks));
      err = dt_opencl_enqueue_kernel_2d(devid, ldkernel, osizes);
      if(err != CL_SUCCESS) goto error;
    }
    else
    {
      err = dt_opencl_enqueue_copy_image(devid, dev_tmp, dev_out, origin, origin, oregion);
      if(err != CL_SUCCESS) goto error;
    }
  }

  if(self->dev->gui_attached && g && piece->pipe->type == DT_DEV_PIXELPIPE_PREVIEW)
  {
    dt_pthread_mutex_lock(&g->lock);
    g->corrections_done = (modflags & LENSFUN_MODFLAG_MASK);
    dt_pthread_mutex_unlock(&g->lock);
  }

  dt_opencl_release_mem_object(dev_tmpbuf);
  dt_opencl_release_mem_object(dev_tmp);
  if(tmpbuf != NULL) dt_free_align(tmpbuf);
  if(modifier != NULL) delete modifier;
  return TRUE;

error:
  dt_opencl_release_mem_object(dev_tmp);
  dt_opencl_release_mem_object(dev_tmpbuf);
  if(tmpbuf != NULL) dt_free_align(tmpbuf);
  if(modifier != NULL) delete modifier;
  dt_print(DT_DEBUG_OPENCL, "[opencl_lens] couldn't enqueue kernel! %d\n", err);
  return FALSE;
}
#endif

void tiling_callback(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece,
                     const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out,
                     struct dt_develop_tiling_t *tiling)
{
  tiling->factor = 4.5f; // in + out + tmp + tmpbuf
  tiling->maxbuf = 1.5f;
  tiling->overhead = 0;
  tiling->overlap = 4;
  tiling->xalign = 1;
  tiling->yalign = 1;
  return;
}

// lensfun does not provide a back-transform routine. So we do it iteratively by assuming that
// a back-transform at one point is just moving the same distance in the opposite direction. This
// is of course not fully correct so we do adjust iteratively the transformation by checking that
// the back transformed points are when transformed very close to the original point.
//
// Again, not perfect but better than having back-transform be equivalent to the transform routine above.
int distort_transform(dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, float *points, size_t points_count)
{
  dt_iop_lensfun_data_t *d = (dt_iop_lensfun_data_t *)piece->data;
  if(!d->lens || !d->lens->Maker || d->crop <= 0.0f) return 0;

  const float orig_w = piece->buf_in.width, orig_h = piece->buf_in.height;
  int modflags;
  const lfModifier *modifier = get_modifier(&modflags, orig_w, orig_h, d, LF_MODIFY_ALL);

  if(modflags & (LF_MODIFY_TCA | LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY | LF_MODIFY_SCALE))
  {
    float *buf = (float *)malloc(2 * 3 * sizeof(float));
    for(size_t i = 0; i < points_count * 2; i += 2)
    {
      float p1 = points[i];
      float p2 = points[i + 1];
      // just loop 10 times max to find the best position. checking that the convergence is
      // often after 2 or 3 loops.
      for(int k=0; k<10; k++)
      {
        modifier->ApplySubpixelGeometryDistortion(p1, p2, 1, 1, buf);
        const float dist1 = points[i]     - buf[0];
        const float dist2 = points[i + 1] - buf[3];
        if(fabs(dist1) < .5f && fabs(dist2) < .5f) break; // we have converged
        p1 += dist1;
        p2 += dist2;
      }

      points[i]     = p1;
      points[i + 1] = p2;
    }
    free(buf);
  }

  delete modifier;
  return 1;
}

int distort_backtransform(dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, float *points,
                          size_t points_count)
{
  dt_iop_lensfun_data_t *d = (dt_iop_lensfun_data_t *)piece->data;

  if(!d->lens || !d->lens->Maker || d->crop <= 0.0f) return 0;

  const float orig_w = piece->buf_in.width, orig_h = piece->buf_in.height;
  int modflags;
  const lfModifier *modifier = get_modifier(&modflags, orig_w, orig_h, d, LF_MODIFY_ALL);

  if(modflags & (LF_MODIFY_TCA | LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY | LF_MODIFY_SCALE))
  {
    float *buf = (float *)malloc(2 * 3 * sizeof(float));
    for(size_t i = 0; i < points_count * 2; i += 2)
    {
      modifier->ApplySubpixelGeometryDistortion(points[i], points[i + 1], 1, 1, buf);
      points[i] = buf[0];
      points[i + 1] = buf[3];
    }
    free(buf);
  }

  delete modifier;
  return 1;
}

// TODO: Shall we keep LF_MODIFY_TCA in the modifiers?
void distort_mask(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, const float *const in,
                  float *const out, const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  const dt_iop_lensfun_data_t *const d = (dt_iop_lensfun_data_t *)piece->data;

  if(!d->lens || !d->lens->Maker || d->crop <= 0.0f)
  {
    memcpy(out, in, sizeof(float) * roi_out->width * roi_out->height);
    return;
  }

  const float orig_w = roi_in->scale * piece->buf_in.width, orig_h = roi_in->scale * piece->buf_in.height;
  dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
  int modflags;
  lfModifier *modifier = get_modifier(&modflags, orig_w, orig_h, d, /*LF_MODIFY_TCA |*/ LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY | LF_MODIFY_SCALE);

  dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);

  if(!(modflags & (LF_MODIFY_TCA | LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY | LF_MODIFY_SCALE)))
  {
    memcpy(out, in, sizeof(float) * roi_out->width * roi_out->height);
    delete modifier;
    return;
  }

  const struct dt_interpolation *const interpolation = dt_interpolation_new(DT_INTERPOLATION_USERPREF);

  // acquire temp memory for distorted pixel coords
  const size_t bufsize = (size_t)roi_out->width * 2 * 3;
  float *buf = (float *)dt_alloc_align(64, bufsize * sizeof(float) * dt_get_num_threads());

#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(bufsize, d, in, interpolation, out, roi_in, roi_out) \
  shared(buf, modifier) \
  schedule(static)
#endif
  for(int y = 0; y < roi_out->height; y++)
  {
    float *bufptr = buf + bufsize * dt_get_thread_num();
    modifier->ApplySubpixelGeometryDistortion(roi_out->x, roi_out->y + y, roi_out->width, 1, bufptr);

    // reverse transform the global coords from lf to our buffer
    float *_out = out + (size_t)y * roi_out->width;
    for(int x = 0; x < roi_out->width; x++, bufptr += 6, _out++)
    {
      if(d->do_nan_checks && (!isfinite(bufptr[2]) || !isfinite(bufptr[3])))
      {
        *_out = 0.0f;
        continue;
      }

      // take green channel distortion also for alpha channel
      const float pi0 = bufptr[2] - roi_in->x;
      const float pi1 = bufptr[3] - roi_in->y;
      *_out = dt_interpolation_compute_sample(interpolation, in, pi0, pi1, roi_in->width, roi_in->height, 1,
                                              roi_in->width);
    }
  }
  dt_free_align(buf);
  delete modifier;
}

void modify_roi_out(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, dt_iop_roi_t *roi_out,
                    const dt_iop_roi_t *roi_in)
{
  *roi_out = *roi_in;
}

void modify_roi_in(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece,
                   const dt_iop_roi_t *const roi_out, dt_iop_roi_t *roi_in)
{
  dt_iop_lensfun_data_t *d = (dt_iop_lensfun_data_t *)piece->data;
  *roi_in = *roi_out;
  // inverse transform with given params

  if(!d->lens || !d->lens->Maker || d->crop <= 0.0f) return;

  const float orig_w = roi_in->scale * piece->buf_in.width, orig_h = roi_in->scale * piece->buf_in.height;
  int modflags;
  lfModifier *modifier = get_modifier(&modflags, orig_w, orig_h, d, LF_MODIFY_ALL);

  if(modflags & (LF_MODIFY_TCA | LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY | LF_MODIFY_SCALE))
  {
    const int xoff = roi_in->x;
    const int yoff = roi_in->y;
    const int width = roi_in->width;
    const int height = roi_in->height;
    const int awidth = abs(width);
    const int aheight = abs(height);
    const int xstep = (width < 0) ? -1 : 1;
    const int ystep = (height < 0) ? -1 : 1;

    float xm = FLT_MAX, xM = -FLT_MAX, ym = FLT_MAX, yM = -FLT_MAX;
    const size_t nbpoints = 2 * awidth + 2 * aheight;

    float *const buf = (float *)dt_alloc_align(64, nbpoints * 2 * 3 * sizeof(float));

#ifdef _OPENMP
#pragma omp parallel default(none) \
    dt_omp_firstprivate(aheight, awidth, buf, height, nbpoints, width, xoff, \
                        xstep, yoff, ystep) \
    shared(modifier) reduction(min : xm, ym) reduction(max : xM, yM)
#endif
    {
#ifdef _OPENMP
#pragma omp for schedule(static)
#endif
      for(int i = 0; i < awidth; i++)
        modifier->ApplySubpixelGeometryDistortion(xoff + i * xstep, yoff, 1, 1, buf + 6 * i);

#ifdef _OPENMP
#pragma omp for schedule(static)
#endif
      for(int i = 0; i < awidth; i++)
        modifier->ApplySubpixelGeometryDistortion(xoff + i * xstep, yoff + (height - 1), 1, 1, buf + 6 * (awidth + i));

#ifdef _OPENMP
#pragma omp for schedule(static)
#endif
      for(int j = 0; j < aheight; j++)
        modifier->ApplySubpixelGeometryDistortion(xoff, yoff + j * ystep, 1, 1, buf + 6 * (2 * awidth + j));

#ifdef _OPENMP
#pragma omp for schedule(static)
#endif
      for(int j = 0; j < aheight; j++)
        modifier->ApplySubpixelGeometryDistortion(xoff + (width - 1), yoff + j * ystep, 1, 1, buf + 6 * (2 * awidth + aheight + j));

#ifdef _OPENMP
#pragma omp barrier
#endif

#ifdef _OPENMP
#pragma omp for schedule(static)
#endif
      for(size_t k = 0; k < nbpoints; k++)
      {
        const float x = buf[6 * k + 0];
        const float y = buf[6 * k + 3];
        xm = isnan(x) ? xm : MIN(xm, x);
        xM = isnan(x) ? xM : MAX(xM, x);
        ym = isnan(y) ? ym : MIN(ym, y);
        yM = isnan(y) ? yM : MAX(yM, y);
      }
    }

    dt_free_align(buf);

    // LensFun can return NAN coords, so we need to handle them carefully.
    if(!isfinite(xm) || !(0 <= xm && xm < orig_w)) xm = 0;
    if(!isfinite(xM) || !(1 <= xM && xM < orig_w)) xM = orig_w;
    if(!isfinite(ym) || !(0 <= ym && ym < orig_h)) ym = 0;
    if(!isfinite(yM) || !(1 <= yM && yM < orig_h)) yM = orig_h;

    const struct dt_interpolation *interpolation = dt_interpolation_new(DT_INTERPOLATION_USERPREF);
    roi_in->x = fmaxf(0.0f, xm - interpolation->width);
    roi_in->y = fmaxf(0.0f, ym - interpolation->width);
    roi_in->width = fminf(orig_w - roi_in->x, xM - roi_in->x + interpolation->width);
    roi_in->height = fminf(orig_h - roi_in->y, yM - roi_in->y + interpolation->width);

    // sanity check.
    roi_in->x = CLAMP(roi_in->x, 0, (int)floorf(orig_w));
    roi_in->y = CLAMP(roi_in->y, 0, (int)floorf(orig_h));
    roi_in->width = CLAMP(roi_in->width, 1, (int)ceilf(orig_w) - roi_in->x);
    roi_in->height = CLAMP(roi_in->height, 1, (int)ceilf(orig_h) - roi_in->y);
  }
  delete modifier;
}

void commit_params(struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe,
                   dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)p1;

  if(p->modified == 0)
  {
    /*
     * user did not modify anything in gui after autodetection - let's
     * use current default_params as params - for presets and mass-export
     */
    p = (dt_iop_lensfun_params_t *)self->default_params;
  }

  dt_iop_lensfun_data_t *d = (dt_iop_lensfun_data_t *)piece->data;

  dt_iop_lensfun_global_data_t *gd = (dt_iop_lensfun_global_data_t *)self->global_data;
  lfDatabase *dt_iop_lensfun_db = (lfDatabase *)gd->db;
  const lfCamera *camera = NULL;
  const lfCamera **cam = NULL;

  if(d->lens)
  {
    delete d->lens;
    d->lens = NULL;
  }
  d->lens = new lfLens;

  if(p->camera[0])
  {
    dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
    cam = dt_iop_lensfun_db->FindCamerasExt(NULL, p->camera, 0);
    if(cam)
    {
      camera = cam[0];
      d->crop = cam[0]->CropFactor;
    }
    dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
  }
  if(p->lens[0])
  {
    dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
    const lfLens **lens
        = dt_iop_lensfun_db->FindLenses(camera, NULL, p->lens, 0);
    dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
    if(lens)
    {
      *d->lens = *lens[0];
      if(p->tca_override)
      {
#ifdef LF_0395
        const dt_image_t *img = &(self->dev->image_storage);

        d->custom_tca =
          {
           .Model     = LF_TCA_MODEL_LINEAR,
           .Focal     = p->focal,
           .Terms     = { p->tca_r, p->tca_b },
           .CalibAttr = {
                         .CenterX = 0.0f,
                         .CenterY = 0.0f,
                         .CropFactor = d->crop,
                         .AspectRatio = (float)img->width / (float)img->height
                         }
          };
#else
        // add manual d->lens stuff:
        lfLensCalibTCA tca = { LF_TCA_MODEL_NONE };
        tca.Focal = 0;
        tca.Model = LF_TCA_MODEL_LINEAR;
        tca.Terms[0] = p->tca_r;
        tca.Terms[1] = p->tca_b;
        if(d->lens->CalibTCA)
          while(d->lens->CalibTCA[0]) d->lens->RemoveCalibTCA(0);
        d->lens->AddCalibTCA(&tca);
#endif
      }
      lf_free(lens);
    }
  }
  lf_free(cam);
  d->modify_flags = p->modify_flags;
  d->inverse = p->inverse;
  d->scale = p->scale;
  d->focal = p->focal;
  d->aperture = p->aperture;
  d->distance = p->distance;
  d->target_geom = p->target_geom;
  d->do_nan_checks = TRUE;
  d->tca_override = p->tca_override;

  /*
   * there are certain situations when LensFun can return NAN coordinated.
   * most common case would be when the FOV is increased.
   */
  if(d->target_geom == LF_RECTILINEAR)
  {
    d->do_nan_checks = FALSE;
  }
  else if(d->target_geom == d->lens->Type)
  {
    d->do_nan_checks = FALSE;
  }
}

void init_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = calloc(1, sizeof(dt_iop_lensfun_data_t));
  self->commit_params(self, self->default_params, pipe, piece);
}

void cleanup_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_lensfun_data_t *d = (dt_iop_lensfun_data_t *)piece->data;

  if(d->lens)
  {
    delete d->lens;
    d->lens = NULL;
  }
  free(piece->data);
  piece->data = NULL;
}

void init_global(dt_iop_module_so_t *module)
{
  const int program = 2; // basic.cl, from programs.conf
  dt_iop_lensfun_global_data_t *gd
      = (dt_iop_lensfun_global_data_t *)calloc(1, sizeof(dt_iop_lensfun_global_data_t));
  module->data = gd;
  gd->kernel_lens_distort_bilinear = dt_opencl_create_kernel(program, "lens_distort_bilinear");
  gd->kernel_lens_distort_bicubic = dt_opencl_create_kernel(program, "lens_distort_bicubic");
  gd->kernel_lens_distort_lanczos2 = dt_opencl_create_kernel(program, "lens_distort_lanczos2");
  gd->kernel_lens_distort_lanczos3 = dt_opencl_create_kernel(program, "lens_distort_lanczos3");
  gd->kernel_lens_vignette = dt_opencl_create_kernel(program, "lens_vignette");

  lfDatabase *dt_iop_lensfun_db = new lfDatabase;
  gd->db = (lfDatabase *)dt_iop_lensfun_db;

#if defined(__MACH__) || defined(__APPLE__)
#else
  if(dt_iop_lensfun_db->Load() != LF_NO_ERROR)
#endif
  {
    char datadir[PATH_MAX] = { 0 };
    dt_loc_get_datadir(datadir, sizeof(datadir));

    // get parent directory
    GFile *file = g_file_parse_name(datadir);
    gchar *path = g_file_get_path(g_file_get_parent(file));
    g_object_unref(file);
#ifdef LF_MAX_DATABASE_VERSION
    gchar *sysdbpath = g_build_filename(path, "lensfun", "version_" STR(LF_MAX_DATABASE_VERSION), NULL);
#endif

#ifdef LF_0395
    const long userdbts = dt_iop_lensfun_db->ReadTimestamp(dt_iop_lensfun_db->UserUpdatesLocation);
    const long sysdbts = dt_iop_lensfun_db->ReadTimestamp(sysdbpath);
    const char *dbpath = userdbts > sysdbts ? dt_iop_lensfun_db->UserUpdatesLocation : sysdbpath;
    if(dt_iop_lensfun_db->Load(dbpath) != LF_NO_ERROR)
      fprintf(stderr, "[iop_lens]: could not load lensfun database in `%s'!\n", dbpath);
    else
      dt_iop_lensfun_db->Load(dt_iop_lensfun_db->UserLocation);
#else
    // code for older lensfun preserved as-is
#ifdef LF_MAX_DATABASE_VERSION
    g_free(dt_iop_lensfun_db->HomeDataDir);
    dt_iop_lensfun_db->HomeDataDir = g_strdup(sysdbpath);
    if(dt_iop_lensfun_db->Load() != LF_NO_ERROR)
    {
      fprintf(stderr, "[iop_lens]: could not load lensfun database in `%s'!\n", sysdbpath);
#endif
      g_free(dt_iop_lensfun_db->HomeDataDir);
      dt_iop_lensfun_db->HomeDataDir = g_build_filename(path, "lensfun", NULL);
      if(dt_iop_lensfun_db->Load() != LF_NO_ERROR)
        fprintf(stderr, "[iop_lens]: could not load lensfun database in `%s'!\n", dt_iop_lensfun_db->HomeDataDir);
#ifdef LF_MAX_DATABASE_VERSION
    }
#endif
#endif

#ifdef LF_MAX_DATABASE_VERSION
    g_free(sysdbpath);
#endif
    g_free(path);
  }
}

static float get_autoscale(dt_iop_module_t *self, dt_iop_lensfun_params_t *p, const lfCamera *camera);

void reload_defaults(dt_iop_module_t *module)
{
  char *new_lens;
  const dt_image_t *img = &module->dev->image_storage;

  // reload image specific stuff
  // get all we can from exif:
  dt_iop_lensfun_params_t tmp = { 0 };

  // we might be called from presets update infrastructure => there is no image
  if(!module->dev) goto end;

  new_lens = _lens_sanitize(img->exif_lens);
  g_strlcpy(tmp.lens, new_lens, sizeof(tmp.lens));
  free(new_lens);
  g_strlcpy(tmp.camera, img->exif_model, sizeof(tmp.camera));
  tmp.crop = img->exif_crop;
  tmp.aperture = img->exif_aperture;
  tmp.focal = img->exif_focal_length;
  tmp.scale = 1.0;
  tmp.inverse = 0;
  tmp.modify_flags = LF_MODIFY_TCA | LF_MODIFY_VIGNETTING | LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY
                     | LF_MODIFY_SCALE;
  // if we did not find focus_distance in EXIF, lets default to 1000
  tmp.distance = img->exif_focus_distance == 0.0f ? 1000.0f : img->exif_focus_distance;
  tmp.target_geom = LF_RECTILINEAR;
  tmp.tca_override = 0;
  tmp.tca_r = 1.0;
  tmp.tca_b = 1.0;
  tmp.modified = 0;

  if(dt_image_is_monochrome(img)) tmp.modify_flags &= ~LF_MODIFY_TCA;

  // init crop from db:
  char model[100]; // truncate often complex descriptions.
  g_strlcpy(model, img->exif_model, sizeof(model));
  for(char cnt = 0, *c = model; c < model + 100 && *c != '\0'; c++)
    if(*c == ' ')
      if(++cnt == 2) *c = '\0';
  if(img->exif_maker[0] || model[0])
  {
    dt_iop_lensfun_global_data_t *gd = (dt_iop_lensfun_global_data_t *)module->global_data;

    // just to be sure
    if(!gd || !gd->db) goto end;

    dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
    const lfCamera **cam = gd->db->FindCamerasExt(img->exif_maker, img->exif_model, 0);
    dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
    if(cam)
    {
      dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
      const lfLens **lens = gd->db->FindLenses(cam[0], NULL, tmp.lens, 0);
      dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);

      if(!lens && islower(cam[0]->Mount[0]))
      {
        /*
         * This is a fixed-lens camera, and LF returned no lens.
         * (reasons: lens is "(65535)" or lens is correct lens name,
         *  but LF have it as "fixed lens")
         *
         * Let's unset lens name and re-run lens query
         */
        g_strlcpy(tmp.lens, "", sizeof(tmp.lens));

        dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
        lens = gd->db->FindLenses(cam[0], NULL, tmp.lens, 0);
        dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
      }

      if(lens)
      {
        int lens_i = 0;

        /*
         * Current SVN lensfun lets you test for a fixed-lens camera by looking
         * at the zeroth character in the mount's name:
         * If it is a lower case letter, it is a fixed-lens camera.
         */
        if(!tmp.lens[0] && islower(cam[0]->Mount[0]))
        {
          /*
           * no lens info in EXIF, and this is fixed-lens camera,
           * let's find shortest lens model in the list of possible lenses
           */
          size_t min_model_len = SIZE_MAX;
          for(int i = 0; lens[i]; i++)
          {
            if(strlen(lens[i]->Model) < min_model_len)
            {
              min_model_len = strlen(lens[i]->Model);
              lens_i = i;
            }
          }

          // and set lens to it
          g_strlcpy(tmp.lens, lens[lens_i]->Model, sizeof(tmp.lens));
        }

        tmp.target_geom = lens[lens_i]->Type;
        lf_free(lens);
      }

      tmp.crop = cam[0]->CropFactor;
      tmp.scale = get_autoscale(module, &tmp, cam[0]);

      lf_free(cam);
    }
  }

  // if we have a gui -> reset corrections_done message
  if(module->gui_data)
  {
    dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)module->gui_data;
    dt_pthread_mutex_lock(&g->lock);
    g->corrections_done = -1;
    dt_pthread_mutex_unlock(&g->lock);
    gtk_label_set_text(g->message, "");
  }

end:
  memcpy(module->params, &tmp, sizeof(dt_iop_lensfun_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_lensfun_params_t));
  module->default_enabled = 0;
}

void init(dt_iop_module_t *module)
{
  module->params = calloc(1, sizeof(dt_iop_lensfun_params_t));
  module->default_params = calloc(1, sizeof(dt_iop_lensfun_params_t));
  module->default_enabled = 0;
  module->params_size = sizeof(dt_iop_lensfun_params_t);
  module->gui_data = NULL;
}

void cleanup(dt_iop_module_t *module)
{
  free(module->params);
  module->params = NULL;
}

void cleanup_global(dt_iop_module_so_t *module)
{
  dt_iop_lensfun_global_data_t *gd = (dt_iop_lensfun_global_data_t *)module->data;
  lfDatabase *dt_iop_lensfun_db = (lfDatabase *)gd->db;
  delete dt_iop_lensfun_db;

  dt_opencl_free_kernel(gd->kernel_lens_distort_bilinear);
  dt_opencl_free_kernel(gd->kernel_lens_distort_bicubic);
  dt_opencl_free_kernel(gd->kernel_lens_distort_lanczos2);
  dt_opencl_free_kernel(gd->kernel_lens_distort_lanczos3);
  dt_opencl_free_kernel(gd->kernel_lens_vignette);
  free(module->data);
  module->data = NULL;
}

/// ############################################################
/// gui stuff: inspired by ufraws lensfun tab:

/* simple function to compute the floating-point precision
   which is enough for "normal use". The criteria is to have
   about 3 leading digits after the initial zeros.  */
static int precision(double x, double adj)
{
  x *= adj;

  if(x == 0) return 1;
  if(x < 1.0)
    if(x < 0.1)
      if(x < 0.01)
        return 5;
      else
        return 4;
    else
      return 3;
  else if(x < 100.0)
    if(x < 10.0)
      return 2;
    else
      return 1;
  else
    return 0;
}

/* -- ufraw ptr array functions -- */

static int ptr_array_insert_sorted(GPtrArray *array, const void *item, GCompareFunc compare)
{
  int length = array->len;
  g_ptr_array_set_size(array, length + 1);
  const void **root = (const void **)array->pdata;

  int m = 0, l = 0, r = length - 1;

  // Skip trailing NULL, if any
  if(l <= r && !root[r]) r--;

  while(l <= r)
  {
    m = (l + r) / 2;
    int cmp = compare(root[m], item);

    if(cmp == 0)
    {
      ++m;
      goto done;
    }
    else if(cmp < 0)
      l = m + 1;
    else
      r = m - 1;
  }
  if(r == m) m++;

done:
  memmove(root + m + 1, root + m, (length - m) * sizeof(void *));
  root[m] = item;
  return m;
}

static int ptr_array_find_sorted(const GPtrArray *array, const void *item, GCompareFunc compare)
{
  int length = array->len;
  void **root = array->pdata;

  int l = 0, r = length - 1;
  int m = 0, cmp = 0;

  if(!length) return -1;

  // Skip trailing NULL, if any
  if(!root[r]) r--;

  while(l <= r)
  {
    m = (l + r) / 2;
    cmp = compare(root[m], item);

    if(cmp == 0)
      return m;
    else if(cmp < 0)
      l = m + 1;
    else
      r = m - 1;
  }

  return -1;
}

static void ptr_array_insert_index(GPtrArray *array, const void *item, int index)
{
  const void **root;
  int length = array->len;
  g_ptr_array_set_size(array, length + 1);
  root = (const void **)array->pdata;
  memmove(root + index + 1, root + index, (length - index) * sizeof(void *));
  root[index] = item;
}

/* -- end ufraw ptr array functions -- */

/* -- camera -- */

static void camera_set(dt_iop_module_t *self, const lfCamera *cam)
{
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;
  gchar *fm;
  const char *maker, *model, *variant;
  char _variant[100];

  if(!cam)
  {
    gtk_button_set_label(GTK_BUTTON(g->camera_model), "");
    gtk_label_set_ellipsize(GTK_LABEL(gtk_bin_get_child(GTK_BIN(g->camera_model))), PANGO_ELLIPSIZE_END);
    gtk_widget_set_tooltip_text(GTK_WIDGET(g->camera_model), "");
    return;
  }

  g_strlcpy(p->camera, cam->Model, sizeof(p->camera));
  p->crop = cam->CropFactor;
  g->camera = cam;

  maker = lf_mlstr_get(cam->Maker);
  model = lf_mlstr_get(cam->Model);
  variant = lf_mlstr_get(cam->Variant);

  if(model)
  {
    if(maker)
      fm = g_strdup_printf("%s, %s", maker, model);
    else
      fm = g_strdup_printf("%s", model);
    gtk_button_set_label(GTK_BUTTON(g->camera_model), fm);
    gtk_label_set_ellipsize(GTK_LABEL(gtk_bin_get_child(GTK_BIN(g->camera_model))), PANGO_ELLIPSIZE_END);
    g_free(fm);
  }

  if(variant)
    snprintf(_variant, sizeof(_variant), " (%s)", variant);
  else
    _variant[0] = 0;

  fm = g_strdup_printf(_("maker:\t\t%s\n"
                         "model:\t\t%s%s\n"
                         "mount:\t\t%s\n"
                         "crop factor:\t%.1f"),
                       maker, model, _variant, cam->Mount, cam->CropFactor);
  gtk_widget_set_tooltip_text(GTK_WIDGET(g->camera_model), fm);
  g_free(fm);
}

static void camera_menu_select(GtkMenuItem *menuitem, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  camera_set(self, (lfCamera *)g_object_get_data(G_OBJECT(menuitem), "lfCamera"));
  if(darktable.gui->reset) return;
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;
  p->modified = 1;
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void camera_menu_fill(dt_iop_module_t *self, const lfCamera *const *camlist)
{
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  unsigned i;
  GPtrArray *makers, *submenus;

  if(g->camera_menu)
  {
    gtk_widget_destroy(GTK_WIDGET(g->camera_menu));
    g->camera_menu = NULL;
  }

  /* Count all existing camera makers and create a sorted list */
  makers = g_ptr_array_new();
  submenus = g_ptr_array_new();
  for(i = 0; camlist[i]; i++)
  {
    GtkWidget *submenu, *item;
    const char *m = lf_mlstr_get(camlist[i]->Maker);
    int idx = ptr_array_find_sorted(makers, m, (GCompareFunc)g_utf8_collate);
    if(idx < 0)
    {
      /* No such maker yet, insert it into the array */
      idx = ptr_array_insert_sorted(makers, m, (GCompareFunc)g_utf8_collate);
      /* Create a submenu for cameras by this maker */
      submenu = gtk_menu_new();
      ptr_array_insert_index(submenus, submenu, idx);
    }

    submenu = (GtkWidget *)g_ptr_array_index(submenus, idx);
    /* Append current camera name to the submenu */
    m = lf_mlstr_get(camlist[i]->Model);
    if(!camlist[i]->Variant)
      item = gtk_menu_item_new_with_label(m);
    else
    {
      gchar *fm = g_strdup_printf("%s (%s)", m, camlist[i]->Variant);
      item = gtk_menu_item_new_with_label(fm);
      g_free(fm);
    }
    gtk_widget_show(item);
    g_object_set_data(G_OBJECT(item), "lfCamera", (void *)camlist[i]);
    g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(camera_menu_select), self);
    gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
  }

  g->camera_menu = GTK_MENU(gtk_menu_new());
  for(i = 0; i < makers->len; i++)
  {
    GtkWidget *item = (GtkWidget *)gtk_menu_item_new_with_label((const gchar *)g_ptr_array_index(makers, i));
    gtk_widget_show(item);
    gtk_menu_shell_append(GTK_MENU_SHELL(g->camera_menu), item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), (GtkWidget *)g_ptr_array_index(submenus, i));
  }

  g_ptr_array_free(submenus, TRUE);
  g_ptr_array_free(makers, TRUE);
}

static void parse_maker_model(const char *txt, char *make, size_t sz_make, char *model, size_t sz_model)
{
  const gchar *sep;

  while(txt[0] && isspace(txt[0])) txt++;
  sep = strchr(txt, ',');
  if(sep)
  {
    size_t len = sep - txt;
    if(len > sz_make - 1) len = sz_make - 1;
    memcpy(make, txt, len);
    make[len] = 0;

    while(*++sep && isspace(sep[0]))
      ;
    len = strlen(sep);
    if(len > sz_model - 1) len = sz_model - 1;
    memcpy(model, sep, len);
    model[len] = 0;
  }
  else
  {
    size_t len = strlen(txt);
    if(len > sz_model - 1) len = sz_model - 1;
    memcpy(model, txt, len);
    model[len] = 0;
    make[0] = 0;
  }
}

static void camera_menusearch_clicked(GtkWidget *button, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_lensfun_global_data_t *gd = (dt_iop_lensfun_global_data_t *)self->global_data;
  lfDatabase *dt_iop_lensfun_db = (lfDatabase *)gd->db;
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;

  (void)button;

  const lfCamera *const *camlist;
  dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
  camlist = dt_iop_lensfun_db->GetCameras();
  dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
  if(!camlist) return;
  camera_menu_fill(self, camlist);

#if GTK_CHECK_VERSION(3, 22, 0)
  gtk_menu_popup_at_pointer(GTK_MENU(g->camera_menu), NULL);
#else
  gtk_menu_popup(GTK_MENU(g->camera_menu), NULL, NULL, NULL, NULL, 0, gtk_get_current_event_time());
#endif
}

static void camera_autosearch_clicked(GtkWidget *button, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_lensfun_global_data_t *gd = (dt_iop_lensfun_global_data_t *)self->global_data;
  lfDatabase *dt_iop_lensfun_db = (lfDatabase *)gd->db;
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  char make[200], model[200];
  const gchar *txt = (const gchar *)((dt_iop_lensfun_params_t *)self->default_params)->camera;

  (void)button;

  if(txt[0] == '\0')
  {
    const lfCamera *const *camlist;
    dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
    camlist = dt_iop_lensfun_db->GetCameras();
    dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
    if(!camlist) return;
    camera_menu_fill(self, camlist);
  }
  else
  {
    parse_maker_model(txt, make, sizeof(make), model, sizeof(model));
    dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
    const lfCamera **camlist = dt_iop_lensfun_db->FindCamerasExt(make, model, 0);
    dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
    if(!camlist) return;
    camera_menu_fill(self, camlist);
    lf_free(camlist);
  }

#if GTK_CHECK_VERSION(3, 22, 0)
  gtk_menu_popup_at_pointer(GTK_MENU(g->camera_menu), NULL);
#else
  gtk_menu_popup(GTK_MENU(g->camera_menu), NULL, NULL, NULL, NULL, 0, gtk_get_current_event_time());
#endif
}

/* -- end camera -- */

static void lens_comboentry_focal_update(GtkWidget *widget, dt_iop_module_t *self)
{
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;
  const char *text = dt_bauhaus_combobox_get_text(widget);
  if(text) (void)sscanf(text, "%f", &p->focal);
  p->modified = 1;
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void lens_comboentry_aperture_update(GtkWidget *widget, dt_iop_module_t *self)
{
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;
  const char *text = dt_bauhaus_combobox_get_text(widget);
  if(text) (void)sscanf(text, "%f", &p->aperture);
  p->modified = 1;
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void lens_comboentry_distance_update(GtkWidget *widget, dt_iop_module_t *self)
{
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;
  const char *text = dt_bauhaus_combobox_get_text(widget);
  if(text) (void)sscanf(text, "%f", &p->distance);
  p->modified = 1;
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void delete_children(GtkWidget *widget, gpointer data)
{
  (void)data;
  gtk_widget_destroy(widget);
}

static void lens_set(dt_iop_module_t *self, const lfLens *lens)
{
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;

  gchar *fm;
  const char *maker, *model;
  unsigned i;
  gdouble focal_values[]
      = { -INFINITY, 4.5, 8,   10,  12,  14,  15,  16,  17,  18,  20,  24,  28,   30,      31,  35,
          38,        40,  43,  45,  50,  55,  60,  70,  75,  77,  80,  85,  90,   100,     105, 110,
          120,       135, 150, 200, 210, 240, 250, 300, 400, 500, 600, 800, 1000, INFINITY };
  gdouble aperture_values[]
      = { -INFINITY, 0.7, 0.8, 0.9, 1, 1.1, 1.2, 1.4, 1.8, 2,  2.2, 2.5, 2.8, 3.2, 3.4, 4,  4.5, 5.0,
          5.6,       6.3, 7.1, 8,   9, 10,  11,  13,  14,  16, 18,  20,  22,  25,  29,  32, 38,  INFINITY };

  if(!lens)
  {
    gtk_widget_set_sensitive(GTK_WIDGET(g->modflags), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(g->target_geom), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(g->scale), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(g->reverse), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(g->tca_r), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(g->tca_b), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(g->message), FALSE);

    gtk_container_foreach(GTK_CONTAINER(g->detection_warning), delete_children, NULL);

    GtkWidget *label;

    label = gtk_label_new(_("camera/lens not found - please select manually"));
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);

    gtk_widget_set_tooltip_text(label, _("try to locate your camera/lens in the above two menus"));

    gtk_box_pack_start(GTK_BOX(g->detection_warning), label, FALSE, FALSE, 0);

    gtk_widget_hide(g->lens_param_box);
    gtk_widget_show_all(g->detection_warning);
    return;
  }
  else
  {
    gtk_widget_set_sensitive(GTK_WIDGET(g->modflags), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(g->target_geom), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(g->scale), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(g->reverse), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(g->tca_r), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(g->tca_b), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(g->message), TRUE);
  }

  maker = lf_mlstr_get(lens->Maker);
  model = lf_mlstr_get(lens->Model);

  g_strlcpy(p->lens, lens->Model, sizeof(p->lens));

  if(model)
  {
    if(maker)
      fm = g_strdup_printf("%s, %s", maker, model);
    else
      fm = g_strdup_printf("%s", model);
    gtk_button_set_label(GTK_BUTTON(g->lens_model), fm);
    gtk_label_set_ellipsize(GTK_LABEL(gtk_bin_get_child(GTK_BIN(g->lens_model))), PANGO_ELLIPSIZE_END);
    g_free(fm);
  }

  char focal[100], aperture[100], mounts[200];

  if(lens->MinFocal < lens->MaxFocal)
    snprintf(focal, sizeof(focal), "%g-%gmm", lens->MinFocal, lens->MaxFocal);
  else
    snprintf(focal, sizeof(focal), "%gmm", lens->MinFocal);
  if(lens->MinAperture < lens->MaxAperture)
    snprintf(aperture, sizeof(aperture), "%g-%g", lens->MinAperture, lens->MaxAperture);
  else
    snprintf(aperture, sizeof(aperture), "%g", lens->MinAperture);

  mounts[0] = 0;
#ifdef LF_0395
  const char* const* mount_names = lens->GetMountNames();
  i = 0;
  while (mount_names && *mount_names) {
    if(i > 0) g_strlcat(mounts, ", ", sizeof(mounts));
    g_strlcat(mounts, *mount_names, sizeof(mounts));
    i++;
    mount_names++;
  }
#else
  if(lens->Mounts)
    for(i = 0; lens->Mounts[i]; i++)
    {
      if(i > 0) g_strlcat(mounts, ", ", sizeof(mounts));
      g_strlcat(mounts, lens->Mounts[i], sizeof(mounts));
    }
#endif
  fm = g_strdup_printf(_("maker:\t\t%s\n"
                         "model:\t\t%s\n"
                         "focal range:\t%s\n"
                         "aperture:\t%s\n"
                         "crop factor:\t%.1f\n"
                         "type:\t\t%s\n"
                         "mounts:\t%s"),
                       maker ? maker : "?", model ? model : "?", focal, aperture,
#ifdef LF_0395
                       g->camera->CropFactor,
#else
                       lens->CropFactor,
#endif
                       lfLens::GetLensTypeDesc(lens->Type, NULL), mounts);

  gtk_widget_set_tooltip_text(GTK_WIDGET(g->lens_model), fm);
  g_free(fm);

  /* Create the focal/aperture/distance combo boxes */
  gtk_container_foreach(GTK_CONTAINER(g->lens_param_box), delete_children, NULL);

  int ffi = 1, fli = -1;
  for(i = 1; i < sizeof(focal_values) / sizeof(gdouble) - 1; i++)
  {
    if(focal_values[i] < lens->MinFocal) ffi = i + 1;
    if(focal_values[i] > lens->MaxFocal && fli == -1) fli = i;
  }
  if(focal_values[ffi] > lens->MinFocal)
  {
    focal_values[ffi - 1] = lens->MinFocal;
    ffi--;
  }
  if(lens->MaxFocal == 0 || fli < 0) fli = sizeof(focal_values) / sizeof(gdouble) - 2;
  if(focal_values[fli + 1] < lens->MaxFocal)
  {
    focal_values[fli + 1] = lens->MaxFocal;
    ffi++;
  }
  if(fli < ffi) fli = ffi + 1;

  GtkWidget *w;
  char txt[30];

  // focal length
  w = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(w, NULL, _("mm"));
  gtk_widget_set_tooltip_text(w, _("focal length (mm)"));
  snprintf(txt, sizeof(txt), "%.*f", precision(p->focal, 10.0), p->focal);
  dt_bauhaus_combobox_add(w, txt);
  for(int k = 0; k < fli - ffi; k++)
  {
    snprintf(txt, sizeof(txt), "%.*f", precision(focal_values[ffi + k], 10.0), focal_values[ffi + k]);
    dt_bauhaus_combobox_add(w, txt);
  }
  g_signal_connect(G_OBJECT(w), "value-changed", G_CALLBACK(lens_comboentry_focal_update), self);
  gtk_box_pack_start(GTK_BOX(g->lens_param_box), w, TRUE, TRUE, 0);
  dt_bauhaus_combobox_set_editable(w, 1);
  g->cbe[0] = w;

  // f-stop
  ffi = 1, fli = sizeof(aperture_values) / sizeof(gdouble) - 1;
  for(i = 1; i < sizeof(aperture_values) / sizeof(gdouble) - 1; i++)
    if(aperture_values[i] < lens->MinAperture) ffi = i + 1;
  if(aperture_values[ffi] > lens->MinAperture)
  {
    aperture_values[ffi - 1] = lens->MinAperture;
    ffi--;
  }

  w = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(w, NULL, _("f/"));
  gtk_widget_set_tooltip_text(w, _("f-number (aperture)"));
  snprintf(txt, sizeof(txt), "%.*f", precision(p->aperture, 10.0), p->aperture);
  dt_bauhaus_combobox_add(w, txt);
  for(int k = 0; k < fli - ffi; k++)
  {
    snprintf(txt, sizeof(txt), "%.*f", precision(aperture_values[ffi + k], 10.0), aperture_values[ffi + k]);
    dt_bauhaus_combobox_add(w, txt);
  }
  g_signal_connect(G_OBJECT(w), "value-changed", G_CALLBACK(lens_comboentry_aperture_update), self);
  gtk_box_pack_start(GTK_BOX(g->lens_param_box), w, TRUE, TRUE, 0);
  dt_bauhaus_combobox_set_editable(w, 1);
  g->cbe[1] = w;

  w = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(w, NULL, _("d"));
  gtk_widget_set_tooltip_text(w, _("distance to subject"));
  snprintf(txt, sizeof(txt), "%.*f", precision(p->distance, 10.0), p->distance);
  dt_bauhaus_combobox_add(w, txt);
  float val = 0.25f;
  for(int k = 0; k < 25; k++)
  {
    if(val > 1000.0f) val = 1000.0f;
    snprintf(txt, sizeof(txt), "%.*f", precision(val, 10.0), val);
    dt_bauhaus_combobox_add(w, txt);
    if(val >= 1000.0f) break;
    val *= sqrtf(2.0f);
  }
  g_signal_connect(G_OBJECT(w), "value-changed", G_CALLBACK(lens_comboentry_distance_update), self);
  gtk_box_pack_start(GTK_BOX(g->lens_param_box), w, TRUE, TRUE, 0);
  dt_bauhaus_combobox_set_editable(w, 1);
  g->cbe[2] = w;

  gtk_widget_hide(g->detection_warning);
  gtk_widget_show_all(g->lens_param_box);
}

static void lens_menu_select(GtkMenuItem *menuitem, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;
  lens_set(self, (lfLens *)g_object_get_data(G_OBJECT(menuitem), "lfLens"));
  if(darktable.gui->reset) return;
  p->modified = 1;
  const float scale = get_autoscale(self, p, g->camera);
  dt_bauhaus_slider_set(g->scale, scale);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void lens_menu_fill(dt_iop_module_t *self, const lfLens *const *lenslist)
{
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  unsigned i;
  GPtrArray *makers, *submenus;

  if(g->lens_menu)
  {
    gtk_widget_destroy(GTK_WIDGET(g->lens_menu));
    g->lens_menu = NULL;
  }

  /* Count all existing lens makers and create a sorted list */
  makers = g_ptr_array_new();
  submenus = g_ptr_array_new();
  for(i = 0; lenslist[i]; i++)
  {
    GtkWidget *submenu, *item;
    const char *m = lf_mlstr_get(lenslist[i]->Maker);
    int idx = ptr_array_find_sorted(makers, m, (GCompareFunc)g_utf8_collate);
    if(idx < 0)
    {
      /* No such maker yet, insert it into the array */
      idx = ptr_array_insert_sorted(makers, m, (GCompareFunc)g_utf8_collate);
      /* Create a submenu for lenses by this maker */
      submenu = gtk_menu_new();
      ptr_array_insert_index(submenus, submenu, idx);
    }

    submenu = (GtkWidget *)g_ptr_array_index(submenus, idx);
    /* Append current lens name to the submenu */
    item = gtk_menu_item_new_with_label(lf_mlstr_get(lenslist[i]->Model));
    gtk_widget_show(item);
    g_object_set_data(G_OBJECT(item), "lfLens", (void *)lenslist[i]);
    g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(lens_menu_select), self);
    gtk_menu_shell_append(GTK_MENU_SHELL(submenu), item);
  }

  g->lens_menu = GTK_MENU(gtk_menu_new());
  for(i = 0; i < makers->len; i++)
  {
    GtkWidget *item = gtk_menu_item_new_with_label((const gchar *)g_ptr_array_index(makers, i));
    gtk_widget_show(item);
    gtk_menu_shell_append(GTK_MENU_SHELL(g->lens_menu), item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), (GtkWidget *)g_ptr_array_index(submenus, i));
  }

  g_ptr_array_free(submenus, TRUE);
  g_ptr_array_free(makers, TRUE);
}

static void lens_menusearch_clicked(GtkWidget *button, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_lensfun_global_data_t *gd = (dt_iop_lensfun_global_data_t *)self->global_data;
  lfDatabase *dt_iop_lensfun_db = (lfDatabase *)gd->db;
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  const lfLens **lenslist;

  (void)button;

  dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
  lenslist = dt_iop_lensfun_db->FindLenses(g->camera, NULL, NULL, LF_SEARCH_SORT_AND_UNIQUIFY);
  dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
  if(!lenslist) return;
  lens_menu_fill(self, lenslist);
  lf_free(lenslist);

#if GTK_CHECK_VERSION(3, 22, 0)
  gtk_menu_popup_at_pointer(GTK_MENU(g->lens_menu), NULL);
#else
  gtk_menu_popup(GTK_MENU(g->lens_menu), NULL, NULL, NULL, NULL, 0, gtk_get_current_event_time());
#endif
}

static void lens_autosearch_clicked(GtkWidget *button, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_lensfun_global_data_t *gd = (dt_iop_lensfun_global_data_t *)self->global_data;
  lfDatabase *dt_iop_lensfun_db = (lfDatabase *)gd->db;
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  const lfLens **lenslist;
  char make[200], model[200];
  const gchar *txt = ((dt_iop_lensfun_params_t *)self->default_params)->lens;

  (void)button;

  parse_maker_model(txt, make, sizeof(make), model, sizeof(model));
  dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
  lenslist = dt_iop_lensfun_db->FindLenses(g->camera, make[0] ? make : NULL,
                                           model[0] ? model : NULL, LF_SEARCH_SORT_AND_UNIQUIFY);
  dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
  if(!lenslist) return;
  lens_menu_fill(self, lenslist);
  lf_free(lenslist);

#if GTK_CHECK_VERSION(3, 22, 0)
  gtk_menu_popup_at_pointer(GTK_MENU(g->lens_menu), NULL);
#else
  gtk_menu_popup(GTK_MENU(g->lens_menu), NULL, NULL, NULL, NULL, 0, gtk_get_current_event_time());
#endif
}

/* -- end lens -- */

static void target_geometry_changed(GtkWidget *widget, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;

  int pos = dt_bauhaus_combobox_get(widget);
  p->target_geom = (lfLensType)(pos + LF_UNKNOWN + 1);
  p->modified = 1;
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void modflags_changed(GtkWidget *widget, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  int pos = dt_bauhaus_combobox_get(widget);
  GList *modifiers = g->modifiers;
  while(modifiers)
  {
    // could use g_list_nth. this seems safer?
    dt_iop_lensfun_modifier_t *mm = (dt_iop_lensfun_modifier_t *)modifiers->data;
    if(mm->pos == pos)
    {
      p->modify_flags = (p->modify_flags & ~LENSFUN_MODFLAG_MASK) | mm->modflag;
      p->modified = 1;
      dt_dev_add_history_item(darktable.develop, self, TRUE);
      break;
    }
    modifiers = g_list_next(modifiers);
  }
}

static void reverse_toggled(GtkWidget *widget, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;
  p->inverse = dt_bauhaus_combobox_get(widget);
  p->modified = 1;
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void tca_changed(GtkWidget *slider, dt_iop_module_t *self)
{
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  const float val = dt_bauhaus_slider_get(slider);
  if(slider == g->tca_r)
    p->tca_r = val;
  else
    p->tca_b = val;
  if(p->tca_r != 1.0 || p->tca_b != 1.0) p->tca_override = 1;
  p->modified = 1;
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void scale_changed(GtkWidget *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;
  p->scale = dt_bauhaus_slider_get(slider);
  p->modified = 1;
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static float get_autoscale(dt_iop_module_t *self, dt_iop_lensfun_params_t *p, const lfCamera *camera)
{
  dt_iop_lensfun_global_data_t *gd = (dt_iop_lensfun_global_data_t *)self->global_data;
  lfDatabase *dt_iop_lensfun_db = (lfDatabase *)gd->db;
  float scale = 1.0;
  if(p->lens[0] != '\0')
  {
    dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
    const lfLens **lenslist
        = dt_iop_lensfun_db->FindLenses(camera, NULL, p->lens, 0);
    if(lenslist)
    {
      const dt_image_t *img = &(self->dev->image_storage);

      // FIXME: get those from rawprepare IOP somehow !!!
      const int iwd = img->width - img->crop_x - img->crop_width,
                iht = img->height - img->crop_y - img->crop_height;

      // create dummy modifier
#if defined(__GNUC__) && (__GNUC__ > 7)
      const dt_iop_lensfun_data_t d =
        {
         .lens         = (lfLens *)lenslist[0],
         .modify_flags = p->modify_flags,
         .inverse      = p->inverse,
         .scale        = 1.0f,
         .crop         = p->crop,
         .focal        = p->focal,
         .aperture     = p->aperture,
         .distance     = p->distance,
         .target_geom  = p->target_geom,
         .custom_tca   = { .Model = LF_TCA_MODEL_NONE }
        };
#else
      // prior to GCC 8.x the / .custom_tca   = { .Model = ??? } / was not supported:
      //    sorry, unimplemented: non-trivial designated initializers not supported
      // ?? This code can be removed when GCC-7 is not used anymore.

      dt_iop_lensfun_data_t d;
      d.lens             = (lfLens *)lenslist[0];
      d.modify_flags     = p->modify_flags;
      d.inverse          = p->inverse;
      d.scale            = 1.0f;
      d.crop             = p->crop;
      d.focal            = p->focal;
      d.aperture         = p->aperture;
      d.distance         = p->distance;
      d.target_geom      = p->target_geom;
      d.custom_tca.Model = LF_TCA_MODEL_NONE;
#endif

      lfModifier *modifier = get_modifier(NULL, iwd, iht, &d, LF_MODIFY_ALL);

      scale = modifier->GetAutoScale(p->inverse);
      delete modifier;
    }
    lf_free(lenslist);
    dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
  }
  return scale;
}

static void autoscale_pressed(GtkWidget *button, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;
  const float scale = get_autoscale(self, p, g->camera);
  p->modified = 1;
  dt_bauhaus_slider_set(g->scale, scale);
}

static void corrections_done(gpointer instance, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  if(darktable.gui->reset) return;

  dt_pthread_mutex_lock(&g->lock);
  const int corrections_done = g->corrections_done;
  dt_pthread_mutex_unlock(&g->lock);

  const char empty_message[] = "";
  char *message = (char *)empty_message;
  GList *modifiers = g->modifiers;
  while(modifiers && self->enabled)
  {
    // could use g_list_nth. this seems safer?
    dt_iop_lensfun_modifier_t *mm = (dt_iop_lensfun_modifier_t *)modifiers->data;
    if(mm->modflag == corrections_done)
    {
      message = mm->name;
      break;
    }
    modifiers = g_list_next(modifiers);
  }

  const int reset = darktable.gui->reset;
  darktable.gui->reset = 1;
  gtk_label_set_text(g->message, message);
  gtk_widget_set_tooltip_text(GTK_WIDGET(g->message), message);
  darktable.gui->reset = reset;
}

void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_lensfun_gui_data_t));
  // dt_iop_lensfun_global_data_t *gd = (dt_iop_lensfun_global_data_t *)self->global_data;
  // lfDatabase *dt_iop_lensfun_db = (lfDatabase *)gd->db;
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;

  dt_pthread_mutex_init(&g->lock, NULL);

  g->camera = NULL;
  g->camera_menu = NULL;
  g->lens_menu = NULL;
  g->modifiers = NULL;

  dt_pthread_mutex_lock(&g->lock);
  g->corrections_done = -1;
  dt_pthread_mutex_unlock(&g->lock);

  // initialize modflags options
  int pos = -1;
  dt_iop_lensfun_modifier_t *modifier;
  modifier = (dt_iop_lensfun_modifier_t *)g_malloc0(sizeof(dt_iop_lensfun_modifier_t));
  dt_utf8_strlcpy(modifier->name, _("none"), sizeof(modifier->name));
  g->modifiers = g_list_append(g->modifiers, modifier);
  modifier->modflag = LENSFUN_MODFLAG_NONE;
  modifier->pos = ++pos;

  modifier = (dt_iop_lensfun_modifier_t *)g_malloc0(sizeof(dt_iop_lensfun_modifier_t));
  dt_utf8_strlcpy(modifier->name, _("all"), sizeof(modifier->name));
  g->modifiers = g_list_append(g->modifiers, modifier);
  modifier->modflag = LENSFUN_MODFLAG_ALL;
  modifier->pos = ++pos;

  modifier = (dt_iop_lensfun_modifier_t *)g_malloc0(sizeof(dt_iop_lensfun_modifier_t));
  dt_utf8_strlcpy(modifier->name, _("distortion & TCA"), sizeof(modifier->name));
  g->modifiers = g_list_append(g->modifiers, modifier);
  modifier->modflag = LENSFUN_MODFLAG_DIST_TCA;
  modifier->pos = ++pos;

  modifier = (dt_iop_lensfun_modifier_t *)g_malloc0(sizeof(dt_iop_lensfun_modifier_t));
  dt_utf8_strlcpy(modifier->name, _("distortion & vignetting"), sizeof(modifier->name));
  g->modifiers = g_list_append(g->modifiers, modifier);
  modifier->modflag = LENSFUN_MODFLAG_DIST_VIGN;
  modifier->pos = ++pos;

  modifier = (dt_iop_lensfun_modifier_t *)g_malloc0(sizeof(dt_iop_lensfun_modifier_t));
  dt_utf8_strlcpy(modifier->name, _("TCA & vignetting"), sizeof(modifier->name));
  g->modifiers = g_list_append(g->modifiers, modifier);
  modifier->modflag = LENSFUN_MODFLAG_TCA_VIGN;
  modifier->pos = ++pos;

  modifier = (dt_iop_lensfun_modifier_t *)g_malloc0(sizeof(dt_iop_lensfun_modifier_t));
  dt_utf8_strlcpy(modifier->name, _("only distortion"), sizeof(modifier->name));
  g->modifiers = g_list_append(g->modifiers, modifier);
  modifier->modflag = LENSFUN_MODFLAG_DIST;
  modifier->pos = ++pos;

  modifier = (dt_iop_lensfun_modifier_t *)g_malloc0(sizeof(dt_iop_lensfun_modifier_t));
  dt_utf8_strlcpy(modifier->name, _("only TCA"), sizeof(modifier->name));
  g->modifiers = g_list_append(g->modifiers, modifier);
  modifier->modflag = LENSFUN_MODFLAG_TCA;
  modifier->pos = ++pos;

  modifier = (dt_iop_lensfun_modifier_t *)g_malloc0(sizeof(dt_iop_lensfun_modifier_t));
  dt_utf8_strlcpy(modifier->name, _("only vignetting"), sizeof(modifier->name));
  g->modifiers = g_list_append(g->modifiers, modifier);
  modifier->modflag = LENSFUN_MODFLAG_VIGN;
  modifier->pos = ++pos;

  GtkWidget *button;

  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, DT_BAUHAUS_SPACE);
  dt_gui_add_help_link(self->widget, dt_get_help_url(self->op));

  // camera selector
  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  g->camera_model = GTK_BUTTON(gtk_button_new_with_label(self->dev->image_storage.exif_model));
  dt_gui_key_accel_block_on_focus_connect(GTK_WIDGET(g->camera_model));
  gtk_label_set_ellipsize(GTK_LABEL(gtk_bin_get_child(GTK_BIN(g->camera_model))), PANGO_ELLIPSIZE_END);
  g_signal_connect(G_OBJECT(g->camera_model), "clicked", G_CALLBACK(camera_menusearch_clicked), self);
  gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(g->camera_model), TRUE, TRUE, 0);
  button = dtgtk_button_new(dtgtk_cairo_paint_solid_triangle, CPF_STYLE_FLAT | CPF_DIRECTION_DOWN, NULL);
  g->find_camera_button = button;
  gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
  gtk_widget_set_tooltip_text(button, _("find camera"));
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(camera_autosearch_clicked), self);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, TRUE, TRUE, 0);

  // lens selector
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  g->lens_model = GTK_BUTTON(gtk_button_new_with_label(self->dev->image_storage.exif_lens));
  dt_gui_key_accel_block_on_focus_connect(GTK_WIDGET(g->lens_model));
  gtk_label_set_ellipsize(GTK_LABEL(gtk_bin_get_child(GTK_BIN(g->lens_model))), PANGO_ELLIPSIZE_END);
  g_signal_connect(G_OBJECT(g->lens_model), "clicked", G_CALLBACK(lens_menusearch_clicked), self);
  gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(g->lens_model), TRUE, TRUE, 0);
  button = dtgtk_button_new(dtgtk_cairo_paint_solid_triangle, CPF_STYLE_FLAT | CPF_DIRECTION_DOWN, NULL);
  g->find_lens_button = GTK_WIDGET(button);
  gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
  gtk_widget_set_tooltip_text(button, _("find lens"));
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(lens_autosearch_clicked), self);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, TRUE, TRUE, 0);

  // lens properties
  g->lens_param_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), g->lens_param_box, TRUE, TRUE, 0);

  // camera/lens not detected warning box
  g->detection_warning = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), g->detection_warning, TRUE, TRUE, 0);

#if 0
  // if unambiguous info is there, use it.
  if(self->dev->image_storage.exif_lens[0] != '\0')
  {
    char make [200], model [200];
    const gchar *txt = gtk_entry_get_text(GTK_ENTRY(g->lens_model));
    parse_maker_model (txt, make, sizeof (make), model, sizeof (model));
    dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
    const lfLens **lenslist = lf_db_find_lenses_hd (dt_iop_lensfun_db, g->camera,
                              make [0] ? make : NULL,
                              model [0] ? model : NULL, 0);
    if(lenslist) lens_set (self, lenslist[0]);
    lf_free (lenslist);
    dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
  }
#endif

  // selector for correction type (modflags): one or more out of distortion, TCA, vignetting
  g->modflags = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(g->modflags, NULL, _("corrections"));
  gtk_box_pack_start(GTK_BOX(self->widget), g->modflags, TRUE, TRUE, 0);
  gtk_widget_set_tooltip_text(g->modflags, _("which corrections to apply"));
  GList *l = g->modifiers;
  while(l)
  {
    modifier = (dt_iop_lensfun_modifier_t *)l->data;
    dt_bauhaus_combobox_add(g->modflags, modifier->name);
    l = g_list_next(l);
  }
  dt_bauhaus_combobox_set(g->modflags, 0);
  g_signal_connect(G_OBJECT(g->modflags), "value-changed", G_CALLBACK(modflags_changed), (gpointer)self);

  // target geometry
  g->target_geom = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(g->target_geom, NULL, _("geometry"));
  gtk_box_pack_start(GTK_BOX(self->widget), g->target_geom, TRUE, TRUE, 0);
  gtk_widget_set_tooltip_text(g->target_geom, _("target geometry"));
  dt_bauhaus_combobox_add(g->target_geom, _("rectilinear"));
  dt_bauhaus_combobox_add(g->target_geom, _("fish-eye"));
  dt_bauhaus_combobox_add(g->target_geom, _("panoramic"));
  dt_bauhaus_combobox_add(g->target_geom, _("equirectangular"));
#if LF_VERSION >= ((0 << 24) | (2 << 16) | (6 << 8) | 0)
  dt_bauhaus_combobox_add(g->target_geom, _("orthographic"));
  dt_bauhaus_combobox_add(g->target_geom, _("stereographic"));
  dt_bauhaus_combobox_add(g->target_geom, _("equisolid angle"));
  dt_bauhaus_combobox_add(g->target_geom, _("thoby fish-eye"));
#endif
  g_signal_connect(G_OBJECT(g->target_geom), "value-changed", G_CALLBACK(target_geometry_changed),
                   (gpointer)self);

  // scale
  g->scale = dt_bauhaus_slider_new_with_range(self, 0.1, 2.0, 0.005, p->scale, 3);
  gtk_widget_set_tooltip_text(g->scale, _("auto scale"));
  dt_bauhaus_widget_set_label(g->scale, NULL, _("scale"));
  g_signal_connect(G_OBJECT(g->scale), "value-changed", G_CALLBACK(scale_changed), self);
  g_signal_connect(G_OBJECT(g->scale), "quad-pressed", G_CALLBACK(autoscale_pressed), self);
  gtk_box_pack_start(GTK_BOX(self->widget), g->scale, TRUE, TRUE, 0);
  dt_bauhaus_widget_set_quad_paint(g->scale, dtgtk_cairo_paint_refresh, 0, NULL);

  // reverse direction
  g->reverse = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(g->reverse, NULL, _("mode"));
  gtk_box_pack_start(GTK_BOX(self->widget), g->reverse, TRUE, TRUE, 0);
  gtk_widget_set_tooltip_text(g->reverse, _("correct distortions or apply them"));
  dt_bauhaus_combobox_add(g->reverse, _("correct"));
  dt_bauhaus_combobox_add(g->reverse, _("distort"));
  g_signal_connect(G_OBJECT(g->reverse), "value-changed", G_CALLBACK(reverse_toggled), (gpointer)self);

  // override linear tca (if not 1.0):
  g->tca_r = dt_bauhaus_slider_new_with_range(self, 0.99, 1.01, 0.0001, p->tca_r, 5);
  gtk_widget_set_tooltip_text(g->tca_r, _("Transversal Chromatic Aberration red"));
  dt_bauhaus_widget_set_label(g->tca_r, NULL, _("TCA red"));
  g_signal_connect(G_OBJECT(g->tca_r), "value-changed", G_CALLBACK(tca_changed), self);
  gtk_box_pack_start(GTK_BOX(self->widget), g->tca_r, TRUE, TRUE, 0);

  g->tca_b = dt_bauhaus_slider_new_with_range(self, 0.99, 1.01, 0.0001, p->tca_b, 5);
  gtk_widget_set_tooltip_text(g->tca_b, _("Transversal Chromatic Aberration blue"));
  dt_bauhaus_widget_set_label(g->tca_b, NULL, _("TCA blue"));
  g_signal_connect(G_OBJECT(g->tca_b), "value-changed", G_CALLBACK(tca_changed), self);
  gtk_box_pack_start(GTK_BOX(self->widget), g->tca_b, TRUE, TRUE, 0);

  // message box to inform user what corrections have been done. this is useful as depending on lensfuns
  // profile only some of the lens flaws can be corrected
  GtkBox *hbox1 = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  GtkWidget *label = gtk_label_new(_("corrections done: "));
  gtk_widget_set_tooltip_text(label, _("which corrections have actually been done"));
  gtk_box_pack_start(GTK_BOX(hbox1), label, FALSE, FALSE, 0);
  g->message = GTK_LABEL(gtk_label_new("")); // This gets filled in by process
  gtk_label_set_ellipsize(GTK_LABEL(g->message), PANGO_ELLIPSIZE_MIDDLE);
  gtk_box_pack_start(GTK_BOX(hbox1), GTK_WIDGET(g->message), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(hbox1), TRUE, TRUE, 0);

  /* add signal handler for preview pipe finish to update message on corrections done */
  dt_control_signal_connect(darktable.signals, DT_SIGNAL_DEVELOP_PREVIEW_PIPE_FINISHED,
                            G_CALLBACK(corrections_done), self);
}

void gui_update(struct dt_iop_module_t *self)
{
  // let gui elements reflect params
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;
  dt_iop_lensfun_params_t *p = (dt_iop_lensfun_params_t *)self->params;

  if(p->modified == 0)
  {
    /*
     * user did not modify anything in gui after autodetection - let's
     * use current default_params as params - for presets and mass-export
     */
    memcpy(self->params, self->default_params, sizeof(dt_iop_lensfun_params_t));
  }

  dt_iop_lensfun_global_data_t *gd = (dt_iop_lensfun_global_data_t *)self->global_data;
  lfDatabase *dt_iop_lensfun_db = (lfDatabase *)gd->db;
  // these are the wrong (untranslated) strings in general but that's ok, they will be overwritten further
  // down
  gtk_button_set_label(g->camera_model, p->camera);
  gtk_button_set_label(g->lens_model, p->lens);
  gtk_label_set_ellipsize(GTK_LABEL(gtk_bin_get_child(GTK_BIN(g->camera_model))), PANGO_ELLIPSIZE_END);
  gtk_label_set_ellipsize(GTK_LABEL(gtk_bin_get_child(GTK_BIN(g->lens_model))), PANGO_ELLIPSIZE_END);
  gtk_widget_set_tooltip_text(GTK_WIDGET(g->camera_model), "");
  gtk_widget_set_tooltip_text(GTK_WIDGET(g->lens_model), "");

  int modflag = p->modify_flags & LENSFUN_MODFLAG_MASK;
  GList *modifiers = g->modifiers;
  while(modifiers)
  {
    // could use g_list_nth. this seems safer?
    dt_iop_lensfun_modifier_t *mm = (dt_iop_lensfun_modifier_t *)modifiers->data;
    if(mm->modflag == modflag)
    {
      dt_bauhaus_combobox_set(g->modflags, mm->pos);
      break;
    }
    modifiers = g_list_next(modifiers);
  }

  dt_bauhaus_combobox_set(g->target_geom, p->target_geom - LF_UNKNOWN - 1);
  dt_bauhaus_combobox_set(g->reverse, p->inverse);
  dt_bauhaus_slider_set(g->tca_r, p->tca_r);
  dt_bauhaus_slider_set(g->tca_b, p->tca_b);
  dt_bauhaus_slider_set(g->scale, p->scale);
  const lfCamera **cam = NULL;
  g->camera = NULL;
  if(p->camera[0])
  {
    dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
    cam = dt_iop_lensfun_db->FindCamerasExt(NULL, p->camera, 0);
    dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
    if(cam)
      camera_set(self, cam[0]);
    else
      camera_set(self, NULL);
  }
  if(g->camera && p->lens[0])
  {
    char make[200], model[200];
    parse_maker_model(p->lens, make, sizeof(make), model, sizeof(model));
    dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
    const lfLens **lenslist = dt_iop_lensfun_db->FindLenses(g->camera, make[0] ? make : NULL,
                                                            model[0] ? model : NULL, 0);
    if(lenslist)
      lens_set(self, lenslist[0]);
    else
      lens_set(self, NULL);
    lf_free(lenslist);
    dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
  }
  else
  {
    dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
    lens_set(self, NULL);
    dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
  }
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  dt_iop_lensfun_gui_data_t *g = (dt_iop_lensfun_gui_data_t *)self->gui_data;

  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(corrections_done), self);

  dt_gui_key_accel_block_on_focus_disconnect(GTK_WIDGET(g->lens_model));
  dt_gui_key_accel_block_on_focus_disconnect(GTK_WIDGET(g->camera_model));
  while(g->modifiers)
  {
    g_free(g->modifiers->data);
    g->modifiers = g_list_delete_link(g->modifiers, g->modifiers);
  }

  dt_pthread_mutex_destroy(&g->lock);

  free(self->gui_data);
  self->gui_data = NULL;
}

}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
