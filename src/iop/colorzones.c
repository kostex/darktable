/*
    This file is part of darktable,
    copyright (c) 2009--2010 johannes hanika.
    copyright (c) 2019 edgardo hoszowski.

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "bauhaus/bauhaus.h"
#include "common/colorspaces_inline_conversions.h"
#include "develop/imageop.h"
#include "dtgtk/drawingarea.h"
#include "gui/color_picker_proxy.h"
#include "gui/presets.h"
#include "libs/colorpicker.h"

DT_MODULE_INTROSPECTION(4, dt_iop_colorzones_params_t)

#define DT_IOP_COLORZONES_INSET DT_PIXEL_APPLY_DPI(5)
#define DT_IOP_COLORZONES_CURVE_INFL .3f
#define DT_IOP_COLORZONES_RES 256
#define DT_IOP_COLORZONES_LUT_RES 0x10000

#define DT_IOP_COLORZONES_BANDS 8

#define DT_IOP_COLORZONES_MAXNODES 20
#define DT_IOP_COLORZONES_DEFAULT_STEP (0.001f)

#define DT_IOP_COLORZONES_MIN_X_DISTANCE 0.0025f

typedef enum dt_iop_colorzones_modes_t {
  DT_IOP_COLORZONES_MODE_OLD = 0,
  DT_IOP_COLORZONES_MODE_NEW = 1
} dt_iop_colorzones_modes_t;

typedef enum dt_iop_colorzones_channel_t {
  DT_IOP_COLORZONES_L = 0,
  DT_IOP_COLORZONES_C = 1,
  DT_IOP_COLORZONES_h = 2,
  DT_IOP_COLORZONES_MAX_CHANNELS = 3
} dt_iop_colorzones_channel_t;

typedef enum dt_iop_colorzones_pickcolor_type_t {
  DT_IOP_COLORZONES_PICK_NONE = 0,
  DT_IOP_COLORZONES_PICK_COLORPICK = 1,
  DT_IOP_COLORZONES_PICK_SET_VALUES = 2
} dt_iop_colorzones_pickcolor_type_t;

typedef struct dt_iop_colorzones_node_t
{
  float x;
  float y;
} dt_iop_colorzones_node_t;

typedef struct dt_iop_colorzones_params_t
{
  int32_t channel;
  dt_iop_colorzones_node_t curve[DT_IOP_COLORZONES_MAX_CHANNELS]
                                [DT_IOP_COLORZONES_MAXNODES]; // three curves (L, C, h) with max number of nodes
  int curve_num_nodes[DT_IOP_COLORZONES_MAX_CHANNELS];        // number of nodes per curve
  int curve_type[DT_IOP_COLORZONES_MAX_CHANNELS];             // CUBIC_SPLINE, CATMULL_ROM, MONOTONE_HERMITE
  float strength;
  int mode;
} dt_iop_colorzones_params_t;

typedef struct dt_iop_colorzones_gui_data_t
{
  dt_draw_curve_t *minmax_curve[DT_IOP_COLORZONES_MAX_CHANNELS]; // curve for gui to draw
  int minmax_curve_nodes[DT_IOP_COLORZONES_MAX_CHANNELS];
  int minmax_curve_type[DT_IOP_COLORZONES_MAX_CHANNELS];
  GtkBox *hbox;
  GtkDrawingArea *area;
  GtkWidget *bottom_area;
  GtkNotebook *channel_tabs;
  GtkWidget *select_by;
  GtkWidget *strength;
  GtkWidget *interpolator; // curve type
  GtkWidget *mode;
  GtkWidget *bt_showmask;
  double mouse_x, mouse_y;
  float mouse_radius;
  int selected;
  int dragging;
  int x_move;
  GtkWidget *colorpicker;
  GtkWidget *colorpicker_set_values;
  GtkWidget *chk_edit_by_area;
  int picker_set_upper_lower; // creates the curve flat, positive or negative
  dt_iop_colorzones_channel_t channel;
  float draw_ys[DT_IOP_COLORZONES_MAX_CHANNELS][DT_IOP_COLORZONES_RES];
  float draw_min_ys[DT_IOP_COLORZONES_RES];
  float draw_max_ys[DT_IOP_COLORZONES_RES];
  dt_iop_color_picker_t color_picker;
  float zoom_factor;
  float offset_x, offset_y;
  int edit_by_area;
  gboolean display_mask;
} dt_iop_colorzones_gui_data_t;

typedef struct dt_iop_colorzones_data_t
{
  dt_draw_curve_t *curve[DT_IOP_COLORZONES_MAX_CHANNELS];
  int curve_nodes[DT_IOP_COLORZONES_MAX_CHANNELS]; // number of nodes
  int curve_type[DT_IOP_COLORZONES_MAX_CHANNELS];  // curve style (e.g. CUBIC_SPLINE)
  dt_iop_colorzones_channel_t channel;
  float lut[3][DT_IOP_COLORZONES_LUT_RES];
  int mode;
} dt_iop_colorzones_data_t;

typedef struct dt_iop_colorzones_global_data_t
{
  int kernel_colorzones;
  int kernel_colorzones_v3;
} dt_iop_colorzones_global_data_t;


const char *name()
{
  return _("color zones");
}

int flags()
{
  return IOP_FLAGS_INCLUDE_IN_STYLES | IOP_FLAGS_SUPPORTS_BLENDING | IOP_FLAGS_ALLOW_TILING;
}

int default_group()
{
  return IOP_GROUP_COLOR;
}

int default_colorspace(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  return iop_cs_Lab;
}

int legacy_params(dt_iop_module_t *self, const void *const old_params, const int old_version,
                  void *new_params, const int new_version)
{
#define DT_IOP_COLORZONES1_BANDS 6

  if(old_version == 1 && new_version == 4)
  {
    typedef struct dt_iop_colorzones_params1_t
    {
      int32_t channel;
      float equalizer_x[3][DT_IOP_COLORZONES1_BANDS], equalizer_y[3][DT_IOP_COLORZONES1_BANDS];
    } dt_iop_colorzones_params1_t;

    const dt_iop_colorzones_params1_t *old = old_params;
    dt_iop_colorzones_params_t *new = new_params;

    new->channel = old->channel;

    // keep first point
    for(int i = 0; i < 3; i++)
    {
      new->curve[i][0].x = old->equalizer_x[i][0];
      new->curve[i][0].y = old->equalizer_y[i][0];
    }

    for(int i = 0; i < 3; i++)
      for(int k = 0; k < 6; k++)
      {
        //  first+1 and last-1 are set to just after and before the first and last point
        if(k == 0)
          new->curve[i][k + 1].x = old->equalizer_x[i][k] + 0.001;
        else if(k == 5)
          new->curve[i][k + 1].x = old->equalizer_x[i][k] - 0.001;
        else
          new->curve[i][k + 1].x = old->equalizer_x[i][k];
        new->curve[i][k + 1].y = old->equalizer_y[i][k];
      }

    // keep last point
    for(int i = 0; i < 3; i++)
    {
      new->curve[i][7].x = old->equalizer_x[i][5];
      new->curve[i][7].y = old->equalizer_y[i][5];
    }
    for(int c = 0; c < 3; c++)
    {
      new->curve_num_nodes[c] = DT_IOP_COLORZONES_BANDS;
      new->curve_type[c] = CATMULL_ROM;
    }
    new->strength = 0.0;
    new->mode = DT_IOP_COLORZONES_MODE_OLD;
    return 0;
  }
  if(old_version == 2 && new_version == 4)
  {
    typedef struct dt_iop_colorzones_params2_t
    {
      int32_t channel;
      float equalizer_x[3][DT_IOP_COLORZONES_BANDS], equalizer_y[3][DT_IOP_COLORZONES_BANDS];
    } dt_iop_colorzones_params2_t;

    const dt_iop_colorzones_params2_t *old = old_params;
    dt_iop_colorzones_params_t *new = new_params;
    new->channel = old->channel;

    for(int b = 0; b < DT_IOP_COLORZONES_BANDS; b++)
      for(int c = 0; c < 3; c++)
      {
        new->curve[c][b].x = old->equalizer_x[c][b];
        new->curve[c][b].y = old->equalizer_y[c][b];
      }
    for(int c = 0; c < 3; c++)
    {
      new->curve_num_nodes[c] = DT_IOP_COLORZONES_BANDS;
      new->curve_type[c] = CATMULL_ROM;
    }
    new->strength = 0.0;
    new->mode = DT_IOP_COLORZONES_MODE_OLD;
    return 0;
  }
  if(old_version == 3 && new_version == 4)
  {
    typedef struct dt_iop_colorzones_params3_t
    {
      int32_t channel;
      float equalizer_x[3][DT_IOP_COLORZONES_BANDS], equalizer_y[3][DT_IOP_COLORZONES_BANDS];
      float strength;
    } dt_iop_colorzones_params3_t;

    const dt_iop_colorzones_params3_t *old = old_params;
    dt_iop_colorzones_params_t *new = new_params;
    new->channel = old->channel;

    for(int b = 0; b < DT_IOP_COLORZONES_BANDS; b++)
    {
      for(int c = 0; c < 3; c++)
      {
        new->curve[c][b].x = old->equalizer_x[c][b];
        new->curve[c][b].y = old->equalizer_y[c][b];
      }
    }
    for(int c = 0; c < 3; c++)
    {
      new->curve_num_nodes[c] = DT_IOP_COLORZONES_BANDS;
      new->curve_type[c] = CATMULL_ROM;
    }
    new->strength = 0.0;
    new->mode = DT_IOP_COLORZONES_MODE_OLD;
    return 0;
  }

#undef DT_IOP_COLORZONES1_BANDS

  return 1;
}

static float _curve_to_mouse(const float x, const float zoom_factor, const float offset)
{
  return (x - offset) * zoom_factor;
}

static float _mouse_to_curve(const float x, const float zoom_factor, const float offset)
{
  return (x / zoom_factor) + offset;
}

// fills in new parameters based on mouse position (in 0,1)
static void dt_iop_colorzones_get_params(dt_iop_colorzones_params_t *p, dt_iop_colorzones_gui_data_t *c,
                                         const int ch, const double mouse_x, const double mouse_y,
                                         const float radius)
{
  const int bands = p->curve_num_nodes[ch];

  const float lin_mouse_x = _mouse_to_curve(mouse_x, c->zoom_factor, c->offset_x);
  const float lin_mouse_y = _mouse_to_curve(mouse_y, c->zoom_factor, c->offset_y);

  const float rad = radius / c->zoom_factor;

  if(p->channel == DT_IOP_COLORZONES_h)
  {
    // periodic boundary
    for(int k = 1; k < bands - 1; k++)
    {
      const float f = expf(-(lin_mouse_x - p->curve[ch][k].x) * (lin_mouse_x - p->curve[ch][k].x) / (rad * rad));
      p->curve[ch][k].y = (1.f - f) * p->curve[ch][k].y + f * lin_mouse_y;
    }
    const int m = bands - 1;
    const float mind = fminf((lin_mouse_x - p->curve[ch][0].x) * (lin_mouse_x - p->curve[ch][0].x),
                             (lin_mouse_x - p->curve[ch][m].x) * (lin_mouse_x - p->curve[ch][m].x));
    const float f = expf(-mind / (rad * rad));
    p->curve[ch][0].y = (1.f - f) * p->curve[ch][0].y + f * lin_mouse_y;
    p->curve[ch][m].y = (1.f - f) * p->curve[ch][m].y + f * lin_mouse_y;
  }
  else
  {
    for(int k = 0; k < bands; k++)
    {
      const float f = expf(-(lin_mouse_x - p->curve[ch][k].x) * (lin_mouse_x - p->curve[ch][k].x) / (rad * rad));
      p->curve[ch][k].y = (1.f - f) * p->curve[ch][k].y + f * lin_mouse_y;
    }
  }
}

static float lookup(const float *lut, const float i)
{
  const int bin0 = MIN(0xffff, MAX(0, (int)(DT_IOP_COLORZONES_LUT_RES * i)));
  const int bin1 = MIN(0xffff, MAX(0, (int)(DT_IOP_COLORZONES_LUT_RES * i) + 1));
  const float f = DT_IOP_COLORZONES_LUT_RES * i - bin0;
  return lut[bin1] * f + lut[bin0] * (1. - f);
}

static float strength(float value, float strength)
{
  return value + (value - 0.5) * (strength / 100.0);
}

void process_v3(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const void *const i, void *const o,
                const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  dt_iop_colorzones_data_t *d = (dt_iop_colorzones_data_t *)(piece->data);
  const int ch = piece->colors;
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(ch, i, o, roi_out) \
  shared(d) \
  schedule(static)
#endif
  for(size_t k = 0; k < (size_t)roi_out->width * roi_out->height; k++)
  {
    float *in = (float *)i + ch * k;
    float *out = (float *)o + ch * k;
    const float a = in[1], b = in[2];
    const float h = fmodf(atan2f(b, a) + 2.0 * M_PI, 2.0 * M_PI) / (2.0 * M_PI);
    const float C = sqrtf(b * b + a * a);
    float select = 0.0f;
    float blend = 0.0f;
    switch(d->channel)
    {
      case DT_IOP_COLORZONES_L:
        select = fminf(1.0, in[0] / 100.0);
        break;
      case DT_IOP_COLORZONES_C:
        select = fminf(1.0, C / 128.0);
        break;
      default:
      case DT_IOP_COLORZONES_h:
        select = h;
        blend = powf(1.0f - C / 128.0f, 2.0f);
        break;
    }
    const float Lm = (blend * .5f + (1.0f - blend) * lookup(d->lut[0], select)) - .5f;
    const float hm = (blend * .5f + (1.0f - blend) * lookup(d->lut[2], select)) - .5f;
    blend *= blend; // saturation isn't as prone to artifacts:
    // const float Cm = 2.0 * (blend*.5f + (1.0f-blend)*lookup(d->lut[1], select));
    const float Cm = 2.0 * lookup(d->lut[1], select);
    const float L = in[0] * powf(2.0f, 4.0f * Lm);
    out[0] = L;
    out[1] = cosf(2.0 * M_PI * (h + hm)) * Cm * C;
    out[2] = sinf(2.0 * M_PI * (h + hm)) * Cm * C;
    out[3] = in[3];
  }
}

void process(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const void *const ivoid,
             void *const ovoid, const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  dt_iop_colorzones_data_t *d = (dt_iop_colorzones_data_t *)(piece->data);
  dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)self->gui_data;

  const int ch = piece->colors;
  const float normalize_C = 1.f / (128.0f * sqrtf(2.f));

  // display selection if requested
  if(piece->pipe->type == DT_DEV_PIXELPIPE_FULL && g && g->display_mask && self->dev->gui_attached
     && (self == self->dev->gui_module) && (piece->pipe == self->dev->pipe))
  {
    const dt_iop_colorzones_channel_t display_channel = g->channel;

    memcpy(ovoid, ivoid, roi_out->width * roi_out->height * ch * sizeof(float));

#ifdef _OPENMP
#pragma omp parallel for default(none) schedule(static)                                                           \
    dt_omp_firstprivate(normalize_C, ch, ivoid, ovoid, roi_out, display_channel) shared(d)
#endif
    for(size_t k = 0; k < (size_t)roi_out->width * roi_out->height; k++)
    {
      float *in = (float *)ivoid + ch * k;
      float *out = (float *)ovoid + ch * k;

      float LCh[3];

      dt_Lab_2_LCH(in, LCh);

      float select = 0.0f;
      switch(d->channel)
      {
        case DT_IOP_COLORZONES_L:
          select = LCh[0] * 0.01f;
          break;
        case DT_IOP_COLORZONES_C:
          select = LCh[1] * normalize_C;
          break;
        case DT_IOP_COLORZONES_h:
        default:
          select = LCh[2];
          break;
      }
      select = CLAMP(select, 0.f, 1.f);

      out[3] = fabs(lookup(d->lut[display_channel], select) - .5f) * 4.f;
      out[3] = CLAMP(out[3], 0.f, 1.f);
    }

    piece->pipe->mask_display = DT_DEV_PIXELPIPE_DISPLAY_MASK;
    piece->pipe->bypass_blendif = 1;

    return;
  }

  if(d->mode == DT_IOP_COLORZONES_MODE_OLD)
  {
    process_v3(self, piece, ivoid, ovoid, roi_in, roi_out);
    return;
  }

#ifdef _OPENMP
#pragma omp parallel for default(none) dt_omp_firstprivate(normalize_C, ch, ivoid, ovoid, roi_out) shared(d)      \
    schedule(static)
#endif
  for(size_t k = 0; k < (size_t)roi_out->width * roi_out->height; k++)
  {
    float *in = (float *)ivoid + ch * k;
    float *out = (float *)ovoid + ch * k;

    float LCh[3];

    dt_Lab_2_LCH(in, LCh);

    float select = 0.0f;
    switch(d->channel)
    {
      case DT_IOP_COLORZONES_L:
        select = LCh[0] * 0.01f;
        break;
      case DT_IOP_COLORZONES_C:
        select = LCh[1] * normalize_C;
        break;
      case DT_IOP_COLORZONES_h:
      default:
        select = LCh[2];
        break;
    }
    select = CLAMP(select, 0.f, 1.f);

    LCh[0] *= powf(2.0f, 4.0f * (lookup(d->lut[0], select) - .5f));
    LCh[1] *= 2.f * lookup(d->lut[1], select);
    LCh[2] += lookup(d->lut[2], select) - .5f;

    dt_LCH_2_Lab(LCh, out);

    out[3] = in[3];
  }
}

#ifdef HAVE_OPENCL
int process_cl(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, cl_mem dev_in, cl_mem dev_out,
               const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  dt_iop_colorzones_data_t *d = (dt_iop_colorzones_data_t *)piece->data;
  dt_iop_colorzones_global_data_t *gd = (dt_iop_colorzones_global_data_t *)self->global_data;
  cl_mem dev_L, dev_a, dev_b = NULL;
  cl_int err = -999;

  const int devid = piece->pipe->devid;
  const int width = roi_in->width;
  const int height = roi_in->height;
  const int kernel_colorzones
      = (d->mode == DT_IOP_COLORZONES_MODE_OLD) ? gd->kernel_colorzones_v3 : gd->kernel_colorzones;

  size_t sizes[] = { ROUNDUPWD(width), ROUNDUPHT(height), 1 };
  dev_L = dt_opencl_copy_host_to_device(devid, d->lut[0], 256, 256, sizeof(float));
  dev_a = dt_opencl_copy_host_to_device(devid, d->lut[1], 256, 256, sizeof(float));
  dev_b = dt_opencl_copy_host_to_device(devid, d->lut[2], 256, 256, sizeof(float));
  if(dev_L == NULL || dev_a == NULL || dev_b == NULL) goto error;

  dt_opencl_set_kernel_arg(devid, kernel_colorzones, 0, sizeof(cl_mem), (void *)&dev_in);
  dt_opencl_set_kernel_arg(devid, kernel_colorzones, 1, sizeof(cl_mem), (void *)&dev_out);
  dt_opencl_set_kernel_arg(devid, kernel_colorzones, 2, sizeof(int), (void *)&width);
  dt_opencl_set_kernel_arg(devid, kernel_colorzones, 3, sizeof(int), (void *)&height);
  dt_opencl_set_kernel_arg(devid, kernel_colorzones, 4, sizeof(int), (void *)&d->channel);
  dt_opencl_set_kernel_arg(devid, kernel_colorzones, 5, sizeof(cl_mem), (void *)&dev_L);
  dt_opencl_set_kernel_arg(devid, kernel_colorzones, 6, sizeof(cl_mem), (void *)&dev_a);
  dt_opencl_set_kernel_arg(devid, kernel_colorzones, 7, sizeof(cl_mem), (void *)&dev_b);
  err = dt_opencl_enqueue_kernel_2d(devid, kernel_colorzones, sizes);

  if(err != CL_SUCCESS) goto error;
  dt_opencl_release_mem_object(dev_L);
  dt_opencl_release_mem_object(dev_a);
  dt_opencl_release_mem_object(dev_b);
  return TRUE;

error:
  dt_opencl_release_mem_object(dev_L);
  dt_opencl_release_mem_object(dev_a);
  dt_opencl_release_mem_object(dev_b);
  dt_print(DT_DEBUG_OPENCL, "[opencl_colorzones] couldn't enqueue kernel! %d\n", err);
  return FALSE;
}
#endif

void init_presets(dt_iop_module_so_t *self)
{
  dt_iop_colorzones_params_t p = { 0 };
  const int version = 4;

  p.strength = 0.0;
  p.mode = DT_IOP_COLORZONES_MODE_OLD;

  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db), "BEGIN", NULL, NULL, NULL);

  // red black white
  p.channel = DT_IOP_COLORZONES_h;
  for(int k = 0; k < DT_IOP_COLORZONES_BANDS; k++)
  {
    p.curve[DT_IOP_COLORZONES_L][k].y = .5f;
    p.curve[DT_IOP_COLORZONES_C][k].y = .0f;
    p.curve[DT_IOP_COLORZONES_h][k].y = .5f;
    p.curve[DT_IOP_COLORZONES_L][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
    p.curve[DT_IOP_COLORZONES_C][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
    p.curve[DT_IOP_COLORZONES_h][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
  }
  p.curve[DT_IOP_COLORZONES_C][0].y = p.curve[DT_IOP_COLORZONES_C][DT_IOP_COLORZONES_BANDS - 1].y = 0.65;
  p.curve[DT_IOP_COLORZONES_C][1].x = 3. / 16.;
  p.curve[DT_IOP_COLORZONES_C][3].x = 0.50;
  p.curve[DT_IOP_COLORZONES_C][4].x = 0.51;
  p.curve[DT_IOP_COLORZONES_C][6].x = 15. / 16.;
  for(int c = 0; c < 3; c++)
  {
    p.curve_num_nodes[c] = DT_IOP_COLORZONES_BANDS;
    p.curve_type[c] = CATMULL_ROM;
  }
  dt_gui_presets_add_generic(_("red black white"), self->op, version, &p, sizeof(p), 1);

  // black white and skin tones
  p.channel = DT_IOP_COLORZONES_h;
  for(int k = 0; k < DT_IOP_COLORZONES_BANDS; k++)
  {
    p.curve[DT_IOP_COLORZONES_L][k].y = .5f;
    p.curve[DT_IOP_COLORZONES_C][k].y = .0f;
    p.curve[DT_IOP_COLORZONES_h][k].y = .5f;
    p.curve[DT_IOP_COLORZONES_L][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
    p.curve[DT_IOP_COLORZONES_C][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
    p.curve[DT_IOP_COLORZONES_h][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
  }
  p.curve[DT_IOP_COLORZONES_C][0].y = p.curve[DT_IOP_COLORZONES_C][DT_IOP_COLORZONES_BANDS - 1].y = 0.5;
  p.curve[DT_IOP_COLORZONES_C][2].x = 0.25f;
  p.curve[DT_IOP_COLORZONES_C][1].x = 0.16f;
  p.curve[DT_IOP_COLORZONES_C][1].y = 0.3f;
  for(int c = 0; c < 3; c++)
  {
    p.curve_num_nodes[c] = DT_IOP_COLORZONES_BANDS;
    p.curve_type[c] = CATMULL_ROM;
  }
  dt_gui_presets_add_generic(_("black white and skin tones"), self->op, version, &p, sizeof(p), 1);

  // polarizing filter
  p.channel = DT_IOP_COLORZONES_C;
  for(int k = 0; k < DT_IOP_COLORZONES_BANDS; k++)
  {
    p.curve[DT_IOP_COLORZONES_L][k].y = .5f;
    p.curve[DT_IOP_COLORZONES_C][k].y = .5f;
    p.curve[DT_IOP_COLORZONES_h][k].y = .5f;
    p.curve[DT_IOP_COLORZONES_L][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
    p.curve[DT_IOP_COLORZONES_C][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
    p.curve[DT_IOP_COLORZONES_h][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
  }
  for(int k = 3; k < DT_IOP_COLORZONES_BANDS; k++)
    p.curve[DT_IOP_COLORZONES_C][k].y += (k - 2.5) / (DT_IOP_COLORZONES_BANDS - 2.0) * 0.25;
  for(int k = 4; k < DT_IOP_COLORZONES_BANDS; k++)
    p.curve[DT_IOP_COLORZONES_L][k].y -= (k - 3.5) / (DT_IOP_COLORZONES_BANDS - 3.0) * 0.35;
  for(int c = 0; c < 3; c++)
  {
    p.curve_num_nodes[c] = DT_IOP_COLORZONES_BANDS;
    p.curve_type[c] = CATMULL_ROM;
  }
  dt_gui_presets_add_generic(_("polarizing filter"), self->op, version, &p, sizeof(p), 1);

  // natural skin tone
  p.channel = DT_IOP_COLORZONES_h;
  for(int k = 0; k < DT_IOP_COLORZONES_BANDS; k++)
  {
    p.curve[DT_IOP_COLORZONES_L][k].y = .5f;
    p.curve[DT_IOP_COLORZONES_C][k].y = .5f;
    p.curve[DT_IOP_COLORZONES_h][k].y = .5f;
    p.curve[DT_IOP_COLORZONES_L][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
    p.curve[DT_IOP_COLORZONES_C][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
    p.curve[DT_IOP_COLORZONES_h][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
  }
  p.curve[DT_IOP_COLORZONES_C][1].y = .45f;
  p.curve[DT_IOP_COLORZONES_h][1].y = .55f;
  for(int c = 0; c < 3; c++)
  {
    p.curve_num_nodes[c] = DT_IOP_COLORZONES_BANDS;
    p.curve_type[c] = CATMULL_ROM;
  }
  dt_gui_presets_add_generic(_("natural skin tones"), self->op, version, &p, sizeof(p), 1);

  // black and white film
  p.channel = DT_IOP_COLORZONES_h;
  for(int k = 0; k < DT_IOP_COLORZONES_BANDS; k++)
  {
    p.curve[DT_IOP_COLORZONES_C][k].y = .0f;
    p.curve[DT_IOP_COLORZONES_h][k].y = .5f;
    p.curve[DT_IOP_COLORZONES_C][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
    p.curve[DT_IOP_COLORZONES_h][k].x = k / (DT_IOP_COLORZONES_BANDS - 1.);
  }
  p.curve[DT_IOP_COLORZONES_L][0].x = 0.000000;
  p.curve[DT_IOP_COLORZONES_L][0].y = 0.613040;
  p.curve[DT_IOP_COLORZONES_L][1].x = 0.010000;
  p.curve[DT_IOP_COLORZONES_L][1].y = 0.613040;
  p.curve[DT_IOP_COLORZONES_L][2].x = 0.245283;
  p.curve[DT_IOP_COLORZONES_L][2].y = 0.447962;
  p.curve[DT_IOP_COLORZONES_L][3].x = 0.498113;
  p.curve[DT_IOP_COLORZONES_L][3].y = 0.529201;
  p.curve[DT_IOP_COLORZONES_L][4].x = 0.641509;
  p.curve[DT_IOP_COLORZONES_L][4].y = 0.664967;
  p.curve[DT_IOP_COLORZONES_L][5].x = 0.879245;
  p.curve[DT_IOP_COLORZONES_L][5].y = 0.777294;
  p.curve[DT_IOP_COLORZONES_L][6].x = 0.990000;
  p.curve[DT_IOP_COLORZONES_L][6].y = 0.613040;
  p.curve[DT_IOP_COLORZONES_L][7].x = 1.000000;
  p.curve[DT_IOP_COLORZONES_L][7].y = 0.613040;
  for(int c = 0; c < 3; c++)
  {
    p.curve_num_nodes[c] = DT_IOP_COLORZONES_BANDS;
    p.curve_type[c] = CATMULL_ROM;
  }
  dt_gui_presets_add_generic(_("black & white film"), self->op, version, &p, sizeof(p), 1);

  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db), "COMMIT", NULL, NULL, NULL);
}

static void _reset_display_selection(dt_iop_module_t *self)
{
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  if(c)
  {
    if(c->display_mask)
    {
      c->display_mask = FALSE;
      dt_dev_reprocess_center(self->dev);
    }
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(c->bt_showmask)))
    {
      const int reset = darktable.gui->reset;
      darktable.gui->reset = 1;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c->bt_showmask), FALSE);
      darktable.gui->reset = reset;
    }
  }
}

static int _select_base_display_color(dt_iop_module_t *self, float *picked_color, float *picker_min,
                                      float *picker_max)
{
  const int select_by_picker = !(self->request_color_pick != DT_REQUEST_COLORPICK_MODULE
                                 || self->picked_color_max[0] < 0.0f || self->picked_color[0] == 0.0f);
  if(!select_by_picker)
  {
    float rgb[3] = { 0.0f, 0.3f, 0.7f };
    float xyz[3];
    float lab[3];
    dt_sRGB_to_XYZ(rgb, xyz);
    dt_XYZ_to_Lab(xyz, lab);
    dt_Lab_2_LCH(lab, picked_color);

    picker_max[0] = picker_min[0] = picked_color[0];
    picker_max[1] = picker_min[1] = picked_color[1];
    picker_max[2] = picker_min[2] = picked_color[2];
  }
  else
  {
    for(int k = 0; k < 3; k++)
    {
      picked_color[k] = self->picked_color[k];
      picker_min[k] = self->picked_color_min[k];
      picker_max[k] = self->picked_color_max[k];
    }
  }
  return select_by_picker;
}

static void _draw_color_picker(dt_iop_module_t *self, cairo_t *cr, dt_iop_colorzones_params_t *p,
                               dt_iop_colorzones_gui_data_t *c, const int width, const int height,
                               const float *const picker_color, const float *const picker_min,
                               const float *const picker_max)
{
  if(self->request_color_pick == DT_REQUEST_COLORPICK_MODULE)
  {
    // the global live samples ...
    GSList *samples = darktable.lib->proxy.colorpicker.live_samples;
    if(samples)
    {
      const dt_iop_order_iccprofile_info_t *const histogram_profile
          = dt_ioppr_get_histogram_profile_info(self->dev);
      const dt_iop_order_iccprofile_info_t *const work_profile
          = dt_ioppr_get_iop_work_profile_info(self, self->dev->iop);
      float pick_mean[4], pick_min[4], pick_max[4];
      int converted_cst;

      if(work_profile && histogram_profile)
      {
        dt_colorpicker_sample_t *sample = NULL;
        while(samples)
        {
          sample = samples->data;

          float picked_i = -1.0;
          float picked_min_i = -1.0;
          float picked_max_i = -1.0;

          // this functions need a 4c image
          for(int k = 0; k < 3; k++)
          {
            pick_mean[k] = sample->picked_color_rgb_mean[k];
            pick_min[k] = sample->picked_color_rgb_min[k];
            pick_max[k] = sample->picked_color_rgb_max[k];
          }
          pick_mean[3] = pick_min[3] = pick_max[3] = 1.f;

          dt_ioppr_transform_image_colorspace_rgb(pick_mean, pick_mean, 1, 1, histogram_profile, work_profile,
                                                  "color zones");
          dt_ioppr_transform_image_colorspace_rgb(pick_min, pick_min, 1, 1, histogram_profile, work_profile,
                                                  "color zones");
          dt_ioppr_transform_image_colorspace_rgb(pick_max, pick_max, 1, 1, histogram_profile, work_profile,
                                                  "color zones");

          dt_ioppr_transform_image_colorspace(self, pick_mean, pick_mean, 1, 1, iop_cs_rgb, iop_cs_Lab,
                                              &converted_cst, work_profile);
          dt_ioppr_transform_image_colorspace(self, pick_min, pick_min, 1, 1, iop_cs_rgb, iop_cs_Lab,
                                              &converted_cst, work_profile);
          dt_ioppr_transform_image_colorspace(self, pick_max, pick_max, 1, 1, iop_cs_rgb, iop_cs_Lab,
                                              &converted_cst, work_profile);

          dt_Lab_2_LCH(pick_mean, pick_mean);
          dt_Lab_2_LCH(pick_min, pick_min);
          dt_Lab_2_LCH(pick_max, pick_max);

          switch(p->channel)
          {
            // select by channel, abscissa:
            case DT_IOP_COLORZONES_L:
              picked_i = pick_mean[0] / 100.0;
              picked_min_i = pick_min[0] / 100.0;
              picked_max_i = pick_max[0] / 100.0;
              break;
            case DT_IOP_COLORZONES_C:
              picked_i = pick_mean[1] / (128.0f * sqrtf(2.f));
              picked_min_i = pick_min[1] / (128.0f * sqrtf(2.f));
              picked_max_i = pick_max[1] / (128.0f * sqrtf(2.f));
              break;
            default: // case DT_IOP_COLORZONES_h:
              picked_i = pick_mean[2];
              picked_min_i = pick_min[2];
              picked_max_i = pick_max[2];
              break;
          }

          // Convert abcissa to log coordinates if needed
          picked_i = _curve_to_mouse(picked_i, c->zoom_factor, c->offset_x);
          picked_min_i = _curve_to_mouse(picked_min_i, c->zoom_factor, c->offset_x);
          picked_max_i = _curve_to_mouse(picked_max_i, c->zoom_factor, c->offset_x);

          cairo_set_source_rgba(cr, 0.5, 0.7, 0.5, 0.15);
          cairo_rectangle(cr, width * picked_min_i, 0, width * fmax(picked_max_i - picked_min_i, 0.0f), height);
          cairo_fill(cr);
          cairo_set_source_rgba(cr, 0.5, 0.7, 0.5, 0.5);
          cairo_move_to(cr, width * picked_i, 0);
          cairo_line_to(cr, width * picked_i, height);
          cairo_stroke(cr);

          samples = g_slist_next(samples);
        }
      }
    }
  }

  if(self->request_color_pick == DT_REQUEST_COLORPICK_MODULE)
  {
    // draw marker for currently selected color:
    float picked_i = -1.0;
    float picked_min_i = -1.0;
    float picked_max_i = -1.0;
    switch(p->channel)
    {
      // select by channel, abscissa:
      case DT_IOP_COLORZONES_L:
        picked_i = picker_color[0] / 100.0;
        picked_min_i = picker_min[0] / 100.0;
        picked_max_i = picker_max[0] / 100.0;
        break;
      case DT_IOP_COLORZONES_C:
        picked_i = picker_color[1] / (128.0f * sqrtf(2.f));
        picked_min_i = picker_min[1] / (128.0f * sqrtf(2.f));
        picked_max_i = picker_max[1] / (128.0f * sqrtf(2.f));
        break;
      default: // case DT_IOP_COLORZONES_h:
        picked_i = picker_color[2];
        picked_min_i = picker_min[2];
        picked_max_i = picker_max[2];
        break;
    }

    picked_i = _curve_to_mouse(picked_i, c->zoom_factor, c->offset_x);
    picked_min_i = _curve_to_mouse(picked_min_i, c->zoom_factor, c->offset_x);
    picked_max_i = _curve_to_mouse(picked_max_i, c->zoom_factor, c->offset_x);

    cairo_save(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.25);
    cairo_rectangle(cr, width * picked_min_i, 0, width * fmax(picked_max_i - picked_min_i, 0.0f), height);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_operator(cr, CAIRO_OPERATOR_XOR);
    cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(2.));
    cairo_move_to(cr, width * picked_i, 0.0);
    cairo_line_to(cr, width * picked_i, height);
    cairo_stroke(cr);

    cairo_restore(cr);
  }
}

// graph x resulolution
#define DT_COLORZONES_CELLSI 64
// graph y resulolution
#define DT_COLORZONES_CELLSJ 36

#define COLORZONES_DRAW_BACKGROUD_BOX                                                                             \
  float Lab[3];                                                                                                   \
  dt_LCH_2_Lab(LCh, Lab);                                                                                         \
  const float L0 = Lab[0];                                                                                        \
  /* gamut mapping magic from iop/exposure.c: */                                                                  \
  const float Lwhite = 100.0f, Lclip = 20.0f;                                                                     \
  const float Lcap = fminf(100.0, Lab[0]);                                                                        \
  const float clip                                                                                                \
      = 1.0f                                                                                                      \
        - (Lcap - L0) * (1.0f / 100.0f) * fminf(Lwhite - Lclip, fmaxf(0.0f, Lab[0] - Lclip)) / (Lwhite - Lclip);  \
  const float clip2 = clip * clip * clip;                                                                         \
  Lab[1] *= Lab[0] / L0 * clip2;                                                                                  \
  Lab[2] *= Lab[0] / L0 * clip2;                                                                                  \
                                                                                                                  \
  float xyz[3];                                                                                                   \
  float rgb[3];                                                                                                   \
  dt_Lab_to_XYZ(Lab, xyz);                                                                                        \
  dt_XYZ_to_sRGB(xyz, rgb);                                                                                       \
                                                                                                                  \
  cairo_set_source_rgb(cr, rgb[0], rgb[1], rgb[2]);

static void _draw_background(cairo_t *cr, dt_iop_colorzones_params_t *p, dt_iop_colorzones_gui_data_t *c,
                             const int select_by_picker, const int width, const int height,
                             const float *picked_color)
{
  const float normalize_C = (128.f * sqrtf(2.f));

  const int cellsi = DT_COLORZONES_CELLSI;
  const int cellsj = DT_COLORZONES_CELLSJ;

  for(int j = 0; j < cellsj; j++)
  {
    for(int i = 0; i < cellsi; i++)
    {
      float LCh[3] = { 0 };

      const float jj = _mouse_to_curve(1.0f - ((float)j - .5f) / (float)(cellsj - 1), c->zoom_factor, c->offset_y);
      const float jjh
          = _mouse_to_curve(1.0f - (((float)j / (float)(cellsj - 1))), c->zoom_factor, c->offset_y) + .5f;
      const float ii = _mouse_to_curve(((float)i + .5f) / (float)(cellsi - 1), c->zoom_factor, c->offset_x);
      const float iih = _mouse_to_curve((float)i / (float)(cellsi - 1), c->zoom_factor, c->offset_x);

      // select by channel, abscissa:
      switch(p->channel)
      {
        // select by channel, abscissa:
        case DT_IOP_COLORZONES_L:
          LCh[0] = 100.0f * ii;
          LCh[1] = normalize_C * .5f;
          LCh[2] = picked_color[2];
          break;
        case DT_IOP_COLORZONES_C:
          LCh[0] = 50.0f;
          LCh[1] = picked_color[1] * 2.f * ii;
          LCh[2] = picked_color[2];
          break;
        default: // DT_IOP_COLORZONES_h
          LCh[0] = 50.0f;
          LCh[1] = normalize_C * .5f;
          LCh[2] = iih;
          break;
      }
      // channel to be altered:
      switch(c->channel)
      {
        // select by channel, abscissa:
        case DT_IOP_COLORZONES_L:
          if(p->channel == DT_IOP_COLORZONES_L)
            LCh[0] *= jj;
          else
            LCh[0] += -50.0 + 100.0 * jj;
          break;
        case DT_IOP_COLORZONES_C:
          LCh[1] *= 2.f * jj;
          break;
        default: // DT_IOP_COLORZONES_h
          LCh[2] += jjh;
          break;
      }

      COLORZONES_DRAW_BACKGROUD_BOX

      cairo_rectangle(cr, width * i / (float)cellsi, height * j / (float)cellsj, width / (float)cellsi,
                      height / (float)cellsj);
      cairo_fill(cr);
    }
  }
}

static gboolean _area_draw_callback(GtkWidget *widget, cairo_t *crf, dt_iop_module_t *self)
{
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_iop_colorzones_params_t p = *(dt_iop_colorzones_params_t *)self->params;
  dt_develop_t *dev = darktable.develop;

  for(int ch = 0; ch < DT_IOP_COLORZONES_MAX_CHANNELS; ch++)
  {
    if(c->minmax_curve_type[ch] != p.curve_type[ch] || c->minmax_curve_nodes[ch] != p.curve_num_nodes[ch])
    {
      dt_draw_curve_destroy(c->minmax_curve[ch]);
      c->minmax_curve[ch] = dt_draw_curve_new(0.0, 1.0, p.curve_type[ch]);
      c->minmax_curve_nodes[ch] = p.curve_num_nodes[ch];
      c->minmax_curve_type[ch] = p.curve_type[ch];

      if(p.channel == DT_IOP_COLORZONES_h)
        (void)dt_draw_curve_add_point(c->minmax_curve[ch], p.curve[ch][p.curve_num_nodes[ch] - 2].x - 1.0,
                                      p.curve[ch][p.curve_num_nodes[ch] - 2].y);
      else
        (void)dt_draw_curve_add_point(c->minmax_curve[ch], p.curve[ch][p.curve_num_nodes[ch] - 2].x - 1.0,
                                      p.curve[ch][0].y);
      for(int k = 0; k < p.curve_num_nodes[ch]; k++)
        (void)dt_draw_curve_add_point(c->minmax_curve[ch], p.curve[ch][k].x, p.curve[ch][k].y);
      if(p.channel == DT_IOP_COLORZONES_h)
        (void)dt_draw_curve_add_point(c->minmax_curve[ch], p.curve[ch][1].x + 1.0, p.curve[ch][1].y);
      else
        (void)dt_draw_curve_add_point(c->minmax_curve[ch], p.curve[ch][1].x + 1.0,
                                      p.curve[ch][p.curve_num_nodes[ch] - 1].y);
    }
    else
    {
      if(p.channel == DT_IOP_COLORZONES_h)
        dt_draw_curve_set_point(c->minmax_curve[ch], 0, p.curve[ch][p.curve_num_nodes[ch] - 2].x - 1.0,
                                p.curve[ch][p.curve_num_nodes[ch] - 2].y);
      else
        dt_draw_curve_set_point(c->minmax_curve[ch], 0, p.curve[ch][p.curve_num_nodes[ch] - 2].x - 1.0,
                                p.curve[ch][0].y);
      for(int k = 0; k < p.curve_num_nodes[ch]; k++)
        dt_draw_curve_set_point(c->minmax_curve[ch], k + 1, p.curve[ch][k].x, p.curve[ch][k].y);
      if(p.channel == DT_IOP_COLORZONES_h)
        dt_draw_curve_set_point(c->minmax_curve[ch], p.curve_num_nodes[ch] + 1, p.curve[ch][1].x + 1.0,
                                p.curve[ch][1].y);
      else
        dt_draw_curve_set_point(c->minmax_curve[ch], p.curve_num_nodes[ch] + 1, p.curve[ch][1].x + 1.0,
                                p.curve[ch][p.curve_num_nodes[ch] - 1].y);
    }
    dt_draw_curve_calc_values(c->minmax_curve[ch], 0.0, 1.0, DT_IOP_COLORZONES_RES, NULL, c->draw_ys[ch]);
  }

  const int ch = (int)c->channel;

  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  const int inset = DT_IOP_COLORZONES_INSET;
  int width = allocation.width, height = allocation.height;
  cairo_surface_t *cst = dt_cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cairo_t *cr = cairo_create(cst);

  // clear bg, match color of the notebook tabs:
  GdkRGBA color;
  GtkStyleContext *context = gtk_widget_get_style_context(widget);
  gboolean color_found = gtk_style_context_lookup_color(context, "selected_bg_color", &color);
  if(!color_found)
  {
    color.red = 1.0;
    color.green = 0.0;
    color.blue = 0.0;
    color.alpha = 1.0;
  }
  gdk_cairo_set_source_rgba(cr, &color);
  cairo_paint(cr);

  cairo_translate(cr, inset, inset);
  width -= 2 * inset;
  height -= 2 * inset;

  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1.0));
  cairo_set_source_rgb(cr, .1, .1, .1);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_stroke(cr);

  cairo_set_source_rgb(cr, .3, .3, .3);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  // if color picker is active we use it as base color
  // otherwise we use a light blue
  // we will work on LCh
  float picked_color[3], picker_min[3], picker_max[3];
  const int select_by_picker = _select_base_display_color(self, picked_color, picker_min, picker_max);

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

  _draw_background(cr, &p, c, select_by_picker, width, height, picked_color);

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

  // draw histogram in background
  // only if module is enabled
  if(self->enabled)
  {
    // only if no color picker
    if(self->request_color_pick != DT_REQUEST_COLORPICK_MODULE)
    {
      const int ch_hist = p.channel;
      const uint32_t *hist = self->histogram;
      const float hist_max = dev->histogram_type == DT_DEV_HISTOGRAM_LINEAR ? self->histogram_max[ch_hist]
                                                                            : logf(1.0 + self->histogram_max[ch_hist]);
      if(hist && hist_max > 0.0f)
      {
        cairo_save(cr);
        cairo_translate(cr, 0, height);
        cairo_scale(cr, width / 255.0, -(height - DT_PIXEL_APPLY_DPI(5)) / hist_max);

        cairo_set_source_rgba(cr, .2, .2, .2, 0.5);
        dt_draw_histogram_8_zoomed(cr, hist, 4, ch_hist, c->zoom_factor, c->offset_x * 255.0,
                                   c->offset_y * hist_max, dev->histogram_type == DT_DEV_HISTOGRAM_LINEAR);

        cairo_restore(cr);
      }
    }

    _draw_color_picker(self, cr, &p, c, width, height, picked_color, picker_min, picker_max);
  }

  if(c->edit_by_area)
  {
    // draw x positions
    cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
    cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1.));
    const float arrw = DT_PIXEL_APPLY_DPI(7.0f);
    for(int k = 0; k < p.curve_num_nodes[ch]; k++)
    {
      const float x = _curve_to_mouse(p.curve[ch][k].x, c->zoom_factor, c->offset_x);

      cairo_move_to(cr, width * x, height + inset - DT_PIXEL_APPLY_DPI(1));
      cairo_rel_line_to(cr, -arrw * .5f, 0);
      cairo_rel_line_to(cr, arrw * .5f, -arrw);
      cairo_rel_line_to(cr, arrw * .5f, arrw);
      cairo_close_path(cr);
      if(c->x_move == k)
        cairo_fill(cr);
      else
        cairo_stroke(cr);
    }
  }

  cairo_translate(cr, 0, height);

  // draw zoom info
  if(darktable.develop->darkroom_skip_mouse_events)
  {
    char text[256];
    PangoLayout *layout;
    PangoRectangle ink;
    PangoFontDescription *desc = pango_font_description_copy_static(darktable.bauhaus->pango_font_desc);
    pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
    pango_font_description_set_absolute_size(desc, PANGO_SCALE);
    layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);

    // scale conservatively to 100% of width:
    snprintf(text, sizeof(text), "zoom: 100 x: 100 y: 100");
    pango_layout_set_text(layout, text, -1);
    pango_layout_get_pixel_extents(layout, &ink, NULL);
    pango_font_description_set_absolute_size(desc, width * 1.0 / ink.width * PANGO_SCALE);
    pango_layout_set_font_description(layout, desc);

    snprintf(text, sizeof(text), "zoom: %i x: %i y: %i", (int)((c->zoom_factor - 1.f) * 100.f),
             (int)(c->offset_x * 100.f), (int)(c->offset_y * 100.f));

    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 0.5);
    pango_layout_set_text(layout, text, -1);
    pango_layout_get_pixel_extents(layout, &ink, NULL);
    cairo_move_to(cr, 0.98f * width - ink.width - ink.x, -0.02 * height - ink.height - ink.y);
    pango_cairo_show_layout(cr, layout);
    cairo_stroke(cr);
    pango_font_description_free(desc);
    g_object_unref(layout);
  }

  // draw curves, selected last.
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(2.));
  for(int i = 0; i < DT_IOP_COLORZONES_MAX_CHANNELS; i++)
  {
    const int ch_inv = ((int)c->channel + i + 1) % 3;

    if(i == 2)
      cairo_set_source_rgba(cr, .7, .7, .7, 1.0);
    else
      cairo_set_source_rgba(cr, .7, .7, .7, 0.3);

    cairo_move_to(cr, 0, -height * _curve_to_mouse(c->draw_ys[ch_inv][0], c->zoom_factor, c->offset_y));
    for(int k = 1; k < DT_IOP_COLORZONES_RES; k++)
    {
      const float xx = (float)k / (float)(DT_IOP_COLORZONES_RES - 1);
      const float yy = c->draw_ys[ch_inv][k];

      const float x = _curve_to_mouse(xx, c->zoom_factor, c->offset_x),
                  y = _curve_to_mouse(yy, c->zoom_factor, c->offset_y);

      cairo_line_to(cr, x * width, -height * y);
    }

    cairo_stroke(cr);
  }

  // draw dots on knots
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1.));
  cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1.));
  for(int k = 0; k < p.curve_num_nodes[ch]; k++)
  {
    const float x = _curve_to_mouse(p.curve[ch][k].x, c->zoom_factor, c->offset_x),
                y = _curve_to_mouse(p.curve[ch][k].y, c->zoom_factor, c->offset_y);
    cairo_arc(cr, width * x, -height * y, DT_PIXEL_APPLY_DPI(3.0), 0.0, 2.0 * M_PI);
    cairo_stroke(cr);
  }

  // draw min/max, if selected
  if(c->edit_by_area && (c->mouse_y > 0 || c->dragging))
  {
    const int bands = p.curve_num_nodes[ch];

    p = *(dt_iop_colorzones_params_t *)self->params;
    dt_iop_colorzones_get_params(&p, c, c->channel, c->mouse_x, 1., c->mouse_radius);
    if(p.channel == DT_IOP_COLORZONES_h)
      dt_draw_curve_set_point(c->minmax_curve[ch], 0, p.curve[ch][bands - 2].x - 1.0, p.curve[ch][bands - 2].y);
    else
      dt_draw_curve_set_point(c->minmax_curve[ch], 0, p.curve[ch][bands - 2].x - 1.0, p.curve[ch][0].y);
    for(int k = 0; k < bands; k++)
      dt_draw_curve_set_point(c->minmax_curve[ch], k + 1, p.curve[ch][k].x, p.curve[ch][k].y);
    if(p.channel == DT_IOP_COLORZONES_h)
      dt_draw_curve_set_point(c->minmax_curve[ch], bands + 1, p.curve[ch][1].x + 1.0, p.curve[ch][1].y);
    else
      dt_draw_curve_set_point(c->minmax_curve[ch], bands + 1, p.curve[ch][1].x + 1.0, p.curve[ch][bands - 1].y);
    dt_draw_curve_calc_values(c->minmax_curve[ch], 0.0, 1.0, DT_IOP_COLORZONES_RES, NULL, c->draw_min_ys);

    p = *(dt_iop_colorzones_params_t *)self->params;
    dt_iop_colorzones_get_params(&p, c, c->channel, c->mouse_x, .0, c->mouse_radius);
    if(p.channel == DT_IOP_COLORZONES_h)
      dt_draw_curve_set_point(c->minmax_curve[ch], 0, p.curve[ch][bands - 2].x - 1.0, p.curve[ch][bands - 2].y);
    else
      dt_draw_curve_set_point(c->minmax_curve[ch], 0, p.curve[ch][bands - 2].x - 1.0, p.curve[ch][0].y);
    for(int k = 0; k < bands; k++)
      dt_draw_curve_set_point(c->minmax_curve[ch], k + 1, p.curve[ch][k].x, p.curve[ch][k].y);
    if(p.channel == DT_IOP_COLORZONES_h)
      dt_draw_curve_set_point(c->minmax_curve[ch], bands + 1, p.curve[ch][1].x + 1.0, p.curve[ch][1].y);
    else
      dt_draw_curve_set_point(c->minmax_curve[ch], bands + 1, p.curve[ch][1].x + 1.0, p.curve[ch][bands - 1].y);
    dt_draw_curve_calc_values(c->minmax_curve[ch], 0.0, 1.0, DT_IOP_COLORZONES_RES, NULL, c->draw_max_ys);

    // restore params values
    p = *(dt_iop_colorzones_params_t *)self->params;

    // draw min/max curves:
    cairo_set_source_rgba(cr, .7, .7, .7, .6);
    cairo_move_to(cr, 0, -height * _curve_to_mouse(c->draw_min_ys[0], c->zoom_factor, c->offset_y));

    for(int k = 1; k < DT_IOP_COLORZONES_RES; k++)
    {
      const float xx = (float)k / (float)(DT_IOP_COLORZONES_RES - 1);
      const float yy = c->draw_min_ys[k];

      const float x = _curve_to_mouse(xx, c->zoom_factor, c->offset_x),
                  y = _curve_to_mouse(yy, c->zoom_factor, c->offset_y);

      cairo_line_to(cr, x * width, -height * y);
    }

    for(int k = DT_IOP_COLORZONES_RES - 1; k >= 0; k--)
    {
      const float xx = (float)k / (float)(DT_IOP_COLORZONES_RES - 1);
      const float yy = c->draw_max_ys[k];

      const float x = _curve_to_mouse(xx, c->zoom_factor, c->offset_x),
                  y = _curve_to_mouse(yy, c->zoom_factor, c->offset_y);

      cairo_line_to(cr, x * width, -height * y);
    }

    cairo_close_path(cr);
    cairo_fill(cr);

    // draw mouse focus circle
    cairo_set_source_rgba(cr, .9, .9, .9, .5);

    const int k = DT_IOP_COLORZONES_RES * _mouse_to_curve(c->mouse_x, c->zoom_factor, c->offset_x);
    const float x = c->mouse_x, y = _curve_to_mouse(c->draw_ys[ch][k], c->zoom_factor, c->offset_y);

    cairo_arc(cr, x * width, -height * y, c->mouse_radius * width, 0, 2. * M_PI);
    cairo_stroke(cr);
  }
  else
  {
    // draw selected cursor
    cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1.));

    if(c->selected >= 0)
    {
      cairo_set_source_rgb(cr, .9, .9, .9);
      const float x = _curve_to_mouse(p.curve[c->channel][c->selected].x, c->zoom_factor, c->offset_x),
                  y = _curve_to_mouse(p.curve[c->channel][c->selected].y, c->zoom_factor, c->offset_y);

      cairo_arc(cr, x * width, -y * height, DT_PIXEL_APPLY_DPI(4), 0, 2. * M_PI);
      cairo_stroke(cr);
    }
  }

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

  cairo_destroy(cr);
  cairo_set_source_surface(crf, cst, 0, 0);
  cairo_paint(crf);
  cairo_surface_destroy(cst);
  return TRUE;
}

static gboolean _bottom_area_draw_callback(GtkWidget *widget, cairo_t *crf, dt_iop_module_t *self)
{
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_iop_colorzones_params_t p = *(dt_iop_colorzones_params_t *)self->params;

  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  const int inset = DT_IOP_COLORZONES_INSET;
  int width = allocation.width, height = allocation.height;
  cairo_surface_t *cst = dt_cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cairo_t *cr = cairo_create(cst);
  // clear bg, match color of the notebook tabs:
  GdkRGBA color;
  GtkStyleContext *context = gtk_widget_get_style_context(widget);
  gboolean color_found = gtk_style_context_lookup_color(context, "selected_bg_color", &color);
  if(!color_found)
  {
    color.red = 1.0;
    color.green = 0.0;
    color.blue = 0.0;
    color.alpha = 1.0;
  }
  gdk_cairo_set_source_rgba(cr, &color);
  cairo_paint(cr);

  cairo_translate(cr, inset, inset);
  width -= 2 * inset;
  height -= 2 * inset;

  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1.0));
  cairo_set_source_rgb(cr, .1, .1, .1);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_stroke(cr);

  cairo_set_source_rgb(cr, .3, .3, .3);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  // if color picker is active we use it as base color
  // otherwise we use a light blue
  // we will work on LCh
  float picked_color[3], picker_min[3], picker_max[3];
  _select_base_display_color(self, picked_color, picker_min, picker_max);
  const float normalize_C = (128.f * sqrtf(2.f));

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

  const int cellsi = DT_COLORZONES_CELLSI;

  for(int i = 0; i < cellsi; i++)
  {
    const float ii = _mouse_to_curve(((float)i + .5f) / (float)(cellsi - 1), c->zoom_factor, c->offset_x);
    const float iih = _mouse_to_curve((float)i / (float)(cellsi - 1), c->zoom_factor, c->offset_x);

    float LCh[3];

    switch(p.channel)
    {
      // select by channel, abscissa:
      case DT_IOP_COLORZONES_L:
        LCh[0] = 100.0f * ii;
        LCh[1] = normalize_C * .5f;
        LCh[2] = picked_color[2];
        break;
      case DT_IOP_COLORZONES_C:
        LCh[0] = 50.0f;
        LCh[1] = picked_color[1] * 2.f * ii;
        LCh[2] = picked_color[2];
        break;
      default: // DT_IOP_COLORZONES_h
        LCh[0] = 50.0f;
        LCh[1] = normalize_C * .5f;
        LCh[2] = iih;
        break;
    }

    COLORZONES_DRAW_BACKGROUD_BOX

    cairo_rectangle(cr, width * i / (float)cellsi, 0, width / (float)cellsi, height);
    cairo_fill(cr);
  }

  cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

  if(self->enabled)
  {
    _draw_color_picker(self, cr, &p, c, width, height, picked_color, picker_min, picker_max);
  }

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

  cairo_destroy(cr);
  cairo_set_source_surface(crf, cst, 0, 0);
  cairo_paint(crf);
  cairo_surface_destroy(cst);
  return TRUE;
}

#undef COLORZONES_DRAW_BACKGROUD_BOX
#undef DT_COLORZONES_CELLSI
#undef DT_COLORZONES_CELLSJ

static gboolean _bottom_area_button_press_callback(GtkWidget *widget, GdkEventButton *event, dt_iop_module_t *self)
{
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;

  if(event->button == 1 && event->type == GDK_2BUTTON_PRESS)
  {
    // reset zoom level
    c->zoom_factor = 1.f;
    c->offset_x = c->offset_y = 0.f;

    gtk_widget_queue_draw(self->widget);

    return TRUE;
  }

  return FALSE;
}

static gboolean _sanity_check(const float x, const int selected, const int nodes,
                              const dt_iop_colorzones_node_t *curve)
{
  gboolean point_valid = TRUE;

  // check if it is not too close to other node
  const float min_dist = DT_IOP_COLORZONES_MIN_X_DISTANCE; // in curve coordinates
  if((selected > 0 && x - curve[selected - 1].x <= min_dist)
     || (selected < nodes - 1 && curve[selected + 1].x - x <= min_dist))
    point_valid = FALSE;

  // for all points, x coordinate of point must be strictly larger than
  // the x coordinate of the previous point
  if((selected > 0 && (curve[selected - 1].x >= x)) || (selected < nodes - 1 && (curve[selected + 1].x <= x)))
  {
    point_valid = FALSE;
  }

  return point_valid;
}

static gboolean _move_point_internal(dt_iop_module_t *self, GtkWidget *widget, float dx, float dy, guint state)
{
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;

  int ch = c->channel;
  dt_iop_colorzones_node_t *curve = p->curve[ch];

  float multiplier;

  GdkModifierType modifiers = gtk_accelerator_get_default_mod_mask();
  if((state & modifiers) == GDK_SHIFT_MASK)
  {
    multiplier = dt_conf_get_float("darkroom/ui/scale_rough_step_multiplier");
  }
  else if((state & modifiers) == GDK_CONTROL_MASK)
  {
    multiplier = dt_conf_get_float("darkroom/ui/scale_precise_step_multiplier");
  }
  else
  {
    multiplier = dt_conf_get_float("darkroom/ui/scale_step_multiplier");
  }

  dx *= multiplier;
  dy *= multiplier;
  // do not move the first or last nodes on the x-axis
  if(c->selected == 0 || c->selected == p->curve_num_nodes[ch] - 1) dx = 0.f;

  const float new_x = CLAMP(curve[c->selected].x + dx, 0.0f, 1.0f);
  const float new_y = CLAMP(curve[c->selected].y + dy, 0.0f, 1.0f);

  if(_sanity_check(new_x, c->selected, p->curve_num_nodes[ch], p->curve[ch]))
  {
    curve[c->selected].x = new_x;
    curve[c->selected].y = new_y;

    if(p->channel == DT_IOP_COLORZONES_h && (c->selected == 0 || c->selected == p->curve_num_nodes[ch] - 1))
    {
      if(c->selected == 0)
      {
        curve[p->curve_num_nodes[ch] - 1].x = 1.f - curve[c->selected].x;
        curve[p->curve_num_nodes[ch] - 1].y = curve[c->selected].y;
      }
      else
      {
        curve[0].x = 1.f - curve[c->selected].x;
        curve[0].y = curve[c->selected].y;
      }
    }

    dt_dev_add_history_item(darktable.develop, self, TRUE);
  }

  gtk_widget_queue_draw(widget);

  return TRUE;
}

static inline int _add_node(dt_iop_colorzones_node_t *curve, int *nodes, float x, float y)
{
  int selected = -1;
  if(curve[0].x > x)
    selected = 0;
  else
  {
    for(int k = 1; k < *nodes; k++)
    {
      if(curve[k].x > x)
      {
        selected = k;
        break;
      }
    }
  }
  if(selected == -1) selected = *nodes;

  // check if it is not too close to other node
  const float min_dist = DT_IOP_COLORZONES_MIN_X_DISTANCE; // in curve coordinates
  if((selected > 0 && x - curve[selected - 1].x <= min_dist)
     || (selected < *nodes && curve[selected].x - x <= min_dist))
    selected = -2;

  if(selected >= 0)
  {
    for(int i = *nodes; i > selected; i--)
    {
      curve[i].x = curve[i - 1].x;
      curve[i].y = curve[i - 1].y;
    }
    // found a new point
    curve[selected].x = x;
    curve[selected].y = y;
    (*nodes)++;
  }
  return selected;
}

static gboolean _area_scrolled_callback(GtkWidget *widget, GdkEventScroll *event, dt_iop_module_t *self)
{
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;

  if(((event->state & gtk_accelerator_get_default_mod_mask()) == darktable.gui->sidebar_scroll_mask) != dt_conf_get_bool("darkroom/ui/sidebar_scroll_default")) return FALSE;
  gdouble delta_y;

  if(darktable.develop->darkroom_skip_mouse_events)
  {
    if(dt_gui_get_scroll_deltas(event, NULL, &delta_y))
    {
      GtkAllocation allocation;
      gtk_widget_get_allocation(widget, &allocation);

      const float mx = c->mouse_x;
      const float my = c->mouse_y;
      const float linx = _mouse_to_curve(mx, c->zoom_factor, c->offset_x),
                  liny = _mouse_to_curve(my, c->zoom_factor, c->offset_y);

      c->zoom_factor *= 1.0 - 0.1 * delta_y;
      if(c->zoom_factor < 1.f) c->zoom_factor = 1.f;

      c->offset_x = linx - (mx / c->zoom_factor);
      c->offset_y = liny - (my / c->zoom_factor);

      c->offset_x = CLAMP(c->offset_x, 0.f, (c->zoom_factor - 1.f) / c->zoom_factor);
      c->offset_y = CLAMP(c->offset_y, 0.f, (c->zoom_factor - 1.f) / c->zoom_factor);

      gtk_widget_queue_draw(self->widget);
    }

    return TRUE;
  }

  if(c->selected < 0 && !c->edit_by_area) return TRUE;

  if(dt_gui_get_scroll_deltas(event, NULL, &delta_y))
  {
    if(c->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES) dt_iop_color_picker_reset(self, TRUE);

    if(c->edit_by_area)
    {
      const int bands = p->curve_num_nodes[c->channel];
      c->mouse_radius = CLAMP(c->mouse_radius * (1.0 + 0.1 * delta_y), 0.2 / bands, 1.0);
      gtk_widget_queue_draw(widget);
    }
    else
    {
      delta_y *= -DT_IOP_COLORZONES_DEFAULT_STEP;
      return _move_point_internal(self, widget, 0.0, delta_y, event->state);
    }
  }

  return TRUE;
}

static gboolean _area_motion_notify_callback(GtkWidget *widget, GdkEventMotion *event, dt_iop_module_t *self)
{
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;

  const int inset = DT_IOP_COLORZONES_INSET;

  // drag the draw area
  if(darktable.develop->darkroom_skip_mouse_events)
  {
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    const int height = allocation.height - 2 * inset, width = allocation.width - 2 * inset;

    const float mx = c->mouse_x;
    const float my = c->mouse_y;

    c->mouse_x = CLAMP(event->x - inset, 0, width) / (float)width;
    c->mouse_y = 1.0 - CLAMP(event->y - inset, 0, height) / (float)height;

    if(event->state & GDK_BUTTON1_MASK)
    {
      c->offset_x += (mx - c->mouse_x) / c->zoom_factor;
      c->offset_y += (my - c->mouse_y) / c->zoom_factor;

      c->offset_x = CLAMP(c->offset_x, 0.f, (c->zoom_factor - 1.f) / c->zoom_factor);
      c->offset_y = CLAMP(c->offset_y, 0.f, (c->zoom_factor - 1.f) / c->zoom_factor);

      gtk_widget_queue_draw(self->widget);
    }
    return TRUE;
  }

  const int ch = c->channel;
  const int nodes = p->curve_num_nodes[ch];
  dt_iop_colorzones_node_t *curve = p->curve[ch];

  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);

  const int height = allocation.height - 2 * inset;
  const int width = allocation.width - 2 * inset;

  const double old_m_x = c->mouse_x;
  const double old_m_y = fabs(c->mouse_y);

  c->mouse_x = CLAMP(event->x - inset, 0, width) / (float)width;
  c->mouse_y = 1.0 - CLAMP(event->y - inset, 0, height) / (float)height;

  // move a node
  if(event->state & GDK_BUTTON1_MASK)
  {
    if(c->edit_by_area)
    {
      if(c->dragging && c->x_move >= 0)
        c->selected = c->x_move;
      else
        c->selected = -1;
    }

    // got a vertex selected:
    if(c->selected >= 0)
    {
      // this is used to translate mause position in zoom_factor to make this behavior unified with linear scale.
      const float translate_mouse_x = old_m_x - _curve_to_mouse(curve[c->selected].x, c->zoom_factor, c->offset_x);
      const float translate_mouse_y = old_m_y - _curve_to_mouse(curve[c->selected].y, c->zoom_factor, c->offset_y);
      // dx & dy are in linear coordinates
      const float dx = _mouse_to_curve(c->mouse_x - translate_mouse_x, c->zoom_factor, c->offset_x)
                       - _mouse_to_curve(old_m_x - translate_mouse_x, c->zoom_factor, c->offset_x);
      const float dy = _mouse_to_curve(c->mouse_y - translate_mouse_y, c->zoom_factor, c->offset_y)
                       - _mouse_to_curve(old_m_y - translate_mouse_y, c->zoom_factor, c->offset_y);

      if(c->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES)
        dt_iop_color_picker_reset(self, TRUE);
      return _move_point_internal(self, widget, dx, dy, event->state);
    }
  }

  if(c->edit_by_area)
  {
    if(c->dragging)
    {
      if(c->x_move < 0)
      {
        dt_iop_colorzones_get_params(p, c, c->channel, c->mouse_x, c->mouse_y, c->mouse_radius);
        if(c->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES)
          dt_iop_color_picker_reset(self, TRUE);
        dt_dev_add_history_item(darktable.develop, self, TRUE);
      }
    }
    else if(event->y > height)
    {
      c->x_move = 0;
      const int bands = p->curve_num_nodes[c->channel];
      const float mouse_x = _mouse_to_curve(c->mouse_x, c->zoom_factor, c->offset_x);
      float dist = fabs(p->curve[c->channel][0].x - mouse_x);
      for(int k = 1; k < bands; k++)
      {
        const float d2 = fabs(p->curve[c->channel][k].x - mouse_x);
        if(d2 < dist)
        {
          c->x_move = k;
          dist = d2;
        }
      }
    }
    else
    {
      c->x_move = -1;
    }
  }
  else
  {
    if(event->state & GDK_BUTTON1_MASK)
    {
      if(nodes < DT_IOP_COLORZONES_MAXNODES && c->selected == -1)
      {
        const float linx = _mouse_to_curve(c->mouse_x, c->zoom_factor, c->offset_x),
                    liny = _mouse_to_curve(c->mouse_y, c->zoom_factor, c->offset_y);

        // no vertex was close, create a new one!
        c->selected = _add_node(curve, &p->curve_num_nodes[ch], linx, liny);

        if(c->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES)
          dt_iop_color_picker_reset(self, TRUE);
        dt_dev_add_history_item(darktable.develop, self, TRUE);
      }
    }
    else
    {
      const float mx = c->mouse_x;
      const float my = c->mouse_y;

      // minimum area around the node to select it:
      float min = .04f * .04f; // comparing against square
      int nearest = -1;
      for(int k = 0; k < nodes; k++)
      {
        float dist = (my - _curve_to_mouse(curve[k].y, c->zoom_factor, c->offset_y))
                         * (my - _curve_to_mouse(curve[k].y, c->zoom_factor, c->offset_y))
                     + (mx - _curve_to_mouse(curve[k].x, c->zoom_factor, c->offset_x))
                           * (mx - _curve_to_mouse(curve[k].x, c->zoom_factor, c->offset_x));
        if(dist < min)
        {
          min = dist;
          nearest = k;
        }
      }
      c->selected = nearest;
    }
    if(c->selected >= 0) gtk_widget_grab_focus(widget);
  }

  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean _area_button_press_callback(GtkWidget *widget, GdkEventButton *event, dt_iop_module_t *self)
{
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;
  dt_iop_colorzones_params_t *d = (dt_iop_colorzones_params_t *)self->default_params;

  if(darktable.develop->darkroom_skip_mouse_events) return TRUE;

  int ch = c->channel;
  int nodes = p->curve_num_nodes[ch];
  dt_iop_colorzones_node_t *curve = p->curve[ch];

  if(event->button == 1)
  {
    if(c->edit_by_area && event->type != GDK_2BUTTON_PRESS && (event->state & GDK_CONTROL_MASK) != GDK_CONTROL_MASK)
    {
      c->dragging = 1;
      return TRUE;
    }
    else if(event->type == GDK_BUTTON_PRESS && (event->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK
            && nodes < DT_IOP_COLORZONES_MAXNODES && (c->selected == -1 || c->edit_by_area))
    {
      // if we are not on a node -> add a new node at the current x of the pointer and y of the curve at that x
      const int inset = DT_IOP_COLORZONES_INSET;
      GtkAllocation allocation;
      gtk_widget_get_allocation(widget, &allocation);
      const int height = allocation.height - 2 * inset;
      const int width = allocation.width - 2 * inset;

      c->mouse_x = CLAMP(event->x - inset, 0, width) / (float)width;
      c->mouse_y = 1.0 - CLAMP(event->y - inset, 0, height) / (float)height;

      const float mx = _mouse_to_curve(c->mouse_x, c->zoom_factor, c->offset_x);

      // don't add a node too close to others in x direction, it can crash dt
      int selected = -1;
      if(curve[0].x > mx)
        selected = 0;
      else
      {
        for(int k = 1; k < nodes; k++)
        {
          if(curve[k].x > mx)
          {
            selected = k;
            break;
          }
        }
      }
      if(selected == -1) selected = nodes;

      // evaluate the curve at the current x position
      const float y = dt_draw_curve_calc_value(c->minmax_curve[ch], mx);

      if(y >= 0.0f && y <= 1.0f) // never add something outside the viewport, you couldn't change it afterwards
      {
        // create a new node
        selected = _add_node(curve, &p->curve_num_nodes[ch], mx, y);

        // maybe set the new one as being selected
        const float min = .04f * .04f; // comparing against square

        for(int k = 0; k < nodes; k++)
        {
          const float other_y = _curve_to_mouse(curve[k].y, c->zoom_factor, c->offset_y);
          const float dist = (y - other_y) * (y - other_y);
          if(dist < min) c->selected = selected;
        }

        if(c->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES)
          dt_iop_color_picker_reset(self, TRUE);
        dt_dev_add_history_item(darktable.develop, self, TRUE);
        gtk_widget_queue_draw(self->widget);
      }

      return TRUE;
    }
    else if(event->type == GDK_2BUTTON_PRESS)
    {
      // reset current curve
      p->curve_num_nodes[ch] = d->curve_num_nodes[ch];
      p->curve_type[ch] = d->curve_type[ch];
      for(int k = 0; k < DT_IOP_COLORZONES_MAXNODES; k++)
      {
        p->curve[c->channel][k].x = d->curve[c->channel][k].x;
        p->curve[c->channel][k].y = d->curve[c->channel][k].y;
      }

      c->selected = -2; // avoid motion notify re-inserting immediately.
      dt_bauhaus_combobox_set(c->interpolator, p->curve_type[ch]);

      if(c->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES)
        dt_iop_color_picker_reset(self, TRUE);
      dt_dev_add_history_item(darktable.develop, self, TRUE);
      gtk_widget_queue_draw(self->widget);

      return TRUE;
    }
  }
  else if(event->button == 3 && c->selected >= 0)
  {
    if(c->selected == 0 || c->selected == nodes - 1)
    {
      if(p->channel == DT_IOP_COLORZONES_h)
      {
        curve[0].y = 0.5f;
        curve[0].x = 0.f;
        curve[nodes - 1].y = 0.5f;
        curve[nodes - 1].x = 1.f;
      }
      else
      {
        const float reset_value = c->selected == 0 ? 0.f : 1.f;
        curve[c->selected].y = 0.5f;
        curve[c->selected].x = reset_value;
      }

      if(c->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES)
        dt_iop_color_picker_reset(self, TRUE);
      gtk_widget_queue_draw(self->widget);
      dt_dev_add_history_item(darktable.develop, self, TRUE);
      return TRUE;
    }

    // ctrl+right click reset the node to y-zero
    if((event->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
    {
      curve[c->selected].y = 0.5f;
    }
    // right click deletes the node
    else
    {
      for(int k = c->selected; k < nodes - 1; k++)
      {
        curve[k].x = curve[k + 1].x;
        curve[k].y = curve[k + 1].y;
      }
      p->curve_num_nodes[ch]--;
    }
    c->selected = -2; // avoid re-insertion of that point immediately after this

    if(c->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES) dt_iop_color_picker_reset(self, TRUE);
    gtk_widget_queue_draw(self->widget);
    dt_dev_add_history_item(darktable.develop, self, TRUE);
    return TRUE;
  }

  return FALSE;
}

static gboolean _area_button_release_callback(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  if(darktable.develop->darkroom_skip_mouse_events) return TRUE;

  if(event->button == 1)
  {
    dt_iop_module_t *self = (dt_iop_module_t *)user_data;
    dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
    c->dragging = 0;
    return TRUE;
  }
  return FALSE;
}

static gboolean _area_enter_notify_callback(GtkWidget *widget, GdkEventCrossing *event, dt_iop_module_t *self)
{
  if(darktable.develop->darkroom_skip_mouse_events) return TRUE;

  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  c->mouse_y = fabs(c->mouse_y);
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean _area_leave_notify_callback(GtkWidget *widget, GdkEventCrossing *event, dt_iop_module_t *self)
{
  if(darktable.develop->darkroom_skip_mouse_events) return TRUE;

  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  // for fluxbox
  c->mouse_y = -fabs(c->mouse_y);
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean _area_resized_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
  GtkRequisition r;
  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  r.width = allocation.width;
  r.height = allocation.width;
  gtk_widget_get_preferred_size(widget, &r, NULL);
  return TRUE;
}

static gboolean _area_key_press_callback(GtkWidget *widget, GdkEventKey *event, dt_iop_module_t *self)
{
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;

  if(darktable.develop->darkroom_skip_mouse_events) return TRUE;

  if(c->selected < 0) return TRUE;

  int handled = 0;
  float dx = 0.0f, dy = 0.0f;
  if(event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_KP_Up)
  {
    handled = 1;
    dy = DT_IOP_COLORZONES_DEFAULT_STEP;
  }
  else if(event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_KP_Down)
  {
    handled = 1;
    dy = -DT_IOP_COLORZONES_DEFAULT_STEP;
  }
  else if(event->keyval == GDK_KEY_Right || event->keyval == GDK_KEY_KP_Right)
  {
    handled = 1;
    dx = DT_IOP_COLORZONES_DEFAULT_STEP;
  }
  else if(event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_KP_Left)
  {
    handled = 1;
    dx = -DT_IOP_COLORZONES_DEFAULT_STEP;
  }

  if(!handled) return TRUE;

  if(c->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES) dt_iop_color_picker_reset(self, TRUE);
  return _move_point_internal(self, widget, dx, dy, event->state);
}

static void _channel_tabs_switch_callback(GtkNotebook *notebook, GtkWidget *page, guint page_num,
                                          dt_iop_module_t *self)
{
  if(self->dt->gui->reset) return;
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;

  c->channel = (dt_iop_colorzones_channel_t)page_num;

  const int reset = self->dt->gui->reset;
  self->dt->gui->reset = 1;

  dt_bauhaus_combobox_set(c->interpolator, p->curve_type[c->channel]);

  self->dt->gui->reset = reset;

  if(c->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES) dt_iop_color_picker_reset(self, TRUE);
  if(c->display_mask) dt_dev_reprocess_center(self->dev);
  gtk_widget_queue_draw(self->widget);
}

static gboolean _color_picker_callback_button_press(GtkWidget *widget, GdkEventButton *e, dt_iop_module_t *module)
{
  if(darktable.gui->reset) return FALSE;

  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)module->gui_data;
  dt_iop_color_picker_t *color_picker = &c->color_picker;

  if(widget == c->colorpicker)
    color_picker->kind = DT_COLOR_PICKER_POINT_AREA;
  else
    color_picker->kind = DT_COLOR_PICKER_AREA;

  GdkModifierType modifiers = gtk_accelerator_get_default_mod_mask();
  if((e->state & modifiers) == GDK_CONTROL_MASK) // flat=0, lower=-1, upper=1
    c->picker_set_upper_lower = 1;
  else if((e->state & modifiers) == GDK_SHIFT_MASK)
    c->picker_set_upper_lower = -1;
  else
    c->picker_set_upper_lower = 0;

  return dt_iop_color_picker_callback_button_press(widget, e, color_picker);
}

static void _select_by_callback(GtkWidget *widget, dt_iop_module_t *self)
{
  if(self->dt->gui->reset) return;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;
  dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)self->gui_data;

  memcpy(p, self->default_params, sizeof(dt_iop_colorzones_params_t));
  p->channel = 2 - (dt_iop_colorzones_channel_t)dt_bauhaus_combobox_get(widget);

  if(g->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES) dt_iop_color_picker_reset(self, TRUE);
  if(g->display_mask) _reset_display_selection(self);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
  gtk_widget_queue_draw(self->widget);
}

static void _strength_changed_callback(GtkWidget *slider, dt_iop_module_t *self)
{
  if(self->dt->gui->reset) return;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;
  dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)self->gui_data;

  p->strength = dt_bauhaus_slider_get(slider);

  if(g->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES) dt_iop_color_picker_reset(self, TRUE);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void _interpolator_callback(GtkWidget *widget, dt_iop_module_t *self)
{
  if(darktable.gui->reset) return;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;
  dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)self->gui_data;

  const int combo = dt_bauhaus_combobox_get(widget);

  if(combo == 0)
    p->curve_type[g->channel] = CUBIC_SPLINE;
  else if(combo == 1)
    p->curve_type[g->channel] = CATMULL_ROM;
  else if(combo == 2)
    p->curve_type[g->channel] = MONOTONE_HERMITE;

  if(g->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES) dt_iop_color_picker_reset(self, TRUE);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
  gtk_widget_queue_draw(GTK_WIDGET(g->area));
}

static void _edit_by_area_callback(GtkWidget *widget, dt_iop_module_t *self)
{
  if(darktable.gui->reset) return;
  dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)self->gui_data;

  g->edit_by_area = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  gtk_widget_queue_draw(GTK_WIDGET(g->area));
}

static void _mode_callback(GtkWidget *widget, dt_iop_module_t *self)
{
  if(darktable.gui->reset) return;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;
  dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)self->gui_data;

  p->mode = dt_bauhaus_combobox_get(widget);

  if(g->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES) dt_iop_color_picker_reset(self, TRUE);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
  gtk_widget_queue_draw(GTK_WIDGET(g->area));
}

static void _display_mask_callback(GtkToggleButton *togglebutton, dt_iop_module_t *module)
{
  if(darktable.gui->reset) return;

  dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)module->gui_data;

  // if blend module is displaying mask do not display it here
  if(module->request_mask_display && !g->display_mask)
  {
    dt_control_log(_("cannot display masks when the blending mask is displayed"));

    const int reset = darktable.gui->reset;
    darktable.gui->reset = 1;
    gtk_toggle_button_set_active(togglebutton, FALSE);
    darktable.gui->reset = reset;
    return;
  }

  g->display_mask = gtk_toggle_button_get_active(togglebutton);

  if(module->off) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(module->off), 1);
  dt_iop_request_focus(module);

  dt_dev_reprocess_center(module->dev);
}

static void _iop_color_picker_apply(dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  if(g->color_picker.current_picker == DT_IOP_COLORZONES_PICK_SET_VALUES)
  {
    dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;
    dt_iop_colorzones_params_t *d = (dt_iop_colorzones_params_t *)self->default_params;

    const int ch_curve = g->channel;
    const int ch_val = p->channel;
    dt_iop_colorzones_node_t *curve = p->curve[ch_curve];

    // reset current curve
    p->curve_num_nodes[ch_curve] = d->curve_num_nodes[ch_curve];
    p->curve_type[ch_curve] = d->curve_type[ch_curve];
    for(int k = 0; k < DT_IOP_COLORZONES_MAXNODES; k++)
    {
      curve[k].x = d->curve[ch_curve][k].x;
      curve[k].y = d->curve[ch_curve][k].y;
    }

    // now add 5 nodes: feather, min, center, max, feather
    const float feather = 0.02f;
    const float increment = 0.1f * g->picker_set_upper_lower;
    float x = 0.f;

    if(ch_val == DT_IOP_COLORZONES_L)
      x = self->picked_color_min[0] / 100.f;
    else if(ch_val == DT_IOP_COLORZONES_C)
      x = self->picked_color_min[1] / (128.f * sqrtf(2.f));
    else if(ch_val == DT_IOP_COLORZONES_h)
      x = self->picked_color_min[2];
    x -= feather;
    if(x > 0.f && x < 1.f) _add_node(curve, &p->curve_num_nodes[ch_curve], x, .5f);

    if(ch_val == DT_IOP_COLORZONES_L)
      x = self->picked_color_min[0] / 100.f;
    else if(ch_val == DT_IOP_COLORZONES_C)
      x = self->picked_color_min[1] / (128.f * sqrtf(2.f));
    else if(ch_val == DT_IOP_COLORZONES_h)
      x = self->picked_color_min[2];
    if(x > 0.f && x < 1.f) _add_node(curve, &p->curve_num_nodes[ch_curve], x, .5f + increment);

    if(ch_val == DT_IOP_COLORZONES_L)
      x = self->picked_color[0] / 100.f;
    else if(ch_val == DT_IOP_COLORZONES_C)
      x = self->picked_color[1] / (128.f * sqrtf(2.f));
    else if(ch_val == DT_IOP_COLORZONES_h)
      x = self->picked_color[2];
    if(x > 0.f && x < 1.f) _add_node(curve, &p->curve_num_nodes[ch_curve], x, .5f + 2.f * increment);

    if(ch_val == DT_IOP_COLORZONES_L)
      x = self->picked_color_max[0] / 100.f;
    else if(ch_val == DT_IOP_COLORZONES_C)
      x = self->picked_color_max[1] / (128.f * sqrtf(2.f));
    else if(ch_val == DT_IOP_COLORZONES_h)
      x = self->picked_color_max[2];
    if(x > 0.f && x < 1.f) _add_node(curve, &p->curve_num_nodes[ch_curve], x, .5f + increment);

    if(ch_val == DT_IOP_COLORZONES_L)
      x = self->picked_color_max[0] / 100.f;
    else if(ch_val == DT_IOP_COLORZONES_C)
      x = self->picked_color_max[1] / (128.f * sqrtf(2.f));
    else if(ch_val == DT_IOP_COLORZONES_h)
      x = self->picked_color_max[2];
    x += feather;
    if(x > 0.f && x < 1.f) _add_node(curve, &p->curve_num_nodes[ch_curve], x, .5f);

    // avoid recursion
    self->picker->skip_apply = TRUE;

    dt_dev_add_history_item(darktable.develop, self, TRUE);
  }

  dt_control_queue_redraw_widget(self->widget);
}

static int _iop_color_picker_get_set(dt_iop_module_t *self, GtkWidget *button)
{
  dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  const int current_picker = g->color_picker.current_picker;

  g->color_picker.current_picker = DT_IOP_COLORZONES_PICK_NONE;

  if(button == g->colorpicker)
    g->color_picker.current_picker = DT_IOP_COLORZONES_PICK_COLORPICK;
  else if(button == g->colorpicker_set_values)
    g->color_picker.current_picker = DT_IOP_COLORZONES_PICK_SET_VALUES;

  if(current_picker == g->color_picker.current_picker)
    return DT_COLOR_PICKER_ALREADY_SELECTED;
  else
    return g->color_picker.current_picker;
}

static void _iop_color_picker_update(dt_iop_module_t *self)
{
  dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  const int which_colorpicker = g->color_picker.current_picker;
  const int reset = darktable.gui->reset;
  darktable.gui->reset = 1;

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->colorpicker),
                               which_colorpicker == DT_IOP_COLORZONES_PICK_COLORPICK);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->colorpicker_set_values),
                               which_colorpicker == DT_IOP_COLORZONES_PICK_SET_VALUES);

  darktable.gui->reset = reset;
  dt_control_queue_redraw_widget(self->widget);
}

void gui_reset(struct dt_iop_module_t *self)
{
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;

  dt_iop_color_picker_reset(self, TRUE);

  c->zoom_factor = 1.f;
  _reset_display_selection(self);
}

void gui_focus(struct dt_iop_module_t *self, gboolean in)
{
  if(!in)
  {
    dt_iop_color_picker_reset(self, TRUE);
    _reset_display_selection(self);
  }
}

void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_colorzones_gui_data_t));
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;

  self->histogram_cst = iop_cs_LCh;

  c->channel = dt_conf_get_int("plugins/darkroom/colorzones/gui_channel");
  for(int ch = 0; ch < DT_IOP_COLORZONES_MAX_CHANNELS; ch++)
  {
    c->minmax_curve[ch] = dt_draw_curve_new(0.0, 1.0, p->curve_type[ch]);
    c->minmax_curve_nodes[ch] = p->curve_num_nodes[ch];
    c->minmax_curve_type[ch] = p->curve_type[ch];

    (void)dt_draw_curve_add_point(c->minmax_curve[ch], p->curve[ch][p->curve_num_nodes[ch] - 2].x - 1.0,
                                  p->curve[ch][p->curve_num_nodes[ch] - 2].y);
    for(int k = 0; k < p->curve_num_nodes[ch]; k++)
      (void)dt_draw_curve_add_point(c->minmax_curve[ch], p->curve[ch][k].x, p->curve[ch][k].y);
    (void)dt_draw_curve_add_point(c->minmax_curve[ch], p->curve[ch][1].x + 1.0, p->curve[ch][1].y);
  }

  c->mouse_x = c->mouse_y = -1.0;
  c->selected = -1;
  c->offset_x = c->offset_y = 0.f;
  c->zoom_factor = 1.f;
  c->picker_set_upper_lower = 0;
  c->x_move = -1;
  c->mouse_radius = 1.0 / DT_IOP_COLORZONES_BANDS;
  c->dragging = 0;
  c->edit_by_area = 0;
  c->display_mask = FALSE;

  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, DT_BAUHAUS_SPACE);
  dt_gui_add_help_link(self->widget, dt_get_help_url(self->op));

  // tabs
  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

  c->channel_tabs = GTK_NOTEBOOK(gtk_notebook_new());

  gtk_notebook_append_page(GTK_NOTEBOOK(c->channel_tabs),
                           GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0)),
                           gtk_label_new(_("lightness")));
  gtk_notebook_append_page(GTK_NOTEBOOK(c->channel_tabs),
                           GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0)),
                           gtk_label_new(_("saturation")));
  gtk_notebook_append_page(GTK_NOTEBOOK(c->channel_tabs),
                           GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0)), gtk_label_new(_("hue")));

  gtk_widget_show_all(GTK_WIDGET(gtk_notebook_get_nth_page(c->channel_tabs, c->channel)));
  gtk_notebook_set_current_page(GTK_NOTEBOOK(c->channel_tabs), c->channel);
  g_signal_connect(G_OBJECT(c->channel_tabs), "switch_page", G_CALLBACK(_channel_tabs_switch_callback), self);
  gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(c->channel_tabs), FALSE, FALSE, 0);

  // color pickers
  c->colorpicker_set_values = dtgtk_togglebutton_new(dtgtk_cairo_paint_colorpicker_set_values,
                                                     CPF_STYLE_FLAT | CPF_DO_NOT_USE_BORDER, NULL);
  gtk_widget_set_tooltip_text(c->colorpicker_set_values, _("create a curve based on an area from the image\n"
                                                           "click to create a flat curve\n"
                                                           "ctrl+click to create a positive curve\n"
                                                           "shift+click to create a negative curve"));
  gtk_widget_set_size_request(GTK_WIDGET(c->colorpicker_set_values), DT_PIXEL_APPLY_DPI(14),
                              DT_PIXEL_APPLY_DPI(14));
  g_signal_connect(G_OBJECT(c->colorpicker_set_values), "button-press-event",
                   G_CALLBACK(_color_picker_callback_button_press), self);
  gtk_box_pack_end(GTK_BOX(hbox), GTK_WIDGET(c->colorpicker_set_values), FALSE, FALSE, 0);

  c->colorpicker
      = dtgtk_togglebutton_new(dtgtk_cairo_paint_colorpicker, CPF_STYLE_FLAT | CPF_DO_NOT_USE_BORDER, NULL);
  gtk_widget_set_tooltip_text(c->colorpicker, _("pick GUI color from image\nctrl+click to select an area"));
  g_signal_connect(G_OBJECT(c->colorpicker), "button-press-event", G_CALLBACK(_color_picker_callback_button_press),
                   self);
  gtk_box_pack_end(GTK_BOX(hbox), c->colorpicker, FALSE, FALSE, 0);

  // the nice graph
  c->area = GTK_DRAWING_AREA(dtgtk_drawing_area_new_with_aspect_ratio(1.0));
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(c->area), TRUE, TRUE, 0);

  GtkWidget *dabox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_name(GTK_WIDGET(dabox), "iop-bottom-bar");
  c->bottom_area = gtk_drawing_area_new();
  gtk_box_pack_start(GTK_BOX(dabox), GTK_WIDGET(c->bottom_area), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(dabox), TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(vbox), TRUE, TRUE, 0);

  GtkWidget *hbox_select_by = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

  // edit by area
  c->chk_edit_by_area = gtk_check_button_new_with_label(_("edit by area"));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c->chk_edit_by_area), c->edit_by_area);
  gtk_widget_set_tooltip_text(c->chk_edit_by_area, _("edit the curve nodes by area"));
  gtk_box_pack_start(GTK_BOX(hbox_select_by), c->chk_edit_by_area, TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(c->chk_edit_by_area), "toggled", G_CALLBACK(_edit_by_area_callback), self);

  // display selection
  c->bt_showmask
      = dtgtk_togglebutton_new(dtgtk_cairo_paint_showmask, CPF_STYLE_FLAT | CPF_DO_NOT_USE_BORDER, NULL);
  g_object_set(G_OBJECT(c->bt_showmask), "tooltip-text", _("display selection"), (char *)NULL);
  g_signal_connect(G_OBJECT(c->bt_showmask), "toggled", G_CALLBACK(_display_mask_callback), self);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c->bt_showmask), FALSE);
  gtk_box_pack_end(GTK_BOX(hbox_select_by), c->bt_showmask, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(self->widget), hbox_select_by, TRUE, TRUE, 0);

  // select by which dimension
  c->select_by = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(c->select_by, NULL, _("select by"));
  gtk_widget_set_tooltip_text(c->select_by, _("choose selection criterion, will be the abscissa in the graph"));
  dt_bauhaus_combobox_add(c->select_by, _("hue"));
  dt_bauhaus_combobox_add(c->select_by, _("saturation"));
  dt_bauhaus_combobox_add(c->select_by, _("lightness"));
  gtk_box_pack_start(GTK_BOX(self->widget), c->select_by, TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(c->select_by), "value-changed", G_CALLBACK(_select_by_callback), (gpointer)self);

  c->mode = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(c->mode, NULL, _("process mode"));
  dt_bauhaus_combobox_add(c->mode, _("smooth"));
  dt_bauhaus_combobox_add(c->mode, _("strong"));
  gtk_box_pack_start(GTK_BOX(self->widget), c->mode, TRUE, TRUE, 0);
  gtk_widget_set_tooltip_text(c->mode, _("choose between a smoother or stronger effect"));
  g_signal_connect(G_OBJECT(c->mode), "value-changed", G_CALLBACK(_mode_callback), self);

  c->strength = dt_bauhaus_slider_new_with_range(self, -200, 200.0, 10.0, p->strength, 1);
  dt_bauhaus_slider_set_format(c->strength, "%.01f%%");
  dt_bauhaus_widget_set_label(c->strength, NULL, _("mix"));
  gtk_widget_set_tooltip_text(c->strength, _("make effect stronger or weaker"));
  g_signal_connect(G_OBJECT(c->strength), "value-changed", G_CALLBACK(_strength_changed_callback), (gpointer)self);
  gtk_box_pack_start(GTK_BOX(self->widget), c->strength, TRUE, TRUE, 0);

  gtk_widget_add_events(GTK_WIDGET(c->area), GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK
                                                 | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                                                 | GDK_LEAVE_NOTIFY_MASK | GDK_SCROLL_MASK
                                                 | darktable.gui->scroll_mask);
  gtk_widget_set_can_focus(GTK_WIDGET(c->area), TRUE);
  g_signal_connect(G_OBJECT(c->area), "draw", G_CALLBACK(_area_draw_callback), self);
  g_signal_connect(G_OBJECT(c->area), "button-press-event", G_CALLBACK(_area_button_press_callback), self);
  g_signal_connect(G_OBJECT(c->area), "button-release-event", G_CALLBACK(_area_button_release_callback), self);
  g_signal_connect(G_OBJECT(c->area), "motion-notify-event", G_CALLBACK(_area_motion_notify_callback), self);
  g_signal_connect(G_OBJECT(c->area), "leave-notify-event", G_CALLBACK(_area_leave_notify_callback), self);
  g_signal_connect(G_OBJECT(c->area), "enter-notify-event", G_CALLBACK(_area_enter_notify_callback), self);
  g_signal_connect(G_OBJECT(c->area), "scroll-event", G_CALLBACK(_area_scrolled_callback), self);
  g_signal_connect(G_OBJECT(c->area), "configure-event", G_CALLBACK(_area_resized_callback), self);
  g_signal_connect(G_OBJECT(c->area), "key-press-event", G_CALLBACK(_area_key_press_callback), self);

  gtk_widget_add_events(GTK_WIDGET(c->bottom_area), GDK_BUTTON_PRESS_MASK);
  g_signal_connect(G_OBJECT(c->bottom_area), "draw", G_CALLBACK(_bottom_area_draw_callback), self);
  g_signal_connect(G_OBJECT(c->bottom_area), "button-press-event", G_CALLBACK(_bottom_area_button_press_callback),
                   self);

  /* From src/common/curve_tools.h :
    #define CUBIC_SPLINE 0
    #define CATMULL_ROM 1
    #define MONOTONE_HERMITE 2
  */
  c->interpolator = dt_bauhaus_combobox_new(self);
  dt_bauhaus_widget_set_label(c->interpolator, NULL, _("interpolation method"));
  dt_bauhaus_combobox_add(c->interpolator, _("cubic spline"));
  dt_bauhaus_combobox_add(c->interpolator, _("centripetal spline"));
  dt_bauhaus_combobox_add(c->interpolator, _("monotonic spline"));
  gtk_box_pack_start(GTK_BOX(self->widget), c->interpolator, TRUE, TRUE, 0);
  gtk_widget_set_tooltip_text(
      c->interpolator,
      _("change this method if you see oscillations or cusps in the curve\n"
        "- cubic spline is better to produce smooth curves but oscillates when nodes are too close\n"
        "- centripetal is better to avoids cusps and oscillations with close nodes but is less smooth\n"
        "- monotonic is better for accuracy of pure analytical functions (log, gamma, exp)\n"));
  g_signal_connect(G_OBJECT(c->interpolator), "value-changed", G_CALLBACK(_interpolator_callback), self);

  dt_iop_init_picker(&c->color_picker, self, DT_COLOR_PICKER_POINT_AREA, _iop_color_picker_get_set,
                     _iop_color_picker_apply, _iop_color_picker_update);
  dt_iop_color_picker_set_cst(&c->color_picker, iop_cs_LCh);
}

void gui_update(struct dt_iop_module_t *self)
{
  dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)self->params;

  dt_bauhaus_combobox_set(g->select_by, 2 - p->channel);
  dt_bauhaus_slider_set(g->strength, p->strength);
  dt_bauhaus_combobox_set(g->interpolator, p->curve_type[g->channel]);
  dt_bauhaus_combobox_set(g->mode, p->mode);

  gtk_widget_queue_draw(self->widget);
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  dt_iop_colorzones_gui_data_t *c = (dt_iop_colorzones_gui_data_t *)self->gui_data;
  dt_conf_set_int("plugins/darkroom/colorzones/gui_channel", c->channel);

  for(int ch = 0; ch < DT_IOP_COLORZONES_MAX_CHANNELS; ch++) dt_draw_curve_destroy(c->minmax_curve[ch]);

  free(self->gui_data);
  self->gui_data = NULL;
}

void init_global(dt_iop_module_so_t *module)
{
  const int program = 2; // basic.cl, from programs.conf
  dt_iop_colorzones_global_data_t *gd
      = (dt_iop_colorzones_global_data_t *)malloc(sizeof(dt_iop_colorzones_global_data_t));
  module->data = gd;
  gd->kernel_colorzones = dt_opencl_create_kernel(program, "colorzones");
  gd->kernel_colorzones_v3 = dt_opencl_create_kernel(program, "colorzones_v3");
}

void cleanup_global(dt_iop_module_so_t *module)
{
  dt_iop_colorzones_global_data_t *gd = (dt_iop_colorzones_global_data_t *)module->data;

  dt_opencl_free_kernel(gd->kernel_colorzones);
  dt_opencl_free_kernel(gd->kernel_colorzones_v3);

  free(module->data);
  module->data = NULL;
}

void commit_params(struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe,
                   dt_dev_pixelpipe_iop_t *piece)
{
  // pull in new params to pipe
  dt_iop_colorzones_data_t *d = (dt_iop_colorzones_data_t *)(piece->data);
  dt_iop_colorzones_params_t *p = (dt_iop_colorzones_params_t *)p1;
  dt_iop_colorzones_gui_data_t *g = (dt_iop_colorzones_gui_data_t *)self->gui_data;

  if(pipe->type == DT_DEV_PIXELPIPE_PREVIEW)
    piece->request_histogram |= (DT_REQUEST_ON);
  else
    piece->request_histogram &= ~(DT_REQUEST_ON);

#if 0 // print new preset
  printf("p.channel = %d;\n", p->channel);
  for(int k=0; k<3; k++) for(int i=0; i<DT_IOP_COLORZONES_MAXNODES; i++)
    {
      printf("p.curve[%d][%i].x = %f;\n", k, i, p->curve[k][i].x);
      printf("p.curve[%d][%i].y = %f;\n", k, i, p->curve[k][i].y);
    }
#endif

  // display selection don't work with opencl
  piece->process_cl_ready = (g && g->display_mask) ? 0: 1;
  d->channel = (dt_iop_colorzones_channel_t)p->channel;
  d->mode = p->mode;
  for(int ch = 0; ch < DT_IOP_COLORZONES_MAX_CHANNELS; ch++)
  {
    // take care of possible change of curve type or number of nodes (not yet implemented in UI)
    if(d->curve_type[ch] != p->curve_type[ch] || d->curve_nodes[ch] != p->curve_num_nodes[ch])
    {
      dt_draw_curve_destroy(d->curve[ch]);
      d->curve[ch] = dt_draw_curve_new(0.0, 1.0, p->curve_type[ch]);
      d->curve_nodes[ch] = p->curve_num_nodes[ch];
      d->curve_type[ch] = p->curve_type[ch];

      if(d->channel == DT_IOP_COLORZONES_h)
        (void)dt_draw_curve_add_point(d->curve[ch], p->curve[ch][p->curve_num_nodes[ch] - 2].x - 1.0,
                                      strength(p->curve[ch][p->curve_num_nodes[ch] - 2].y, p->strength));
      else
        (void)dt_draw_curve_add_point(d->curve[ch], p->curve[ch][p->curve_num_nodes[ch] - 2].x - 1.0,
                                      strength(p->curve[ch][0].y, p->strength));
      for(int k = 0; k < p->curve_num_nodes[ch]; k++)
        (void)dt_draw_curve_add_point(d->curve[ch], p->curve[ch][k].x, strength(p->curve[ch][k].y, p->strength));
      if(d->channel == DT_IOP_COLORZONES_h)
        (void)dt_draw_curve_add_point(d->curve[ch], p->curve[ch][1].x + 1.0,
                                      strength(p->curve[ch][1].y, p->strength));
      else
        (void)dt_draw_curve_add_point(d->curve[ch], p->curve[ch][1].x + 1.0,
                                      strength(p->curve[ch][p->curve_num_nodes[ch] - 1].y, p->strength));
    }
    else
    {
      if(d->channel == DT_IOP_COLORZONES_h)
        dt_draw_curve_set_point(d->curve[ch], 0, p->curve[ch][p->curve_num_nodes[ch] - 2].x - 1.0,
                                strength(p->curve[ch][p->curve_num_nodes[ch] - 2].y, p->strength));
      else
        dt_draw_curve_set_point(d->curve[ch], 0, p->curve[ch][p->curve_num_nodes[ch] - 2].x - 1.0,
                                strength(p->curve[ch][0].y, p->strength));
      for(int k = 0; k < p->curve_num_nodes[ch]; k++)
        dt_draw_curve_set_point(d->curve[ch], k + 1, p->curve[ch][k].x, strength(p->curve[ch][k].y, p->strength));
      if(d->channel == DT_IOP_COLORZONES_h)
        dt_draw_curve_set_point(d->curve[ch], p->curve_num_nodes[ch] + 1, p->curve[ch][1].x + 1.0,
                                strength(p->curve[ch][1].y, p->strength));
      else
        dt_draw_curve_set_point(d->curve[ch], p->curve_num_nodes[ch] + 1, p->curve[ch][1].x + 1.0,
                                strength(p->curve[ch][p->curve_num_nodes[ch] - 1].y, p->strength));
    }

    dt_draw_curve_calc_values(d->curve[ch], 0.0, 1.0, DT_IOP_COLORZONES_LUT_RES, NULL, d->lut[ch]);
  }
}

void init_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_colorzones_data_t *d = (dt_iop_colorzones_data_t *)malloc(sizeof(dt_iop_colorzones_data_t));
  dt_iop_colorzones_params_t *default_params = (dt_iop_colorzones_params_t *)self->default_params;
  piece->data = (void *)d;

  for(int ch = 0; ch < DT_IOP_COLORZONES_MAX_CHANNELS; ch++)
  {
    d->curve[ch] = dt_draw_curve_new(0.0, 1.0, default_params->curve_type[ch]);
    d->curve_nodes[ch] = default_params->curve_num_nodes[ch];
    d->curve_type[ch] = default_params->curve_type[ch];
    (void)dt_draw_curve_add_point(d->curve[ch],
                                  default_params->curve[ch][default_params->curve_num_nodes[ch] - 2].x - 1.0,
                                  default_params->curve[ch][default_params->curve_num_nodes[ch] - 2].y);
    for(int k = 0; k < default_params->curve_num_nodes[ch]; k++)
      (void)dt_draw_curve_add_point(d->curve[ch], default_params->curve[ch][k].x, default_params->curve[ch][k].y);
    (void)dt_draw_curve_add_point(d->curve[ch], default_params->curve[ch][1].x + 1.0,
                                  default_params->curve[ch][1].y);
  }
  d->channel = (dt_iop_colorzones_channel_t)default_params->channel;
  d->mode = default_params->mode;
}

void cleanup_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  // clean up everything again.
  dt_iop_colorzones_data_t *d = (dt_iop_colorzones_data_t *)(piece->data);

  for(int ch = 0; ch < DT_IOP_COLORZONES_MAX_CHANNELS; ch++) dt_draw_curve_destroy(d->curve[ch]);

  free(piece->data);
  piece->data = NULL;
}

void init(dt_iop_module_t *module)
{
  module->params = calloc(1, sizeof(dt_iop_colorzones_params_t));
  module->default_params = calloc(1, sizeof(dt_iop_colorzones_params_t));
  module->default_enabled = 0; // we're a rather slow and rare op.
  module->params_size = sizeof(dt_iop_colorzones_params_t);
  module->gui_data = NULL;
  module->request_histogram |= (DT_REQUEST_ON);

  dt_iop_colorzones_params_t tmp;
  for(int ch = 0; ch < DT_IOP_COLORZONES_MAX_CHANNELS; ch++)
  {
    tmp.curve_num_nodes[ch] = 2;
    tmp.curve_type[ch] = MONOTONE_HERMITE; // CUBIC_SPLINE, CATMULL_ROM

    for(int k = 0; k < tmp.curve_num_nodes[ch]; k++)
    {
      tmp.curve[ch][k].x = (float)k / (float)(tmp.curve_num_nodes[ch] - 1);
      tmp.curve[ch][k].y = 0.5f;
    }
  }
  tmp.strength = 0.0f;
  tmp.channel = DT_IOP_COLORZONES_h;
  tmp.mode = DT_IOP_COLORZONES_MODE_NEW;

  memcpy(module->params, &tmp, sizeof(dt_iop_colorzones_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_colorzones_params_t));
}

void cleanup(dt_iop_module_t *module)
{
  free(module->params);
  module->params = NULL;
}

#undef DT_IOP_COLORZONES_INSET
#undef DT_IOP_COLORZONES_CURVE_INFL
#undef DT_IOP_COLORZONES_RES
#undef DT_IOP_COLORZONES_LUT_RES
#undef DT_IOP_COLORZONES_BANDS
#undef DT_IOP_COLORZONES_MAXNODES
#undef DT_IOP_COLORZONES_DEFAULT_STEP
#undef DT_IOP_COLORZONES_MIN_X_DISTANCE

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
