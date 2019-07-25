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
#include "bauhaus/bauhaus.h"
#include "common/debug.h"
#include "common/opencl.h"
#include "control/conf.h"
#include "control/control.h"
#include "develop/imageop.h"
#include "develop/imageop_math.h"
#include "develop/tiling.h"
#include "dtgtk/drawingarea.h"
#include "gui/accelerators.h"
#include "gui/draw.h"
#include "gui/gtk.h"
#include "gui/presets.h"
#include "iop/iop_api.h"

#include <math.h>
#include <memory.h>
#include <stdlib.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

#define INSET DT_PIXEL_APPLY_DPI(5)
#define INFL .3f


DT_MODULE_INTROSPECTION(1, dt_iop_atrous_params_t)

#define BANDS 6
#define MAX_NUM_SCALES 8 // 2*2^(i+1) + 1 = 1025px support for i = 8
#define RES 64

#define dt_atrous_show_upper_label(cr, text, layout, ink)                                                    \
  pango_layout_set_text(layout, text, -1);                                                                   \
  pango_layout_get_pixel_extents(layout, &ink, NULL);                                                        \
  cairo_move_to(cr, .5 * (width - ink.width), (.08 * height) - ink.height);                                  \
  pango_cairo_show_layout(cr, layout);


#define dt_atrous_show_lower_label(cr, text, layout, ink)                                                    \
  pango_layout_set_text(layout, text, -1);                                                                   \
  pango_layout_get_pixel_extents(layout, &ink, NULL);                                                        \
  cairo_move_to(cr, .5 * (width - ink.width), (.98 * height) - ink.height);                                  \
  pango_cairo_show_layout(cr, layout);


typedef enum atrous_channel_t
{
  atrous_L = 0,  // luminance boost
  atrous_c = 1,  // chrominance boost
  atrous_s = 2,  // edge sharpness
  atrous_Lt = 3, // luminance noise threshold
  atrous_ct = 4, // chrominance noise threshold
  atrous_none = 5
} atrous_channel_t;

typedef struct dt_iop_atrous_params_t
{
  int32_t octaves;
  float x[atrous_none][BANDS], y[atrous_none][BANDS];
} dt_iop_atrous_params_t;

typedef struct dt_iop_atrous_gui_data_t
{
  GtkWidget *mix;
  GtkDrawingArea *area;
  GtkNotebook *channel_tabs;
  double mouse_x, mouse_y, mouse_pick;
  float mouse_radius;
  dt_iop_atrous_params_t drag_params;
  int dragging;
  int x_move;
  dt_draw_curve_t *minmax_curve;
  atrous_channel_t channel, channel2;
  float draw_xs[RES], draw_ys[RES];
  float draw_min_xs[RES], draw_min_ys[RES];
  float draw_max_xs[RES], draw_max_ys[RES];
  float band_hist[MAX_NUM_SCALES];
  float band_max;
  float sample[MAX_NUM_SCALES];
  int num_samples;
} dt_iop_atrous_gui_data_t;

typedef struct dt_iop_atrous_global_data_t
{
  int kernel_decompose;
  int kernel_synthesize;
} dt_iop_atrous_global_data_t;

typedef struct dt_iop_atrous_data_t
{
  // demosaic pattern
  int32_t octaves;
  dt_draw_curve_t *curve[atrous_none];
} dt_iop_atrous_data_t;


const char *name()
{
  return _("equalizer");
}

int default_group()
{
  return IOP_GROUP_CORRECT;
}

int flags()
{
  return IOP_FLAGS_SUPPORTS_BLENDING | IOP_FLAGS_ALLOW_TILING;
}

int default_colorspace(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  return iop_cs_Lab;
}

void init_key_accels(dt_iop_module_so_t *self)
{
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "mix"));
}

void connect_key_accels(dt_iop_module_t *self)
{
  dt_accel_connect_slider_iop(self, "mix", ((dt_iop_atrous_gui_data_t *)self->gui_data)->mix);
}


#if defined(__SSE2__)

#define ALIGNED(a) __attribute__((aligned(a)))
#define VEC4(a)                                                                                              \
  {                                                                                                          \
    (a), (a), (a), (a)                                                                                       \
  }

static const __m128 fone ALIGNED(64) = VEC4(0x3f800000u);
static const __m128 femo ALIGNED(64) = VEC4(0x00adf880u);
static const __m128 ooo1 ALIGNED(64) = { 0.f, 0.f, 0.f, 1.f };

/* SSE intrinsics version of dt_fast_expf defined in darktable.h */
static inline __m128 dt_fast_expf_sse2(const __m128 x)
{
  __m128 f = _mm_add_ps(fone, _mm_mul_ps(x, femo)); // f(n) = i1 + x(n)*(i2-i1)
  __m128i i = _mm_cvtps_epi32(f);                   // i(n) = int(f(n))
  __m128i mask = _mm_srai_epi32(i, 31);             // mask(n) = 0xffffffff if i(n) < 0
  i = _mm_andnot_si128(mask, i);                    // i(n) = 0 if i(n) < 0
  return _mm_castsi128_ps(i);                       // return *(float*)&i
}

#endif

static inline void weight(const float *c1, const float *c2, const float sharpen, float *weight)
{
  float square[3];
  for(int c = 0; c < 3; c++) square[c] = c1[c] - c2[c];
  for(int c = 0; c < 3; c++) square[c] = square[c] * square[c];

  const float wl = dt_fast_expf(-sharpen * square[0]);
  const float wc = dt_fast_expf(-sharpen * (square[1] + square[2]));

  weight[0] = wl;
  weight[1] = wc;
  weight[2] = wc;
  weight[3] = 1.0f;
}

#if defined(__SSE2__)
/* Computes the vector
 * (wl, wc, wc, 1)
 *
 * where:
 * wl = exp(-sharpen*SQR(c1[0] - c2[0]))
 *    = exp(-s*d1) (as noted in code comments below)
 * wc = exp(-sharpen*(SQR(c1[1] - c2[1]) + SQR(c1[2] - c2[2]))
 *    = exp(-s*(d2+d3)) (as noted in code comments below)
 */
static inline __m128 weight_sse2(const __m128 *c1, const __m128 *c2, const float sharpen)
{
  const __m128 vsharpen = _mm_set1_ps(-sharpen); // (-s, -s, -s, -s)
  __m128 diff = _mm_sub_ps(*c1, *c2);
  __m128 square = _mm_mul_ps(diff, diff);                                   // (?, d3, d2, d1)
  __m128 square2 = _mm_shuffle_ps(square, square, _MM_SHUFFLE(3, 1, 2, 0)); // (?, d2, d3, d1)
  __m128 added = _mm_add_ps(square, square2);                               // (?, d2+d3, d2+d3, 2*d1)
  added = _mm_sub_ss(added, square);                                        // (?, d2+d3, d2+d3, d1)
  __m128 sharpened = _mm_mul_ps(added, vsharpen);                   // (?, -s*(d2+d3), -s*(d2+d3), -s*d1)
  __m128 exp = dt_fast_expf_sse2(sharpened);                        // (?, wc, wc, wl)
  exp = _mm_castsi128_ps(_mm_slli_si128(_mm_castps_si128(exp), 4)); // (wc, wc, wl, 0)
  exp = _mm_castsi128_ps(_mm_srli_si128(_mm_castps_si128(exp), 4)); // (0, wc, wc, wl)
  exp = _mm_or_ps(exp, ooo1);                                       // (1, wc, wc, wl)
  return exp;
}
#endif

#define SUM_PIXEL_CONTRIBUTION_COMMON(ii, jj)                                                                \
  {                                                                                                          \
    const float f = filter[(ii)] * filter[(jj)];                                                             \
    float wp[4] = { 0.0f, 0.0f, 0.0f, 0.0f };                                                                \
    weight(px, px2, sharpen, wp);                                                                            \
    float w[4] = { 0.0f, 0.0f, 0.0f, 0.0f };                                                                 \
    for(int c = 0; c < 4; c++) w[c] = f * wp[c];                                                             \
    float pd[4] = { 0.0f, 0.0f, 0.0f, 0.0f };                                                                \
    for(int c = 0; c < 4; c++) pd[c] = w[c] * px2[c];                                                        \
    for(int c = 0; c < 4; c++) sum[c] += pd[c];                                                              \
    for(int c = 0; c < 4; c++) wgt[c] += w[c];                                                               \
  }

#if defined(__SSE2__)
#define SUM_PIXEL_CONTRIBUTION_COMMON_SSE2(ii, jj)                                                           \
  {                                                                                                          \
    const __m128 f = _mm_set1_ps(filter[(ii)] * filter[(jj)]);                                               \
    const __m128 wp = weight_sse2(px, px2, sharpen);                                                         \
    const __m128 w = _mm_mul_ps(f, wp);                                                                      \
    const __m128 pd = _mm_mul_ps(w, *px2);                                                                   \
    sum = _mm_add_ps(sum, pd);                                                                               \
    wgt = _mm_add_ps(wgt, w);                                                                                \
  }
#endif

#define SUM_PIXEL_CONTRIBUTION_WITH_TEST(ii, jj)                                                             \
  do                                                                                                         \
  {                                                                                                          \
    const int iii = (ii)-2;                                                                                  \
    const int jjj = (jj)-2;                                                                                  \
    int x = i + mult * iii;                                                                                  \
    int y = j + mult * jjj;                                                                                  \
                                                                                                             \
    if(x < 0) x = 0;                                                                                         \
    if(x >= width) x = width - 1;                                                                            \
    if(y < 0) y = 0;                                                                                         \
    if(y >= height) y = height - 1;                                                                          \
                                                                                                             \
    px2 = ((float *)in) + 4 * x + (size_t)4 * y * width;                                                     \
                                                                                                             \
    SUM_PIXEL_CONTRIBUTION_COMMON(ii, jj);                                                                   \
  } while(0)

#if defined(__SSE2__)
#define SUM_PIXEL_CONTRIBUTION_WITH_TEST_SSE2(ii, jj)                                                        \
  do                                                                                                         \
  {                                                                                                          \
    const int iii = (ii)-2;                                                                                  \
    const int jjj = (jj)-2;                                                                                  \
    int x = i + mult * iii;                                                                                  \
    int y = j + mult * jjj;                                                                                  \
                                                                                                             \
    if(x < 0) x = 0;                                                                                         \
    if(x >= width) x = width - 1;                                                                            \
    if(y < 0) y = 0;                                                                                         \
    if(y >= height) y = height - 1;                                                                          \
                                                                                                             \
    px2 = ((__m128 *)in) + x + (size_t)y * width;                                                            \
                                                                                                             \
    SUM_PIXEL_CONTRIBUTION_COMMON_SSE2(ii, jj);                                                              \
  } while(0)
#endif

#define ROW_PROLOGUE                                                                                         \
  const float *px = ((float *)in) + (size_t)4 * j * width;                                                   \
  const float *px2;                                                                                          \
  float *pdetail = detail + (size_t)4 * j * width;                                                           \
  float *pcoarse = out + (size_t)4 * j * width;

#if defined(__SSE2__)
#define ROW_PROLOGUE_SSE                                                                                     \
  const __m128 *px = ((__m128 *)in) + (size_t)j * width;                                                     \
  const __m128 *px2;                                                                                         \
  float *pdetail = detail + (size_t)4 * j * width;                                                           \
  float *pcoarse = out + (size_t)4 * j * width;
#endif

#define SUM_PIXEL_PROLOGUE                                                                                   \
  float sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };                                                                 \
  float wgt[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

#if defined(__SSE2__)
#define SUM_PIXEL_PROLOGUE_SSE                                                                               \
  __m128 sum = _mm_setzero_ps();                                                                             \
  __m128 wgt = _mm_setzero_ps();
#endif

#define SUM_PIXEL_EPILOGUE                                                                                   \
  for(int c = 0; c < 4; c++) sum[c] /= wgt[c];                                                               \
                                                                                                             \
  for(int c = 0; c < 4; c++) pdetail[c] = (px[c] - sum[c]);                                                  \
  for(int c = 0; c < 4; c++) pcoarse[c] = sum[c];                                                            \
  px += 4;                                                                                                   \
  pdetail += 4;                                                                                              \
  pcoarse += 4;

#if defined(__SSE2__)
#define SUM_PIXEL_EPILOGUE_SSE                                                                               \
  sum = _mm_mul_ps(sum, _mm_rcp_ps(wgt));                                                                    \
                                                                                                             \
  _mm_stream_ps(pdetail, _mm_sub_ps(*px, sum));                                                              \
  _mm_stream_ps(pcoarse, sum);                                                                               \
  px++;                                                                                                      \
  pdetail += 4;                                                                                              \
  pcoarse += 4;
#endif

typedef void((*eaw_decompose_t)(float *const out, const float *const in, float *const detail, const int scale,
                                const float sharpen, const int32_t width, const int32_t height));

static void eaw_decompose(float *const out, const float *const in, float *const detail, const int scale,
                          const float sharpen, const int32_t width, const int32_t height)
{
  const int mult = 1 << scale;
  static const float filter[5] = { 1.0f / 16.0f, 4.0f / 16.0f, 6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f };

/* The first "2*mult" lines use the macro with tests because the 5x5 kernel
 * requires nearest pixel interpolation for at least a pixel in the sum */
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(detail, filter, height, in, mult, out, sharpen, width) \
  schedule(static)
#endif
  for(int j = 0; j < 2 * mult; j++)
  {
    ROW_PROLOGUE

    for(int i = 0; i < width; i++)
    {
      SUM_PIXEL_PROLOGUE
      for(int jj = 0; jj < 5; jj++)
      {
        for(int ii = 0; ii < 5; ii++)
        {
          SUM_PIXEL_CONTRIBUTION_WITH_TEST(ii, jj);
        }
      }
      SUM_PIXEL_EPILOGUE
    }
  }

#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(detail, filter, height, in, mult, out, sharpen, width) \
  schedule(static)
#endif
  for(int j = 2 * mult; j < height - 2 * mult; j++)
  {
    ROW_PROLOGUE

    /* The first "2*mult" pixels use the macro with tests because the 5x5 kernel
     * requires nearest pixel interpolation for at least a pixel in the sum */
    for(int i = 0; i < 2 * mult; i++)
    {
      SUM_PIXEL_PROLOGUE
      for(int jj = 0; jj < 5; jj++)
      {
        for(int ii = 0; ii < 5; ii++)
        {
          SUM_PIXEL_CONTRIBUTION_WITH_TEST(ii, jj);
        }
      }
      SUM_PIXEL_EPILOGUE
    }

    /* For pixels [2*mult, width-2*mult], we can safely use macro w/o tests
     * to avoid unneeded branching in the inner loops */
    for(int i = 2 * mult; i < width - 2 * mult; i++)
    {
      SUM_PIXEL_PROLOGUE
      px2 = ((float *)in) + (size_t)4 * (i - 2 * mult + (size_t)(j - 2 * mult) * width);
      for(int jj = 0; jj < 5; jj++)
      {
        for(int ii = 0; ii < 5; ii++)
        {
          SUM_PIXEL_CONTRIBUTION_COMMON(ii, jj);
          px2 += (size_t)4 * mult;
        }
        px2 += (size_t)4 * (width - 5) * mult;
      }
      SUM_PIXEL_EPILOGUE
    }

    /* Last two pixels in the row require a slow variant... blablabla */
    for(int i = width - 2 * mult; i < width; i++)
    {
      SUM_PIXEL_PROLOGUE
      for(int jj = 0; jj < 5; jj++)
      {
        for(int ii = 0; ii < 5; ii++)
        {
          SUM_PIXEL_CONTRIBUTION_WITH_TEST(ii, jj);
        }
      }
      SUM_PIXEL_EPILOGUE
    }
  }

/* The last "2*mult" lines use the macro with tests because the 5x5 kernel
 * requires nearest pixel interpolation for at least a pixel in the sum */
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(detail, filter, height, in, mult, out, sharpen, width) \
  schedule(static)
#endif
  for(int j = height - 2 * mult; j < height; j++)
  {
    ROW_PROLOGUE

    for(int i = 0; i < width; i++)
    {
      SUM_PIXEL_PROLOGUE
      for(int jj = 0; jj < 5; jj++)
      {
        for(int ii = 0; ii < 5; ii++)
        {
          SUM_PIXEL_CONTRIBUTION_WITH_TEST(ii, jj);
        }
      }
      SUM_PIXEL_EPILOGUE
    }
  }
}

#undef SUM_PIXEL_CONTRIBUTION_COMMON
#undef SUM_PIXEL_CONTRIBUTION_WITH_TEST
#undef ROW_PROLOGUE
#undef SUM_PIXEL_PROLOGUE
#undef SUM_PIXEL_EPILOGUE

#if defined(__SSE2__)
static void eaw_decompose_sse2(float *const out, const float *const in, float *const detail, const int scale,
                               const float sharpen, const int32_t width, const int32_t height)
{
  const int mult = 1 << scale;
  static const float filter[5] = { 1.0f / 16.0f, 4.0f / 16.0f, 6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f };

/* The first "2*mult" lines use the macro with tests because the 5x5 kernel
 * requires nearest pixel interpolation for at least a pixel in the sum */
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(detail, filter, height, in, mult, out, sharpen, width) \
  schedule(static)
#endif
  for(int j = 0; j < 2 * mult; j++)
  {
    ROW_PROLOGUE_SSE

    for(int i = 0; i < width; i++)
    {
      SUM_PIXEL_PROLOGUE_SSE
      for(int jj = 0; jj < 5; jj++)
      {
        for(int ii = 0; ii < 5; ii++)
        {
          SUM_PIXEL_CONTRIBUTION_WITH_TEST_SSE2(ii, jj);
        }
      }
      SUM_PIXEL_EPILOGUE_SSE
    }
  }

#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(detail, filter, height, in, mult, out, sharpen, width) \
  schedule(static)
#endif
  for(int j = 2 * mult; j < height - 2 * mult; j++)
  {
    ROW_PROLOGUE_SSE

    /* The first "2*mult" pixels use the macro with tests because the 5x5 kernel
     * requires nearest pixel interpolation for at least a pixel in the sum */
    for(int i = 0; i < 2 * mult; i++)
    {
      SUM_PIXEL_PROLOGUE_SSE
      for(int jj = 0; jj < 5; jj++)
      {
        for(int ii = 0; ii < 5; ii++)
        {
          SUM_PIXEL_CONTRIBUTION_WITH_TEST_SSE2(ii, jj);
        }
      }
      SUM_PIXEL_EPILOGUE_SSE
    }

    /* For pixels [2*mult, width-2*mult], we can safely use macro w/o tests
     * to avoid unneeded branching in the inner loops */
    for(int i = 2 * mult; i < width - 2 * mult; i++)
    {
      SUM_PIXEL_PROLOGUE_SSE
      px2 = ((__m128 *)in) + i - 2 * mult + (size_t)(j - 2 * mult) * width;
      for(int jj = 0; jj < 5; jj++)
      {
        for(int ii = 0; ii < 5; ii++)
        {
          SUM_PIXEL_CONTRIBUTION_COMMON_SSE2(ii, jj);
          px2 += mult;
        }
        px2 += (width - 5) * mult;
      }
      SUM_PIXEL_EPILOGUE_SSE
    }

    /* Last two pixels in the row require a slow variant... blablabla */
    for(int i = width - 2 * mult; i < width; i++)
    {
      SUM_PIXEL_PROLOGUE_SSE
      for(int jj = 0; jj < 5; jj++)
      {
        for(int ii = 0; ii < 5; ii++)
        {
          SUM_PIXEL_CONTRIBUTION_WITH_TEST_SSE2(ii, jj);
        }
      }
      SUM_PIXEL_EPILOGUE_SSE
    }
  }

/* The last "2*mult" lines use the macro with tests because the 5x5 kernel
 * requires nearest pixel interpolation for at least a pixel in the sum */
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(detail, filter, height, in, mult, out, sharpen, width) \
  schedule(static)
#endif
  for(int j = height - 2 * mult; j < height; j++)
  {
    ROW_PROLOGUE_SSE

    for(int i = 0; i < width; i++)
    {
      SUM_PIXEL_PROLOGUE_SSE
      for(int jj = 0; jj < 5; jj++)
      {
        for(int ii = 0; ii < 5; ii++)
        {
          SUM_PIXEL_CONTRIBUTION_WITH_TEST_SSE2(ii, jj);
        }
      }
      SUM_PIXEL_EPILOGUE_SSE
    }
  }

  _mm_sfence();
}

#undef SUM_PIXEL_CONTRIBUTION_COMMON_SSE2
#undef SUM_PIXEL_CONTRIBUTION_WITH_TEST_SSE2
#undef ROW_PROLOGUE_SSE
#undef SUM_PIXEL_PROLOGUE_SSE
#undef SUM_PIXEL_EPILOGUE_SSE
#endif

typedef void((*eaw_synthesize_t)(float *const out, const float *const in, const float *const detail,
                                 const float *thrsf, const float *boostf, const int32_t width,
                                 const int32_t height));

static void eaw_synthesize(float *const out, const float *const in, const float *const detail,
                           const float *thrsf, const float *boostf, const int32_t width, const int32_t height)
{
  const float threshold[4] = { thrsf[0], thrsf[1], thrsf[2], thrsf[3] };
  const float boost[4] = { boostf[0], boostf[1], boostf[2], boostf[3] };

#ifdef _OPENMP
#pragma omp parallel for SIMD() default(none) \
  dt_omp_firstprivate(boost, detail, height, in, out, width, threshold) \
  schedule(static) \
  collapse(2)
#endif
  for(size_t k = 0; k < (size_t)4 * width * height; k += 4)
  {
    for(size_t c = 0; c < 4; c++)
    {
      const float absamt = fmaxf(0.0f, (fabsf(detail[k + c]) - threshold[c]));
      const float amount = copysignf(absamt, detail[k + c]);
      out[k + c] = in[k + c] + (boost[c] * amount);
    }
  }
}

#if defined(__SSE2__)
static void eaw_synthesize_sse2(float *const out, const float *const in, const float *const detail,
                                const float *thrsf, const float *boostf, const int32_t width,
                                const int32_t height)
{
  const __m128 threshold = _mm_set_ps(thrsf[3], thrsf[2], thrsf[1], thrsf[0]);
  const __m128 boost = _mm_set_ps(boostf[3], boostf[2], boostf[1], boostf[0]);

#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(boost, detail, height, in, out, threshold, width) \
  schedule(static)
#endif
  for(int j = 0; j < height; j++)
  {
    // TODO: prefetch? _mm_prefetch()
    const __m128 *pin = (__m128 *)in + (size_t)j * width;
    __m128 *pdetail = (__m128 *)detail + (size_t)j * width;
    float *pout = out + (size_t)4 * j * width;
    for(int i = 0; i < width; i++)
    {
      const __m128i maski = _mm_set1_epi32(0x80000000u);
      const __m128 *mask = (__m128 *)&maski;
      const __m128 absamt
          = _mm_max_ps(_mm_setzero_ps(), _mm_sub_ps(_mm_andnot_ps(*mask, *pdetail), threshold));
      const __m128 amount = _mm_or_ps(_mm_and_ps(*pdetail, *mask), absamt);
      _mm_stream_ps(pout, _mm_add_ps(*pin, _mm_mul_ps(boost, amount)));
      pdetail++;
      pin++;
      pout += 4;
    }
  }
  _mm_sfence();
}
#endif

static int get_samples(float *t, const dt_iop_atrous_data_t *const d, const dt_iop_roi_t *roi_in,
                       const dt_dev_pixelpipe_iop_t *const piece)
{
  const float scale = roi_in->scale;
  const float supp0
      = MIN(2 * (2 << (MAX_NUM_SCALES - 1)) + 1, MAX(piece->buf_in.height, piece->buf_in.width) * 0.2f);
  const float i0 = dt_log2f((supp0 - 1.0f) * .5f);
  int i = 0;
  for(; i < MAX_NUM_SCALES; i++)
  {
    // actual filter support on scaled buffer
    const float supp = 2 * (2 << i) + 1;
    // approximates this filter size on unscaled input image:
    const float supp_in = supp * (1.0f / scale);
    const float i_in = dt_log2f((supp_in - 1) * .5f) - 1.0f;
    t[i] = 1.0f - (i_in + .5f) / i0;
    if(t[i] < 0.0f) break;
  }
  return i;
}

static int get_scales(float (*thrs)[4], float (*boost)[4], float *sharp, const dt_iop_atrous_data_t *const d,
                      const dt_iop_roi_t *roi_in, const dt_dev_pixelpipe_iop_t *const piece)
{
  // we want coeffs to span max 20% of the image
  // finest is 5x5 filter
  //
  // 1:1 : w=20% buf_in.width                     w=5x5
  //     : ^ ...            ....            ....  ^
  // buf :  17x17  9x9  5x5     2*2^k+1
  // .....
  // . . . . .
  // .   .   .   .   .
  // cut off too fine ones, if image is not detailed enough (due to roi_in->scale)
  const float scale = roi_in->scale / piece->iscale;
  // largest desired filter on input buffer (20% of input dim)
  const float supp0
      = MIN(2 * (2 << (MAX_NUM_SCALES - 1)) + 1,
            MAX(piece->buf_in.height * piece->iscale, piece->buf_in.width * piece->iscale) * 0.2f);
  const float i0 = dt_log2f((supp0 - 1.0f) * .5f);
  int i = 0;
  for(; i < MAX_NUM_SCALES; i++)
  {
    // actual filter support on scaled buffer
    const float supp = 2 * (2 << i) + 1;
    // approximates this filter size on unscaled input image:
    const float supp_in = supp * (1.0f / scale);
    const float i_in = dt_log2f((supp_in - 1) * .5f) - 1.0f;
    // i_in = max_scale .. .. .. 0
    const float t = 1.0f - (i_in + .5f) / i0;
    boost[i][3] = boost[i][0] = 2.0f * dt_draw_curve_calc_value(d->curve[atrous_L], t);
    boost[i][1] = boost[i][2] = 2.0f * dt_draw_curve_calc_value(d->curve[atrous_c], t);
    for(int k = 0; k < 4; k++) boost[i][k] *= boost[i][k];
    thrs[i][0] = thrs[i][3] = powf(2.0f, -7.0f * (1.0f - t)) * 10.0f
                              * dt_draw_curve_calc_value(d->curve[atrous_Lt], t);
    thrs[i][1] = thrs[i][2] = powf(2.0f, -7.0f * (1.0f - t)) * 20.0f
                              * dt_draw_curve_calc_value(d->curve[atrous_ct], t);
    sharp[i] = 0.0025f * dt_draw_curve_calc_value(d->curve[atrous_s], t);
    // printf("scale %d boost %f %f thrs %f %f sharpen %f\n", i, boost[i][0], boost[i][2], thrs[i][0],
    // thrs[i][1], sharp[i]);
    if(t < 0.0f) break;
  }
  // ensure that return value max_scale is such that
  // 2 * 2 *(1 << max_scale) <= min(width, height)
  const int max_scale_roi = (int)floorf(dt_log2f((float)MIN(roi_in->width, roi_in->height))) - 2;
  return MIN(max_scale_roi, i);
}

/* just process the supplied image buffer, upstream default_process_tiling() does the rest */
static void process_wavelets(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece,
                             const void *const i, void *const o, const dt_iop_roi_t *const roi_in,
                             const dt_iop_roi_t *const roi_out, const eaw_decompose_t decompose,
                             const eaw_synthesize_t synthesize)
{
  dt_iop_atrous_data_t *d = (dt_iop_atrous_data_t *)piece->data;
  float thrs[MAX_NUM_SCALES][4];
  float boost[MAX_NUM_SCALES][4];
  float sharp[MAX_NUM_SCALES];
  const int max_scale = get_scales(thrs, boost, sharp, d, roi_in, piece);

  if(self->dev->gui_attached && piece->pipe->type == DT_DEV_PIXELPIPE_FULL)
  {
    dt_iop_atrous_gui_data_t *g = (dt_iop_atrous_gui_data_t *)self->gui_data;
    g->num_samples = get_samples(g->sample, d, roi_in, piece);
    // tries to acquire gdk lock and this prone to deadlock:
    // dt_control_queue_draw(GTK_WIDGET(g->area));
  }

  float *detail[MAX_NUM_SCALES] = { NULL };
  float *tmp = NULL;
  float *buf2 = NULL;
  float *buf1 = NULL;

  const int width = roi_out->width;
  const int height = roi_out->height;

  tmp = (float *)dt_alloc_align(64, (size_t)sizeof(float) * 4 * width * height);
  if(tmp == NULL)
  {
    fprintf(stderr, "[atrous] failed to allocate coarse buffer!\n");
    goto error;
  }

  for(int k = 0; k < max_scale; k++)
  {
    detail[k] = (float *)dt_alloc_align(64, (size_t)sizeof(float) * 4 * width * height);
    if(detail[k] == NULL)
    {
      fprintf(stderr, "[atrous] failed to allocate one of the detail buffers!\n");
      goto error;
    }
  }

  buf1 = (float *)i;
  buf2 = tmp;

  for(int scale = 0; scale < max_scale; scale++)
  {
    decompose(buf2, buf1, detail[scale], scale, sharp[scale], width, height);
    if(scale == 0) buf1 = (float *)o; // now switch to (float *)o for buffer ping-pong between buf1 and buf2
    float *buf3 = buf2;
    buf2 = buf1;
    buf1 = buf3;
  }

  for(int scale = max_scale - 1; scale >= 0; scale--)
  {
    synthesize(buf2, buf1, detail[scale], thrs[scale], boost[scale], width, height);
    float *buf3 = buf2;
    buf2 = buf1;
    buf1 = buf3;
  }
  /* due to symmetric processing, output will be left in (float *)o */

  for(int k = 0; k < max_scale; k++) dt_free_align(detail[k]);
  dt_free_align(tmp);

  if(piece->pipe->mask_display & DT_DEV_PIXELPIPE_DISPLAY_MASK) dt_iop_alpha_copy(i, o, width, height);

  return;

error:
  for(int k = 0; k < max_scale; k++)
    if(detail[k] != NULL) dt_free_align(detail[k]);
  if(tmp != NULL) dt_free_align(tmp);
  return;
}

void process(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, const void *const i,
             void *const o, const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  process_wavelets(self, piece, i, o, roi_in, roi_out, eaw_decompose, eaw_synthesize);
}

#if defined(__SSE2__)
void process_sse2(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, const void *const i,
                  void *const o, const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  process_wavelets(self, piece, i, o, roi_in, roi_out, eaw_decompose_sse2, eaw_synthesize_sse2);
}
#endif

#ifdef HAVE_OPENCL
/* this version is adapted to the new global tiling mechanism. it no longer does tiling by itself. */
int process_cl(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, cl_mem dev_in, cl_mem dev_out,
               const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  dt_iop_atrous_data_t *d = (dt_iop_atrous_data_t *)piece->data;
  float thrs[MAX_NUM_SCALES][4];
  float boost[MAX_NUM_SCALES][4];
  float sharp[MAX_NUM_SCALES];
  const int max_scale = get_scales(thrs, boost, sharp, d, roi_in, piece);

  if(self->dev->gui_attached && piece->pipe->type == DT_DEV_PIXELPIPE_FULL)
  {
    dt_iop_atrous_gui_data_t *g = (dt_iop_atrous_gui_data_t *)self->gui_data;
    g->num_samples = get_samples(g->sample, d, roi_in, piece);
    // dt_control_queue_redraw_widget(GTK_WIDGET(g->area));
    // tries to acquire gdk lock and this prone to deadlock:
    // dt_control_queue_draw(GTK_WIDGET(g->area));
  }

  dt_iop_atrous_global_data_t *gd = (dt_iop_atrous_global_data_t *)self->global_data;

  const int devid = piece->pipe->devid;
  cl_int err = -999;
  cl_mem dev_filter = NULL;
  cl_mem dev_tmp = NULL;
  cl_mem *dev_detail = calloc(max_scale, sizeof(cl_mem));

  float m[] = { 0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f }; // 1/16, 4/16, 6/16, 4/16, 1/16
  float mm[5][5];
  for(int j = 0; j < 5; j++)
    for(int i = 0; i < 5; i++) mm[j][i] = m[i] * m[j];

  dev_filter = dt_opencl_copy_host_to_device_constant(devid, sizeof(float) * 25, mm);
  if(dev_filter == NULL) goto error;

  /* allocate space for a temporary buffer. we don't want to use dev_in in the buffer ping-pong below, as we
     need to keep it for blendops */
  dev_tmp = dt_opencl_alloc_device(devid, roi_out->width, roi_out->height, 4 * sizeof(float));
  if(dev_tmp == NULL) goto error;

  /* allocate space to store detail information. Requires a number of additional buffers, each with full image
   * size */
  for(int k = 0; k < max_scale; k++)
  {
    dev_detail[k] = dt_opencl_alloc_device(devid, roi_out->width, roi_out->height, 4 * sizeof(float));
    if(dev_detail[k] == NULL) goto error;
  }

  const int width = roi_out->width;
  const int height = roi_out->height;
  size_t sizes[] = { ROUNDUPWD(width), ROUNDUPHT(height), 1 };
  size_t origin[] = { 0, 0, 0 };
  size_t region[] = { width, height, 1 };

  // copy original input from dev_in -> dev_out as starting point
  err = dt_opencl_enqueue_copy_image(devid, dev_in, dev_out, origin, origin, region);
  if(err != CL_SUCCESS) goto error;

  /* decompose image into detail scales and coarse (the latter is left in dev_tmp or dev_out) */
  for(int s = 0; s < max_scale; s++)
  {
    const int scale = s;

    if(s & 1)
    {
      dt_opencl_set_kernel_arg(devid, gd->kernel_decompose, 0, sizeof(cl_mem), (void *)&dev_tmp);
      dt_opencl_set_kernel_arg(devid, gd->kernel_decompose, 1, sizeof(cl_mem), (void *)&dev_out);
    }
    else
    {
      dt_opencl_set_kernel_arg(devid, gd->kernel_decompose, 0, sizeof(cl_mem), (void *)&dev_out);
      dt_opencl_set_kernel_arg(devid, gd->kernel_decompose, 1, sizeof(cl_mem), (void *)&dev_tmp);
    }
    dt_opencl_set_kernel_arg(devid, gd->kernel_decompose, 2, sizeof(cl_mem), (void *)&dev_detail[s]);
    dt_opencl_set_kernel_arg(devid, gd->kernel_decompose, 3, sizeof(int), (void *)&width);
    dt_opencl_set_kernel_arg(devid, gd->kernel_decompose, 4, sizeof(int), (void *)&height);
    dt_opencl_set_kernel_arg(devid, gd->kernel_decompose, 5, sizeof(unsigned int), (void *)&scale);
    dt_opencl_set_kernel_arg(devid, gd->kernel_decompose, 6, sizeof(float), (void *)&sharp[s]);
    dt_opencl_set_kernel_arg(devid, gd->kernel_decompose, 7, sizeof(cl_mem), (void *)&dev_filter);

    err = dt_opencl_enqueue_kernel_2d(devid, gd->kernel_decompose, sizes);
    if(err != CL_SUCCESS) goto error;

    // indirectly give gpu some air to breathe (and to do display related stuff)
    dt_iop_nap(darktable.opencl->micro_nap);
  }

  /* now synthesize again */
  for(int scale = max_scale - 1; scale >= 0; scale--)
  {
    if(scale & 1)
    {
      dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 0, sizeof(cl_mem), (void *)&dev_tmp);
      dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 1, sizeof(cl_mem), (void *)&dev_out);
    }
    else
    {
      dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 0, sizeof(cl_mem), (void *)&dev_out);
      dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 1, sizeof(cl_mem), (void *)&dev_tmp);
    }

    dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 2, sizeof(cl_mem), (void *)&dev_detail[scale]);
    dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 3, sizeof(int), (void *)&width);
    dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 4, sizeof(int), (void *)&height);
    dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 5, sizeof(float), (void *)&thrs[scale][0]);
    dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 6, sizeof(float), (void *)&thrs[scale][1]);
    dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 7, sizeof(float), (void *)&thrs[scale][2]);
    dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 8, sizeof(float), (void *)&thrs[scale][3]);
    dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 9, sizeof(float), (void *)&boost[scale][0]);
    dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 10, sizeof(float), (void *)&boost[scale][1]);
    dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 11, sizeof(float), (void *)&boost[scale][2]);
    dt_opencl_set_kernel_arg(devid, gd->kernel_synthesize, 12, sizeof(float), (void *)&boost[scale][3]);

    err = dt_opencl_enqueue_kernel_2d(devid, gd->kernel_synthesize, sizes);
    if(err != CL_SUCCESS) goto error;

    // indirectly give gpu some air to breathe (and to do display related stuff)
    dt_iop_nap(darktable.opencl->micro_nap);
  }

  if(!darktable.opencl->async_pixelpipe || piece->pipe->type == DT_DEV_PIXELPIPE_EXPORT)
    dt_opencl_finish(devid);

  dt_opencl_release_mem_object(dev_filter);
  dt_opencl_release_mem_object(dev_tmp);
  for(int k = 0; k < max_scale; k++)
    dt_opencl_release_mem_object(dev_detail[k]);
  free(dev_detail);
  return TRUE;

error:
  dt_opencl_release_mem_object(dev_filter);
  dt_opencl_release_mem_object(dev_tmp);
  for(int k = 0; k < max_scale; k++)
    dt_opencl_release_mem_object(dev_detail[k]);
  free(dev_detail);
  dt_print(DT_DEBUG_OPENCL, "[opencl_atrous] couldn't enqueue kernel! %d\n", err);
  return FALSE;
}
#endif

void tiling_callback(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece,
                     const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out,
                     struct dt_develop_tiling_t *tiling)
{
  dt_iop_atrous_data_t *d = (dt_iop_atrous_data_t *)piece->data;
  float thrs[MAX_NUM_SCALES][4];
  float boost[MAX_NUM_SCALES][4];
  float sharp[MAX_NUM_SCALES];
  const int max_scale = get_scales(thrs, boost, sharp, d, roi_in, piece);
  const int max_filter_radius = 2 * (1 << max_scale); // 2 * 2^max_scale

  tiling->factor = 3.0f + max_scale; // in + out + tmp + scale buffers
  tiling->maxbuf = 1.0f;
  tiling->overhead = 0;
  tiling->overlap = max_filter_radius;
  tiling->xalign = 1;
  tiling->yalign = 1;
  return;
}

void init(dt_iop_module_t *module)
{
  module->params = calloc(1, sizeof(dt_iop_atrous_params_t));
  module->default_params = calloc(1, sizeof(dt_iop_atrous_params_t));
  module->default_enabled = 0;
  module->params_size = sizeof(dt_iop_atrous_params_t);
  module->gui_data = NULL;
  dt_iop_atrous_params_t tmp;
  tmp.octaves = 3;
  for(int k = 0; k < BANDS; k++)
  {
    tmp.y[atrous_L][k] = tmp.y[atrous_s][k] = tmp.y[atrous_c][k] = 0.5f;
    tmp.x[atrous_L][k] = tmp.x[atrous_s][k] = tmp.x[atrous_c][k] = k / (BANDS - 1.0f);
    tmp.y[atrous_Lt][k] = tmp.y[atrous_ct][k] = 0.0f;
    tmp.x[atrous_Lt][k] = tmp.x[atrous_ct][k] = k / (BANDS - 1.0f);
  }
  memcpy(module->params, &tmp, sizeof(dt_iop_atrous_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_atrous_params_t));
}

void init_global(dt_iop_module_so_t *module)
{
  const int program = 1; // from programs.conf
  dt_iop_atrous_global_data_t *gd
      = (dt_iop_atrous_global_data_t *)malloc(sizeof(dt_iop_atrous_global_data_t));
  module->data = gd;
  gd->kernel_decompose = dt_opencl_create_kernel(program, "eaw_decompose");
  gd->kernel_synthesize = dt_opencl_create_kernel(program, "eaw_synthesize");
}

void cleanup(dt_iop_module_t *module)
{
  free(module->params);
  module->params = NULL;
}

void cleanup_global(dt_iop_module_so_t *module)
{
  dt_iop_atrous_global_data_t *gd = (dt_iop_atrous_global_data_t *)module->data;
  dt_opencl_free_kernel(gd->kernel_decompose);
  dt_opencl_free_kernel(gd->kernel_synthesize);
  free(module->data);
  module->data = NULL;
}

void commit_params(struct dt_iop_module_t *self, dt_iop_params_t *params, dt_dev_pixelpipe_t *pipe,
                   dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_atrous_params_t *p = (dt_iop_atrous_params_t *)params;
  dt_iop_atrous_data_t *d = (dt_iop_atrous_data_t *)piece->data;
#if 0
  printf("---------- atrous preset begin\n");
  printf("p.octaves = %d;\n", p->octaves);
  for(int ch=0; ch<atrous_none; ch++) for(int k=0; k<BANDS; k++)
    {
      printf("p.x[%d][%d] = %f;\n", ch, k, p->x[ch][k]);
      printf("p.y[%d][%d] = %f;\n", ch, k, p->y[ch][k]);
    }
  printf("---------- atrous preset end\n");
#endif
  d->octaves = p->octaves;
  for(int ch = 0; ch < atrous_none; ch++)
    for(int k = 0; k < BANDS; k++) dt_draw_curve_set_point(d->curve[ch], k, p->x[ch][k], p->y[ch][k]);
  int l = 0;
  for(int k = (int)MIN(pipe->iwidth * pipe->iscale, pipe->iheight * pipe->iscale); k; k >>= 1) l++;
  d->octaves = MIN(BANDS, l);
}

void init_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_atrous_data_t *d = (dt_iop_atrous_data_t *)malloc(sizeof(dt_iop_atrous_data_t));
  dt_iop_atrous_params_t *default_params = (dt_iop_atrous_params_t *)self->default_params;
  piece->data = (void *)d;
  for(int ch = 0; ch < atrous_none; ch++)
  {
    d->curve[ch] = dt_draw_curve_new(0.0, 1.0, CATMULL_ROM);
    for(int k = 0; k < BANDS; k++)
      (void)dt_draw_curve_add_point(d->curve[ch], default_params->x[ch][k], default_params->y[ch][k]);
  }
  int l = 0;
  for(int k = (int)MIN(pipe->iwidth * pipe->iscale, pipe->iheight * pipe->iscale); k; k >>= 1) l++;
  d->octaves = MIN(BANDS, l);
}

void cleanup_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_atrous_data_t *d = (dt_iop_atrous_data_t *)(piece->data);
  for(int ch = 0; ch < atrous_none; ch++) dt_draw_curve_destroy(d->curve[ch]);
  free(piece->data);
  piece->data = NULL;
}

#define GAUSS(x, sigma) expf( -(1.0f - x) * (1.0f - x) / (sigma * sigma)) / (2.0 * sigma * powf(M_PI, 0.5f))

void init_presets(dt_iop_module_so_t *self)
{
  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db), "BEGIN", NULL, NULL, NULL);
  dt_iop_atrous_params_t p;
  p.octaves = 7;

  for(int k = 0; k < BANDS; k++)
  {
    p.x[atrous_L][k] = k / (BANDS - 1.0);
    p.x[atrous_c][k] = k / (BANDS - 1.0);
    p.x[atrous_s][k] = k / (BANDS - 1.0);
    p.y[atrous_L][k] = fmaxf(.5f, .75f - .5f * k / (BANDS - 1.0));
    p.y[atrous_c][k] = fmaxf(.5f, .55f - .5f * k / (BANDS - 1.0));
    p.y[atrous_s][k] = fminf(.5f, .2f + .35f * k / (BANDS - 1.0));
    p.x[atrous_Lt][k] = k / (BANDS - 1.0);
    p.x[atrous_ct][k] = k / (BANDS - 1.0);
    p.y[atrous_Lt][k] = 0.0f;
    p.y[atrous_ct][k] = 0.0f;
  }
  dt_gui_presets_add_generic(C_("eq_preset", "coarse"), self->op, self->version(), &p, sizeof(p), 1);
  for(int k = 0; k < BANDS; k++)
  {
    p.x[atrous_L][k] = k / (BANDS - 1.0);
    p.x[atrous_c][k] = k / (BANDS - 1.0);
    p.x[atrous_s][k] = k / (BANDS - 1.0);
    p.y[atrous_L][k] = .5f + .25f * k / (float)BANDS;
    p.y[atrous_c][k] = .5f;
    p.y[atrous_s][k] = .5f;
    p.x[atrous_Lt][k] = k / (BANDS - 1.0);
    p.x[atrous_ct][k] = k / (BANDS - 1.0);
    p.y[atrous_Lt][k] = .2f * k / (float)BANDS;
    p.y[atrous_ct][k] = .3f * k / (float)BANDS;
  }
  dt_gui_presets_add_generic(_("denoise & sharpen"), self->op, self->version(), &p, sizeof(p), 1);
  for(int k = 0; k < BANDS; k++)
  {
    p.x[atrous_L][k] = k / (BANDS - 1.0);
    p.x[atrous_c][k] = k / (BANDS - 1.0);
    p.x[atrous_s][k] = k / (BANDS - 1.0);
    p.y[atrous_L][k] = .5f + .25f * k / (float)BANDS;
    p.y[atrous_c][k] = .5f;
    p.y[atrous_s][k] = .5f;
    p.x[atrous_Lt][k] = k / (BANDS - 1.0);
    p.x[atrous_ct][k] = k / (BANDS - 1.0);
    p.y[atrous_Lt][k] = 0.0f;
    p.y[atrous_ct][k] = 0.0f;
  }
  dt_gui_presets_add_generic(C_("atrous", "sharpen"), self->op, self->version(), &p, sizeof(p), 1);
  for(int k = 0; k < BANDS; k++)
  {
    p.x[atrous_L][k] = k / (BANDS - 1.0);
    p.x[atrous_c][k] = k / (BANDS - 1.0);
    p.x[atrous_s][k] = k / (BANDS - 1.0);
    p.y[atrous_L][k] = .5f;
    p.y[atrous_c][k] = .5f;
    p.y[atrous_s][k] = .0f;
    p.x[atrous_Lt][k] = k / (BANDS - 1.0);
    p.x[atrous_ct][k] = k / (BANDS - 1.0);
    p.y[atrous_Lt][k] = .0f;
    p.y[atrous_ct][k] = fmaxf(0.0f, (.60f * k / (float)BANDS) - 0.30f);
  }
  dt_gui_presets_add_generic(_("denoise chroma"), self->op, self->version(), &p, sizeof(p), 1);
  for(int k = 0; k < BANDS; k++)
  {
    p.x[atrous_L][k] = k / (BANDS - 1.0);
    p.x[atrous_c][k] = k / (BANDS - 1.0);
    p.x[atrous_s][k] = k / (BANDS - 1.0);
    p.y[atrous_L][k] = .5f; //-.2f*k/(float)BANDS;
    p.y[atrous_c][k] = .5f; // fmaxf(0.0f, .5f-.3f*k/(float)BANDS);
    p.y[atrous_s][k] = .5f;
    p.x[atrous_Lt][k] = k / (BANDS - 1.0);
    p.x[atrous_ct][k] = k / (BANDS - 1.0);
    p.y[atrous_Lt][k] = .2f * k / (float)BANDS;
    p.y[atrous_ct][k] = .3f * k / (float)BANDS;
  }
  dt_gui_presets_add_generic(_("denoise"), self->op, self->version(), &p, sizeof(p), 1);
  for(int k = 0; k < BANDS; k++)
  {
    p.x[atrous_L][k] = k / (BANDS - 1.0);
    p.x[atrous_c][k] = k / (BANDS - 1.0);
    p.x[atrous_s][k] = k / (BANDS - 1.0);
    p.y[atrous_L][k] = fminf(.5f, .3f + .35f * k / (BANDS - 1.0));
    p.y[atrous_c][k] = .5f;
    p.y[atrous_s][k] = .0f;
    p.x[atrous_Lt][k] = k / (BANDS - 1.0);
    p.x[atrous_ct][k] = k / (BANDS - 1.0);
    p.y[atrous_Lt][k] = 0.0f;
    p.y[atrous_ct][k] = 0.0f;
  }
  p.y[atrous_L][0] = .5f;
  dt_gui_presets_add_generic(_("bloom"), self->op, self->version(), &p, sizeof(p), 1);
  for(int k = 0; k < BANDS; k++)
  {
    p.x[atrous_L][k] = k / (BANDS - 1.0);
    p.x[atrous_c][k] = k / (BANDS - 1.0);
    p.x[atrous_s][k] = k / (BANDS - 1.0);
    p.y[atrous_L][k] = 0.6f;
    p.y[atrous_c][k] = .55f;
    p.y[atrous_s][k] = .0f;
    p.x[atrous_Lt][k] = k / (BANDS - 1.0);
    p.x[atrous_ct][k] = k / (BANDS - 1.0);
    p.y[atrous_Lt][k] = 0.0f;
    p.y[atrous_ct][k] = 0.0f;
  }
  dt_gui_presets_add_generic(_("clarity"), self->op, self->version(), &p, sizeof(p), 1);

  float sigma = 1 / (BANDS - 1.0);

  for(int k = 0; k < BANDS; k++)
  {
    float x = log2f(128.0 * k / (BANDS - 1.0) + 1.0) / log2f(129.0);
    float fine = GAUSS(x, 0.5 * sigma);
    float medium = GAUSS(x, sigma);
    float coarse = GAUSS(x, 2 * sigma);
    float coeff = 0.5f + (coarse + medium + fine) / 18.0f;
    float noise = (coarse + medium + fine) / 810;

    p.x[atrous_L][k] = p.x[atrous_c][k] = p.x[atrous_s][k] = x;
    p.y[atrous_L][k] = p.y[atrous_c][k] = p.y[atrous_s][k] = coeff;
    p.x[atrous_Lt][k] = p.x[atrous_ct][k] = x;
    p.y[atrous_Lt][k] = p.y[atrous_ct][k] = noise;
  }
  dt_gui_presets_add_generic(_("deblur: large blur, strength 4"), self->op, self->version(), &p, sizeof(p), 1);

  for(int k = 0; k < BANDS; k++)
  {
    float x = log2f(128.0 * k / (BANDS - 1.0) + 1.0) / log2f(129.0);
    float fine = GAUSS(x, 0.5 * sigma);
    float medium = GAUSS(x, sigma);
    float coarse = GAUSS(x, 2 * sigma);
    float coeff = 0.5f + (coarse + medium + fine) / 24.0f;
    float noise = (coarse + medium + fine) / 1080;

    p.x[atrous_L][k] = p.x[atrous_c][k] = p.x[atrous_s][k] = x;
    p.y[atrous_L][k] = p.y[atrous_c][k] = p.y[atrous_s][k] = coeff;
    p.x[atrous_Lt][k] = p.x[atrous_ct][k] = x;
    p.y[atrous_Lt][k] = p.y[atrous_ct][k] = noise;
  }
  dt_gui_presets_add_generic(_("deblur: large blur, strength 3"), self->op, self->version(), &p, sizeof(p), 1);
  for(int k = 0; k < BANDS; k++)
  {
    float x = log2f(128.0 * k / (BANDS - 1.0) + 1.0) / log2f(129.0);
    float fine = GAUSS(x, 0.5 * sigma);
    float medium = GAUSS(x, sigma);
    float coeff = 0.5f + (medium + fine) / 21.0f;
    float noise = (medium + fine) / 720;

    p.x[atrous_L][k] = p.x[atrous_c][k] = p.x[atrous_s][k] = x;
    p.y[atrous_L][k] = p.y[atrous_c][k] = p.y[atrous_s][k] = coeff;
    p.x[atrous_Lt][k] = p.x[atrous_ct][k] = x;
    p.y[atrous_Lt][k] = p.y[atrous_ct][k] = noise;
  }
  dt_gui_presets_add_generic(_("deblur: medium blur, strength 3"), self->op, self->version(), &p, sizeof(p), 1);
  for(int k = 0; k < BANDS; k++)
  {
    float x = log2f(128.0 * k / (BANDS - 1.0) + 1.0) / log2f(129.0);
    float fine = GAUSS(x, 0.5 * sigma);
    float coeff = 0.5f + fine / 14.25f;
    float noise = fine / 360;

    p.x[atrous_L][k] = p.x[atrous_c][k] = p.x[atrous_s][k] = x;
    p.y[atrous_L][k] = p.y[atrous_c][k] = p.y[atrous_s][k] = coeff;
    p.x[atrous_Lt][k] = p.x[atrous_ct][k] = x;
    p.y[atrous_Lt][k] = p.y[atrous_ct][k] = noise;
  }
  dt_gui_presets_add_generic(_("deblur: fine blur, strength 3"), self->op, self->version(), &p, sizeof(p), 1);


  for(int k = 0; k < BANDS; k++)
  {
    float x = log2f(128.0 * k / (BANDS - 1.0) + 1.0) / log2f(129.0);
    float fine = GAUSS(x, 0.5 * sigma);
    float medium = GAUSS(x, sigma);
    float coarse = GAUSS(x, 2 * sigma);
    float coeff = 0.5f + (coarse + medium + fine) / 32.0f;
    float noise = (coarse + medium + fine) / 1440;

    p.x[atrous_L][k] = p.x[atrous_c][k] = p.x[atrous_s][k] = x;
    p.y[atrous_L][k] = p.y[atrous_c][k] = p.y[atrous_s][k] = coeff;
    p.x[atrous_Lt][k] = p.x[atrous_ct][k] = x;
    p.y[atrous_Lt][k] = p.y[atrous_ct][k] = noise;
  }
  dt_gui_presets_add_generic(_("deblur: large blur, strength 2"), self->op, self->version(), &p, sizeof(p), 1);
  for(int k = 0; k < BANDS; k++)
  {
    float x = log2f(128.0 * k / (BANDS - 1.0) + 1.0) / log2f(129.0);
    float fine = GAUSS(x, 0.5 * sigma);
    float medium = GAUSS(x, sigma);
    float coeff = 0.5f + (medium + fine) / 28.0f;
    float noise = (medium + fine) / 960;

    p.x[atrous_L][k] = p.x[atrous_c][k] = p.x[atrous_s][k] = x;
    p.y[atrous_L][k] = p.y[atrous_c][k] = p.y[atrous_s][k] = coeff;
    p.x[atrous_Lt][k] = p.x[atrous_ct][k] = x;
    p.y[atrous_Lt][k] = p.y[atrous_ct][k] = noise;
  }
  dt_gui_presets_add_generic(_("deblur: medium blur, strength 2"), self->op, self->version(), &p, sizeof(p), 1);
  for(int k = 0; k < BANDS; k++)
  {
    float x = log2f(128.0 * k / (BANDS - 1.0) + 1.0) / log2f(129.0);
    float fine = GAUSS(x, 0.5 * sigma);
    float coeff = 0.5f + fine / 19.0f;
    float noise = fine / 480;

    p.x[atrous_L][k] = p.x[atrous_c][k] = p.x[atrous_s][k] = x;
    p.y[atrous_L][k] = p.y[atrous_c][k] = p.y[atrous_s][k] = coeff;
    p.x[atrous_Lt][k] = p.x[atrous_ct][k] = x;
    p.y[atrous_Lt][k] = p.y[atrous_ct][k] = noise;
  }
  dt_gui_presets_add_generic(_("deblur: fine blur, strength 2"), self->op, self->version(), &p, sizeof(p), 1);


  for(int k = 0; k < BANDS; k++)
  {
    float x = log2f(128.0 * k / (BANDS - 1.0) + 1.0) / log2f(129.0);
    float fine = GAUSS(x, 0.5 * sigma);
    float medium = GAUSS(x, sigma);
    float coarse = GAUSS(x, 2 * sigma);
    float coeff = 0.5f + (coarse + medium + fine) / 48.0f;
    float noise = (coarse + medium + fine) / 2160;

    p.x[atrous_L][k] = p.x[atrous_c][k] = p.x[atrous_s][k] = x;
    p.y[atrous_L][k] = p.y[atrous_c][k] = p.y[atrous_s][k] = coeff;
    p.x[atrous_Lt][k] = p.x[atrous_ct][k] = x;
    p.y[atrous_Lt][k] = p.y[atrous_ct][k] = noise;
  }
  dt_gui_presets_add_generic(_("deblur: large blur, strength 1"), self->op, self->version(), &p, sizeof(p), 1);
  for(int k = 0; k < BANDS; k++)
  {
    float x = log2f(128.0 * k / (BANDS - 1.0) + 1.0) / log2f(129.0);
    float fine = GAUSS(x, 0.5 * sigma);
    float medium = GAUSS(x, sigma);
    float coeff = 0.5f + (medium + fine) / 42.0f;
    float noise = (medium + fine) / 1440;

    p.x[atrous_L][k] = p.x[atrous_c][k] = p.x[atrous_s][k] = x;
    p.y[atrous_L][k] = p.y[atrous_c][k] = p.y[atrous_s][k] = coeff;
    p.x[atrous_Lt][k] = p.x[atrous_ct][k] = x;
    p.y[atrous_Lt][k] = p.y[atrous_ct][k] = noise;
  }
  dt_gui_presets_add_generic(_("deblur: medium blur, strength 1"), self->op, self->version(), &p, sizeof(p), 1);
  for(int k = 0; k < BANDS; k++)
  {
    float x = log2f(128.0 * k / (BANDS - 1.0) + 1.0) / log2f(129.0);
    float fine = GAUSS(x, 0.5 * sigma);
    float coeff = 0.5f + fine / 28.5f;
    float noise = fine / 720;

    p.x[atrous_L][k] = p.x[atrous_c][k] = p.x[atrous_s][k] = x;
    p.y[atrous_L][k] = p.y[atrous_c][k] = p.y[atrous_s][k] = coeff;
    p.x[atrous_Lt][k] = p.x[atrous_ct][k] = x;
    p.y[atrous_Lt][k] = p.y[atrous_ct][k] = noise;
  }
  dt_gui_presets_add_generic(_("deblur: fine blur, strength 1"), self->op, self->version(), &p, sizeof(p), 1);

  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db), "COMMIT", NULL, NULL, NULL);
}

static void reset_mix(dt_iop_module_t *self)
{
  dt_iop_atrous_gui_data_t *c = (dt_iop_atrous_gui_data_t *)self->gui_data;
  c->drag_params = *(dt_iop_atrous_params_t *)self->params;
  const int reset = darktable.gui->reset;
  darktable.gui->reset = 1;
  dt_bauhaus_slider_set(c->mix, 1.0f);
  darktable.gui->reset = reset;
}

void gui_update(struct dt_iop_module_t *self)
{
  reset_mix(self);
  gtk_widget_queue_draw(self->widget);
}


// gui stuff:

static gboolean area_enter_notify(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_atrous_gui_data_t *c = (dt_iop_atrous_gui_data_t *)self->gui_data;
  if(!c->dragging) c->mouse_y = fabs(c->mouse_y);
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean area_leave_notify(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_atrous_gui_data_t *c = (dt_iop_atrous_gui_data_t *)self->gui_data;
  if(!c->dragging) c->mouse_y = -fabs(c->mouse_y);
  gtk_widget_queue_draw(widget);
  return TRUE;
}

// fills in new parameters based on mouse position (in 0,1)
static void get_params(dt_iop_atrous_params_t *p, const int ch, const double mouse_x, const double mouse_y,
                       const float rad)
{
  for(int k = 0; k < BANDS; k++)
  {
    const float f = expf(-(mouse_x - p->x[ch][k]) * (mouse_x - p->x[ch][k]) / (rad * rad));
    p->y[ch][k] = MAX(0.0f, MIN(1.0f, (1 - f) * p->y[ch][k] + f * mouse_y));
  }
}

static gboolean area_draw(GtkWidget *widget, cairo_t *crf, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_atrous_gui_data_t *c = (dt_iop_atrous_gui_data_t *)self->gui_data;
  dt_iop_atrous_params_t p = *(dt_iop_atrous_params_t *)self->params;

  for(int k = 0; k < BANDS; k++)
    dt_draw_curve_set_point(c->minmax_curve, k, p.x[(int)c->channel2][k], p.y[(int)c->channel2][k]);
  const int inset = INSET;
  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  int width = allocation.width, height = allocation.height;
  cairo_surface_t *cst = dt_cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cairo_t *cr = cairo_create(cst);
  // clear bg, match color of the notebook tabs:
  GdkRGBA bright_bg_color, really_dark_bg_color;
  GtkStyleContext *context = gtk_widget_get_style_context(self->expander);
  gboolean color_found = gtk_style_context_lookup_color (context, "selected_bg_color", &bright_bg_color);
  if(!color_found)
  {
    bright_bg_color.red = 1.0;
    bright_bg_color.green = 0.0;
    bright_bg_color.blue = 0.0;
    bright_bg_color.alpha = 1.0;
  }

  color_found = gtk_style_context_lookup_color (context, "really_dark_bg_color", &really_dark_bg_color);
  if(!color_found)
  {
    really_dark_bg_color.red = 1.0;
    really_dark_bg_color.green = 0.0;
    really_dark_bg_color.blue = 0.0;
    really_dark_bg_color.alpha = 1.0;
  }

  gdk_cairo_set_source_rgba(cr, &bright_bg_color);
  cairo_paint(cr);

  cairo_translate(cr, inset, inset);
  width -= 2 * inset;
  height -= 2 * inset;

  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1.0));
  gdk_cairo_set_source_rgba(cr, &really_dark_bg_color);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_stroke(cr);

  gdk_cairo_set_source_rgba(cr, &bright_bg_color);
  cairo_rectangle(cr, 0, 0, width, height);
  cairo_fill(cr);

  if(c->mouse_y > 0 || c->dragging)
  {
    int ch2 = (int)c->channel2;

    // draw min/max curves:
    get_params(&p, ch2, c->mouse_x, 1., c->mouse_radius);
    for(int k = 0; k < BANDS; k++) dt_draw_curve_set_point(c->minmax_curve, k, p.x[ch2][k], p.y[ch2][k]);
    dt_draw_curve_calc_values(c->minmax_curve, 0.0, 1.0, RES, c->draw_min_xs, c->draw_min_ys);

    p = *(dt_iop_atrous_params_t *)self->params;
    get_params(&p, ch2, c->mouse_x, .0, c->mouse_radius);
    for(int k = 0; k < BANDS; k++) dt_draw_curve_set_point(c->minmax_curve, k, p.x[ch2][k], p.y[ch2][k]);
    dt_draw_curve_calc_values(c->minmax_curve, 0.0, 1.0, RES, c->draw_max_xs, c->draw_max_ys);
  }

  // draw grid
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(.4));
  gdk_cairo_set_source_rgba(cr, &really_dark_bg_color);
  dt_draw_grid(cr, 8, 0, 0, width, height);

  cairo_save(cr);

  // draw selected cursor
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1.));
  cairo_translate(cr, 0, height);

// draw frequency histogram in bg.
#if 1
  if(c->num_samples > 0)
  {
    cairo_save(cr);
    for(int k = 1; k < c->num_samples; k += 2)
    {
      cairo_set_source_rgba(cr, really_dark_bg_color.red, really_dark_bg_color.green, really_dark_bg_color.blue, .3);
      cairo_move_to(cr, width * c->sample[k - 1], 0.0f);
      cairo_line_to(cr, width * c->sample[k - 1], -height);
      cairo_line_to(cr, width * c->sample[k], -height);
      cairo_line_to(cr, width * c->sample[k], 0.0f);
      cairo_fill(cr);
    }
    if(c->num_samples & 1)
    {
      cairo_move_to(cr, width * c->sample[c->num_samples - 1], 0.0f);
      cairo_line_to(cr, width * c->sample[c->num_samples - 1], -height);
      cairo_line_to(cr, 0.0f, -height);
      cairo_line_to(cr, 0.0f, 0.0f);
      cairo_fill(cr);
    }
    cairo_restore(cr);
  }
  if(c->band_max > 0)
  {
    cairo_save(cr);
    cairo_scale(cr, width / (BANDS - 1.0), -(height - DT_PIXEL_APPLY_DPI(5)) / c->band_max);
    cairo_set_source_rgba(cr, really_dark_bg_color.red, really_dark_bg_color.green, really_dark_bg_color.blue, .3);
    cairo_move_to(cr, 0, 0);
    for(int k = 0; k < BANDS; k++) cairo_line_to(cr, k, c->band_hist[k]);
    cairo_line_to(cr, BANDS - 1.0, 0.);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_restore(cr);
  }
#endif

  // cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(2.));
  for(int i = 0; i <= atrous_s; i++)
  {
    // draw curves, selected last.
    int ch = ((int)c->channel + i + 1) % (atrous_s + 1);
    int ch2 = -1;
    const float bgmul = i < atrous_s ? 0.5f : 1.0f;
    switch(ch)
    {
      case atrous_L:
        cairo_set_source_rgba(cr, .6, .6, .6, .3 * bgmul);
        ch2 = atrous_Lt;
        break;
      case atrous_c:
        cairo_set_source_rgba(cr, .4, .2, .0, .4 * bgmul);
        ch2 = atrous_ct;
        break;
      default: // case atrous_s:
        cairo_set_source_rgba(cr, .1, .2, .3, .4 * bgmul);
        break;
    }
    p = *(dt_iop_atrous_params_t *)self->params;

    // reverse order if bottom is active (to end up with correct values in minmax_curve):
    if(c->channel2 == ch2)
    {
      ch2 = ch;
      ch = c->channel2;
    }

    if(ch2 >= 0)
    {
      for(int k = 0; k < BANDS; k++) dt_draw_curve_set_point(c->minmax_curve, k, p.x[ch2][k], p.y[ch2][k]);
      dt_draw_curve_calc_values(c->minmax_curve, 0.0, 1.0, RES, c->draw_xs, c->draw_ys);
      cairo_move_to(cr, width, -height * p.y[ch2][BANDS - 1]);
      for(int k = RES - 2; k >= 0; k--)
        cairo_line_to(cr, k * width / (float)(RES - 1), -height * c->draw_ys[k]);
    }
    else
      cairo_move_to(cr, 0, 0);
    for(int k = 0; k < BANDS; k++) dt_draw_curve_set_point(c->minmax_curve, k, p.x[ch][k], p.y[ch][k]);
    dt_draw_curve_calc_values(c->minmax_curve, 0.0, 1.0, RES, c->draw_xs, c->draw_ys);
    for(int k = 0; k < RES; k++) cairo_line_to(cr, k * width / (float)(RES - 1), -height * c->draw_ys[k]);
    if(ch2 < 0) cairo_line_to(cr, width, 0);
    cairo_close_path(cr);
    cairo_stroke_preserve(cr);
    cairo_fill(cr);
  }

  if(c->mouse_y > 0 || c->dragging)
  {
    int ch = (int)c->channel;
    int ch2 = (int)c->channel2;

    // draw dots on knots
    cairo_save(cr);
    if(ch != ch2)
      cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    else
      cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1.));
    for(int k = 0; k < BANDS; k++)
    {
      cairo_arc(cr, width * p.x[ch2][k], -height * p.y[ch2][k], DT_PIXEL_APPLY_DPI(3.0), 0.0, 2.0 * M_PI);
      if(c->x_move == k)
        cairo_fill(cr);
      else
        cairo_stroke(cr);
    }
    cairo_restore(cr);
  }

  if(c->mouse_y > 0 || c->dragging)
  {
    // draw min/max, if selected
    // cairo_set_source_rgba(cr, .6, .6, .6, .5);
    cairo_move_to(cr, 0, -height * c->draw_min_ys[0]);
    for(int k = 1; k < RES; k++) cairo_line_to(cr, k * width / (float)(RES - 1), -height * c->draw_min_ys[k]);
    for(int k = RES - 1; k >= 0; k--)
      cairo_line_to(cr, k * width / (float)(RES - 1), -height * c->draw_max_ys[k]);
    cairo_close_path(cr);
    cairo_fill(cr);
    // draw mouse focus circle
    cairo_set_source_rgba(cr, .9, .9, .9, .5);
    const float pos = RES * c->mouse_x;
    int k = (int)pos;
    const float f = k - pos;
    if(k >= RES - 1) k = RES - 2;
    float ht = -height * (f * c->draw_ys[k] + (1 - f) * c->draw_ys[k + 1]);
    cairo_arc(cr, c->mouse_x * width, ht, c->mouse_radius * width, 0, 2. * M_PI);
    cairo_stroke(cr);
  }

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

  // draw x positions
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1.));
  cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
  const float arrw = DT_PIXEL_APPLY_DPI(7.0f);
  for(int k = 1; k < BANDS - 1; k++)
  {
    cairo_move_to(cr, width * p.x[(int)c->channel][k], inset - DT_PIXEL_APPLY_DPI(1));
    cairo_rel_line_to(cr, -arrw * .5f, 0);
    cairo_rel_line_to(cr, arrw * .5f, -arrw);
    cairo_rel_line_to(cr, arrw * .5f, arrw);
    cairo_close_path(cr);
    if(c->x_move == k)
      cairo_fill(cr);
    else
      cairo_stroke(cr);
  }

  cairo_restore(cr);

  if(c->mouse_y > 0 || c->dragging)
  {
    // draw labels:
    PangoLayout *layout;
    PangoRectangle ink;
    PangoFontDescription *desc = pango_font_description_copy_static(darktable.bauhaus->pango_font_desc);
    pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
    pango_font_description_set_absolute_size(desc, (.06 * height) * PANGO_SCALE);
    layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, desc);
    gdk_cairo_set_source_rgba(cr, &really_dark_bg_color);
    //cairo_select_font_face(cr, "Roboto", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, .06 * height);
    pango_layout_set_text(layout, _("coarse"), -1);
    pango_layout_get_pixel_extents(layout, &ink, NULL);
    cairo_move_to(cr, .02 * width - ink.y, .14 * height + ink.width);
    cairo_save(cr);
    cairo_rotate(cr, -M_PI * .5f);
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);
    pango_layout_set_text(layout, _("fine"), -1);
    pango_layout_get_pixel_extents(layout, &ink, NULL);
    cairo_move_to(cr, .98 * width - ink.height, .14 * height + ink.width);
    cairo_save(cr);
    cairo_rotate(cr, -M_PI * .5f);
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);

    switch(c->channel2)
    {
      case atrous_L:
      case atrous_c:
        dt_atrous_show_upper_label(cr, _("contrasty"), layout, ink);
        dt_atrous_show_lower_label(cr, _("smooth"), layout, ink);
        break;
      case atrous_Lt:
      case atrous_ct:
        dt_atrous_show_upper_label(cr, _("smooth"), layout, ink);
        dt_atrous_show_lower_label(cr, _("noisy"), layout, ink);
        break;
      default: // case atrous_s:
        dt_atrous_show_upper_label(cr, _("bold"), layout, ink);
        dt_atrous_show_lower_label(cr, _("dull"), layout, ink);
        break;
    }
    pango_font_description_free(desc);
    g_object_unref(layout);
  }


  cairo_destroy(cr);
  cairo_set_source_surface(crf, cst, 0, 0);
  cairo_paint(crf);
  cairo_surface_destroy(cst);
  return TRUE;
}

static gboolean area_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_atrous_gui_data_t *c = (dt_iop_atrous_gui_data_t *)self->gui_data;
  dt_iop_atrous_params_t *p = (dt_iop_atrous_params_t *)self->params;
  const int inset = INSET;
  GtkAllocation allocation;
  gtk_widget_get_allocation(widget, &allocation);
  int height = allocation.height - 2 * inset, width = allocation.width - 2 * inset;
  if(!c->dragging) c->mouse_x = CLAMP(event->x - inset, 0, width) / (float)width;
  c->mouse_y = 1.0 - CLAMP(event->y - inset, 0, height) / (float)height;
  int ch2 = c->channel;
  if(c->channel == atrous_L) ch2 = atrous_Lt;
  if(c->channel == atrous_c) ch2 = atrous_ct;
  if(c->dragging)
  {
    // drag y-positions
    *p = c->drag_params;
    if(c->x_move >= 0)
    {
      const float mx = CLAMP(event->x - inset, 0, width) / (float)width;
      if(c->x_move > 0 && c->x_move < BANDS - 1)
      {
        const float minx = p->x[c->channel][c->x_move - 1] + 0.001f;
        const float maxx = p->x[c->channel][c->x_move + 1] - 0.001f;
        p->x[ch2][c->x_move] = p->x[c->channel][c->x_move] = fminf(maxx, fmaxf(minx, mx));
      }
    }
    else
    {
      get_params(p, c->channel2, c->mouse_x, c->mouse_y + c->mouse_pick, c->mouse_radius);
    }
    dt_dev_add_history_item(darktable.develop, self, TRUE);
  }
  else if(event->y > height)
  {
    // move x-positions
    c->x_move = 0;
    float dist = fabs(p->x[c->channel][0] - c->mouse_x);
    for(int k = 1; k < BANDS; k++)
    {
      float d2 = fabs(p->x[c->channel][k] - c->mouse_x);
      if(d2 < dist)
      {
        c->x_move = k;
        dist = d2;
      }
    }
  }
  else
  {
    // choose between bottom and top curve:
    int ch = c->channel;
    float dist = 1000000.0f;
    for(int k = 0; k < BANDS; k++)
    {
      float d2 = fabs(p->x[c->channel][k] - c->mouse_x);
      if(d2 < dist)
      {
        if(fabs(c->mouse_y - p->y[ch][k]) < fabs(c->mouse_y - p->y[ch2][k]))
          c->channel2 = ch;
        else
          c->channel2 = ch2;
        dist = d2;
      }
    }
    // don't move x-positions:
    c->x_move = -1;
  }
  gtk_widget_queue_draw(widget);
  gint x, y;
#if GTK_CHECK_VERSION(3, 20, 0)
  gdk_window_get_device_position(event->window,
      gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(widget))),
      &x, &y, 0);
#else
  gdk_window_get_device_position(event->window,
                                 gdk_device_manager_get_client_pointer(
                                     gdk_display_get_device_manager(gdk_window_get_display(event->window))),
                                 &x, &y, NULL);
#endif
  return TRUE;
}

static gboolean area_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(event->button == 1 && event->type == GDK_2BUTTON_PRESS)
  {
    // reset current curve
    dt_iop_atrous_params_t *p = (dt_iop_atrous_params_t *)self->params;
    dt_iop_atrous_params_t *d = (dt_iop_atrous_params_t *)self->default_params;
    dt_iop_atrous_gui_data_t *c = (dt_iop_atrous_gui_data_t *)self->gui_data;
    reset_mix(self);
    for(int k = 0; k < BANDS; k++)
    {
      p->x[c->channel2][k] = d->x[c->channel2][k];
      p->y[c->channel2][k] = d->y[c->channel2][k];
    }
    dt_dev_add_history_item(darktable.develop, self, TRUE);
    gtk_widget_queue_draw(self->widget);
  }
  else if(event->button == 1)
  {
    // set active point
    dt_iop_atrous_gui_data_t *c = (dt_iop_atrous_gui_data_t *)self->gui_data;
    reset_mix(self);
    const int inset = INSET;
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int height = allocation.height - 2 * inset, width = allocation.width - 2 * inset;
    c->mouse_pick
        = dt_draw_curve_calc_value(c->minmax_curve, CLAMP(event->x - inset, 0, width) / (float)width);
    c->mouse_pick -= 1.0 - CLAMP(event->y - inset, 0, height) / (float)height;
    c->dragging = 1;
    return TRUE;
  }
  return FALSE;
}

static gboolean area_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  if(event->button == 1)
  {
    dt_iop_module_t *self = (dt_iop_module_t *)user_data;
    dt_iop_atrous_gui_data_t *c = (dt_iop_atrous_gui_data_t *)self->gui_data;
    c->dragging = 0;
    reset_mix(self);
    return TRUE;
  }
  return FALSE;
}

static gboolean area_scrolled(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_atrous_gui_data_t *c = (dt_iop_atrous_gui_data_t *)self->gui_data;

  if(((event->state & gtk_accelerator_get_default_mod_mask()) == darktable.gui->sidebar_scroll_mask) != dt_conf_get_bool("darkroom/ui/sidebar_scroll_default")) return FALSE;
  gdouble delta_y;
  if(dt_gui_get_scroll_deltas(event, NULL, &delta_y))
  {
    c->mouse_radius = CLAMP(c->mouse_radius * (1.0 + 0.1 * delta_y), 0.25 / BANDS, 1.0);
    gtk_widget_queue_draw(widget);
  }
  return TRUE;
}

static void tab_switch(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_atrous_gui_data_t *c = (dt_iop_atrous_gui_data_t *)self->gui_data;
  if(self->dt->gui->reset) return;
  c->channel = c->channel2 = (atrous_channel_t)page_num;
  gtk_widget_queue_draw(self->widget);
}

static void mix_callback(GtkWidget *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_atrous_params_t *p = (dt_iop_atrous_params_t *)self->params;
  dt_iop_atrous_params_t *d = (dt_iop_atrous_params_t *)self->default_params;
  dt_iop_atrous_gui_data_t *c = (dt_iop_atrous_gui_data_t *)self->gui_data;
  const float mix = dt_bauhaus_slider_get(slider);
  for(int ch = 0; ch < atrous_none; ch++)
    for(int k = 0; k < BANDS; k++)
    {
      p->x[ch][k] = fminf(1.0f, fmaxf(0.0f, d->x[ch][k] + mix * (c->drag_params.x[ch][k] - d->x[ch][k])));
      p->y[ch][k] = fminf(1.0f, fmaxf(0.0f, d->y[ch][k] + mix * (c->drag_params.y[ch][k] - d->y[ch][k])));
    }
  dt_dev_add_history_item(darktable.develop, self, TRUE);
  gtk_widget_queue_draw(self->widget);
}

void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_atrous_gui_data_t));
  dt_iop_atrous_gui_data_t *c = (dt_iop_atrous_gui_data_t *)self->gui_data;
  dt_iop_atrous_params_t *p = (dt_iop_atrous_params_t *)self->params;

  c->num_samples = 0;
  c->band_max = 0;
  c->channel = c->channel2 = dt_conf_get_int("plugins/darkroom/atrous/gui_channel");
  int ch = (int)c->channel;
  c->minmax_curve = dt_draw_curve_new(0.0, 1.0, CATMULL_ROM);
  for(int k = 0; k < BANDS; k++) (void)dt_draw_curve_add_point(c->minmax_curve, p->x[ch][k], p->y[ch][k]);
  c->mouse_x = c->mouse_y = c->mouse_pick = -1.0;
  c->dragging = 0;
  c->x_move = -1;
  c->mouse_radius = 1.0 / BANDS;
  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, DT_BAUHAUS_SPACE);
  dt_gui_add_help_link(self->widget, dt_get_help_url(self->op));
  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), vbox, FALSE, FALSE, 0);

  c->channel_tabs = GTK_NOTEBOOK(gtk_notebook_new());

  gtk_notebook_append_page(GTK_NOTEBOOK(c->channel_tabs),
                           GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0)), gtk_label_new(_("luma")));
  gtk_widget_set_tooltip_text(gtk_notebook_get_tab_label(c->channel_tabs, gtk_notebook_get_nth_page(c->channel_tabs, -1)),
                              _("change lightness at each feature size"));
  gtk_notebook_append_page(GTK_NOTEBOOK(c->channel_tabs),
                           GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0)),
                           gtk_label_new(_("chroma")));
  gtk_widget_set_tooltip_text(gtk_notebook_get_tab_label(c->channel_tabs, gtk_notebook_get_nth_page(c->channel_tabs, -1)),
                              _("change color saturation at each feature size"));
  gtk_notebook_append_page(GTK_NOTEBOOK(c->channel_tabs),
                           GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0)), gtk_label_new(_("edges")));
  gtk_widget_set_tooltip_text(gtk_notebook_get_tab_label(c->channel_tabs, gtk_notebook_get_nth_page(c->channel_tabs, -1)),
                              _("change edge halos at each feature size\nonly changes results of luma and chroma tabs"));

  gtk_widget_show_all(GTK_WIDGET(gtk_notebook_get_nth_page(c->channel_tabs, c->channel)));
  gtk_notebook_set_current_page(GTK_NOTEBOOK(c->channel_tabs), c->channel);

  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(c->channel_tabs), FALSE, FALSE, 0);

  g_signal_connect(G_OBJECT(c->channel_tabs), "switch_page", G_CALLBACK(tab_switch), self);

  // graph
  c->area = GTK_DRAWING_AREA(dtgtk_drawing_area_new_with_aspect_ratio(0.75));
  gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(c->area), TRUE, TRUE, 0);

  gtk_widget_add_events(GTK_WIDGET(c->area), GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK
                                             | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                                             | GDK_LEAVE_NOTIFY_MASK | darktable.gui->scroll_mask);
  g_signal_connect(G_OBJECT(c->area), "draw", G_CALLBACK(area_draw), self);
  g_signal_connect(G_OBJECT(c->area), "button-press-event", G_CALLBACK(area_button_press), self);
  g_signal_connect(G_OBJECT(c->area), "button-release-event", G_CALLBACK(area_button_release), self);
  g_signal_connect(G_OBJECT(c->area), "motion-notify-event", G_CALLBACK(area_motion_notify), self);
  g_signal_connect(G_OBJECT(c->area), "leave-notify-event", G_CALLBACK(area_leave_notify), self);
  g_signal_connect(G_OBJECT(c->area), "enter-notify-event", G_CALLBACK(area_enter_notify), self);
  g_signal_connect(G_OBJECT(c->area), "scroll-event", G_CALLBACK(area_scrolled), self);

  // mix slider
  c->mix = dt_bauhaus_slider_new_with_range(self, -2.0f, 2.0f, 0.1f, 1.0f, 3);
  dt_bauhaus_widget_set_label(c->mix, NULL, _("mix"));
  gtk_widget_set_tooltip_text(c->mix, _("make effect stronger or weaker"));
  gtk_box_pack_start(GTK_BOX(self->widget), c->mix, TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(c->mix), "value-changed", G_CALLBACK(mix_callback), self);
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  dt_iop_atrous_gui_data_t *c = (dt_iop_atrous_gui_data_t *)self->gui_data;
  dt_conf_set_int("plugins/darkroom/atrous/gui_channel", c->channel);
  dt_draw_curve_destroy(c->minmax_curve);
  free(self->gui_data);
  self->gui_data = NULL;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
