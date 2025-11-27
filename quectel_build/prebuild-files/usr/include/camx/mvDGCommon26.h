#ifndef __MV_DG_COMMON_H__
#define __MV_DG_COMMON_H__

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2016, 2019-2022 Qualcomm Technologies, Inc.
// All Rights Reserved.
// Confidential and Proprietary - Qualcomm Technologies, Inc.
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
// Defines
//==============================================================================


//==============================================================================
// Includes
//==============================================================================
#include "mv26.h"

//==============================================================================
// Declarations
//==============================================================================

#ifdef __cplusplus
extern "C"
{
#endif

#define WARP_MESH_MAX_W (64)
#define WARP_MESH_MAX_H (96)

#define WARP_GRID_MAX_W (WARP_MESH_MAX_W+1)
#define WARP_GRID_MAX_H (WARP_MESH_MAX_H+1)

#ifndef GYRO_BIAS_TMPT_COMP_KNEE_NUM
#define GYRO_BIAS_TMPT_COMP_KNEE_NUM (6)
#endif

#define MVDGTC_FILTER_PARAM_LENGTH (8)
#define MVDGTC_FILTER_PARAM_LENGTH_ORIGINAL (8)
#define MAX_EIS_STENGTH_ENTRIES (10)

#ifndef MAX_IMU_BUFFER_SIZE
#define MAX_IMU_BUFFER_SIZE (266*IMU_BUFFER_FRAME_CAP) // 8000 Hz sampling rate
#endif

#define MAX_EXTPOSE_CNT (MAX_IMU_BUFFER_SIZE)
#define EISDG_INVALID_DATA (-1)
#define MAX_EXP_FRAMES (4)

    //------------------------------------------------------------------------------
    /// @brief
    //------------------------------------------------------------------------------
    typedef mesh_one_entry_t mvDG_mesh_one_entry_t;

    //------------------------------------------------------------------------------
    /// @brief
    //------------------------------------------------------------------------------
    typedef struct {
        int32_t block_w;
        int32_t block_h;
        uint32_t input_width;
        uint32_t input_height;
        uint32_t output_width;
        uint32_t output_height;
        int32_t type; // 0 for pixel shifts, 1 for 3x3 matrix
        mvDG_mesh_one_entry_t mapping[WARP_GRID_MAX_H][WARP_GRID_MAX_W];
    } mvDG_mesh_t;

    //------------------------------------------------------------------------------
    /// @brief
    //------------------------------------------------------------------------------
    typedef struct {
        int32_t tmpt_bias_comp_en;
        float32_t drift_table[GYRO_BIAS_TMPT_COMP_KNEE_NUM][3];
        uint32_t drift_temp_knee[GYRO_BIAS_TMPT_COMP_KNEE_NUM];
    }mvDG_imu_temp_t;

    //------------------------------------------------------------------------------
    /// @brief
    //------------------------------------------------------------------------------
    typedef struct {
        float32_t R_gyro_2_cam[9]; // map gyro coordinate to EIS camera coordinate
        float32_t R_accel_2_cam[9]; // map accelerometer coordinate to EIS camera coordinate
        float32_t R_extpose_2_cam[9]; // map external pose coordinate to EIS camera coordinate
        float32_t R_aux_gyro_2_cam[9]; // map aux gyro coordinate to EIS camera coordinate
        float32_t R_aux_accel_2_cam[9]; // map aux accel coordinate to EIS camera coordinate
        uint32_t acc_unit;
        uint32_t gyro_unit;
        int32_t constant_delay_off_pts_in_us;
        int32_t accel_constant_delay_off_pts_in_us;
        mvDG_imu_temp_t temp_calib;
    } mvDG_imu_calib_t;

    //------------------------------------------------------------------------------
    /// @brief
    ///     Configuration and tuning parameters for horizon-leveling.
    //------------------------------------------------------------------------------
    typedef struct {
        uint32_t horizon_leveling_en;
        uint32_t ext_pose_en; // only works when horizon_leveling_en == 1
        uint32_t accel_sampling_rate;
        int32_t accel_unit;
        float32_t mat_accel2cam[9]; // map accelerometer coordinate to EIS camera coordinate
        float32_t mat_extpose2cam[9]; // map external pose coordinate to EIS camera coordinate
        int32_t constant_delay_off_pts_in_us;
        uint32_t num_of_init_frame; // number of frames for pose initialization
        float32_t init_upper_th_deg; // upper threshold: frame-to-frame rotation(degree) larger than this value during num_of_init_frame won't be included for pose initialization
        float32_t comp_filter_k; // complementary filtering: weighting for accelerometer
        float32_t reserved[256];
        // AEC use reserved[N] for changing ROI mode; 0 linear lens; 1 for distorted lens
    } mvDG_HL_t;

    //------------------------------------------------------------------------------
    /// @brief
    //------------------------------------------------------------------------------
    typedef struct {
        float trigger_start;
        float trigger_end;
        float strength;
    } mvDGTC_eis_strength_one_entry_t;

    typedef struct {
        float temperol_filter;
        float number_of_entries;
        mvDGTC_eis_strength_one_entry_t eis_strength_one_entry[MAX_EIS_STENGTH_ENTRIES];
    } mvDGTC_eis_strength_t;

    //--------------------------------------------------------------------------------
    // @brief
    // Configuration and tuning parameters for low pass filter in horizon - leveling.
    //--------------------------------------------------------------------------------
    typedef struct {
        float64_t hl_lp_enable[3];  // hl lp enable flag
        float64_t hl_lp_lower_th[3]; // Lower threshold of lp in hl
        float64_t hl_lp_upper_th[3]; // Upper threshold of lp in hl
        uint8_t hl_lp_enable_yaw;
    }hl_lp_tuning_t;

   //------------------------------------------------------------------------------
   // @brief
   // Tuning parameters for libSmoothing library
   //------------------------------------------------------------------------------
    typedef struct {
        float32_t smoothing_index_pitch;
        float32_t smoothing_index_yaw;
        float32_t smoothing_index_roll;
        float32_t smoothing_additional_k;
        float32_t smoothing_lut_1st_order_small_motion;
        float32_t smoothing_lut_2nd_order_small_motion;
        float32_t smoothing_lut_1st_order_large_motion;
        float32_t smoothing_lut_2nd_order_large_motion;
    }filter_param_t;

    //------------------------------------------------------------------------------
    /// @brief
    //------------------------------------------------------------------------------
    typedef struct {
        filter_param_t filter_param;
        mvDGTC_eis_strength_t *eis_strength;

        uint32_t horizon_leveling_en;
        uint32_t ext_pose_en; // only works when horizon_leveling_en == 1

        // Tuning parameters related to libsensor fusion v3
        float32_t q_6degree[4];
        uint8_t pipeline_config_mode;  // 0: regular 1 : accel_f_gyrp_c 2 : gyro_c_ext 3 : prefusion(The mode of pipeline config in option 2)
        float64_t small_number; // The initial small number(not pre - fusion mode) in fulfil lib
        float64_t big_number; // The initial big number(not pre - fusion mode) in fulfil lib
        float64_t prefusion_small_number; // The initial small number(pre - fusion mode) in fulfil lib
        float64_t prefusion_big_number; // The initial big number(pre - fusion mode) in fulfil lib
        float64_t running_phase_small_number;
        float64_t running_phase_big_number;
        float64_t gyro_bias; // Gyro bias used in fulfil lib
        float64_t gyro_bias_prefusion; // Gyro bias used in fulfil lib(prefusion mode)
        float64_t pre_take_off_accel_std;
        float64_t pre_take_off_gyro_std;
        uint32_t pre_take_off_revert_length;
        float64_t inc_number; // Inc used in fitting logic
        uint16_t window_size; // window size of fitting logic
        float64_t aero_norm_lower_bound; // Lower bound of norm threshold
        float64_t aero_norm_upper_bound; // Upper bound of norm threshold
        float64_t aero_norm_diff_threshold; // aero dynamic norm diff threshold
        uint8_t aero_iterative_en; // aero dynamic iterative / non - iterative label
        uint8_t aero_dynamic_en; // aero dynamic enable flag
        uint8_t sensor_fusion_type; // sensor fusion type 0: v1 1 : v3
        hl_lp_tuning_t hl_lp_tuning_parameters;  // HL low pass filter tuning parameters
        float64_t pitch_offset; // tuning value : 0
        uint32_t check_switch_point_en;
        uint32_t pts_gap_between_cimu_and_fimu;
    } mvDGTC_tuning_t;

    //------------------------------------------------------------------------------
    /// @brief
    /// reserved fields
    //------------------------------------------------------------------------------
    typedef enum {
        MVDG_CONFIG_RESERVED_FIELD_NUMBER_OF_FRAMES,
        MVDG_CONFIG_RESERVED_FIELD_MAX
    }mvDGTC_reserved_t;

    //------------------------------------------------------------------------------
    /// @brief
    ///     Configuration parameters for initializing DG
    //------------------------------------------------------------------------------
    typedef struct {
        mvCameraConfiguration camera;   /// Incoming camera intrinsic calibration

        uint32_t pxlWidth, pxlHeight;  /// Output image size and memory layout
        uint16_t FrameRateInHz;
        uint16_t minFrameRateInHz; //minimun frame rate for calculating gyro buffer size

        // added for EIS4.0
        int32_t debug_print_en;
        mvDG_mesh_t* ldc_mesh;

        float32_t ldc_r2_lut[1024]; // pinhole to distorted
        float ldc_r2_lut_size;
        mvDG_imu_calib_t imu_calib;
        float32_t pixel_size_in_um;
        float32_t focal_length_in_mm;
        uint32_t imu_data_sampling_rate; // main gyro sampling rate
        uint32_t accel_sampling_rate; // main accel sampling rate
        uint32_t ext_pose_sampling_rate; // external pose sampling rate
        float32_t fbis_margin;
        float32_t rsc_margin;
        mvDGTC_tuning_t tuning_param;

        uint32_t eis_en; //support pure GFA, pure SHA, GFA+SHA as well.
        uint32_t hw_dewarping_en; //enable flag for supporting IPE ICA dewarping. 0 for disable, postIPE case; 1 for enable, preIPE case.
        uint32_t gfa_en; //enable flag for global alignment
        uint32_t sha_en; //enable flag for SHDR alignment
        uint32_t ldc_en; // enable flag for ldc
        uint32_t partial_ldc_en; // enable flag for partial ldc
        uint32_t superview_en; // enable flag for superview
        uint32_t tars_en; // enable flag for tars
        uint32_t number_of_eis_frames; //number of EIS frames for MFEIS
        uint32_t number_of_exp_frames; //number of exposure frames for SHA
        uint32_t sha_anchor_frame; //anchor frame number for SHA
    } mvDGConfiguration;

    //------------------------------------------------------------------------------
    /// @brief
    ///     timestamp and temperature information. (degree)
    //------------------------------------------------------------------------------
    typedef struct {
        unsigned long long  time_usec;
        float temperature;
    } mvTemp; // for receiving temperature

    //------------------------------------------------------------------------------
    /// @brief
    ///     Configuration parameters for libSensorFusion_custom
    //------------------------------------------------------------------------------
    typedef struct {
        unsigned long long pts;
        float gyro[3];
        float accel[3];
        float reserved[3];
        unsigned int temperature_in_fahrenheit;
    } mvDG_imu_data_t;

    typedef struct {
        mvDG_imu_data_t imu_data;
        void* eis_hl_config_ptr;

        int accel_unit;

        unsigned long long input_pts;
        unsigned long long frame_cnt;
        const void* smoothing_ctx;
        mvDG_imu_data_t imu_pred;
        mvDG_imu_data_t imu_accumulated_hp;

        int debug_mode;
        int lp_debug;

        float pose[3];

        float(*lib_smoothing)(const void* p, void* proc_param);
    } mvDG_libsensor_fusion_proc_t;

    //------------------------------------------------------------------------------
    /// @brief
    ///     Pose of coordinate definition and presentation that will be passed by mvDG_AddExtPose API
    //------------------------------------------------------------------------------
    typedef enum {
        mvDG_COORD_DEFINE_EIS_COORD = 0,
        mvDG_COORD_DEFINE_NED_COORD,
        mvDG_COORD_DEFINE_Max
    } mvDG_COORD_DEFINE;

    typedef enum {
        mvDG_COORD_PRESENTATION_EULER_ANGLE = 0,
        mvDG_COORD_PRESENTATION_QUATERNION,
        mvDG_COORD_PRESENTATION_Max
    } mvDG_COORD_PRESENTATION;

    //------------------------------------------------------------------------------
    /// @brief
    ///     IMU data type that will be passed by mvDG_AddMultiGyro and mvDG_AddMultiAccel
    //------------------------------------------------------------------------------
    typedef enum {
        mvDG_IMU_MAIN = 0,
        mvDG_IMU_AUX,
        mvDG_IMU_Max
    } mvDG_IMU_TYPE;

    //------------------------------------------------------------------------------
    /// @brief
    ///     timestamp and gyro information the order of roll, yaw, and pitch,.
    //------------------------------------------------------------------------------
    typedef struct
    {
        uint64_t  time_usec;
        float32_t gyro_rad_sec[3];
    } mvGyro;

    //------------------------------------------------------------------------------
    /// @brief
    ///     timestamp and accelerometer information. (m/s^2)
    //------------------------------------------------------------------------------
    typedef struct {
        uint64_t  time_usec;
        float32_t accel_meter_sec_sqr[3];
    } mvAccel; // for receiving accelerometer


    //------------------------------------------------------------------------------
    /// @brief
    ///     timestamp and quaternion information.
    //------------------------------------------------------------------------------
    typedef struct {
        uint64_t  time_usec;
        float32_t quaternion[4]; // [0] for scalar
        //float32_t reserved[12];
    } mvExtPose; // for receiving external pose from app

    //------------------------------------------------------------------------------
    /// @brief
    ///     timestamp and aec weighting table 16 by 16
    //------------------------------------------------------------------------------
    typedef struct {
        uint64_t  time_usec;
        float32_t aec_weightings[256]; // 16x16 weighting table
        float32_t Homography[9];
        float32_t invHomography[9];
    } mvAECWeighting; // for sending out aec ROI weightings

    //------------------------------------------------------------------------------
    /// @brief
    ///     timestamp and flihgt attitude information in Rotation  matrix form.
    //------------------------------------------------------------------------------
    typedef struct {
        uint64_t  time_usec;
        float32_t rotation[9];
    } mvAttitude;

    //------------------------------------------------------------------------------
    /// @brief
    ///     This structure contains timing information of each image data frame.
    /// @time_usec   Timestamp of start of camera frame (SOF)
    /// @exposureTime_usec    Frame exposure time duration in micro seconds
    /// @durationTime_usec    Entire image readout duration
    /// @lineReadoutTime_usec    Sensor one line readout time
    /// @frameid     Image data frame identifiaction number.
    //------------------------------------------------------------------------------
    typedef struct {
        int32_t frameid;
        uint64_t time_usec;
        int64_t exposureTime_usec;
        int64_t durationTime_usec;
        float32_t lineReadoutTime_usec;
        uint32_t max_exp_time_in_us; // max exposure time
    } mvDGCameraFrameTimeInfo;

    //------------------------------------------------------------------------------
    /// @brief
    ///     This structure contains tranformatiom matrix inforamtion of image data frame
    ///     specified by frameid.
    /// @frameid     Image data frame identifiaction number.
    /// @size   Number of tranformation matrix in transArray
    /// @transfArray    Array of transformation matrix
    //------------------------------------------------------------------------------
    typedef struct {
        int32_t frameid;
        uint32_t gridW;
        uint32_t gridH;
        mvPose3DR *transfArray;
    } mvDGTransfMatrixArrayInfo;

    //------------------------------------------------------------------------------
    /// @brief
    ///     This structure contains alpha array inforamtion of image data frame
    ///     specified by frameid.
    /// @frameid     Image data frame identifiaction number.
    /// @size   Number of tranformation matrix in alphaArray
    /// @alphaArray    Array of alpha value
    //------------------------------------------------------------------------------
    typedef struct {
        int32_t frameid;
        uint32_t gridW;
        uint32_t gridH;
        float *alphaArray;
    } mvDGAlphaArrayInfo;

    //------------------------------------------------------------------------------
    /// @brief
    ///     This structure contains image content data for camera frame
    ///     specified by frameid.
    /// @frameid  Image data frame identifiaction number.
    /// @pxlNV12  Pointer to camera frame data in NV12 (YUV420sp v12) format
    /// @ionFd    Pointer to Ion buffer
    //------------------------------------------------------------------------------
    typedef struct {
        int32_t frameid;
        uint8_t *pxlNV12;
        uint32_t *ionFd;
    } mvDGCameraFrameContentInfo;

    typedef struct {
        uint32_t input_width;
        uint32_t input_height;
        uint32_t start_x;
        uint32_t start_y;
        uint32_t end_x;
        uint32_t end_y;
        uint32_t output_width;
        uint32_t output_height;
        uint32_t hblank;
        uint32_t vblank;
        uint32_t binning_factor_h;
        uint32_t binning_factor_v;
    } mvDGWindowInfo;

    //==============================================================================
    /// @brief
    ///     Digital Gimbal (DG)
    //==============================================================================
    typedef struct mvDG mvDG;


    //------------------------------------------------------------------------------
    /// @brief
    ///     Return string of version information.
    ///     Pointer to DG object; returns NULL if failed
    //------------------------------------------------------------------------------
    MV_API const char* mvDG_Version(void);


    //------------------------------------------------------------------------------
    /// @brief
    ///     Initialize DG object
    /// @return
    ///     Pointer to DG object
    //------------------------------------------------------------------------------
    MV_API mvDG* mvDG_Initialize();


    //------------------------------------------------------------------------------
    /// @brief
    ///     set the algorithm parameters to library
    /// @param pnConfig
    ///     Pointer to DG configuration
    ///    NULL is a legal value, it means the mvDG object will take the configuration
    ///    parameter from a config file defined by env. variable DG_CONFIG_FILE_PATH.
    ///
    ///    First, the value in the config file overwrittes hard coded  default values.
    ///    Then, the paramters provided by this API will overwrite the value of same
    ///    parameter in the config file.
    ///
    /// @return
    ///     0 is OK; other value if failed
    //------------------------------------------------------------------------------
    MV_API int32_t mvDG_StartSession(mvDG* pObj, mvDGConfiguration* pnConfig);

    //------------------------------------------------------------------------------
    /// @brief
    ///     clean all the internal state and reset the internal variables.
    /// @param pObj
    ///     Pointer to DG object
    /// @param frameid
    ///     the latest buffered Frame's id.
    //------------------------------------------------------------------------------
    MV_API int32_t mvDG_StopSession(mvDG* pObj, int32_t *frameid);


    //------------------------------------------------------------------------------
    /// @brief
    ///     Deinitialize DG object
    /// @param pObj
    ///     Pointer to DG object
    //------------------------------------------------------------------------------
    void MV_API mvDG_Deinitialize(mvDG* pObj);

#ifdef __cplusplus
}
#endif


#if defined _WIN32 && !defined MV_EXPORTS
// #include "win/mvDG_DLLGlue.h"
#endif


#endif
