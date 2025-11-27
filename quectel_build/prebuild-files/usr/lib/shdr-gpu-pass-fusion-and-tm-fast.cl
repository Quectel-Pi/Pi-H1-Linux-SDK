/****************************************************************************
 * Copyright (c) 2017-2020,2022 Qualcomm Technologies, Inc.                  *
 * All Rights Reserved.                                                      *
 * Confidential and Proprietary - Qualcomm Technologies, Inc.                *
 ****************************************************************************/

#define KERNEL_X_SIZE 4
#define KERNEL_Y_SIZE 2

#define SEF_LOWBND 64.0f
#define HALO_BRIGHTHR 0.95f
#define HALO_DARKTHR 0.1f
#define LEF_BRIGHTCOMPEN 0.95f
#define LEF_DARKCOMPEN 0.0f

// #define USE_HALF_FLOAT
#ifdef USE_HALF_FLOAT
#pragma OPENCL EXTENSION cl_khr_fp16 : enable
#define INT short
#define UINT ushort
#define UINT4 ushort4
#define FLOAT half
#define FLOAT2 half2
#define FLOAT4 half4
#define FLOAT8 half8
#define FLOAT16 half16
#define CONVERT_FLOAT(x) convert_half(x)
#define CONVERT_FLOAT2(x) convert_half2(x)
#define CONVERT_FLOAT4(x) convert_half4(x)
#define ZERO 0.0h
#define ONE 1.0h
#define TWO 2.0h
#define FOUR 4.0h
#define SIXTEEN_r 0.0625h
#define TWO56 256.0h
#else
#define INT int
#define UINT uint
#define UINT4 uint4
#define FLOAT float
#define FLOAT2 float2
#define FLOAT4 float4
#define FLOAT8 float8
#define FLOAT16 float16
#define CONVERT_FLOAT4(x) convert_float4(x)
#define CONVERT_FLOAT2(x) convert_float2(x)
#define CONVERT_FLOAT(x) convert_float(x)
#define ZERO 0.0f
#define ONE 1.0f
#define TWO 2.0f
#define FOUR 4.0f
#define SIXTEEN_r 0.0625f
#define TWO56 256.0f
#endif

FLOAT16 constant res_x_add = {
    ONE, TWO, ONE, ZERO, ONE, TWO, ONE, ZERO,
    ONE, TWO, ONE, ZERO, ONE, TWO, ONE, ZERO,
};
FLOAT16 constant res_x_mult = {
    -ONE, -ONE, ONE, ONE, -ONE, -ONE, ONE, ONE,
    -ONE, -ONE, ONE, ONE, -ONE, -ONE, ONE, ONE,
};
FLOAT16 constant res_y_add = {
    ONE, ONE, ONE, ONE, TWO,  TWO,  TWO,  TWO,
    ONE, ONE, ONE, ONE, ZERO, ZERO, ZERO, ZERO,
};
FLOAT16 constant res_y_mult = {
    -ONE, -ONE, -ONE, -ONE, -ONE, -ONE, -ONE, -ONE,
    ONE,  ONE,  ONE,  ONE,  ONE,  ONE,  ONE,  ONE,
};

kernel void shdr_fusion_and_tm(
    __global uchar* frame_shortexp, __global uchar* frame_longexp,
    __global uchar* output_frame,
    __global const float* gain_map
    __attribute__((max_constant_size(MESH_X_plus3 * MESH_X_plus3 *
                                     sizeof(float)))),
    float black_level_LEF_R, float black_level_LEF_Gr, float black_level_LEF_Gb, float black_level_LEF_B, float black_level_SEF_R, float black_level_SEF_Gr, float black_level_SEF_Gb, float black_level_SEF_B,
    float r_gain_arg, float b_gain_arg, float r_gain_r_arg, float b_gain_r_arg,
    float max_fusion_input_val_arg, float max_fusion_input_val_r_arg,
    float max_tm_output_val_arg, float linear_factor_arg_LEF_R, float linear_factor_arg_LEF_Gr, float linear_factor_arg_LEF_Gb, float linear_factor_arg_LEF_B, float linear_factor_arg_SEF_R, float linear_factor_arg_SEF_Gr, float linear_factor_arg_SEF_Gb, float linear_factor_arg_SEF_B, float exp_ratio_arg,
    float max_gain_arg, float comp_target_div_by_2_arg,
    float inverse_tone_perc_arg, float inverse_tone_perc_r_arg,
    float hdr_bayer_gtm_gamma, int short_offset,int long_offset_arg
#ifdef DEBUG_OPEN_CL_KERNEL
    ,
    __global float* dbg
#endif
) {
  FLOAT r_gain = r_gain_arg;
  FLOAT b_gain = b_gain_arg;
  FLOAT r_gain_r = r_gain_r_arg;
  FLOAT b_gain_r = b_gain_r_arg;
  FLOAT max_fusion_input_val = max_fusion_input_val_arg;
  FLOAT max_fusion_input_val_r = max_fusion_input_val_r_arg;
  FLOAT max_tm_output_val = max_tm_output_val_arg;
  FLOAT exp_ratio = exp_ratio_arg;
  FLOAT max_gain = max_gain_arg;
  FLOAT comp_target_div_by_2 = comp_target_div_by_2_arg;
  FLOAT inverse_tone_perc = inverse_tone_perc_arg;
  FLOAT inverse_tone_perc_r = inverse_tone_perc_r_arg;

  short output_x, output_y;
  output_x = get_global_id(0) * KERNEL_X_SIZE;
  output_y = get_global_id(1) * KERNEL_Y_SIZE;

#ifdef DEBUG_OPEN_CL_KERNEL
  bool dbg_en = false;
  ushort dbg_idx = dbg[0] + 1;
  if ((get_global_id(0) == (0 / KERNEL_X_SIZE)) &&
      (get_global_id(1) == (0 / KERNEL_Y_SIZE))) {
    dbg_en = true;
  }
  if (dbg_en) {
    dbg[dbg_idx++] = 0.111111f;
    dbg[dbg_idx++] = output_x;
    dbg[dbg_idx++] = output_y;
    dbg[dbg_idx++] = MESH_X;
    dbg[dbg_idx++] = MESH_X_plus3;
    dbg[dbg_idx++] = INPUT_STRIDE;
    dbg[dbg_idx++] = OUTPUT_STRIDE;
    dbg[dbg_idx++] = OUT_OFFSET;
    dbg[dbg_idx++] = black_level_LEF_R;
    dbg[dbg_idx++] = black_level_LEF_Gr;
    dbg[dbg_idx++] = black_level_LEF_Gb;
    dbg[dbg_idx++] = black_level_LEF_B;
    dbg[dbg_idx++] = black_level_SEF_R;
    dbg[dbg_idx++] = black_level_SEF_Gr;
    dbg[dbg_idx++] = black_level_SEF_Gb;
    dbg[dbg_idx++] = black_level_SEF_B;
    dbg[dbg_idx++] = DARK;
    dbg[dbg_idx++] = TM_GAIN;
    dbg[dbg_idx++] = short_offset;
    dbg[dbg_idx++] = long_offset_arg;
    dbg[dbg_idx++] = X_r;
    dbg[dbg_idx++] = Y_r;
    dbg[dbg_idx++] = hdr_bayer_gtm_gamma;
    dbg[dbg_idx++] = 0.6666666f;
    dbg[dbg_idx++] = r_gain_arg;
    dbg[dbg_idx++] = b_gain_arg;
    dbg[dbg_idx++] = r_gain_r_arg;
    dbg[dbg_idx++] = b_gain_r_arg;
    dbg[dbg_idx++] = max_fusion_input_val_arg;
    dbg[dbg_idx++] = max_fusion_input_val_r_arg;
    dbg[dbg_idx++] = max_tm_output_val_arg;
    dbg[dbg_idx++] = linear_factor_arg_LEF_R;
    dbg[dbg_idx++] = linear_factor_arg_LEF_Gr;
    dbg[dbg_idx++] = linear_factor_arg_LEF_Gb;
    dbg[dbg_idx++] = linear_factor_arg_LEF_B;
    dbg[dbg_idx++] = linear_factor_arg_SEF_R;
    dbg[dbg_idx++] = linear_factor_arg_SEF_Gr;
    dbg[dbg_idx++] = linear_factor_arg_SEF_Gb;
    dbg[dbg_idx++] = linear_factor_arg_SEF_B;
    dbg[dbg_idx++] = exp_ratio_arg;
    dbg[dbg_idx++] = max_gain_arg;
    dbg[dbg_idx++] = comp_target_div_by_2_arg;
    dbg[dbg_idx++] = inverse_tone_perc_arg;
    dbg[dbg_idx++] = inverse_tone_perc_r_arg;
  }
#endif

  FLOAT luma_q01, luma_q1, alpha;
  FLOAT4 quad0_L, quad0_S, quad1_L, quad1_S;
  FLOAT2 luma_q0;

  int stride_in = INPUT_STRIDE;
  int sub_strideL = 0, sub_strideS = 0;

#ifdef ENABLE_MIPI10_IN
  uchar* long_addr;
  long_addr =
      frame_longexp + long_offset_arg + output_y * INPUT_STRIDE + output_x * 10 / 8;
#if (SENSOR_LAYOUT == 4)  // SHDR_INTERLEAVED_NO_STATUS_2LINES_NO_BLANK
  stride_in = INPUT_STRIDE >> 1;
  long_addr = frame_longexp + long_offset_arg +
              min((int)output_y, (int)ROWS_SSTART) * stride_in +
              output_x * 10 / 8;
  long_addr += INPUT_STRIDE * max((int)(output_y - ROWS_SSTART), (int)0);
#endif
#if (SENSOR_LAYOUT == 5)  // SHDR_INTERLEAVED_NO_STATUS_1LINES_NO_BLANK
  int half_stride = INPUT_STRIDE >> 1;
  sub_strideL =
      min(max((int)((ROWS_SSTART)-output_y), (int)0), (int)1) * half_stride;
  long_addr = frame_longexp + long_offset_arg + output_x * 10 / 8;
  long_addr += min((int)output_y, (int)ROWS_SSTART) * half_stride;
  long_addr += max((int)(output_y - ROWS_SSTART), (int)0) * (INPUT_STRIDE);
  long_addr += min(max((int)(output_y - (ROWS_SSTART - 2)), (int)0), (int)1) *
               half_stride * SFIRST;
#endif
#else
  uchar* long_addr;
  long_addr =
      frame_longexp + long_offset_arg + output_y * INPUT_STRIDE + output_x * 2;
#if (SENSOR_LAYOUT == 4)  // SHDR_INTERLEAVED_NO_STATUS_2LINES_NO_BLANK
  stride_in = INPUT_STRIDE >> 1;
  long_addr = frame_longexp + long_offset_arg +
              min((int)output_y, (int)ROWS_SSTART) * stride_in + output_x * 2;
  long_addr += INPUT_STRIDE * max((int)(output_y - ROWS_SSTART), (int)0);
#endif
#if (SENSOR_LAYOUT == 5)  // SHDR_INTERLEAVED_NO_STATUS_1LINES_NO_BLANK
  int half_stride = INPUT_STRIDE >> 1;
  sub_strideL =
      min(max((int)((ROWS_SSTART)-output_y), (int)0), (int)1) * half_stride;
  long_addr = frame_longexp + long_offset_arg + output_x * 2;
  long_addr += min((int)output_y, (int)ROWS_SSTART) * half_stride;
  long_addr += max((int)(output_y - ROWS_SSTART), (int)0) * (INPUT_STRIDE);
  long_addr += min(max((int)(output_y - (ROWS_SSTART - 2)), (int)0), (int)1) *
               half_stride * SFIRST;
#endif
#endif

#ifdef ENABLE_MIPI10_IN
  uchar* short_addr = frame_shortexp + short_offset + output_y * INPUT_STRIDE +
                      output_x * 10 / 8;
#if (SENSOR_LAYOUT)
  short_addr = long_addr + short_offset - long_offset_arg;
#endif
#if (SENSOR_LAYOUT == 4)  // SHDR_INTERLEAVED_NO_STATUS_2LINES_NO_BLANK
  short_addr = frame_longexp + mad24((int)INPUT_STRIDE,
                                     (int)min((int)output_y, (int)ROWS_LEND),
                                     (int)(output_x * 10 / 8));
  short_addr +=
      (stride_in * max((int)(output_y - ROWS_LEND), (int)0) + short_offset);
#endif
#if (SENSOR_LAYOUT == 5)  // SHDR_INTERLEAVED_NO_STATUS_1LINES_NO_BLANK
  sub_strideS =
      min(max((int)(output_y - (ROWS_LEND - 2)), (int)0), (int)1) * half_stride;
  short_addr = frame_longexp + mad24((int)INPUT_STRIDE,
                                     (int)min((int)output_y, (int)ROWS_LEND),
                                     (int)(output_x * 10 / 8));
  short_addr +=
      (half_stride * max((int)(output_y - ROWS_LEND), (int)0) + short_offset);
#endif
#else
  uchar* short_addr =
      frame_shortexp + short_offset + output_y * INPUT_STRIDE + output_x * 2;
#if (SENSOR_LAYOUT)
  short_addr = long_addr + short_offset - long_offset_arg;
#endif
#if (SENSOR_LAYOUT == 4)  // SHDR_INTERLEAVED_NO_STATUS_2LINES_NO_BLANK
  short_addr = frame_longexp + mad24((int)INPUT_STRIDE,
                                     (int)min((int)output_y, (int)ROWS_LEND),
                                     (int)(output_x * 2));
  short_addr +=
      (stride_in * max((int)(output_y - ROWS_LEND), (int)0) + short_offset);
#endif
#if (SENSOR_LAYOUT == 5)  // SHDR_INTERLEAVED_NO_STATUS_1LINES_NO_BLANK
  sub_strideS =
      min(max((int)(output_y - (ROWS_LEND - 2)), (int)0), (int)1) * half_stride;
  short_addr = frame_longexp + mad24((int)INPUT_STRIDE,
                                     (int)min((int)output_y, (int)ROWS_LEND),
                                     (int)(output_x * 2));
  short_addr +=
      (half_stride * max((int)(output_y - ROWS_LEND), (int)0) + short_offset);
#endif
#endif

#ifdef ENABLE_MIPI10_IN
  uchar8 packed_line;

  /* first get the long and short exposure value for the same pixel */
  uchar8 packed_line1, packed_line2;
  packed_line = *(uchar8*)(long_addr);
  packed_line2 = *(uchar8*)(long_addr + stride_in - sub_strideL);
  packed_line1 = *(uchar8*)(long_addr);

#if (BAYER_PATTERN == 0)
  // SHDR_BAYER_GRBG - GRBG --> RGGB
  quad0_L.s1 =
      ((((ushort)packed_line1.s0) << 2) | ((((ushort)packed_line1.s4)) & 3));
  quad0_L.s0 =
      ((((ushort)packed_line1.s1) << 2) | ((((ushort)packed_line1.s4) >> 2) & 3));
  quad1_L.s1 =
      ((((ushort)packed_line1.s2) << 2) | ((((ushort)packed_line1.s4) >> 4) & 3));
  quad1_L.s0 =
      ((((ushort)packed_line1.s3) << 2) | ((((ushort)packed_line1.s4) >> 6) & 3));

  packed_line = *(uchar8*)(long_addr + stride_in - sub_strideL);

  quad0_L.s3 =
      ((((ushort)packed_line2.s0) << 2) | ((((ushort)packed_line2.s4)) & 3));
  quad0_L.s2 =
      ((((ushort)packed_line2.s1) << 2) | ((((ushort)packed_line2.s4) >> 2) & 3));
  quad1_L.s3 =
      ((((ushort)packed_line2.s2) << 2) | ((((ushort)packed_line2.s4) >> 4) & 3));
  quad1_L.s2 =
      ((((ushort)packed_line2.s3) << 2) | ((((ushort)packed_line2.s4) >> 6) & 3));
#endif

#if (BAYER_PATTERN == 1)
  // SHDR_BAYER_RGGB - RGGB --> RGGB
  quad0_L.s0 =
      ((((ushort)packed_line1.s0) << 2) | ((((ushort)packed_line1.s4)) & 3));
  quad0_L.s1 =
      ((((ushort)packed_line1.s1) << 2) | ((((ushort)packed_line1.s4) >> 2) & 3));
  quad1_L.s0 =
      ((((ushort)packed_line1.s2) << 2) | ((((ushort)packed_line1.s4) >> 4) & 3));
  quad1_L.s1 =
      ((((ushort)packed_line1.s3) << 2) | ((((ushort)packed_line1.s4) >> 6) & 3));

  packed_line = *(uchar8*)(long_addr + stride_in - sub_strideL);

  quad0_L.s2 =
      ((((ushort)packed_line2.s0) << 2) | ((((ushort)packed_line2.s4)) & 3));
  quad0_L.s3 =
      ((((ushort)packed_line2.s1) << 2) | ((((ushort)packed_line2.s4) >> 2) & 3));
  quad1_L.s2 =
      ((((ushort)packed_line2.s2) << 2) | ((((ushort)packed_line2.s4) >> 4) & 3));
  quad1_L.s3 =
      ((((ushort)packed_line2.s3) << 2) | ((((ushort)packed_line2.s4) >> 6) & 3));
#endif

#if (BAYER_PATTERN == 2)
  // SHDR_BAYER_BGGR - BGGR --> RGGB
  quad0_L.s3 =
      ((((ushort)packed_line1.s0) << 2) | ((((ushort)packed_line1.s4)) & 3));
  quad0_L.s2 =
      ((((ushort)packed_line1.s1) << 2) | ((((ushort)packed_line1.s4) >> 2) & 3));
  quad1_L.s3 =
      ((((ushort)packed_line1.s2) << 2) | ((((ushort)packed_line1.s4) >> 4) & 3));
  quad1_L.s2 =
      ((((ushort)packed_line1.s3) << 2) | ((((ushort)packed_line1.s4) >> 6) & 3));

  packed_line = *(uchar8*)(long_addr + stride_in - sub_strideL);

  quad0_L.s1 =
      ((((ushort)packed_line2.s0) << 2) | ((((ushort)packed_line2.s4)) & 3));
  quad0_L.s0 =
      ((((ushort)packed_line2.s1) << 2) | ((((ushort)packed_line2.s4) >> 2) & 3));
  quad1_L.s1 =
      ((((ushort)packed_line2.s2) << 2) | ((((ushort)packed_line2.s4) >> 4) & 3));
  quad1_L.s0 =
      ((((ushort)packed_line2.s3) << 2) | ((((ushort)packed_line2.s4) >> 6) & 3));
#endif

#if (BAYER_PATTERN == 3)
  // SHDR_BAYER_GBRG - GBRG --> RGGB
  quad0_L.s2 =
      ((((ushort)packed_line1.s0) << 2) | ((((ushort)packed_line1.s4)) & 3));
  quad0_L.s3 =
      ((((ushort)packed_line1.s1) << 2) | ((((ushort)packed_line1.s4) >> 2) & 3));
  quad1_L.s2 =
      ((((ushort)packed_line1.s2) << 2) | ((((ushort)packed_line1.s4) >> 4) & 3));
  quad1_L.s3 =
      ((((ushort)packed_line1.s3) << 2) | ((((ushort)packed_line1.s4) >> 6) & 3));

  packed_line = *(uchar8*)(long_addr + stride_in - sub_strideL);

  quad0_L.s0 =
      ((((ushort)packed_line2.s0) << 2) | ((((ushort)packed_line2.s4)) & 3));
  quad0_L.s1 =
      ((((ushort)packed_line2.s1) << 2) | ((((ushort)packed_line2.s4) >> 2) & 3));
  quad1_L.s0 =
      ((((ushort)packed_line2.s2) << 2) | ((((ushort)packed_line2.s4) >> 4) & 3));
  quad1_L.s1 =
      ((((ushort)packed_line2.s3) << 2) | ((((ushort)packed_line2.s4) >> 6) & 3));
#endif

#else
  ushort4 packed_line;
  ushort4 packed_line1, packed_line2;
  packed_line2 = vload4(0, (ushort*)(long_addr + stride_in - sub_strideL));
  packed_line1 = vload4(0, (ushort*)(long_addr));

  /* first get the long and short exposure value for the same pixel */
  packed_line = *(ushort4*)(long_addr);

#if (BAYER_PATTERN == 0)
  // SHDR_BAYER_GRBG - GRBG --> RGGB
  quad0_L.s1 = packed_line1.s0;
  quad0_L.s0 = packed_line1.s1;
  quad1_L.s1 = packed_line1.s2;
  quad1_L.s0 = packed_line1.s3;

  packed_line = *(ushort4*)(long_addr + stride_in - sub_strideL);

  quad0_L.s3 = packed_line2.s0;
  quad0_L.s2 = packed_line2.s1;
  quad1_L.s3 = packed_line2.s2;
  quad1_L.s2 = packed_line2.s3;
#endif

#if (BAYER_PATTERN == 1)
  // SHDR_BAYER_RGGB - RGGB --> RGGB
  quad0_L.s0 = packed_line1.s0;
  quad0_L.s1 = packed_line1.s1;
  quad1_L.s0 = packed_line1.s2;
  quad1_L.s1 = packed_line1.s3;

  packed_line = *(ushort4*)(long_addr + stride_in - sub_strideL);

  quad0_L.s2 = packed_line2.s0;
  quad0_L.s3 = packed_line2.s1;
  quad1_L.s2 = packed_line2.s2;
  quad1_L.s3 = packed_line2.s3;
#endif

#if (BAYER_PATTERN == 2)
  // SHDR_BAYER_BGGR - BGGR --> RGGB
  quad0_L.s3 = packed_line1.s0;
  quad0_L.s2 = packed_line1.s1;
  quad1_L.s3 = packed_line1.s2;
  quad1_L.s2 = packed_line1.s3;

  packed_line = *(ushort4*)(long_addr + stride_in - sub_strideL);

  quad0_L.s1 = packed_line2.s0;
  quad0_L.s0 = packed_line2.s1;
  quad1_L.s1 = packed_line2.s2;
  quad1_L.s0 = packed_line2.s3;
#endif

#if (BAYER_PATTERN == 3)
  // SHDR_BAYER_GBRG - GBRG --> RGGB
  quad0_L.s2 = packed_line1.s0;
  quad0_L.s3 = packed_line1.s1;
  quad1_L.s2 = packed_line1.s2;
  quad1_L.s3 = packed_line1.s3;

  packed_line = *(ushort4*)(long_addr + stride_in - sub_strideL);

  quad0_L.s0 = packed_line2.s0;
  quad0_L.s1 = packed_line2.s1;
  quad1_L.s0 = packed_line2.s2;
  quad1_L.s1 = packed_line2.s3;
#endif

#endif

  FLOAT4 black_level_l = {black_level_LEF_R, black_level_LEF_Gr, black_level_LEF_Gb, black_level_LEF_B};
  FLOAT4 black_level_s = {black_level_SEF_R, black_level_SEF_Gr, black_level_SEF_Gb, black_level_SEF_B};

  FLOAT4 linear_factor_l = {linear_factor_arg_LEF_R, linear_factor_arg_LEF_Gr, linear_factor_arg_LEF_Gb, linear_factor_arg_LEF_B};
  FLOAT4 linear_factor_s = {linear_factor_arg_SEF_R, linear_factor_arg_SEF_Gr, linear_factor_arg_SEF_Gb, linear_factor_arg_SEF_B};
  FLOAT4 gains = {TM_GAIN * r_gain, TM_GAIN,
                  TM_GAIN, TM_GAIN * b_gain};
  FLOAT4 gains_l = gains * linear_factor_l;
  FLOAT4 gains_s = gains * linear_factor_s;

  quad0_L = min(max(ZERO, (quad0_L - black_level_l) * gains_l), INPUT_MAX_VAL);
  quad1_L = min(max(ZERO, (quad1_L - black_level_l) * gains_l), INPUT_MAX_VAL);

  luma_q01 = max(quad0_L.s3, max(quad0_L.s0, (quad0_L.s2 + quad0_L.s1) / 2));
  luma_q1 = max(quad1_L.s3, max(quad1_L.s0, (quad1_L.s2 + quad1_L.s1) / 2));

  if ((luma_q01 > DARK) || (luma_q1 > DARK)) {
    /* first get the long and short exposure value for the same pixel */

#ifdef ENABLE_MIPI10_IN
    packed_line = *(uchar8*)(short_addr);
    packed_line2 = *(uchar8*)(short_addr + stride_in - sub_strideS);
    packed_line1 = *(uchar8*)(short_addr);

#if (BAYER_PATTERN == 0)
    // SHDR_BAYER_GRBG - GRBG --> RGGB
    quad0_S.s1 =
        ((((ushort)packed_line1.s0) << 2) | ((((ushort)packed_line1.s4)) & 3));
    quad0_S.s0 = ((((ushort)packed_line1.s1) << 2) |
                  ((((ushort)packed_line1.s4) >> 2) & 3));
    quad1_S.s1 = ((((ushort)packed_line1.s2) << 2) |
                  ((((ushort)packed_line1.s4) >> 4) & 3));
    quad1_S.s0 = ((((ushort)packed_line1.s3) << 2) |
                  ((((ushort)packed_line1.s4) >> 6) & 3));

    packed_line = *(uchar8*)(short_addr + stride_in - sub_strideS);

    quad0_S.s3 =
        ((((ushort)packed_line2.s0) << 2) | ((((ushort)packed_line2.s4)) & 3));
    quad0_S.s2 = ((((ushort)packed_line2.s1) << 2) |
                  ((((ushort)packed_line2.s4) >> 2) & 3));
    quad1_S.s3 = ((((ushort)packed_line2.s2) << 2) |
                  ((((ushort)packed_line2.s4) >> 4) & 3));
    quad1_S.s2 = ((((ushort)packed_line2.s3) << 2) |
                  ((((ushort)packed_line2.s4) >> 6) & 3));
#endif

#if (BAYER_PATTERN == 1)
    // SHDR_BAYER_RGGB - RGGB --> RGGB
    quad0_S.s0 =
        ((((ushort)packed_line1.s0) << 2) | ((((ushort)packed_line1.s4)) & 3));
    quad0_S.s1 = ((((ushort)packed_line1.s1) << 2) |
                  ((((ushort)packed_line1.s4) >> 2) & 3));
    quad1_S.s0 = ((((ushort)packed_line1.s2) << 2) |
                  ((((ushort)packed_line1.s4) >> 4) & 3));
    quad1_S.s1 = ((((ushort)packed_line1.s3) << 2) |
                  ((((ushort)packed_line1.s4) >> 6) & 3));

    packed_line = *(uchar8*)(short_addr + stride_in - sub_strideS);

    quad0_S.s2 =
        ((((ushort)packed_line2.s0) << 2) | ((((ushort)packed_line2.s4)) & 3));
    quad0_S.s3 = ((((ushort)packed_line2.s1) << 2) |
                  ((((ushort)packed_line2.s4) >> 2) & 3));
    quad1_S.s2 = ((((ushort)packed_line2.s2) << 2) |
                  ((((ushort)packed_line2.s4) >> 4) & 3));
    quad1_S.s3 = ((((ushort)packed_line2.s3) << 2) |
                  ((((ushort)packed_line2.s4) >> 6) & 3));
#endif

#if (BAYER_PATTERN == 2)
    // SHDR_BAYER_BGGR - BGGR --> RGGB
    quad0_S.s3 =
        ((((ushort)packed_line1.s0) << 2) | ((((ushort)packed_line1.s4)) & 3));
    quad0_S.s2 = ((((ushort)packed_line1.s1) << 2) |
                  ((((ushort)packed_line1.s4) >> 2) & 3));
    quad1_S.s3 = ((((ushort)packed_line1.s2) << 2) |
                  ((((ushort)packed_line1.s4) >> 4) & 3));
    quad1_S.s2 = ((((ushort)packed_line1.s3) << 2) |
                  ((((ushort)packed_line1.s4) >> 6) & 3));

    packed_line = *(uchar8*)(short_addr + stride_in - sub_strideS);

    quad0_S.s1 =
        ((((ushort)packed_line2.s0) << 2) | ((((ushort)packed_line2.s4)) & 3));
    quad0_S.s0 = ((((ushort)packed_line2.s1) << 2) |
                  ((((ushort)packed_line2.s4) >> 2) & 3));
    quad1_S.s1 = ((((ushort)packed_line2.s2) << 2) |
                  ((((ushort)packed_line2.s4) >> 4) & 3));
    quad1_S.s0 = ((((ushort)packed_line2.s3) << 2) |
                  ((((ushort)packed_line2.s4) >> 6) & 3));
#endif

#if (BAYER_PATTERN == 3)
    // SHDR_BAYER_GBRG - GBRG --> RGGB
    quad0_S.s2 =
        ((((ushort)packed_line1.s0) << 2) | ((((ushort)packed_line1.s4)) & 3));
    quad0_S.s3 = ((((ushort)packed_line1.s1) << 2) |
                  ((((ushort)packed_line1.s4) >> 2) & 3));
    quad1_S.s2 = ((((ushort)packed_line1.s2) << 2) |
                  ((((ushort)packed_line1.s4) >> 4) & 3));
    quad1_S.s3 = ((((ushort)packed_line1.s3) << 2) |
                  ((((ushort)packed_line1.s4) >> 6) & 3));

    packed_line = *(uchar8*)(short_addr + stride_in - sub_strideS);

    quad0_S.s0 =
        ((((ushort)packed_line2.s0) << 2) | ((((ushort)packed_line2.s4)) & 3));
    quad0_S.s1 = ((((ushort)packed_line2.s1) << 2) |
                  ((((ushort)packed_line2.s4) >> 2) & 3));
    quad1_S.s0 = ((((ushort)packed_line2.s2) << 2) |
                  ((((ushort)packed_line2.s4) >> 4) & 3));
    quad1_S.s1 = ((((ushort)packed_line2.s3) << 2) |
                  ((((ushort)packed_line2.s4) >> 6) & 3));
#endif

#else
    ushort4 packed_line;

    /* first get the long and short exposure value for the same pixel */
    packed_line = *(ushort4*)(short_addr);
    packed_line2 = vload4(0, (ushort*)(short_addr + stride_in - sub_strideS));
    packed_line1 = vload4(0, (ushort*)(short_addr));

#if (BAYER_PATTERN == 0)
    // SHDR_BAYER_GRBG - GRBG --> RGGB
    quad0_S.s1 = packed_line1.s0;
    quad0_S.s0 = packed_line1.s1;
    quad1_S.s1 = packed_line1.s2;
    quad1_S.s0 = packed_line1.s3;

    packed_line = *(ushort4*)(short_addr + stride_in - sub_strideS);

    quad0_S.s3 = packed_line2.s0;
    quad0_S.s2 = packed_line2.s1;
    quad1_S.s3 = packed_line2.s2;
    quad1_S.s2 = packed_line2.s3;
#endif

#if (BAYER_PATTERN == 1)
    // SHDR_BAYER_RGGB - RGGB --> RGGB
    quad0_S.s0 = packed_line1.s0;
    quad0_S.s1 = packed_line1.s1;
    quad1_S.s0 = packed_line1.s2;
    quad1_S.s1 = packed_line1.s3;

    packed_line = *(ushort4*)(short_addr + stride_in - sub_strideS);

    quad0_S.s2 = packed_line2.s0;
    quad0_S.s3 = packed_line2.s1;
    quad1_S.s2 = packed_line2.s2;
    quad1_S.s3 = packed_line2.s3;
#endif

#if (BAYER_PATTERN == 2)
    // SHDR_BAYER_BGGR - BGGR --> RGGB
    quad0_S.s3 = packed_line1.s0;
    quad0_S.s2 = packed_line1.s1;
    quad1_S.s3 = packed_line1.s2;
    quad1_S.s2 = packed_line1.s3;

    packed_line = *(ushort4*)(short_addr + stride_in - sub_strideS);

    quad0_S.s1 = packed_line2.s0;
    quad0_S.s0 = packed_line2.s1;
    quad1_S.s1 = packed_line2.s2;
    quad1_S.s0 = packed_line2.s3;
#endif

#if (BAYER_PATTERN == 3)
    // SHDR_BAYER_GBRG - GBRG --> RGGB
    quad0_S.s2 = packed_line1.s0;
    quad0_S.s3 = packed_line1.s1;
    quad1_S.s2 = packed_line1.s2;
    quad1_S.s3 = packed_line1.s3;

    packed_line = *(ushort4*)(short_addr + stride_in - sub_strideS);

    quad0_S.s0 = packed_line2.s0;
    quad0_S.s1 = packed_line2.s1;
    quad1_S.s0 = packed_line2.s2;
    quad1_S.s1 = packed_line2.s3;
#endif

#endif

    quad0_S = min(max(ZERO, (quad0_S - black_level_s) * gains_s) * exp_ratio,
                  max_fusion_input_val);
    quad1_S = min(max(ZERO, (quad1_S - black_level_s) * gains_s) * exp_ratio,
                  max_fusion_input_val);

    FLOAT sluma_q0 =
        max(quad0_S.s3, max(quad0_S.s0, (quad0_S.s2 + quad0_S.s1) / 2));
    FLOAT sluma_q1 =
        max(quad1_S.s3, max(quad1_S.s0, (quad1_S.s2 + quad1_S.s1) / 2));

    //////////Quad 0 fusion
    alpha = min(SEF_LOWBND, max(ZERO, (luma_q01 - DARK)));
    alpha /= SEF_LOWBND;
    if (sluma_q0 < luma_q01) {
      if (sluma_q0 < INPUT_MAX_VAL * HALO_DARKTHR) {
        alpha = LEF_DARKCOMPEN;
      } else if (sluma_q0 < INPUT_MAX_VAL * HALO_BRIGHTHR) {
        alpha = LEF_DARKCOMPEN + ((LEF_BRIGHTCOMPEN - LEF_DARKCOMPEN) *
                                  (sluma_q0 - INPUT_MAX_VAL * HALO_DARKTHR)) /
                                     (INPUT_MAX_VAL * HALO_BRIGHTHR -
                                      INPUT_MAX_VAL * HALO_DARKTHR);
      }
    }
    FLOAT temp = ONE - alpha;
    quad0_L = (temp * quad0_L + alpha * quad0_S);

    //////////Quad 1 fusion
    alpha = min(SEF_LOWBND, max(ZERO, (luma_q1 - DARK)));
    alpha /= SEF_LOWBND;
    if (sluma_q1 < luma_q1) {
      if (sluma_q1 < INPUT_MAX_VAL * HALO_DARKTHR) {
        alpha = LEF_DARKCOMPEN;
      } else if (sluma_q1 < INPUT_MAX_VAL * HALO_BRIGHTHR) {
        alpha = LEF_DARKCOMPEN + ((LEF_BRIGHTCOMPEN - LEF_DARKCOMPEN) *
                                  (sluma_q1 - INPUT_MAX_VAL * HALO_DARKTHR)) /
                                     (INPUT_MAX_VAL * HALO_BRIGHTHR -
                                      INPUT_MAX_VAL * HALO_DARKTHR);
      }
    }
    temp = ONE - alpha;
    quad1_L = (temp * quad1_L + alpha * quad1_S);
  }

  /********************** Second phase **********************/

  FLOAT map_idx_x = (FLOAT)output_x * X_r + ONE;
  FLOAT map_idx_y = (FLOAT)output_y * Y_r + ONE;

  FLOAT16 wt;
  FLOAT residual_x = map_idx_x - (short)(map_idx_x);
  FLOAT residual_y = map_idx_y - (short)(map_idx_y);
  wt = mad(res_x_mult, residual_x, res_x_add);
  wt *= mad(res_y_mult, residual_y, res_y_add);

  short offset = (short)((FLOAT)output_y * Y_r) * MESH_X_plus3 +
                 (short)((FLOAT)output_x * X_r);
  FLOAT16 lp;
  lp.lo.lo = vload4(0, (float*)(gain_map + offset));

  lp.lo.hi = vload4(0, (float*)(gain_map + offset + MESH_X_plus3));

  lp.hi.lo = vload4(0, (float*)(gain_map + offset + MESH_X_plus3 * 2));

  lp.hi.hi = vload4(0, (float*)(gain_map + offset + MESH_X_plus3 * 3));

  FLOAT4 pow_quad0_L, pow_quad1_L;
  pow_quad0_L = native_powr((quad0_L * max_fusion_input_val_r), hdr_bayer_gtm_gamma);
  pow_quad1_L = native_powr((quad1_L * max_fusion_input_val_r), hdr_bayer_gtm_gamma);

#ifdef HDR1_MODE

  quad0_L = pow_quad0_L * max_tm_output_val;
  quad1_L = pow_quad1_L * max_tm_output_val;

#else

  FLOAT2 tm_gains[2] = {
       {pow_quad0_L.s1, pow_quad0_L.s2},
       {pow_quad1_L.s1, pow_quad1_L.s2},
  };

  for (offset = 0; offset < 2; offset++) {
    luma_q0 = tm_gains[offset] * max_fusion_input_val;

    float16 diff[2];
    diff[0] = fabs(lp - luma_q0.s0);
    diff[1] = fabs(lp - luma_q0.s1);

    diff[0] = min(luma_q0.s0, diff[0]);
    diff[1] = min(luma_q0.s1, diff[1]);

    diff[0] = (luma_q0.s0 + ONE) - diff[0];
    diff[1] = (luma_q0.s1 + ONE) - diff[1];

    diff[0] = native_sqrt(diff[0]);
    diff[1] = native_sqrt(diff[1]);

    diff[0] *= wt;
    diff[1] *= wt;

    float2 wt_sm;
    wt_sm.s0 = diff[0].s0 + diff[0].s1 + diff[0].s2 + diff[0].s3 + diff[0].s4 + diff[0].s5 +
                diff[0].s6 + diff[0].s7 + diff[0].s8 + diff[0].s9 + diff[0].sa + diff[0].sb +
                diff[0].sc + diff[0].sd + diff[0].se + diff[0].sf;
    wt_sm.s1 = diff[1].s0 + diff[1].s1 + diff[1].s2 + diff[1].s3 + diff[1].s4 + diff[1].s5 +
                diff[1].s6 + diff[1].s7 + diff[1].s8 + diff[1].s9 + diff[1].sa + diff[1].sb +
                diff[1].sc + diff[1].sd + diff[1].se + diff[1].sf;

    diff[0] *= lp;
    diff[1] *= lp;

    tm_gains[offset].s0 = diff[0].s0 + diff[0].s1 + diff[0].s2 + diff[0].s3 + diff[0].s4 +
                          diff[0].s5 + diff[0].s6 + diff[0].s7 + diff[0].s8 + diff[0].s9 +
                          diff[0].sa + diff[0].sb + diff[0].sc + diff[0].sd + diff[0].se +
                          diff[0].sf;
    tm_gains[offset].s1 = diff[1].s0 + diff[1].s1 + diff[1].s2 + diff[1].s3 + diff[1].s4 +
                          diff[1].s5 + diff[1].s6 + diff[1].s7 + diff[1].s8 + diff[1].s9 +
                          diff[1].sa + diff[1].sb + diff[1].sc + diff[1].sd + diff[1].se +
                          diff[1].sf;

    tm_gains[offset] = native_divide(tm_gains[offset], wt_sm);

    float2 lp_total;
    lp_total.s0 = max(tm_gains[offset].s0, ONE);
    lp_total.s1 = max(tm_gains[offset].s1, ONE);

    float K1 = comp_target_div_by_2 * TWO;
    float K2 = K1 * TWO;

    tm_gains[offset].s0 = isless(lp_total.s0, K1) * (lp_total.s0 + comp_target_div_by_2) + isgreater(lp_total.s0, K2) * lp_total.s0;
    tm_gains[offset].s1 = isless(lp_total.s1, K1) * (lp_total.s1 + comp_target_div_by_2) + isgreater(lp_total.s1, K2) * lp_total.s1;

    tm_gains[offset].s0 += isequal(tm_gains[offset].s0, ZERO) * (comp_target_div_by_2 * TWO + lp_total.s0 / TWO);
    tm_gains[offset].s1 += isequal(tm_gains[offset].s1, ZERO) * (comp_target_div_by_2 * TWO + lp_total.s1 / TWO);

    tm_gains[offset] = native_divide(tm_gains[offset], lp_total);

    tm_gains[offset].s0 = clamp(tm_gains[offset].s0, ONE, max_gain);
    tm_gains[offset].s1 = clamp(tm_gains[offset].s1, ONE, max_gain);

    K2 = inverse_tone_perc * TWO;

    luma_q0.s0 = TWO - clamp(luma_q0.s0, inverse_tone_perc, K2) * inverse_tone_perc_r;
    luma_q0.s1 = TWO - clamp(luma_q0.s1, inverse_tone_perc, K2) * inverse_tone_perc_r;

    tm_gains[offset].s0 = tm_gains[offset].s0 * luma_q0.s0 + ONE - luma_q0.s0;
    tm_gains[offset].s1 = tm_gains[offset].s1 * luma_q0.s1 + ONE - luma_q0.s1;

    tm_gains[offset].s0 = tm_gains[offset].s0 * max_tm_output_val;
    tm_gains[offset].s1 = tm_gains[offset].s1 * max_tm_output_val;
  }

  quad0_L.lo = pow_quad0_L.lo * tm_gains[0].s0;
  quad0_L.hi = pow_quad0_L.hi * tm_gains[0].s1;

  quad1_L.lo = pow_quad1_L.lo * tm_gains[1].s0;
  quad1_L.hi = pow_quad1_L.hi * tm_gains[1].s1;

#endif

  quad0_S.s0 = quad0_L.s0;
  quad0_S.s1 = quad0_L.s1;
  quad0_S.s2 = quad1_L.s0;
  quad0_S.s3 = quad1_L.s1;
  quad1_S.s0 = quad0_L.s2;
  quad1_S.s1 = quad0_L.s3;
  quad1_S.s2 = quad1_L.s2;
  quad1_S.s3 = quad1_L.s3;

  /*shdr_postwb *=============================================================*/
  quad0_S.even = quad0_S.even * r_gain_r + 0.5f;
  quad1_S.odd = quad1_S.odd * b_gain_r + 0.5f;

  quad0_S = clamp(floor(quad0_S), ZERO, max_tm_output_val);
  quad1_S = clamp(floor(quad1_S), ZERO, max_tm_output_val);

#ifdef ENABLE_MIPI12_OUT
  ushort3 mipi12_out;

#if (BAYER_PATTERN == 0)
  // SHDR_BAYER_RGGB - RGGB --> GRBG
  mipi12_out.x = (((ushort)quad0_S.s0 >> 4) << 8) | ((ushort)quad0_S.s1 >> 4);
  mipi12_out.y = (((ushort)quad0_S.s3 >> 4) << 8) |
                 (((ushort)quad0_S.s0 & 0xF) << 4) | ((ushort)quad0_S.s1 & 0xF);
  mipi12_out.z =
      (((((ushort)quad0_S.s2 & 0xF) << 4) | ((ushort)quad0_S.s3 & 0xF)) << 8) |
      ((ushort)quad0_S.s2 >> 4);

  uchar* output_addr =
      output_frame + output_y * OUTPUT_STRIDE + output_x * 12 / 8 + OUT_OFFSET;
  *(ushort3*)output_addr = mipi12_out;
  output_addr += OUTPUT_STRIDE;

  mipi12_out.x = (((ushort)quad1_S.s0 >> 4) << 8) | ((ushort)quad1_S.s1 >> 4);
  mipi12_out.y = (((ushort)quad1_S.s3 >> 4) << 8) |
                 (((ushort)quad1_S.s0 & 0xF) << 4) | ((ushort)quad1_S.s1 & 0xF);
  mipi12_out.z =
      (((((ushort)quad1_S.s2 & 0xF) << 4) | ((ushort)quad1_S.s3 & 0xF)) << 8) |
      ((ushort)quad1_S.s2 >> 4);

  *(ushort3*)output_addr = mipi12_out;
#endif

#if (BAYER_PATTERN == 1)
  // SHDR_BAYER_RGGB - RGGB --> RGGB
  mipi12_out.x = (((ushort)quad0_S.s1 >> 4) << 8) | ((ushort)quad0_S.s0 >> 4);
  mipi12_out.y = (((ushort)quad0_S.s2 >> 4) << 8) |
                 (((ushort)quad0_S.s1 & 0xF) << 4) | ((ushort)quad0_S.s0 & 0xF);
  mipi12_out.z =
      (((((ushort)quad0_S.s3 & 0xF) << 4) | ((ushort)quad0_S.s2 & 0xF)) << 8) |
      ((ushort)quad0_S.s3 >> 4);

  uchar* output_addr =
      output_frame + output_y * OUTPUT_STRIDE + output_x * 12 / 8 + OUT_OFFSET;
  *(ushort3*)output_addr = mipi12_out;
  output_addr += OUTPUT_STRIDE;

  mipi12_out.x = (((ushort)quad1_S.s1 >> 4) << 8) | ((ushort)quad1_S.s0 >> 4);
  mipi12_out.y = (((ushort)quad1_S.s2 >> 4) << 8) |
                 (((ushort)quad1_S.s1 & 0xF) << 4) | ((ushort)quad1_S.s0 & 0xF);
  mipi12_out.z =
      (((((ushort)quad1_S.s3 & 0xF) << 4) | ((ushort)quad1_S.s2 & 0xF)) << 8) |
      ((ushort)quad1_S.s3 >> 4);

  *(ushort3*)output_addr = mipi12_out;
#endif

#if (BAYER_PATTERN == 2)
  // SHDR_BAYER_BGGR - RGGB --> BGGR
  mipi12_out.x = (((ushort)quad1_S.s0 >> 4) << 8) | ((ushort)quad1_S.s1 >> 4);
  mipi12_out.y = (((ushort)quad1_S.s3 >> 4) << 8) |
                 (((ushort)quad1_S.s0 & 0xF) << 4) | ((ushort)quad1_S.s1 & 0xF);
  mipi12_out.z =
      (((((ushort)quad1_S.s2 & 0xF) << 4) | ((ushort)quad1_S.s3 & 0xF)) << 8) |
      ((ushort)quad1_S.s2 >> 4);

  uchar* output_addr =
      output_frame + output_y * OUTPUT_STRIDE + output_x * 12 / 8 + OUT_OFFSET;
  *(ushort3*)output_addr = mipi12_out;
  output_addr += OUTPUT_STRIDE;

  mipi12_out.x = (((ushort)quad0_S.s0 >> 4) << 8) | ((ushort)quad0_S.s1 >> 4);
  mipi12_out.y = (((ushort)quad0_S.s3 >> 4) << 8) |
                 (((ushort)quad0_S.s0 & 0xF) << 4) | ((ushort)quad0_S.s1 & 0xF);
  mipi12_out.z =
      (((((ushort)quad0_S.s2 & 0xF) << 4) | ((ushort)quad0_S.s3 & 0xF)) << 8) |
      ((ushort)quad0_S.s2 >> 4);

  *(ushort3*)output_addr = mipi12_out;
#endif

#if (BAYER_PATTERN == 3)
  // SHDR_BAYER_GBRG - RGGB --> GBRG
  mipi12_out.x = (((ushort)quad1_S.s1 >> 4) << 8) | ((ushort)quad1_S.s0 >> 4);
  mipi12_out.y = (((ushort)quad1_S.s2 >> 4) << 8) |
                 (((ushort)quad1_S.s1 & 0xF) << 4) | ((ushort)quad1_S.s0 & 0xF);
  mipi12_out.z =
      (((((ushort)quad1_S.s3 & 0xF) << 4) | ((ushort)quad1_S.s2 & 0xF)) << 8) |
      ((ushort)quad1_S.s3 >> 4);

  uchar* output_addr =
      output_frame + output_y * OUTPUT_STRIDE + output_x * 12 / 8 + OUT_OFFSET;
  *(ushort3*)output_addr = mipi12_out;
  output_addr += OUTPUT_STRIDE;

  mipi12_out.x = (((ushort)quad0_S.s1 >> 4) << 8) | ((ushort)quad0_S.s0 >> 4);
  mipi12_out.y = (((ushort)quad0_S.s2 >> 4) << 8) |
                 (((ushort)quad0_S.s1 & 0xF) << 4) | ((ushort)quad0_S.s0 & 0xF);
  mipi12_out.z =
      (((((ushort)quad0_S.s3 & 0xF) << 4) | ((ushort)quad0_S.s2 & 0xF)) << 8) |
      ((ushort)quad0_S.s3 >> 4);

  *(ushort3*)output_addr = mipi12_out;
#endif

#else

#if (BAYER_PATTERN == 0)
  // SHDR_BAYER_RGGB - RGGB --> GRBG
  quad0_L.s0 = quad0_S.s1;
  quad0_L.s1 = quad0_S.s0;
  quad0_L.s2 = quad0_S.s3;
  quad0_L.s3 = quad0_S.s2;
  quad1_L.s0 = quad1_S.s1;
  quad1_L.s1 = quad1_S.s0;
  quad1_L.s2 = quad1_S.s3;
  quad1_L.s3 = quad1_S.s2;
#endif

#if (BAYER_PATTERN == 1)
  // SHDR_BAYER_RGGB - RGGB --> RGGB
  quad0_L.s0 = quad0_S.s0;
  quad0_L.s1 = quad0_S.s1;
  quad0_L.s2 = quad0_S.s2;
  quad0_L.s3 = quad0_S.s3;
  quad1_L.s0 = quad1_S.s0;
  quad1_L.s1 = quad1_S.s1;
  quad1_L.s2 = quad1_S.s2;
  quad1_L.s3 = quad1_S.s3;
#endif

#if (BAYER_PATTERN == 2)
  // SHDR_BAYER_BGGR - RGGB --> BGGR
  quad0_L.s0 = quad1_S.s1;
  quad0_L.s1 = quad1_S.s0;
  quad0_L.s2 = quad1_S.s3;
  quad0_L.s3 = quad1_S.s2;
  quad1_L.s0 = quad0_S.s1;
  quad1_L.s1 = quad0_S.s0;
  quad1_L.s2 = quad0_S.s3;
  quad1_L.s3 = quad0_S.s2;
#endif

#if (BAYER_PATTERN == 3)
  // SHDR_BAYER_GBRG - RGGB --> GBRG
  quad0_L.s0 = quad1_S.s0;
  quad0_L.s1 = quad1_S.s1;
  quad0_L.s2 = quad1_S.s2;
  quad0_L.s3 = quad1_S.s3;
  quad1_L.s0 = quad0_S.s0;
  quad1_L.s1 = quad0_S.s1;
  quad1_L.s2 = quad0_S.s2;
  quad1_L.s3 = quad0_S.s3;
#endif
 uchar* output_addr =
       output_frame + output_y * OUTPUT_STRIDE + output_x * 2 + OUT_OFFSET;

 vstore4(convert_ushort4(quad0_L), 0, (ushort*)(output_addr));
 vstore4(convert_ushort4(quad1_L), 0, (ushort*)(output_addr + OUTPUT_STRIDE));
#endif

#ifdef DEBUG_OPEN_CL_KERNEL
  if (dbg_en) {
    dbg[0] = dbg_idx - 1;
  }
#endif
}
