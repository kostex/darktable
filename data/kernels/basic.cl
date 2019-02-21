/*
    This file is part of darktable,
    copyright (c) 2009--2013 johannes hanika.
    copyright (c) 2014 Ulrich Pegelow.
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

#include "common.h"

#include "colorspace.cl"

int
BL(const int row, const int col)
{
  return (((row & 1) << 1) + (col & 1));
}

kernel void
rawprepare_1f(read_only image2d_t in, write_only image2d_t out,
              const int width, const int height,
              const int cx, const int cy,
              global const float *sub, global const float *div,
              const int rx, const int ry)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  const float pixel = read_imageui(in, sampleri, (int2)(x + cx, y + cy)).x;

  const int id = BL(ry+cy+y, rx+cx+x);
  const float pixel_scaled = (pixel - sub[id]) / div[id];

  write_imagef(out, (int2)(x, y), (float4)(pixel_scaled, 0.0f, 0.0f, 0.0f));
}

kernel void
rawprepare_1f_unnormalized(read_only image2d_t in, write_only image2d_t out,
                           const int width, const int height,
                           const int cx, const int cy,
                           global const float *sub, global const float *div,
                           const int rx, const int ry)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width  || y >= height) return;

  const float pixel = read_imagef(in, sampleri, (int2)(x + cx, y + cy)).x;

  const int id = BL(ry+cy+y, rx+cx+x);
  const float pixel_scaled = (pixel - sub[id]) / div[id];

  write_imagef(out, (int2)(x, y), (float4)(pixel_scaled, 0.0f, 0.0f, 0.0f));
}

kernel void
rawprepare_4f(read_only image2d_t in, write_only image2d_t out,
              const int width, const int height,
              const int cx, const int cy,
              global const float *black, global const float *div)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x + cx, y + cy));
  pixel.xyz = (pixel.xyz - black[0]) / div[0];

  write_imagef(out, (int2)(x, y), pixel);
}

kernel void
invert_1f(read_only image2d_t in, write_only image2d_t out, const int width, const int height, global float *color,
          const unsigned int filters, const int rx, const int ry)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= width || y >= height) return;
  const float pixel = read_imagef(in, sampleri, (int2)(x, y)).x;
  const float inv_pixel = color[FC(ry+y, rx+x, filters)] - pixel;

  write_imagef (out, (int2)(x, y), (float4)(clamp(inv_pixel, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f));
}

kernel void
invert_4f(read_only image2d_t in, write_only image2d_t out, const int width, const int height, global float *color,
                const unsigned int filters, const int rx, const int ry)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= width || y >= height) return;
  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  pixel.x = color[0] - pixel.x;
  pixel.y = color[1] - pixel.y;
  pixel.z = color[2] - pixel.z;
  pixel.xyz = clamp(pixel.xyz, 0.0f, 1.0f);

  write_imagef (out, (int2)(x, y), pixel);
}

kernel void
whitebalance_1f(read_only image2d_t in, write_only image2d_t out, const int width, const int height, global float *coeffs,
    const unsigned int filters, const int rx, const int ry, global const unsigned char (*const xtrans)[6])
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= width || y >= height) return;
  const float pixel = read_imagef(in, sampleri, (int2)(x, y)).x;
  write_imagef (out, (int2)(x, y), (float4)(pixel * coeffs[FC(ry+y, rx+x, filters)], 0.0f, 0.0f, 0.0f));
}

kernel void
whitebalance_1f_xtrans(read_only image2d_t in, write_only image2d_t out, const int width, const int height, global float *coeffs,
    const unsigned int filters, const int rx, const int ry, global const unsigned char (*const xtrans)[6])
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= width || y >= height) return;
  const float pixel = read_imagef(in, sampleri, (int2)(x, y)).x;
  write_imagef (out, (int2)(x, y), (float4)(pixel * coeffs[FCxtrans(ry+y, rx+x, xtrans)], 0.0f, 0.0f, 0.0f));
}


kernel void
whitebalance_4f(read_only image2d_t in, write_only image2d_t out, const int width, const int height, global float *coeffs,
    const unsigned int filters, const int rx, const int ry, global const unsigned char (*const xtrans)[6])
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= width || y >= height) return;
  const float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  write_imagef (out, (int2)(x, y), (float4)(pixel.x * coeffs[0], pixel.y * coeffs[1], pixel.z * coeffs[2], pixel.w));
}

/* kernel for the exposure plugin. should work transparently with float4 and float image2d. */
kernel void
exposure (read_only image2d_t in, write_only image2d_t out, const int width, const int height, const float black, const float scale)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;
  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  pixel.xyz = (pixel.xyz - black)*scale;
  write_imagef (out, (int2)(x, y), pixel);
}

/* kernel for the highlights plugin. */
kernel void
highlights_4f_clip (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                    const int mode, const float clip)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  // 4f/pixel means that this has been debayered already.
  // it's thus hopeless to recover highlights here (this code path is just used for preview and non-raw images)
  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  // default: // 0, DT_IOP_HIGHLIGHTS_CLIP
  pixel.x = fmin(clip, pixel.x);
  pixel.y = fmin(clip, pixel.y);
  pixel.z = fmin(clip, pixel.z);
  write_imagef (out, (int2)(x, y), pixel);
}

kernel void
highlights_1f_clip (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                    const float clip, const int rx, const int ry, const int filters)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float pixel = read_imagef(in, sampleri, (int2)(x, y)).x;

  pixel = fmin(clip, pixel);

  write_imagef (out, (int2)(x, y), pixel);
}

#define SQRT3 1.7320508075688772935274463415058723669f
#define SQRT12 3.4641016151377545870548926830117447339f // 2*SQRT3
kernel void
highlights_1f_lch_bayer (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                         const float clip, const int rx, const int ry, const int filters)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  int clipped = 0;
  float R = 0.0f;
  float Gmin = FLT_MAX;
  float Gmax = -FLT_MAX;
  float B = 0.0f;
  float pixel = 0.0f;

  // sample 1 bayer block. thus we will have 2 green values.
  for(int jj = 0; jj <= 1; jj++)
  {
    for(int ii = 0; ii <= 1; ii++)
    {
      const float val = read_imagef(in, sampleri, (int2)(x+ii, y+jj)).x;

      pixel = (ii == 0 && jj == 0) ? val : pixel;

      clipped = (clipped || (val > clip));

      const int c = FC(y + jj + ry, x + ii + rx, filters);

      switch(c)
      {
        case 0:
          R = val;
          break;
        case 1:
          Gmin = min(Gmin, val);
          Gmax = max(Gmax, val);
          break;
        case 2:
          B = val;
          break;
      }
    }
  }

  if(clipped)
  {
    const float Ro = min(R, clip);
    const float Go = min(Gmin, clip);
    const float Bo = min(B, clip);

    const float L = (R + Gmax + B) / 3.0f;

    float C = SQRT3 * (R - Gmax);
    float H = 2.0f * B - Gmax - R;

    const float Co = SQRT3 * (Ro - Go);
    const float Ho = 2.0f * Bo - Go - Ro;

    const float ratio = (R != Gmax && Gmax != B) ? sqrt((Co * Co + Ho * Ho) / (C * C + H * H)) : 1.0f;

    C *= ratio;
    H *= ratio;

    /*
     * backtransform proof, sage:
     *
     * R,G,B,L,C,H = var('R,G,B,L,C,H')
     * solve([L==(R+G+B)/3, C==sqrt(3)*(R-G), H==2*B-G-R], R, G, B)
     *
     * result:
     * [[R == 1/6*sqrt(3)*C - 1/6*H + L, G == -1/6*sqrt(3)*C - 1/6*H + L, B == 1/3*H + L]]
     */
    const int c = FC(y + ry, x + rx, filters);
    C = (c == 1) ? -C : C;

    pixel = L;
    pixel += (c == 2) ? H / 3.0f : -H / 6.0f + C / SQRT12;
  }

  write_imagef (out, (int2)(x, y), pixel);
}


kernel void
highlights_1f_lch_xtrans (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                         const float clip, const int rx, const int ry, global const unsigned char (*const xtrans)[6],
                         local float *buffer)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  const int xlsz = get_local_size(0);
  const int ylsz = get_local_size(1);
  const int xlid = get_local_id(0);
  const int ylid = get_local_id(1);
  const int xgid = get_group_id(0);
  const int ygid = get_group_id(1);

  // individual control variable in this work group and the work group size
  const int l = mad24(ylid, xlsz, xlid);
  const int lsz = mul24(xlsz, ylsz);

  // stride and maximum capacity of local buffer
  // cells of 1*float per pixel with a surrounding border of 2 cells
  const int stride = xlsz + 2*2;
  const int maxbuf = mul24(stride, ylsz + 2*2);

  // coordinates of top left pixel of buffer
  // this is 2 pixel left and above of the work group origin
  const int xul = mul24(xgid, xlsz) - 2;
  const int yul = mul24(ygid, ylsz) - 2;

  // populate local memory buffer
  for(int n = 0; n <= maxbuf/lsz; n++)
  {
    const int bufidx = mad24(n, lsz, l);
    if(bufidx >= maxbuf) continue;
    const int xx = xul + bufidx % stride;
    const int yy = yul + bufidx / stride;
    buffer[bufidx] = read_imagef(in, sampleri, (int2)(xx, yy)).x;
  }

  // center buffer around current x,y-Pixel
  buffer += mad24(ylid + 2, stride, xlid + 2);

  barrier(CLK_LOCAL_MEM_FENCE);

  if(x >= width || y >= height) return;

  float pixel = 0.0f;

  if(x < 2 || x > width - 3 || y < 2 || y > height - 3)
  {
    // fast path for border
    pixel = min(clip, buffer[0]);
  }
  else
  {
    // if current pixel is clipped, always reconstruct
    int clipped = (buffer[0] > clip);

    if(!clipped)
    {
      clipped = 1;
      // check if there is any 3x3 block touching the current
      // pixel which has no clipping, as then we don't need to
      // reconstruct the current pixel. This avoids zippering in
      // edge transitions from clipped to unclipped areas. The
      // X-Trans sensor seems prone to this, unlike Bayer, due
      // to its irregular pattern.
      for(int offset_j = -2; offset_j <= 0; offset_j++)
      {
        for(int offset_i = -2; offset_i <= 0; offset_i++)
        {
          if(clipped)
          {
            clipped = 0;
            for(int jj = offset_j; jj <= offset_j + 2; jj++)
            {
              for(int ii = offset_i; ii <= offset_i + 2; ii++)
              {
                const float val = buffer[mad24(jj, stride, ii)];
                clipped = (clipped || (val > clip));
              }
            }
          }
        }
      }
    }

    if(clipped)
    {
      float mean[3] = { 0.0f, 0.0f, 0.0f };
      int cnt[3] = { 0, 0, 0 };
      float RGBmax[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

      for(int jj = -1; jj <= 1; jj++)
      {
        for(int ii = -1; ii <= 1; ii++)
        {
          const float val = buffer[mad24(jj, stride, ii)];
          const int c = FCxtrans(y + jj + ry, x + ii + rx, xtrans);
          mean[c] += val;
          cnt[c]++;
          RGBmax[c] = max(RGBmax[c], val);
        }
      }

      const float Ro = min(mean[0]/cnt[0], clip);
      const float Go = min(mean[1]/cnt[1], clip);
      const float Bo = min(mean[2]/cnt[2], clip);

      const float R = RGBmax[0];
      const float G = RGBmax[1];
      const float B = RGBmax[2];

      const float L = (R + G + B) / 3.0f;
      float C = SQRT3 * (R - G);
      float H = 2.0f * B - G - R;

      const float Co = SQRT3 * (Ro - Go);
      const float Ho = 2.0f * Bo - Go - Ro;

      if(R != G && G != B)
      {
        const float ratio = sqrt((Co * Co + Ho * Ho) / (C * C + H * H));
        C *= ratio;
        H *= ratio;
      }

      float RGB[3] = { 0.0f, 0.0f, 0.0f };

      RGB[0] = L - H / 6.0f + C / SQRT12;
      RGB[1] = L - H / 6.0f - C / SQRT12;
      RGB[2] = L + H / 3.0f;

      pixel = RGB[FCxtrans(y + ry, x + rx, xtrans)];
    }
    else
      pixel = buffer[0];
  }

  write_imagef (out, (int2)(x, y), pixel);
}
#undef SQRT3
#undef SQRT12

float
lookup_unbounded(read_only image2d_t lut, const float x, global const float *a)
{
  // in case the tone curve is marked as linear, return the fast
  // path to linear unbounded (does not clip x at 1)
  if(a[0] >= 0.0f)
  {
    if(x < 1.0f)
    {
      const int xi = clamp((int)(x * 0x10000ul), 0, 0xffff);
      const int2 p = (int2)((xi & 0xff), (xi >> 8));
      return read_imagef(lut, sampleri, p).x;
    }
    else return a[1] * native_powr(x*a[0], a[2]);
  }
  else return x;
}

float
lookup_unbounded_twosided(read_only image2d_t lut, const float x, global const float *a)
{
  // in case the tone curve is marked as linear, return the fast
  // path to linear unbounded (does not clip x at 1)
  if(a[0] >= 0.0f)
  {
    const float ar = 1.0f/a[0];
    const float al = 1.0f - 1.0f/a[3];
    if(x < ar && x >= al)
    {
      // lut lookup
      const int xi = clamp((int)(x * 0x10000ul), 0, 0xffff);
      const int2 p = (int2)((xi & 0xff), (xi >> 8));
      return read_imagef(lut, sampleri, p).x;
    }
    else
    {
      // two-sided extrapolation (with inverted x-axis for left side)
      const float xx = (x >= ar) ? x : 1.0f - x;
      global const float *aa = (x >= ar) ? a : a + 3;
      return aa[1] * native_powr(xx*aa[0], aa[2]);
    }
  }
  else return x;
}

float
lerp_lookup_unbounded(read_only image2d_t lut, const float x, global const float *a)
{
  // in case the tone curve is marked as linear, return the fast
  // path to linear unbounded (does not clip x at 1)
  if(a[0] >= 0.0f)
  {
    if(x < 1.0f)
    {
      const float ft = clamp(x * (float)0xffff, 0.0f, (float)0xffff);
      const int t = ft < 0xfffe ? ft : 0xfffe;
      const float f = ft - t;
      const int2 p1 = (int2)((t & 0xff), (t >> 8));
      const int2 p2 = (int2)(((t + 1) & 0xff), ((t + 1) >> 8));
      const float l1 = read_imagef(lut, sampleri, p1).x;
      const float l2 = read_imagef(lut, sampleri, p2).x;
      return l1 * (1.0f - f) + l2 * f;
    }
    else return a[1] * native_powr(x*a[0], a[2]);
  }
  else return x;
}

float
lookup(read_only image2d_t lut, const float x)
{
  int xi = clamp((int)(x * 0x10000ul), 0, 0xffff);
  int2 p = (int2)((xi & 0xff), (xi >> 8));
  return read_imagef(lut, sampleri, p).x;
}


/* kernel for the plugin colorin: unbound processing */
kernel void
colorin_unbound (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                 global float *cmat, global float *lmat,
                 read_only image2d_t lutr, read_only image2d_t lutg, read_only image2d_t lutb,
                 const int blue_mapping, global const float (*const a)[3])
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  float cam[3], XYZ[3];
  cam[0] = lerp_lookup_unbounded(lutr, pixel.x, a[0]);
  cam[1] = lerp_lookup_unbounded(lutg, pixel.y, a[1]);
  cam[2] = lerp_lookup_unbounded(lutb, pixel.z, a[2]);

  if(blue_mapping)
  {
    const float YY = cam[0] + cam[1] + cam[2];
    if(YY > 0.0f)
    {
      // manual gamut mapping. these values cause trouble when converting back from Lab to sRGB:
      const float zz = cam[2] / YY;
      // lower amount and higher bound_z make the effect smaller.
      // the effect is weakened the darker input values are, saturating at bound_Y
      const float bound_z = 0.5f, bound_Y = 0.8f;
      const float amount = 0.11f;
      if (zz > bound_z)
      {
        const float t = (zz - bound_z) / (1.0f - bound_z) * fmin(1.0f, YY / bound_Y);
        cam[1] += t * amount;
        cam[2] -= t * amount;
      }
    }
  }

  // now convert camera to XYZ using the color matrix
  for(int j=0;j<3;j++)
  {
    XYZ[j] = 0.0f;
    for(int i=0;i<3;i++) XYZ[j] += cmat[3*j+i] * cam[i];
  }
  float4 xyz = (float4)(XYZ[0], XYZ[1], XYZ[2], 0.0f);
  pixel.xyz = XYZ_to_Lab(xyz).xyz;
  write_imagef (out, (int2)(x, y), pixel);
}

/* kernel for the plugin colorin: with clipping */
kernel void
colorin_clipping (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                  global float *cmat, global float *lmat,
                  read_only image2d_t lutr, read_only image2d_t lutg, read_only image2d_t lutb,
                  const int blue_mapping, global const float (*const a)[3])
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  float cam[3], RGB[3], XYZ[3];
  cam[0] = lerp_lookup_unbounded(lutr, pixel.x, a[0]);
  cam[1] = lerp_lookup_unbounded(lutg, pixel.y, a[1]);
  cam[2] = lerp_lookup_unbounded(lutb, pixel.z, a[2]);

  if(blue_mapping)
  {
    const float YY = cam[0] + cam[1] + cam[2];
    if(YY > 0.0f)
    {
      // manual gamut mapping. these values cause trouble when converting back from Lab to sRGB:
      const float zz = cam[2] / YY;
      // lower amount and higher bound_z make the effect smaller.
      // the effect is weakened the darker input values are, saturating at bound_Y
      const float bound_z = 0.5f, bound_Y = 0.8f;
      const float amount = 0.11f;
      if (zz > bound_z)
      {
        const float t = (zz - bound_z) / (1.0f - bound_z) * fmin(1.0f, YY / bound_Y);
        cam[1] += t * amount;
        cam[2] -= t * amount;
      }
    }
  }

  // convert camera to RGB using the first color matrix
  for(int j=0;j<3;j++)
  {
    RGB[j] = 0.0f;
    for(int i=0;i<3;i++) RGB[j] += cmat[3*j+i] * cam[i];
  }

  // clamp at this stage
  for(int i=0; i<3; i++) RGB[i] = clamp(RGB[i], 0.0f, 1.0f);

  // convert clipped RGB to XYZ
  for(int j=0;j<3;j++)
  {
    XYZ[j] = 0.0f;
    for(int i=0;i<3;i++) XYZ[j] += lmat[3*j+i] * RGB[i];
  }

  float4 xyz = (float4)(XYZ[0], XYZ[1], XYZ[2], 0.0f);
  pixel.xyz = XYZ_to_Lab(xyz).xyz;
  write_imagef (out, (int2)(x, y), pixel);
}

/* kernel for the tonecurve plugin. */
kernel void
tonecurve (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
           read_only image2d_t table_L, read_only image2d_t table_a, read_only image2d_t table_b,
           const int autoscale_ab, const int unbound_ab, global float *coeffs_L, global float *coeffs_ab,
           const float low_approximation)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  const float L_in = pixel.x/100.0f;
  // use lut or extrapolation:
  const float L = lookup_unbounded(table_L, L_in, coeffs_L);

  if (autoscale_ab == 0)
  {
    const float a_in = (pixel.y + 128.0f) / 256.0f;
    const float b_in = (pixel.z + 128.0f) / 256.0f;

    if (unbound_ab == 0)
    {
      pixel.y = lookup(table_a, a_in);
      pixel.z = lookup(table_b, b_in);
    }
    else
    {
      // use lut or two-sided extrapolation
      pixel.y = lookup_unbounded_twosided(table_a, a_in, coeffs_ab);
      pixel.z = lookup_unbounded_twosided(table_b, b_in, coeffs_ab + 6);
    }
    pixel.x = L;
  }
  else if(autoscale_ab == 1)
  {
    if(L_in > 0.01f)
    {
      pixel.y *= L/pixel.x;
      pixel.z *= L/pixel.x;
    }
    else
    {
      pixel.y *= low_approximation;
      pixel.z *= low_approximation;
    }
    pixel.x = L;
  }
  else if(autoscale_ab == 2)
  {
    float4 xyz = Lab_to_XYZ(pixel);
    xyz.x = lookup_unbounded(table_L, xyz.x, coeffs_L);
    xyz.y = lookup_unbounded(table_L, xyz.y, coeffs_L);
    xyz.z = lookup_unbounded(table_L, xyz.z, coeffs_L);
    pixel.xyz = XYZ_to_Lab(xyz).xyz;
  }
  else if(autoscale_ab == 3)
  {
    float4 rgb = Lab_to_prophotorgb(pixel);
    rgb.x = lookup_unbounded(table_L, rgb.x, coeffs_L);
    rgb.y = lookup_unbounded(table_L, rgb.y, coeffs_L);
    rgb.z = lookup_unbounded(table_L, rgb.z, coeffs_L);
    pixel.xyz = prophotorgb_to_Lab(rgb).xyz;
  }

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernel for the colorcorrection plugin. */
__kernel void
colorcorrection (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                 const float saturation, const float a_scale, const float a_base,
                 const float b_scale, const float b_base)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  pixel.y = saturation*(pixel.y + pixel.x * a_scale + a_base);
  pixel.z = saturation*(pixel.z + pixel.x * b_scale + b_base);
  write_imagef (out, (int2)(x, y), pixel);
}


void
mul_mat_vec_2(const float4 m, const float2 *p, float2 *o)
{
  (*o).x = (*p).x*m.x + (*p).y*m.y;
  (*o).y = (*p).x*m.z + (*p).y*m.w;
}

void
backtransform(float2 *p, float2 *o, const float4 m, const float2 t)
{
  (*p).y /= (1.0f + (*p).x*t.x);
  (*p).x /= (1.0f + (*p).y*t.y);
  mul_mat_vec_2(m, p, o);
}

void
keystone_backtransform(float2 *i, const float4 k_space, const float2 ka, const float4 ma, const float2 mb)
{
  float xx = (*i).x - k_space.x;
  float yy = (*i).y - k_space.y;

  /*float u = ka.x-kb.x+kc.x-kd.x;
  float v = ka.x-kb.x;
  float w = ka.x-kd.x;
  float z = ka.x;
  //(*i).x = (xx/k_space.z)*(yy/k_space.w)*(ka.x-kb.x+kc.x-kd.x) - (xx/k_space.z)*(ka.x-kb.x) - (yy/k_space.w)*(ka.x-kd.x) + ka.x + k_space.x;
  (*i).x = (xx/k_space.z)*(yy/k_space.w)*u - (xx/k_space.z)*v - (yy/k_space.w)*w + z + k_space.x;
  u = ka.y-kb.y+kc.y-kd.y;
  v = ka.y-kb.y;
  w = ka.y-kd.y;
  z = ka.y;
  //(*i).y = (xx/k_space.z)*(yy/k_space.w)*(ka.y-kb.y+kc.y-kd.y) - (xx/k_space.z)*(ka.y-kb.y) - (yy/k_space.w)*(ka.y-kd.y) + ka.y + k_space.y;
  (*i).y = (xx/k_space.z)*(yy/k_space.w)*u - (xx/k_space.z)*v - (yy/k_space.w)*w + z + k_space.y;*/
  float div = ((ma.z*xx-ma.x*yy)*mb.y+(ma.y*yy-ma.w*xx)*mb.x+ma.x*ma.w-ma.y*ma.z);

  (*i).x = (ma.w*xx-ma.y*yy)/div + ka.x;
  (*i).y =-(ma.z*xx-ma.x*yy)/div + ka.y;
}


float
interpolation_func_bicubic(float t)
{
  float r;
  t = fabs(t);

  r = (t >= 2.0f) ? 0.0f : ((t > 1.0f) ? (0.5f*(t*(-t*t + 5.0f*t - 8.0f) + 4.0f)) : (0.5f*(t*(3.0f*t*t - 5.0f*t) + 2.0f)));

  return r;
}

#define DT_LANCZOS_EPSILON (1e-9f)

#if 0
float
interpolation_func_lanczos(float width, float t)
{
  float ta = fabs(t);

  float r = (ta > width) ? 0.0f : ((ta < DT_LANCZOS_EPSILON) ? 1.0f : width*native_sin(M_PI_F*t)*native_sin(M_PI_F*t/width)/(M_PI_F*M_PI_F*t*t));

  return r;
}
#else
float
sinf_fast(float t)
{
  const float a = 4.0f/(M_PI_F*M_PI_F);
  const float p = 0.225f;

  t = a*t*(M_PI_F - fabs(t));

  return p*(t*fabs(t) - t) + t;
}

float
interpolation_func_lanczos(float width, float t)
{
  /* Compute a value for sinf(pi.t) in [-pi pi] for which the value will be
   * correct */
  int a = (int)t;
  float r = t - (float)a;

  // Compute the correct sign for sinf(pi.r)
  union { float f; unsigned int i; } sign;
  sign.i = ((a&1)<<31) | 0x3f800000;

  return (DT_LANCZOS_EPSILON + width*sign.f*sinf_fast(M_PI_F*r)*sinf_fast(M_PI_F*t/width))/(DT_LANCZOS_EPSILON + M_PI_F*M_PI_F*t*t);
}
#endif


/* kernel for clip&rotate: bilinear interpolation */
__kernel void
clip_rotate_bilinear(read_only image2d_t in, write_only image2d_t out, const int width, const int height,
            const int in_width, const int in_height,
            const int2 roi_in, const float2 roi_out, const float scale_in, const float scale_out,
            const int flip, const float2 t, const float2 k, const float4 mat,
            const float4 k_space, const float2 ka, const float4 ma, const float2 mb)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float2 pi, po;

  pi.x = roi_out.x + x + 0.5f;
  pi.y = roi_out.y + y + 0.5f;

  pi.x -= flip ? t.y * scale_out : t.x * scale_out;
  pi.y -= flip ? t.x * scale_out : t.y * scale_out;

  pi /= scale_out;
  backtransform(&pi, &po, mat, k);
  po *= scale_in;

  po.x += t.x * scale_in;
  po.y += t.y * scale_in;

  if (k_space.z > 0.0f) keystone_backtransform(&po,k_space,ka,ma,mb);

  po.x -= roi_in.x + 0.5f;
  po.y -= roi_in.y + 0.5f;

  const int ii = (int)po.x;
  const int jj = (int)po.y;

  float4 o = (ii >=0 && jj >= 0 && ii < in_width && jj < in_height) ? read_imagef(in, samplerf, po) : (float4)0.0f;

  write_imagef (out, (int2)(x, y), o);
}



/* kernel for clip&rotate: bicubic interpolation */
__kernel void
clip_rotate_bicubic(read_only image2d_t in, write_only image2d_t out, const int width, const int height,
            const int in_width, const int in_height,
            const int2 roi_in, const float2 roi_out, const float scale_in, const float scale_out,
            const int flip, const float2 t, const float2 k, const float4 mat,
            const float4 k_space, const float2 ka, const float4 ma, const float2 mb)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 2;

  if(x >= width || y >= height) return;

  float2 pi, po;

  pi.x = roi_out.x + x + 0.5f;
  pi.y = roi_out.y + y + 0.5f;

  pi.x -= flip ? t.y * scale_out : t.x * scale_out;
  pi.y -= flip ? t.x * scale_out : t.y * scale_out;

  pi /= scale_out;
  backtransform(&pi, &po, mat, k);
  po *= scale_in;

  po.x += t.x * scale_in;
  po.y += t.y * scale_in;

  if (k_space.z > 0.0f) keystone_backtransform(&po,k_space,ka,ma,mb);

  po.x -= roi_in.x + 0.5f;
  po.y -= roi_in.y + 0.5f;

  int tx = po.x;
  int ty = po.y;

  float4 pixel = (float4)0.0f;
  float weight = 0.0f;

  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_bicubic((float)i - po.x);
    float wy = interpolation_func_bicubic((float)j - po.y);
    float w = wx * wy;

    pixel += read_imagef(in, sampleri, (int2)(i, j)) * w;
    weight += w;
  }

  pixel = (tx >= 0 && ty >= 0 && tx < in_width && ty < in_height) ? pixel / weight : (float4)0.0f;

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernel for clip&rotate: lanczos2 interpolation */
__kernel void
clip_rotate_lanczos2(read_only image2d_t in, write_only image2d_t out, const int width, const int height,
            const int in_width, const int in_height,
            const int2 roi_in, const float2 roi_out, const float scale_in, const float scale_out,
            const int flip, const float2 t, const float2 k, const float4 mat,
            const float4 k_space, const float2 ka, const float4 ma, const float2 mb)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 2;

  if(x >= width || y >= height) return;

  float2 pi, po;

  pi.x = roi_out.x + x + 0.5f;
  pi.y = roi_out.y + y + 0.5f;

  pi.x -= flip ? t.y * scale_out : t.x * scale_out;
  pi.y -= flip ? t.x * scale_out : t.y * scale_out;

  pi /= scale_out;
  backtransform(&pi, &po, mat, k);
  po *= scale_in;

  po.x += t.x * scale_in;
  po.y += t.y * scale_in;

  if (k_space.z > 0.0f) keystone_backtransform(&po,k_space,ka,ma,mb);

  po.x -= roi_in.x + 0.5f;
  po.y -= roi_in.y + 0.5f;

  int tx = po.x;
  int ty = po.y;

  float4 pixel = (float4)0.0f;
  float weight = 0.0f;

  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_lanczos(2, (float)i - po.x);
    float wy = interpolation_func_lanczos(2, (float)j - po.y);
    float w = wx * wy;

    pixel += read_imagef(in, sampleri, (int2)(i, j)) * w;
    weight += w;
  }

  pixel = (tx >= 0 && ty >= 0 && tx < in_width && ty < in_height) ? pixel / weight : (float4)0.0f;

  write_imagef (out, (int2)(x, y), pixel);
}



/* kernel for clip&rotate: lanczos3 interpolation */
__kernel void
clip_rotate_lanczos3(read_only image2d_t in, write_only image2d_t out, const int width, const int height,
            const int in_width, const int in_height,
            const int2 roi_in, const float2 roi_out, const float scale_in, const float scale_out,
            const int flip, const float2 t, const float2 k, const float4 mat,
            const float4 k_space, const float2 ka, const float4 ma, const float2 mb)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 3;

  if(x >= width || y >= height) return;

  float2 pi, po;

  pi.x = roi_out.x + x + 0.5f;
  pi.y = roi_out.y + y + 0.5f;

  pi.x -= flip ? t.y * scale_out : t.x * scale_out;
  pi.y -= flip ? t.x * scale_out : t.y * scale_out;

  pi /= scale_out;
  backtransform(&pi, &po, mat, k);
  po *= scale_in;

  po.x += t.x * scale_in;
  po.y += t.y * scale_in;

  if (k_space.z > 0.0f) keystone_backtransform(&po,k_space,ka,ma,mb);

  po.x -= roi_in.x + 0.5f;
  po.y -= roi_in.y + 0.5f;

  int tx = (int)po.x;
  int ty = (int)po.y;

  float4 pixel = (float4)0.0f;
  float weight = 0.0f;

  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_lanczos(3, (float)i - po.x);
    float wy = interpolation_func_lanczos(3, (float)j - po.y);
    float w = wx * wy;

    pixel += read_imagef(in, sampleri, (int2)(i, j)) * w;
    weight += w;
  }

  pixel = (tx >= 0 && ty >= 0 && tx < in_width && ty < in_height) ? pixel / weight : (float4)0.0f;

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernels for the lens plugin: bilinear interpolation */
kernel void
lens_distort_bilinear (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
               const int iwidth, const int iheight, const int roi_in_x, const int roi_in_y, global float *pi,
               const int do_nan_checks)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel;

  float rx, ry;
  const int piwidth = 2*3*width;
  global float *ppi = pi + mad24(y, piwidth, 2*3*x);

  if(do_nan_checks)
  {
    bool valid = true;

    for(int i = 0; i < 6; i++) valid = valid && isfinite(ppi[i]);

    if(!valid)
    {
      pixel = (float4)0.0f;
      write_imagef (out, (int2)(x, y), pixel);
      return;
    }
  }

  rx = ppi[0] - roi_in_x;
  ry = ppi[1] - roi_in_y;
  pixel.x = (rx >= 0 && ry >= 0 && rx <= iwidth - 1 && ry <= iheight - 1) ? read_imagef(in, samplerf, (float2)(rx, ry)).x : NAN;

  rx = ppi[2] - roi_in_x;
  ry = ppi[3] - roi_in_y;
  pixel.yw = (rx >= 0 && ry >= 0 && rx <= iwidth - 1 && ry <= iheight - 1) ? read_imagef(in, samplerf, (float2)(rx, ry)).yw : (float2)NAN;

  rx = ppi[4] - roi_in_x;
  ry = ppi[5] - roi_in_y;
  pixel.z = (rx >= 0 && ry >= 0 && rx <= iwidth - 1 && ry <= iheight - 1) ? read_imagef(in, samplerf, (float2)(rx, ry)).z : NAN;

  pixel = all(isfinite(pixel.xyz)) ? pixel : (float4)0.0f;

  write_imagef (out, (int2)(x, y), pixel);
}

/* kernels for the lens plugin: bicubic interpolation */
kernel void
lens_distort_bicubic (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                      const int iwidth, const int iheight, const int roi_in_x, const int roi_in_y, global float *pi,
                      const int do_nan_checks)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 2;

  if(x >= width || y >= height) return;

  float4 pixel = (float4)0.0f;

  float rx, ry;
  int tx, ty;
  float sum, weight;
  float2 sum2;
  const int piwidth = 2*3*width;
  global float *ppi = pi + mad24(y, piwidth, 2*3*x);

  if(do_nan_checks)
  {
    bool valid = true;

    for(int i = 0; i < 6; i++) valid = valid && isfinite(ppi[i]);

    if(!valid)
    {
      pixel = (float4)0.0f;
      write_imagef (out, (int2)(x, y), pixel);
      return;
    }
  }


  rx = ppi[0] - (float)roi_in_x;
  ry = ppi[1] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum = 0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_bicubic((float)i - rx);
    float wy = interpolation_func_bicubic((float)j - ry);
    float w = (i < 0 || j < 0 || i >= iwidth || j >= iheight) ? 0.0f : wx * wy;

    sum += read_imagef(in, samplerc, (int2)(i, j)).x * w;
    weight += w;
  }
  pixel.x = sum/weight;


  rx = ppi[2] - (float)roi_in_x;
  ry = ppi[3] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum2 = (float2)0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_bicubic((float)i - rx);
    float wy = interpolation_func_bicubic((float)j - ry);
    float w = (i < 0 || j < 0 || i >= iwidth || j >= iheight) ? 0.0f : wx * wy;

    sum2 += read_imagef(in, samplerc, (int2)(i, j)).yw * w;
    weight += w;
  }
  pixel.yw = sum2/weight;


  rx = ppi[4] - (float)roi_in_x;
  ry = ppi[5] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum = 0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_bicubic((float)i - rx);
    float wy = interpolation_func_bicubic((float)j - ry);
    float w = (i < 0 || j < 0 || i >= iwidth || j >= iheight) ? 0.0f : wx * wy;

    sum += read_imagef(in, samplerc, (int2)(i, j)).z * w;
    weight += w;
  }
  pixel.z = sum/weight;

  pixel = all(isfinite(pixel.xyz)) ? pixel : (float4)0.0f;

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernels for the lens plugin: lanczos2 interpolation */
kernel void
lens_distort_lanczos2 (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                      const int iwidth, const int iheight, const int roi_in_x, const int roi_in_y, global float *pi,
                      const int do_nan_checks)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 2;

  if(x >= width || y >= height) return;

  float4 pixel = (float4)0.0f;

  float rx, ry;
  int tx, ty;
  float sum, weight;
  float2 sum2;
  const int piwidth = 2*3*width;
  global float *ppi = pi + mad24(y, piwidth, 2*3*x);

  if(do_nan_checks)
  {
    bool valid = true;

    for(int i = 0; i < 6; i++) valid = valid && isfinite(ppi[i]);

    if(!valid)
    {
      pixel = (float4)0.0f;
      write_imagef (out, (int2)(x, y), pixel);
      return;
    }
  }


  rx = ppi[0] - (float)roi_in_x;
  ry = ppi[1] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum = 0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_lanczos(2, (float)i - rx);
    float wy = interpolation_func_lanczos(2, (float)j - ry);
    float w = (i < 0 || j < 0 || i >= iwidth || j >= iheight) ? 0.0f : wx * wy;

    sum += read_imagef(in, samplerc, (int2)(i, j)).x * w;
    weight += w;
  }
  pixel.x = sum/weight;


  rx = ppi[2] - (float)roi_in_x;
  ry = ppi[3] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum2 = (float2)0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_lanczos(2, (float)i - rx);
    float wy = interpolation_func_lanczos(2, (float)j - ry);
    float w = (i < 0 || j < 0 || i >= iwidth || j >= iheight) ? 0.0f : wx * wy;

    sum2 += read_imagef(in, samplerc, (int2)(i, j)).yw * w;
    weight += w;
  }
  pixel.yw = sum2/weight;


  rx = ppi[4] - (float)roi_in_x;
  ry = ppi[5] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum = 0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_lanczos(2, (float)i - rx);
    float wy = interpolation_func_lanczos(2, (float)j - ry);
    float w = (i < 0 || j < 0 || i >= iwidth || j >= iheight) ? 0.0f : wx * wy;

    sum += read_imagef(in, samplerc, (int2)(i, j)).z * w;
    weight += w;
  }
  pixel.z = sum/weight;

  pixel = all(isfinite(pixel.xyz)) ? pixel : (float4)0.0f;

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernels for the lens plugin: lanczos3 interpolation */
kernel void
lens_distort_lanczos3 (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                      const int iwidth, const int iheight, const int roi_in_x, const int roi_in_y, global float *pi,
                      const int do_nan_checks)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 3;

  if(x >= width || y >= height) return;

  float4 pixel = (float4)0.0f;

  float rx, ry;
  int tx, ty;
  float sum, weight;
  float2 sum2;
  const int piwidth = 2*3*width;
  global float *ppi = pi + mad24(y, piwidth, 2*3*x);

  if(do_nan_checks)
  {
    bool valid = true;

    for(int i = 0; i < 6; i++) valid = valid && isfinite(ppi[i]);

    if(!valid)
    {
      pixel = (float4)0.0f;
      write_imagef (out, (int2)(x, y), pixel);
      return;
    }
  }

  rx = ppi[0] - (float)roi_in_x;
  ry = ppi[1] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum = 0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_lanczos(3, (float)i - rx);
    float wy = interpolation_func_lanczos(3, (float)j - ry);
    float w = (i < 0 || j < 0 || i >= iwidth || j >= iheight) ? 0.0f : wx * wy;

    sum += read_imagef(in, samplerc, (int2)(i, j)).x * w;
    weight += w;
  }
  pixel.x = sum/weight;


  rx = ppi[2] - (float)roi_in_x;
  ry = ppi[3] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum2 = (float2)0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_lanczos(3, (float)i - rx);
    float wy = interpolation_func_lanczos(3, (float)j - ry);
    float w = (i < 0 || j < 0 || i >= iwidth || j >= iheight) ? 0.0f : wx * wy;

    sum2 += read_imagef(in, samplerc, (int2)(i, j)).yw * w;
    weight += w;
  }
  pixel.yw = sum2/weight;


  rx = ppi[4] - (float)roi_in_x;
  ry = ppi[5] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum = 0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_lanczos(3, (float)i - rx);
    float wy = interpolation_func_lanczos(3, (float)j - ry);
    float w = (i < 0 || j < 0 || i >= iwidth || j >= iheight) ? 0.0f : wx * wy;

    sum += read_imagef(in, samplerc, (int2)(i, j)).z * w;
    weight += w;
  }
  pixel.z = sum/weight;

  pixel = all(isfinite(pixel.xyz)) ? pixel : (float4)0.0f;

  write_imagef (out, (int2)(x, y), pixel);
}



/* kernel for the ashift module: bilinear interpolation */
kernel void
ashift_bilinear(read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                const int iwidth, const int iheight, const int2 roi_in, const int2 roi_out,
                const float in_scale, const float out_scale, const float2 clip, global float *homograph)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float pin[3], pout[3];

  // convert output pixel coordinates to original image coordinates
  pout[0] = roi_out.x + x + clip.x;
  pout[1] = roi_out.y + y + clip.y;
  pout[0] /= out_scale;
  pout[1] /= out_scale;
  pout[2] = 1.0f;

  // apply homograph
  for(int i = 0; i < 3; i++)
  {
    pin[i] = 0.0f;
    for(int j = 0; j < 3; j++) pin[i] += homograph[3 * i + j] * pout[j];
  }

  // convert to input pixel coordinates
  pin[0] /= pin[2];
  pin[1] /= pin[2];
  pin[0] *= in_scale;
  pin[1] *= in_scale;
  pin[0] -= roi_in.x;
  pin[1] -= roi_in.y;

  // get output values by interpolation from input image using fast hardware bilinear interpolation
  float rx = pin[0];
  float ry = pin[1];
  int tx = rx;
  int ty = ry;

  float4 pixel = (tx >= 0 && ty >= 0 && tx < iwidth && ty < iheight) ? read_imagef(in, samplerf, (float2)(rx, ry)) : (float4)0.0f;

  write_imagef (out, (int2)(x, y), pixel);
}

/* kernel for the ashift module: bicubic interpolation */
kernel void
ashift_bicubic (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                const int iwidth, const int iheight, const int2 roi_in, const int2 roi_out,
                const float in_scale, const float out_scale, const float2 clip, global float *homograph)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 2;

  if(x >= width || y >= height) return;

  float pin[3], pout[3];

  // convert output pixel coordinates to original image coordinates
  pout[0] = roi_out.x + x + clip.x;
  pout[1] = roi_out.y + y + clip.y;
  pout[0] /= out_scale;
  pout[1] /= out_scale;
  pout[2] = 1.0f;

  // apply homograph
  for(int i = 0; i < 3; i++)
  {
    pin[i] = 0.0f;
    for(int j = 0; j < 3; j++) pin[i] += homograph[3 * i + j] * pout[j];
  }

  // convert to input pixel coordinates
  pin[0] /= pin[2];
  pin[1] /= pin[2];
  pin[0] *= in_scale;
  pin[1] *= in_scale;
  pin[0] -= roi_in.x;
  pin[1] -= roi_in.y;

  // get output values by interpolation from input image
  float rx = pin[0];
  float ry = pin[1];
  int tx = rx;
  int ty = ry;

  float4 pixel = (float4)0.0f;
  float weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_bicubic((float)i - rx);
    float wy = interpolation_func_bicubic((float)j - ry);
    float w = wx * wy;

    pixel += read_imagef(in, sampleri, (int2)(i, j)) * w;
    weight += w;
  }

  pixel = (tx >= 0 && ty >= 0 && tx < iwidth && ty < iheight) ? pixel/weight : (float4)0.0f;

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernel for the ashift module: lanczos2 interpolation */
kernel void
ashift_lanczos2(read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                const int iwidth, const int iheight, const int2 roi_in, const int2 roi_out,
                const float in_scale, const float out_scale, const float2 clip, global float *homograph)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 2;

  if(x >= width || y >= height) return;

  float pin[3], pout[3];

  // convert output pixel coordinates to original image coordinates
  pout[0] = roi_out.x + x + clip.x;
  pout[1] = roi_out.y + y + clip.y;
  pout[0] /= out_scale;
  pout[1] /= out_scale;
  pout[2] = 1.0f;

  // apply homograph
  for(int i = 0; i < 3; i++)
  {
    pin[i] = 0.0f;
    for(int j = 0; j < 3; j++) pin[i] += homograph[3 * i + j] * pout[j];
  }

  // convert to input pixel coordinates
  pin[0] /= pin[2];
  pin[1] /= pin[2];
  pin[0] *= in_scale;
  pin[1] *= in_scale;
  pin[0] -= roi_in.x;
  pin[1] -= roi_in.y;

  // get output values by interpolation from input image
  float rx = pin[0];
  float ry = pin[1];
  int tx = rx;
  int ty = ry;

  float4 pixel = (float4)0.0f;
  float weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_lanczos(2, (float)i - rx);
    float wy = interpolation_func_lanczos(2, (float)j - ry);
    float w = wx * wy;

    pixel += read_imagef(in, sampleri, (int2)(i, j)) * w;
    weight += w;
  }

  pixel = (tx >= 0 && ty >= 0 && tx < iwidth && ty < iheight) ? pixel/weight : (float4)0.0f;

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernels for the ashift module: lanczos3 interpolation */
kernel void
ashift_lanczos3(read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                const int iwidth, const int iheight, const int2 roi_in, const int2 roi_out,
                const float in_scale, const float out_scale, const float2 clip, global float *homograph)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 3;

  if(x >= width || y >= height) return;

  float pin[3], pout[3];

  // convert output pixel coordinates to original image coordinates
  pout[0] = roi_out.x + x + clip.x;
  pout[1] = roi_out.y + y + clip.y;
  pout[0] /= out_scale;
  pout[1] /= out_scale;
  pout[2] = 1.0f;

  // apply homograph
  for(int i = 0; i < 3; i++)
  {
    pin[i] = 0.0f;
    for(int j = 0; j < 3; j++) pin[i] += homograph[3 * i + j] * pout[j];
  }

  // convert to input pixel coordinates
  pin[0] /= pin[2];
  pin[1] /= pin[2];
  pin[0] *= in_scale;
  pin[1] *= in_scale;
  pin[0] -= roi_in.x;
  pin[1] -= roi_in.y;

  // get output values by interpolation from input image
  float rx = pin[0];
  float ry = pin[1];
  int tx = rx;
  int ty = ry;

  float4 pixel = (float4)0.0f;
  float weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    const int i = tx + ii;
    const int j = ty + jj;

    float wx = interpolation_func_lanczos(3, (float)i - rx);
    float wy = interpolation_func_lanczos(3, (float)j - ry);
    float w = wx * wy;

    pixel += read_imagef(in, sampleri, (int2)(i, j)) * w;
    weight += w;
  }

  pixel = (tx >= 0 && ty >= 0 && tx < iwidth && ty < iheight) ? pixel/weight : (float4)0.0f;

  write_imagef (out, (int2)(x, y), pixel);
}


kernel void
lens_vignette (read_only image2d_t in, write_only image2d_t out, const int width, const int height, global float4 *pi)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  float4 scale = pi[mad24(y, width, x)]/(float4)0.5f;

  pixel.xyz *= scale.xyz;

  write_imagef (out, (int2)(x, y), pixel);
}



/* kernel for flip */
__kernel void
flip(read_only image2d_t in, write_only image2d_t out, const int width, const int height, const int orientation)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  int nx = (orientation & 4) ? y : x;
  int ny = (orientation & 4) ? x : y;
  int wd = (orientation & 4) ? height : width;
  int ht = (orientation & 4) ? width : height;
  nx = (orientation & 2) ? wd - nx - 1 : nx;
  ny = (orientation & 1) ? ht - ny - 1 : ny;

  write_imagef (out, (int2)(nx, ny), pixel);
}


/* we use this exp approximation to maintain full identity with cpu path */
float
fast_expf(const float x)
{
  // meant for the range [-100.0f, 0.0f]. largest error ~ -0.06 at 0.0f.
  // will get _a_lot_ worse for x > 0.0f (9000 at 10.0f)..
  const int i1 = 0x3f800000u;
  // e^x, the comment would be 2^x
  const int i2 = 0x402DF854u;//0x40000000u;
  // const int k = CLAMPS(i1 + x * (i2 - i1), 0x0u, 0x7fffffffu);
  // without max clamping (doesn't work for large x, but is faster):
  const int k0 = i1 + x * (i2 - i1);
  const int k = k0 > 0 ? k0 : 0;
  const float f = *(const float *)&k;
  return f;
}


float
envelope(const float L)
{
  const float x = clamp(L/100.0f, 0.0f, 1.0f);
  // const float alpha = 2.0f;
  const float beta = 0.6f;
  if(x < beta)
  {
    // return 1.0f-fabsf(x/beta-1.0f)^2
    const float tmp = fabs(x/beta-1.0f);
    return 1.0f-tmp*tmp;
  }
  else
  {
    const float tmp1 = (1.0f-x)/(1.0f-beta);
    const float tmp2 = tmp1*tmp1;
    const float tmp3 = tmp2*tmp1;
    return 3.0f*tmp2 - 2.0f*tmp3;
  }
}

/* kernel for monochrome */
kernel void
monochrome_filter(
    read_only image2d_t in,
    write_only image2d_t out,
    const int width,
    const int height,
    const float a,
    const float b,
    const float size)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef (in,   sampleri, (int2)(x, y));
  // TODO: this could be a native_expf, or exp2f, need to evaluate comparisons with cpu though:
  pixel.x = 100.0f*fast_expf(-clamp(((pixel.y - a)*(pixel.y - a) + (pixel.z - b)*(pixel.z - b))/(2.0f * size), 0.0f, 1.0f));
  write_imagef (out, (int2)(x, y), pixel);
}

kernel void
monochrome(
    read_only image2d_t in,
    read_only image2d_t base,
    write_only image2d_t out,
    const int width,
    const int height,
    const float a,
    const float b,
    const float size,
    float highlights)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef (in,   sampleri, (int2)(x, y));
  float4 basep = read_imagef (base, sampleri, (int2)(x, y));
  float filter  = fast_expf(-clamp(((pixel.y - a)*(pixel.y - a) + (pixel.z - b)*(pixel.z - b))/(2.0f * size), 0.0f, 1.0f));
  float tt = envelope(pixel.x);
  float t  = tt + (1.0f-tt)*(1.0f-highlights);
  pixel.x = mix(pixel.x, pixel.x*basep.x/100.0f, t);
  pixel.y = pixel.z = 0.0f;
  write_imagef (out, (int2)(x, y), pixel);
}



/* kernel for the plugin colorout, fast matrix + shaper path only */
kernel void
colorout (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
          global float *mat, read_only image2d_t lutr, read_only image2d_t lutg, read_only image2d_t lutb,
          global const float (*const a)[3])
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  float XYZ[3], rgb[3];
  float4 xyz = Lab_to_XYZ(pixel);
  XYZ[0] = xyz.x;
  XYZ[1] = xyz.y;
  XYZ[2] = xyz.z;
  for(int i=0;i<3;i++)
  {
    rgb[i] = 0.0f;
    for(int j=0;j<3;j++) rgb[i] += mat[3*i+j]*XYZ[j];
  }
  pixel.x = lerp_lookup_unbounded(lutr, rgb[0], a[0]);
  pixel.y = lerp_lookup_unbounded(lutg, rgb[1], a[1]);
  pixel.z = lerp_lookup_unbounded(lutb, rgb[2], a[2]);
  write_imagef (out, (int2)(x, y), pixel);
}


/* kernel for the levels plugin */
kernel void
levels (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
        read_only image2d_t lut, const float in_low, const float in_high, const float in_inv_gamma)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  const float L = pixel.x;
  const float L_in = pixel.x/100.0f;

  if(L_in <= in_low)
  {
    pixel.x = 0.0f;
  }
  else if(L_in >= in_high)
  {
    float percentage = (L_in - in_low) / (in_high - in_low);
    pixel.x = 100.0f * pow(percentage, in_inv_gamma);
  }
  else
  {
    float percentage = (L_in - in_low) / (in_high - in_low);
    pixel.x = lookup(lut, percentage);
  }

  if(L_in > 0.01f)
  {
    pixel.y *= pixel.x/L;
    pixel.z *= pixel.x/L;
  }
  else
  {
    pixel.y *= pixel.x;
    pixel.z *= pixel.x;
  }

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernel for the colorzones plugin */
enum
{
  DT_IOP_COLORZONES_L = 0,
  DT_IOP_COLORZONES_C = 1,
  DT_IOP_COLORZONES_h = 2
};


kernel void
colorzones (read_only image2d_t in, write_only image2d_t out, const int width, const int height, const int channel,
            read_only image2d_t table_L, read_only image2d_t table_a, read_only image2d_t table_b)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  const float a = pixel.y;
  const float b = pixel.z;
  const float h = fmod(atan2(b, a) + 2.0f*M_PI_F, 2.0f*M_PI_F)/(2.0f*M_PI_F);
  const float C = sqrt(b*b + a*a);

  float select = 0.0f;
  float blend = 0.0f;

  switch(channel)
  {
    case DT_IOP_COLORZONES_L:
      select = fmin(1.0f, pixel.x/100.0f);
      break;
    case DT_IOP_COLORZONES_C:
      select = fmin(1.0f, C/128.0f);
      break;
    default:
    case DT_IOP_COLORZONES_h:
      select = h;
      blend = pow(1.0f - C/128.0f, 2.0f);
      break;
  }

  const float Lm = (blend * 0.5f + (1.0f-blend)*lookup(table_L, select)) - 0.5f;
  const float hm = (blend * 0.5f + (1.0f-blend)*lookup(table_b, select)) - 0.5f;
  blend *= blend; // saturation isn't as prone to artifacts:
  // const float Cm = 2.0f* (blend*0.5f + (1.0f-blend)*lookup(d->lut[1], select));
  const float Cm = 2.0f * lookup(table_a, select);
  const float L = pixel.x * pow(2.0f, 4.0f*Lm);

  pixel.x = L;
  pixel.y = cos(2.0f*M_PI_F*(h + hm)) * Cm * C;
  pixel.z = sin(2.0f*M_PI_F*(h + hm)) * Cm * C;

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernel for the zonesystem plugin */
kernel void
zonesystem (read_only image2d_t in, write_only image2d_t out, const int width, const int height, const int size,
            global float *zonemap_offset, global float *zonemap_scale)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  const float rzscale = (float)(size-1)/100.0f;
  const int rz = clamp((int)(pixel.x*rzscale), 0, size-2);
  const float zs = ((rz > 0) ? (zonemap_offset[rz]/pixel.x) : 0) + zonemap_scale[rz];

  pixel.xyz *= zs;

  write_imagef (out, (int2)(x, y), pixel);
}




/* kernel to fill an image with a color (for the borders plugin). */
kernel void
borders_fill (write_only image2d_t out, const int left, const int top, const int width, const int height, const float4 color)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x < left || y < top) return;
  if(x >= width + left || y >= height + top) return;

  write_imagef (out, (int2)(x, y), color);
}


/* kernel for the overexposed plugin. */
kernel void
overexposed (read_only image2d_t in, write_only image2d_t out, read_only image2d_t tmp, const int width, const int height,
             const float lower, const float upper, const float4 lower_color, const float4 upper_color)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  float4 pixel_tmp = read_imagef(tmp, sampleri, (int2)(x, y));

  if(pixel_tmp.x >= upper || pixel_tmp.y >= upper || pixel_tmp.z >= upper)
  {
    pixel.xyz = upper_color.xyz;
  }
  else if(pixel_tmp.x <= lower && pixel_tmp.y <= lower && pixel_tmp.z <= lower)
  {
    pixel.xyz = lower_color.xyz;
  }

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernel for the rawoverexposed plugin. */
kernel void
rawoverexposed_mark_cfa (
        read_only image2d_t in, write_only image2d_t out, global float *pi,
        const int width, const int height,
        read_only image2d_t raw, const int raw_width, const int raw_height,
        const unsigned int filters, global const unsigned char (*const xtrans)[6],
        global unsigned int *threshold, global float *colors)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  const int piwidth = 2*width;
  global float *ppi = pi + mad24(y, piwidth, 2*x);

  const int raw_x = ppi[0];
  const int raw_y = ppi[1];

  if(raw_x < 0 || raw_y < 0 || raw_x >= raw_width || raw_y >= raw_height) return;

  const uint raw_pixel = read_imageui(raw, sampleri, (int2)(raw_x, raw_y)).x;

  const int c = (filters == 9u) ? FCxtrans(raw_y, raw_x, xtrans) : FC(raw_y, raw_x, filters);

  if(raw_pixel < threshold[c]) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  global float *color = colors + mad24(4, c, 0);

  // cfa color
  pixel.x = color[0];
  pixel.y = color[1];
  pixel.z = color[2];

  write_imagef (out, (int2)(x, y), pixel);
}

kernel void
rawoverexposed_mark_solid (
        read_only image2d_t in, write_only image2d_t out, global float *pi,
        const int width, const int height,
        read_only image2d_t raw, const int raw_width, const int raw_height,
        const unsigned int filters, global const unsigned char (*const xtrans)[6],
        global unsigned int *threshold, const float4 solid_color)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  const int piwidth = 2*width;
  global float *ppi = pi + mad24(y, piwidth, 2*x);

  const int raw_x = ppi[0];
  const int raw_y = ppi[1];

  if(raw_x < 0 || raw_y < 0 || raw_x >= raw_width || raw_y >= raw_height) return;

  const uint raw_pixel = read_imageui(raw, sampleri, (int2)(raw_x, raw_y)).x;

  const int c = (filters == 9u) ? FCxtrans(raw_y, raw_x, xtrans) : FC(raw_y, raw_x, filters);

  if(raw_pixel < threshold[c]) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  // solid color
  pixel.xyz = solid_color.xyz;

  write_imagef (out, (int2)(x, y), pixel);
}

kernel void
rawoverexposed_falsecolor (
        read_only image2d_t in, write_only image2d_t out, global float *pi,
        const int width, const int height,
        read_only image2d_t raw, const int raw_width, const int raw_height,
        const unsigned int filters, global const unsigned char (*const xtrans)[6],
        global unsigned int *threshold)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  const int piwidth = 2*width;
  global float *ppi = pi + mad24(y, piwidth, 2*x);

  const int raw_x = ppi[0];
  const int raw_y = ppi[1];

  if(raw_x < 0 || raw_y < 0 || raw_x >= raw_width || raw_y >= raw_height) return;

  const uint raw_pixel = read_imageui(raw, sampleri, (int2)(raw_x, raw_y)).x;

  const int c = (filters == 9u) ? FCxtrans(raw_y, raw_x, xtrans) : FC(raw_y, raw_x, filters);

  if(raw_pixel < threshold[c]) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  float p[4];
  vstore4(pixel, 0, p);
  // falsecolor
  p[c] = 0.0f;
  pixel = vload4(0, p);

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernel for the lowlight plugin. */
kernel void
lowlight (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
          const float4 XYZ_sw, read_only image2d_t lut)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  const float c = 0.5f;
  const float threshold = 0.01f;

  float V;
  float w;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  float4 XYZ = Lab_to_XYZ(pixel);

  // calculate scotopic luminance
  if (XYZ.x > threshold)
  {
    // normal flow
    V = XYZ.y * ( 1.33f * ( 1.0f + (XYZ.y+XYZ.z)/XYZ.x) - 1.68f );
  }
  else
  {
    // low red flow, avoids "snow" on dark noisy areas
    V = XYZ.y * ( 1.33f * ( 1.0f + (XYZ.y+XYZ.z)/threshold) - 1.68f );
  }

  // scale using empiric coefficient and fit inside limits
  V = clamp(c*V, 0.0f, 1.0f);

  // blending coefficient from curve
  w = lookup(lut, pixel.x/100.0f);

  XYZ = w * XYZ + (1.0f - w) * V * XYZ_sw;

  pixel = XYZ_to_Lab(XYZ);

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernel for the contrast lightness saturation module */
kernel void
colisa (read_only image2d_t in, write_only image2d_t out, unsigned int width, unsigned int height, const float saturation,
        read_only image2d_t ctable, global const float *ca, read_only image2d_t ltable, global const float *la)
{
  const unsigned int x = get_global_id(0);
  const unsigned int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 i = read_imagef(in, sampleri, (int2)(x, y));
  float4 o;

  o.x = lookup_unbounded(ctable, i.x/100.0f, ca);
  o.x = lookup_unbounded(ltable, o.x/100.0f, la);
  o.y = i.y*saturation;
  o.z = i.z*saturation;
  o.w = i.w;

  write_imagef(out, (int2)(x, y), o);
}

/* kernel for the unbreak input profile module - gamma version */

kernel void
profilegamma (read_only image2d_t in, write_only image2d_t out, int width, int height,
        read_only image2d_t table, global const float *ta)
{
  const unsigned int x = get_global_id(0);
  const unsigned int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 i = read_imagef(in, sampleri, (int2)(x, y));
  float4 o;

  o.x = lookup_unbounded(table, i.x, ta);
  o.y = lookup_unbounded(table, i.y, ta);
  o.z = lookup_unbounded(table, i.z, ta);
  o.w = i.w;

  write_imagef(out, (int2)(x, y), o);
}

/* kernel for the unbreak input profile module - log version */
kernel void
profilegamma_log (read_only image2d_t in, write_only image2d_t out, int width, int height, const float dynamic_range, const float shadows_range, const float grey)
{
  const unsigned int x = get_global_id(0);
  const unsigned int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 i = read_imagef(in, sampleri, (int2)(x, y));
  const float4 noise = pow((float4)2.0f, (float4)-16.0f);
  const float4 dynamic4 = dynamic_range;
  const float4 shadows4 = shadows_range;
  const float4 grey4 = grey;

  float4 o;

  o = (i < noise) ? noise : i / grey4;
  o = (log2(o) - shadows4) / dynamic4;
  o = (o < noise) ? noise : o;
  i.xyz = o.xyz;

  write_imagef(out, (int2)(x, y), i);
}

/* kernel for the interpolation resample helper */
kernel void
interpolation_resample (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                        const global int *hmeta, const global int *vmeta,
                        const global int *hlength, const global int *vlength,
                        const global int *hindex, const global int *vindex,
                        const global float *hkernel, const global float *vkernel,
                        const int htaps, const int vtaps,
                        local float *lkernel, local int *lindex,
                        local float4 *buffer)
{
  const int x = get_global_id(0);
  const int yi = get_global_id(1);
  const int ylsz = get_local_size(1);
  const int xlid = get_local_id(0);
  const int ylid = get_local_id(1);
  const int y = yi / vtaps;
  const int iy = yi % vtaps;

  // Initialize resampling indices
  const int xm = min(x, width - 1);
  const int ym = min(y, height - 1);
  const int hlidx = hmeta[xm*3];   // H(orizontal) L(ength) I(n)d(e)x
  const int hkidx = hmeta[xm*3+1]; // H(orizontal) K(ernel) I(n)d(e)x
  const int hiidx = hmeta[xm*3+2]; // H(orizontal) I(ndex) I(n)d(e)x
  const int vlidx = vmeta[ym*3];   // V(ertical) L(ength) I(n)d(e)x
  const int vkidx = vmeta[ym*3+1]; // V(ertical) K(ernel) I(n)d(e)x
  const int viidx = vmeta[ym*3+2]; // V(ertical) I(ndex) I(n)d(e)x

  const int hl = hlength[hlidx];   // H(orizontal) L(ength)
  const int vl = vlength[vlidx];   // V(ertical) L(ength)

  // generate local copy of horizontal index field and kernel
  for(int n = 0; n <= htaps/ylsz; n++)
  {
    int k = mad24(n, ylsz, ylid);
    if(k >= hl) continue;
    lindex[k] = hindex[hiidx+k];
    lkernel[k] = hkernel[hkidx+k];
  }

  barrier(CLK_LOCAL_MEM_FENCE);

  // horizontal convolution kernel; store intermediate result in local buffer
  if(x < width && y < height)
  {
    const int yvalid = iy < vl;

    const int yy = yvalid ? vindex[viidx+iy] : -1;

    float4 vpixel = (float4)0.0f;

    for (int ix = 0; ix < hl && yvalid; ix++)
    {
      const int xx = lindex[ix];
      float4 hpixel = read_imagef(in, sampleri,(int2)(xx, yy));
      vpixel += hpixel * lkernel[ix];
    }

    buffer[ylid] = yvalid ? vpixel * vkernel[vkidx+iy] : (float4)0.0f;
  }
  else
    buffer[ylid] = (float4)0.0f;

  barrier(CLK_LOCAL_MEM_FENCE);

  // recursively reduce local buffer (vertical convolution kernel)
  for(int offset = vtaps / 2; offset > 0; offset >>= 1)
  {
    if (iy < offset)
    {
      buffer[ylid] += buffer[ylid + offset];
    }
    barrier(CLK_LOCAL_MEM_FENCE);
  }

  // store final result
  if (iy == 0 && x < width && y < height)
  {
    write_imagef (out, (int2)(x, y), buffer[ylid]);
  }
}
