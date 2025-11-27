////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2016-2022 Qualcomm Technologies, Inc.
// All Rights Reserved.
// Confidential and Proprietary - Qualcomm Technologies, Inc.
////////////////////////////////////////////////////////////////////////////////
#ifndef __MESH_FUSION_H__
#define __MESH_FUSION_H__

#if defined(__cplusplus)
extern "C"
{
#endif

#ifdef _WIN32
#define MESH_EXPORTS
#endif

#ifdef MESH_EXPORTS
    /// MACRO enables function to be visible in shared-library case.
#define MESH_API __declspec(dllexport)
#else
    /// MACRO empty for non-shared-library case.
#define MESH_API
#endif

//==============================================================================
// Build Config.
//==============================================================================

#define WARP_MESH_MAX_W (64)
#define WARP_MESH_MAX_H (96)

#define WARP_GRID_MAX_W (WARP_MESH_MAX_W+1)
#define WARP_GRID_MAX_H (WARP_MESH_MAX_H+1)

#define UWM_MESH_SIZE (940)

//==============================================================================
// Declarations
//==============================================================================
typedef struct mesh_one_entry_s {
    float x_in;
    float y_in;
} mesh_one_entry_t;

typedef struct mesh_s {
    unsigned int mesh_vertices_num_x;
    unsigned int mesh_vertices_num_y;
    unsigned int input_width;
    unsigned int input_height;
    unsigned int output_width;
    unsigned int output_height;
    int type; // 0 for pixel shifts, 1 for 3x3 matrix
    mesh_one_entry_t mapping[WARP_GRID_MAX_H][WARP_GRID_MAX_W];

	// added for visualization of AEC ROI
	//float sensorHomography[9];
} mesh_t;

typedef struct alpha_mesh_s {
    float alpha[WARP_GRID_MAX_H][WARP_GRID_MAX_W];
}alpha_mesh_t;

typedef struct mesh_fusion_win_s {
    unsigned int input_width;
    unsigned int input_height;
    unsigned int start_x;
    unsigned int start_y;
    unsigned int end_x;
    unsigned int end_y;
    unsigned int output_width;
    unsigned int output_height;
    unsigned int hblank;
    unsigned int vblank;
    unsigned int binning_factor_h;
    unsigned int binning_factor_v;
} mesh_fusion_win_t;

typedef struct mesh_config_s {
    int ldc_en;
    int sv_en;
    int eis_en;
    int hw_dewarping_en;
    mesh_t *ldc_mesh;
    mesh_t *super_view_mesh;
    mesh_t *eis_mesh;
    mesh_t *output_mesh;
}mesh_config_t;

MESH_API int mesh_fusion_init(void** handle);
MESH_API int mesh_fusion_deinit(void* handle);
MESH_API int mesh_fusion(void* handle, mesh_config_t* config);
MESH_API int mesh_fusion_adapt_window(void * handle, mesh_config_t* config, mesh_fusion_win_t* sensor_win, mesh_fusion_win_t* ife_win, mesh_fusion_win_t* isp_win);

#if defined(__cplusplus)
}
#endif

#endif
