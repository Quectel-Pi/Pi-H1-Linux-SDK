// SPDX-License-Identifier: MIT
#include "hevc_headers.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

#include "bit_writer.h"

/* H.265 table 7-1, NAL unit types. */
#define VENUS_HEVC_NAL_VPS 32u
#define VENUS_HEVC_NAL_SPS 33u
#define VENUS_HEVC_NAL_PPS 34u

#define VENUS_HEVC_LEVEL_4 120u
#define VENUS_HEVC_LEVEL_5 150u
#define VENUS_HEVC_LEVEL_5_1 153u

/* H.265 A.4.1: the largest luma picture size each level allows. */
#define VENUS_HEVC_LEVEL_4_MAX_LUMA 2228224u
#define VENUS_HEVC_LEVEL_5_MAX_LUMA 8912896u

/*
 * Picture parameters that mirror an SPS or a PPS are copied across, and the
 * values this interface does not carry are given the setting an encoder
 * leaves behind: a single temporal sub layer, no VUI, flat scaling lists.
 */
/*
 * sps_max_num_reorder_pics for both the VPS and the SPS.  A VA-API HEVC
 * client reports whether the stream reorders pictures (NoPicReorderingFlag)
 * but never how far; stamping the whole DPB size in instead makes the decoder
 * lag that many pictures behind its input, and a client that stops submitting
 * once it wants a frame back can no longer make progress at all.  The smallest
 * lag that keeps the reordering the client asked for is used.
 */
static uint32_t venus_hevc_reorder_pics(
    const VAPictureParameterBufferHEVC *picture)
{
    if (picture->pic_fields.bits.NoPicReorderingFlag)
        return 0;

    return 1;
}

static void write_profile_tier_level(struct venus_bit_writer *bits,
                                     uint32_t luma_samples)
{
    uint32_t level = VENUS_HEVC_LEVEL_5_1;

    if (luma_samples <= VENUS_HEVC_LEVEL_4_MAX_LUMA)
        level = VENUS_HEVC_LEVEL_4;
    else if (luma_samples <= VENUS_HEVC_LEVEL_5_MAX_LUMA)
        level = VENUS_HEVC_LEVEL_5;

    venus_bits_write(bits, 0, 2);         /* general_profile_space */
    venus_bits_write(bits, 0, 1);         /* general_tier_flag */
    venus_bits_write(bits, 1, 5);         /* general_profile_idc: Main */
    venus_bits_write(bits, 0x40000000u, 32); /* ...compatibility: Main */
    venus_bits_write(bits, 1, 1);         /* progressive_source */
    venus_bits_write(bits, 0, 1);         /* interlaced_source */
    venus_bits_write(bits, 1, 1);         /* non_packed_constraint */
    venus_bits_write(bits, 1, 1);         /* frame_only_constraint */
    venus_bits_write(bits, 0, 9);         /* max_12bit ... lower_bit_rate */
    venus_bits_write(bits, 0, 32);        /* general_reserved_zero_34bits */
    venus_bits_write(bits, 0, 2);
    venus_bits_write(bits, 0, 1);         /* general_inbld_flag */
    venus_bits_write(bits, level, 8);     /* general_level_idc */
}

static void write_nal_header(uint8_t *header, uint32_t nal_type)
{
    /* forbidden_zero_bit: 0, nuh_layer_id: 0, nuh_temporal_id_plus1: 1 */
    header[0] = (uint8_t)(nal_type << 1);
    header[1] = 1;
}

static int build_vps(const struct venus_hevc_sequence *sequence,
                     uint8_t *rbsp, size_t capacity, size_t *size)
{
    const VAPictureParameterBufferHEVC *picture = sequence->picture;
    struct venus_bit_writer bits;
    uint32_t buffering = picture->sps_max_dec_pic_buffering_minus1;
    uint32_t reorder = venus_hevc_reorder_pics(picture);

    venus_bits_init(&bits, rbsp, capacity);
    venus_bits_write(&bits, 0, 4);        /* vps_video_parameter_set_id */
    venus_bits_write(&bits, 1, 1);        /* vps_base_layer_internal_flag */
    venus_bits_write(&bits, 1, 1);        /* vps_base_layer_available_flag */
    venus_bits_write(&bits, 0, 6);        /* vps_max_layers_minus1 */
    venus_bits_write(&bits, 0, 3);        /* vps_max_sub_layers_minus1 */
    venus_bits_write(&bits, 1, 1);        /* vps_temporal_id_nesting_flag */
    venus_bits_write(&bits, 0xffff, 16);  /* vps_reserved_0xffff_16bits */
    write_profile_tier_level(
        &bits, sequence->coded_width * sequence->coded_height);
    venus_bits_write(&bits, 0, 1);        /* sub_layer_ordering_info */
    venus_bits_write_ue(&bits, buffering);
    venus_bits_write_ue(&bits, reorder);   /* num_reorder_pics */
    venus_bits_write_ue(&bits, 0);         /* max_latency_increase_plus1 */
    venus_bits_write(&bits, 0, 6);        /* vps_max_layer_id */
    venus_bits_write_ue(&bits, 0);        /* vps_num_layer_sets_minus1 */
    venus_bits_write(&bits, 0, 1);        /* vps_timing_info_present_flag */
    venus_bits_write(&bits, 0, 1);        /* vps_extension_flag */
    venus_bits_finish_rbsp(&bits);

    if (!venus_bits_ok(&bits))
        return -ENOSPC;
    *size = venus_bits_size(&bits);
    return 0;
}

static int build_sps(const struct venus_hevc_sequence *sequence,
                     uint8_t *rbsp, size_t capacity, size_t *size)
{
    const VAPictureParameterBufferHEVC *picture = sequence->picture;
    struct venus_bit_writer bits;
    uint32_t chroma = picture->pic_fields.bits.chroma_format_idc;
    uint32_t width = picture->pic_width_in_luma_samples;
    uint32_t height = picture->pic_height_in_luma_samples;
    uint32_t right_crop = 0;
    uint32_t bottom_crop = 0;
    uint32_t buffering = picture->sps_max_dec_pic_buffering_minus1;
    uint32_t reorder = venus_hevc_reorder_pics(picture);

    if (!width || !height)
        return -EINVAL;
    /* Reference picture sets living in the SPS are not part of the VA-API
     * picture parameters, and neither are the long term reference picture
     * POC values; a stream that uses them cannot be described here.
     */
    if (picture->num_short_term_ref_pic_sets ||
        picture->num_long_term_ref_pic_sps)
        return -ENOTSUP;

    if (width > sequence->visible_width && height > sequence->visible_height &&
        !((width - sequence->visible_width) & 1u) &&
        !((height - sequence->visible_height) & 1u)) {
        /* 4:2:0: the conformance window counts chroma samples. */
        right_crop = (width - sequence->visible_width) / 2u;
        bottom_crop = (height - sequence->visible_height) / 2u;
    }

    venus_bits_init(&bits, rbsp, capacity);
    venus_bits_write(&bits, 0, 4);        /* sps_video_parameter_set_id */
    venus_bits_write(&bits, 0, 3);        /* sps_max_sub_layers_minus1 */
    venus_bits_write(&bits, 1, 1);        /* sps_temporal_id_nesting_flag */
    write_profile_tier_level(&bits, width * height);
    venus_bits_write_ue(&bits, 0);        /* sps_seq_parameter_set_id */
    venus_bits_write_ue(&bits, chroma);
    if (chroma == 3)
        venus_bits_write(&bits, 0, 1);    /* separate_colour_plane_flag */
    venus_bits_write_ue(&bits, width);
    venus_bits_write_ue(&bits, height);
    venus_bits_write(&bits, right_crop || bottom_crop ? 1 : 0, 1);
    if (right_crop || bottom_crop) {
        venus_bits_write_ue(&bits, 0);            /* conf_win_left_offset */
        venus_bits_write_ue(&bits, right_crop);
        venus_bits_write_ue(&bits, 0);            /* conf_win_top_offset */
        venus_bits_write_ue(&bits, bottom_crop);
    }
    venus_bits_write_ue(&bits, picture->bit_depth_luma_minus8);
    venus_bits_write_ue(&bits, picture->bit_depth_chroma_minus8);
    venus_bits_write_ue(&bits, picture->log2_max_pic_order_cnt_lsb_minus4);
    venus_bits_write(&bits, 0, 1);        /* sub_layer_ordering_info */
    venus_bits_write_ue(&bits, buffering);
    venus_bits_write_ue(&bits, reorder);   /* sps_max_num_reorder_pics */
    venus_bits_write_ue(&bits, 0);         /* max_latency_increase_plus1 */
    venus_bits_write_ue(
        &bits, picture->log2_min_luma_coding_block_size_minus3);
    venus_bits_write_ue(
        &bits, picture->log2_diff_max_min_luma_coding_block_size);
    venus_bits_write_ue(
        &bits, picture->log2_min_transform_block_size_minus2);
    venus_bits_write_ue(
        &bits, picture->log2_diff_max_min_transform_block_size);
    venus_bits_write_ue(&bits, picture->max_transform_hierarchy_depth_inter);
    venus_bits_write_ue(&bits, picture->max_transform_hierarchy_depth_intra);
    venus_bits_write(
        &bits, picture->pic_fields.bits.scaling_list_enabled_flag, 1);
    if (picture->pic_fields.bits.scaling_list_enabled_flag)
        venus_bits_write(&bits, 0, 1);    /* sps_scaling_list_data: default */
    venus_bits_write(&bits, picture->pic_fields.bits.amp_enabled_flag, 1);
    venus_bits_write(
        &bits,
        picture->slice_parsing_fields.bits.sample_adaptive_offset_enabled_flag,
        1);
    venus_bits_write(&bits, picture->pic_fields.bits.pcm_enabled_flag, 1);
    if (picture->pic_fields.bits.pcm_enabled_flag) {
        venus_bits_write(&bits, picture->pcm_sample_bit_depth_luma_minus1, 4);
        venus_bits_write(&bits, picture->pcm_sample_bit_depth_chroma_minus1, 4);
        venus_bits_write_ue(
            &bits, picture->log2_min_pcm_luma_coding_block_size_minus3);
        venus_bits_write_ue(
            &bits, picture->log2_diff_max_min_pcm_luma_coding_block_size);
        venus_bits_write(
            &bits, picture->pic_fields.bits.pcm_loop_filter_disabled_flag, 1);
    }
    venus_bits_write_ue(&bits, picture->num_short_term_ref_pic_sets);
    venus_bits_write(
        &bits,
        picture->slice_parsing_fields.bits.long_term_ref_pics_present_flag, 1);
    if (picture->slice_parsing_fields.bits.long_term_ref_pics_present_flag)
        venus_bits_write_ue(&bits, picture->num_long_term_ref_pic_sps);
    venus_bits_write(
        &bits, picture->slice_parsing_fields.bits.sps_temporal_mvp_enabled_flag,
        1);
    venus_bits_write(
        &bits,
        picture->pic_fields.bits.strong_intra_smoothing_enabled_flag, 1);
    venus_bits_write(&bits, 0, 1);        /* vui_parameters_present_flag */
    venus_bits_write(&bits, 0, 1);        /* sps_extension_present_flag */
    venus_bits_finish_rbsp(&bits);

    if (!venus_bits_ok(&bits))
        return -ENOSPC;
    *size = venus_bits_size(&bits);
    return 0;
}

static int build_pps(const struct venus_hevc_sequence *sequence,
                     unsigned int pps_id, uint8_t *rbsp, size_t capacity,
                     size_t *size)
{
    const VAPictureParameterBufferHEVC *picture = sequence->picture;
    struct venus_bit_writer bits;

    if (picture->num_tile_columns_minus1 >=
            sizeof(picture->column_width_minus1) /
                sizeof(picture->column_width_minus1[0]) ||
        picture->num_tile_rows_minus1 >=
            sizeof(picture->row_height_minus1) /
                sizeof(picture->row_height_minus1[0]))
        return -EINVAL;

    venus_bits_init(&bits, rbsp, capacity);
    venus_bits_write_ue(&bits, pps_id);
    venus_bits_write_ue(&bits, 0);        /* pps_seq_parameter_set_id */
    venus_bits_write(
        &bits,
        picture->slice_parsing_fields.bits.dependent_slice_segments_enabled_flag,
        1);
    venus_bits_write(
        &bits, picture->slice_parsing_fields.bits.output_flag_present_flag, 1);
    venus_bits_write(&bits, picture->num_extra_slice_header_bits, 3);
    venus_bits_write(&bits, picture->pic_fields.bits.sign_data_hiding_enabled_flag, 1);
    venus_bits_write(
        &bits, picture->slice_parsing_fields.bits.cabac_init_present_flag, 1);
    venus_bits_write_ue(&bits, picture->num_ref_idx_l0_default_active_minus1);
    venus_bits_write_ue(&bits, picture->num_ref_idx_l1_default_active_minus1);
    venus_bits_write_se(&bits, picture->init_qp_minus26);
    venus_bits_write(&bits, picture->pic_fields.bits.constrained_intra_pred_flag, 1);
    venus_bits_write(&bits, picture->pic_fields.bits.transform_skip_enabled_flag, 1);
    venus_bits_write(&bits, picture->pic_fields.bits.cu_qp_delta_enabled_flag, 1);
    if (picture->pic_fields.bits.cu_qp_delta_enabled_flag)
        venus_bits_write_ue(&bits, picture->diff_cu_qp_delta_depth);
    venus_bits_write_se(&bits, picture->pps_cb_qp_offset);
    venus_bits_write_se(&bits, picture->pps_cr_qp_offset);
    venus_bits_write(
        &bits,
        picture->slice_parsing_fields.bits
            .pps_slice_chroma_qp_offsets_present_flag,
        1);
    venus_bits_write(&bits, picture->pic_fields.bits.weighted_pred_flag, 1);
    venus_bits_write(&bits, picture->pic_fields.bits.weighted_bipred_flag, 1);
    venus_bits_write(
        &bits, picture->pic_fields.bits.transquant_bypass_enabled_flag, 1);
    venus_bits_write(&bits, picture->pic_fields.bits.tiles_enabled_flag, 1);
    venus_bits_write(
        &bits, picture->pic_fields.bits.entropy_coding_sync_enabled_flag, 1);
    if (picture->pic_fields.bits.tiles_enabled_flag) {
        uint32_t index;

        venus_bits_write_ue(&bits, picture->num_tile_columns_minus1);
        venus_bits_write_ue(&bits, picture->num_tile_rows_minus1);
        venus_bits_write(&bits, 0, 1);    /* uniform_spacing_flag */
        for (index = 0;
             index < picture->num_tile_columns_minus1; index++)
            venus_bits_write_ue(&bits, picture->column_width_minus1[index]);
        for (index = 0; index < picture->num_tile_rows_minus1; index++)
            venus_bits_write_ue(&bits, picture->row_height_minus1[index]);
        venus_bits_write(
            &bits, picture->pic_fields.bits.loop_filter_across_tiles_enabled_flag,
            1);
    }
    venus_bits_write(
        &bits, picture->pic_fields.bits.pps_loop_filter_across_slices_enabled_flag,
        1);
    /* The slice headers of a stream that signals deblocking parameters need
     * this flag set, and VA-API reports the parameters themselves.
     */
    venus_bits_write(&bits, 1, 1);        /* deblocking_filter_control */
    venus_bits_write(
        &bits,
        picture->slice_parsing_fields.bits
            .deblocking_filter_override_enabled_flag,
        1);
    venus_bits_write(
        &bits,
        picture->slice_parsing_fields.bits.pps_disable_deblocking_filter_flag,
        1);
    if (!picture->slice_parsing_fields.bits.pps_disable_deblocking_filter_flag) {
        venus_bits_write_se(&bits, picture->pps_beta_offset_div2);
        venus_bits_write_se(&bits, picture->pps_tc_offset_div2);
    }
    venus_bits_write(&bits, 0, 1);        /* pps_scaling_list_data_present */
    venus_bits_write(
        &bits, picture->slice_parsing_fields.bits.lists_modification_present_flag,
        1);
    venus_bits_write_ue(&bits, picture->log2_parallel_merge_level_minus2);
    venus_bits_write(
        &bits,
        picture->slice_parsing_fields.bits
            .slice_segment_header_extension_present_flag,
        1);
    venus_bits_write(&bits, 0, 1);        /* pps_extension_present_flag */
    venus_bits_finish_rbsp(&bits);

    if (!venus_bits_ok(&bits))
        return -ENOSPC;
    *size = venus_bits_size(&bits);
    return 0;
}

static int append_set(struct venus_annexb_writer *writer, uint32_t nal_type,
                      const uint8_t *rbsp, size_t size)
{
    uint8_t header[2];

    write_nal_header(header, nal_type);
    return venus_annexb_write_nal(writer, header, sizeof(header), rbsp, size);
}

int venus_hevc_write_parameter_sets(struct venus_annexb_writer *writer,
                                    const struct venus_hevc_sequence *sequence,
                                    unsigned int pps_id)
{
    uint8_t rbsp[VENUS_HEVC_HEADERS_MAX];
    size_t size;
    int status;

    if (!writer || !sequence || !sequence->picture)
        return -EINVAL;

    status = build_vps(sequence, rbsp, sizeof(rbsp), &size);
    if (status < 0)
        return status;
    status = append_set(writer, VENUS_HEVC_NAL_VPS, rbsp, size);
    if (status < 0)
        return status;

    status = build_sps(sequence, rbsp, sizeof(rbsp), &size);
    if (status < 0)
        return status;
    status = append_set(writer, VENUS_HEVC_NAL_SPS, rbsp, size);
    if (status < 0)
        return status;

    status = build_pps(sequence, pps_id, rbsp, sizeof(rbsp), &size);
    if (status < 0)
        return status;
    return append_set(writer, VENUS_HEVC_NAL_PPS, rbsp, size);
}

static unsigned int read_bit(const uint8_t *data, size_t bit_offset)
{
    return (data[bit_offset / 8] >> (7u - bit_offset % 8)) & 1u;
}

/* Exp-Golomb, the code the parameter set identifiers use.  UINT_MAX means
 * the bits ran out.
 */
static uint32_t read_ue(const uint8_t *data, size_t size, size_t *bit_offset)
{
    unsigned int leading = 0;
    unsigned int index;
    uint32_t value = 0;

    while (*bit_offset < size * 8 && read_bit(data, *bit_offset) == 0) {
        if (++leading > 16)
            return UINT_MAX;
        (*bit_offset)++;
    }
    if (*bit_offset < size * 8)
        (*bit_offset)++; /* the '1' that closes the prefix */

    for (index = 0; index < leading; index++) {
        if (*bit_offset >= size * 8)
            return UINT_MAX;
        value = (value << 1) | read_bit(data, *bit_offset);
        (*bit_offset)++;
    }

    return value + ((1u << leading) - 1u);
}

unsigned int venus_hevc_pps_id(const uint8_t *nal, size_t size, bool rap_pic)
{
    size_t bit_offset = 16; /* the two NAL unit header bytes */
    uint32_t value;

    if (!nal || size < 4)
        return 0;

    if (read_bit(nal, bit_offset) != 1) {
        /* Not a first slice segment: the parameter set id sits behind a
         * segment address whose length this parser would have to derive.
         */
        return 0;
    }
    bit_offset++;
    if (rap_pic)
        bit_offset++; /* no_output_of_prior_pics_flag */

    value = read_ue(nal, size, &bit_offset);
    return value < 64 ? value : 0;
}
