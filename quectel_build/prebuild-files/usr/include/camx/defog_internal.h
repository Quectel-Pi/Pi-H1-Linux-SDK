////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2019 Qualcomm Technologies, Inc.
// All Rights Reserved.
// Confidential and Proprietary - Qualcomm Technologies, Inc.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @file  defog_internal.h
/// @brief DefogLib class declarations
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef __DEFOG_INTERNAL_H__
#define __DEFOG_INTERNAL_H__

#include "defog.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#define GAMMA_INDEX_NUM 257
#define GAMMA_MAX_VAL 1023
#define MAX_AEC_AWB_CHECKER_INDEX_NUM 10
#define MAX_SHDR_CHECKER_INDEX_NUM 2
#define MAX_MIX_CHECKER_NUM 8
extern UINT32  g_log_level;

    /// @brief HDR scene type from histogram analysis for fog scene detection
    typedef enum
    {
        HdrSceneTypeInvalid                     =  -1,
        HdrSceneTypeLDRRunFD                    =   0,
        HdrSceneTypeLDRIncorrectHist            =   1,
        HdrSceneTypeLDRRunFDWithLEF             =   2,
        HdrSceneTypeUnknownSceneLDRLefHDRSef    =   3,
        HdrSceneTypeLDRRunFDWithSEF             =   4,
        HdrSceneTypeImageTooBright              =   5,
        HdrSceneTypeHDRScene1                   =   6,
        HdrSceneTypeHDRScene2                   =   7,
        HdrSceneTypeLDRSceneHistSwitched1       =   8,
        HdrSceneTypeIncorrectHistSwitched       =   9,
        HdrSceneTypeImageTooDark                =   10,
        HdrSceneTypeHDRSceneHistSwitched        =   11,
        HdrSceneTypeUnknownSceneHDRLefLDRSef    =   12,
        HdrSceneTypeHDRSceneHistSwitched2       =   13,
        HdrSceneTypeHDRScene3                   =   14,
        HdrSceneTypeHDRScene4                   =   15,
        HdrSceneTypeMax                         =   16,
    } HdrSceneType;

    typedef struct st_ipe_cv
    {
        FLOAT r_to_y;
        FLOAT g_to_y;
        FLOAT b_to_y;
        INT32 y_offset;
    } IPE_CV_PARAMS;

    /* simple histograms for contrast calculation */
    typedef struct st_simple_histogram
    {
        UINT64 shist_3bins[3];
        UINT64 shist_4bins[4];
        UINT64 shist_8bins[8];
        UINT64 shist_8bins_pct[8]; /* ex: 755 = 75.5% */
        UINT64 shist_16bins[16];
        UINT64 shist_16bins_pct[16]; /* ex: 755 = 75.5% */
    } S_HIST;

    /* @brief Structure to measure KPI */
    typedef struct
    {
        UINT32 start_time;
        UINT32 start_time_of_each_frame;
        UINT32 end_time;
        UINT32 total_time;
        UINT32 frame_count;
    } StatsKPIType;

	/* simple AEC checker for settle detection */
	typedef struct st_simple_aec_checker
	{
        BOOL settledFromAECLib[MAX_AEC_AWB_CHECKER_INDEX_NUM];
        FLOAT luxIdxTb[MAX_AEC_AWB_CHECKER_INDEX_NUM];
        UINT32 idxNum;
	} S_AEC_CHECKER;

    /* simple AWB checker for settle detection */
    typedef struct st_simple_awb_checker
    {
        BOOL settledFromAWBLib[MAX_AEC_AWB_CHECKER_INDEX_NUM];
        UINT32 cctTb[MAX_AEC_AWB_CHECKER_INDEX_NUM];
        UINT32 idxNum;
    } S_AWB_CHECKER;

    /* simple SHDR checker for settle detection */
    typedef struct st_simple_shdr_checker
    {
        BOOL settledFromSHDRLib[MAX_SHDR_CHECKER_INDEX_NUM];
        UINT32 ratioTb[MAX_SHDR_CHECKER_INDEX_NUM];
        UINT32 idxNum;
    } S_SHDR_CHECKER;

    /* histogram calculating infomation for fog detection and process */
    typedef struct st_hist_calc_info
    {
        BOOL is_hist_settled;
        //BOOL is_hist_ready;                    ///< hist or bHist module can get sensor data and ouput hist
        //BOOL is_hist_valid;                    ///< Check if hist data populated by SHDR3 lib is valid per frame
        UINT32 hist_sum;
        UINT32 hist_deci_cnt;
        INT32 hist_dark_bin;
        //INT32 hist_mid_bin;
        INT32 hist_bright_bin;
        INT32 hist_min_bins;
        INT32 hist_max_bins;
        INT32 hist_avg_bins;
        INT32 hist_old_dark_bin;
        INT32 hist_old_bright_bin;
    } S_HIST_CALC_INFO;

    /* Internal parameters for ATES/Defog control */
    typedef SHDR_MODE_ATES_DEFOG_TUNING_PARAMS S_ATES_DEFOG_CTRL;

    /* Internal common parameters for runtime control */
    struct DefogConfigParams
    {
        //UINT32 param_update_mask;                       ///< [0x00000000:0x0000FFFF], refer to 'DefogConfParamGroup' and decide which below parameters should be updated.

                                                        /* ATES/Defog/CE enanle */
        BOOL   ates_en;                                 ///< 0: disable ATES, 1: enable ATES
        FLOAT  ates_str;                                ///< [0.0:10.0], 0.0: no ATES effect, 10.0: strongest ATES effect            (default: 1.0)

        BOOL   defog_en;                                ///< 0: disable defog, 1: enable defog                                       (default: 1)
        BOOL   ce_en;                                   ///< 0: disable CE(contrast enahancement), 1: enable CE                      (default: 0)

                                                        /* Defog/CE basic settings */
        UINT32 speed;                                   ///< [0:10], convergence speed, 0: slowest, 10: fastest                      (default: 10)
        UINT32 convergence_mode;                        ///< 1: serial mode, 2: parallel mode                                        (default: 1)

                                                        /* Defog/CE scene detector settings */
        BOOL   sd_en;                                   ///< 0: disable SD(scene detector), 1: enable SD                             (default: 1)
        BOOL   sd_2a_en;                                ///< 0: SD without 2A info, 1: SD with 2A info                               (default: 1)

                                                        /* Defog basic settings */
        UINT32 algo;                                    ///< 0: Gamma Defog, 1: CV Defog, 2: Manual Gamma Defog, 3: Manual CV Defog  (default: 0)
        FLOAT  strength;                                ///< [0.0:10.0], 0.0: no defog effect, 10.0: strongest defog effect          (default: 1.0)
        UINT32 lp_algo_decision_mode;                   ///< 0: min bin decision, 1: avg bin decision, 2: max bin decision           (default: 0)
        FLOAT  lp_algo_color_comp_gain;                 ///< 0.0: disable color compensation, 1.0: 1x color compensation gain        (default: 1.0)

                                                        /* Defog advanced settings */
        BOOL   abc_en;                                  ///< 0: disable ABC(adaptive brightness compensation), 1: enable ABC         (default: 1)
        BOOL   acc_en;                                  ///< 0: disable ACC(advanced contrast control), 1: enable ACC                (default: 1)
        UINT32 defog_dark_thres;                        ///< [1:256]  (default: 20): the two parameters are for defog reprocess. if defog status is settled and dark/bright bins are
        UINT32 defog_bright_thres;                      ///< [1:256]  (default: 80):   within the range of defog_bright_thres and defog_dark_thres, it will keep the previous defog settings.
        UINT32 dark_limit;                              ///< [0:255]: limit max internal defog strength for dark regions             (default: 255)
        UINT32 bright_limit;                            ///< [0:255]: limit max internal defog strength for bright regions           (default: 0)
        UINT32 dark_preserve;                           ///< [0:255]: move dark bin to preserve dark details                         (default: 10)
        UINT32 bright_preserve;                         ///< [0:255]: move bright bin to preserve bright details                     (default: 50)
        FLOAT  abc_str;                                 ///< [0.0:4.0]: strength for adaptive brightness compensation                (default: 2.0)
        FLOAT  acc_dark_str;                            ///< [0.0:4.0]: ACC strength for dark region                                 (default: 2.0)
        FLOAT  acc_bright_str;                          ///< [0.0:4.0]: ACC strength for bright region                               (default: 0.5)
        FOG_SCENE_DETECTION_PARAMS defog_trig_params;   ///< Define DNR/LuxIdx/CCT triggers for fog scene detection                  (default settings are as below)

                                                        // CE advanced settings
        BOOL    guc_en;                                 // 0: disable GUC(Gain-up control), enable GUC                             (default: 1)
        BOOL    dcc_en;                                 // 0: disable DCC(dynamic contrast control), 1: enable DCC                 (default: 1)
        FLOAT   guc_str;                                // [0.0:5.0]: GUC strength on CE mode                                      (default: 1.0)
        FLOAT   dcc_dark_str;                           // [0.0:4.0]: DCC strength for dark region on CE mode                      (default: 1.0)
        FLOAT   dcc_bright_str;                         // [0.0:4.0]: DCC strength for bright region on CE mode                    (default: 1.0)
        CE_SCENE_DETECTION_PARAMS ce_trig_params;       // Define Gain triggers for ce scene detection                             (default settings are as below)
    };

    struct DefogInternalData
    {
        /* Store input data */
        DefogConfigParams                                           defogConfigData;
        DefogConfigDefogParams                                      vendorTagConfigDefogData;
        DefogConfigATESParams                                       vendorTagConfigATESData;
        DefogInputData                                              defogInputData;
        DefogOutputData                                             defogOutputData;

        /* Defog KPI convergence time structure */
        StatsKPIType                                                kpi;

        /* internal parameters */
        UINT32 convergence_mode;
        FLOAT lp_defog_param;
        ParsedIHistStatsOutput*                                     pInterIHistStats;
        ParsedHDRBHistStatsOutput*                                  pInterHDRBHistStats;
        S_HIST                                                      s_hist;
        IPE_CV_PARAMS                                               ipe_cv_param;
        IPE_CV_PARAMS                                               defogCV;
        FLOAT                                                       gain_up_tb[GAMMA_INDEX_NUM];
        FLOAT                                                       contrast_tb[GAMMA_INDEX_NUM];
        S_AEC_CHECKER                                               aecChecker;
        S_AWB_CHECKER                                               awbChecker;
        S_SHDR_CHECKER                                              shdrChecker;
        S_HIST_CALC_INFO                                            DefogHistCalcInfo;
        S_HIST_CALC_INFO                                            AtesHistCalcInfo;
        S_HIST_CALC_INFO                                            SefHistCalcInfo;
        S_HIST_CALC_INFO                                            LefHistCalcInfo;
        UINT32 fog_scene_probability;
        UINT32 ce_scene_probability;
        INT32 hq_defog_state;
        INT32 lp_defog_state;
        INT32 abc_state;
        INT32 acc_state;
        INT32 guc_state;
        INT32 dcc_state;
        BOOL is_aec_settled;
        BOOL is_awb_settled;
        BOOL is_shdr_settled;
        BOOL is_hist_settled;
        BOOL is_hist_ready;                    ///< hist module can get sensor data and ouput hist
        BOOL is_hist_valid;                    ///< Check if hist data populated by SHDR3 lib is valid per frame
        BOOL is_defog_settled;
        BOOL is_ce_settled;
        BOOL is_mix_settled;
        UINT32 shdr_scene_type;
        UINT32 hist_sum;
        UINT32 hist_deci_cnt;
        INT32 hist_dark_bin;
        INT32 hist_mid_bin;
        INT32 hist_bright_bin;
        INT32 hist_min_bins;
        INT32 hist_max_bins;
        INT32 hist_avg_bins;
        INT32 hist_old_dark_bin;
        INT32 hist_old_bright_bin;

        /* Internal parameters for ATES/Defog IQ control */
        BOOL ates_en;
        BOOL isYUVSHDREnabled;

        UINT32 mix_settle_count;
        BOOL is_xml_updated;
        BOOL is_vendor_tag_updated;
        TUNING_XML_PARAMS tuning_xml_data;              ///< Save a copy of XML value
        TUNING_XML_PARAMS runtime_xml_data;           ///< Adjusted XML values according to DefogSetDefogParams() and DefogSetATESParams()
        SHDR_MODE_ATES_DEFOG_TUNING_PARAMS fsd_ctrl;    ///< Internal use for fog detection of SHDR mode

        /* parameters for video simulation */
        FLOAT defog_cur_strength;
        FLOAT defog_cur_abc_knee_pt;
        FLOAT defog_cur_acc_dark_knee_pt;
        FLOAT defog_cur_acc_bright_knee_pt;
        FLOAT ce_cur_strength;
        FLOAT ce_cur_guc_knee_pt;
        FLOAT ce_cur_dcc_dark_knee_pt;
        FLOAT ce_cur_dcc_bright_knee_pt;
        FLOAT acc_strength;
        FLOAT lp_defog_cur_param;
        FLOAT lp_defog_step;
        INT32 hq_defog_org_dark_bin;
        INT32 hq_defog_org_bright_bin;
        INT32 hq_defog_cur_dark_bin;
        INT32 hq_defog_cur_bright_bin;
        INT32 defog_ce_step_num;
        INT32 hq_defog_dark_step_sz;
        INT32 hq_defog_bright_step_sz;
        INT32 hq_defog_step_num;                ///< final HQ defog steps
        FLOAT defog_abc_step;
        FLOAT defog_acc_dark_step;
        FLOAT defog_acc_bright_step;
        FLOAT ce_guc_step;
        FLOAT ce_dcc_dark_step;
        FLOAT ce_dcc_bright_step;
    };

#if 1
#undef LOG_ERROR
#undef LOG_WARN
#undef LOG_INFO
#undef LOG_VERBOSE

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////
/// @brief Macro for printing error logs to the system
///
/// @param group LogGroup of the log
/// @param fmt Format string, printf style
/// @param ... Parameters required by format
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////

#if 1
#define LOG_ERROR(fmt, args...)                                         \
    ALOGE(" %s():%d " fmt "\n", __func__, __LINE__, ##args);

#endif
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////
/// @brief Macro for printing warning logs to the system
///
/// @param group LogGroup of the log
/// @param fmt Format string, printf style
/// @param ... Parameters required by format
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////
#define DEFOG_LOG_WARN(fmt, args...)                                          \
    if (g_log_level >= CamxLogWarning)                                         \
    {                                                                          \
        ALOGW(" %s():%d " fmt "\n", __func__, __LINE__, ##args);               \
    }

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////
/// @brief Macro for printing info logs to the system
///
/// @param group LogGroup of the log
/// @param fmt Format string, printf style
/// @param ... Parameters required by format
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////

#if 1

#define DEFOG_LOG_INFO(fmt, args...)                                          \
    if (g_log_level >= CamxLogInfo)                                            \
    {                                                                          \
        ALOGI(" %s():%d " fmt "\n", __func__, __LINE__, ##args);               \
    }

#endif
///////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////
/// @brief Macro for printing verbose logs to the system
///
/// @param group LogGroup of the log
/// @param fmt Format string, printf style
/// @param ... Parameters required by format
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////
#define LOG_VERBOSE(group, fmt, args...)                                       \
    if (g_log_level >= CamxLogVerbose)                                         \
    {                                                                          \
        ALOGV(" %s():%d " fmt "\n", __func__, __LINE__, ##args);               \
    }

#if 0

#if defined(_MSC_VER) || defined(_WIN32)
#define CAMX_LOG_ERROR(group, fmt, ...)                                    \
    printf(fmt"\n",  __VA_ARGS__)
#define CAMX_LOG_WARN(group, fmt, ...)                                     \
    printf(fmt"\n", __VA_ARGS__)
#define CAMX_LOG_INFO(group, fmt, ...)                                     \
    printf(fmt"\n", __VA_ARGS__)
#define CAMX_LOG_VERBOSE(group, fmt, ...)                                  \
    printf(fmt"\n", __VA_ARGS__)
#else
#undef CAMX_LOG_VERBOSE
#undef CAMX_LOG_INFO
#undef CAMX_LOG_WARN
#undef CAMX_LOG_ERROR
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Macro for printing error logs to the system
/// @param group LogGroup of the log
/// @param fmt Format string, printf style
/// @param ... Parameters required by format
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define CAMX_LOG_ERROR(group, fmt, ...)                                    \
    CAMX_LOG(CamxLogError, "[ERROR]", group, fmt, ##__VA_ARGS__)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Macro for printing warning logs to the system
/// @param group LogGroup of the log
/// @param fmt Format string, printf style
/// @param ... Parameters required by format
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define CAMX_LOG_WARN(group, fmt, ...)                                     \
    if (g_log_level >= CamxLogWarning)                               \
    {                                                                      \
            CAMX_LOG(CamxLogWarning, "[ WARN]", group, fmt, ##__VA_ARGS__) \
    }
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Macro for printing info logs to the system
/// @param group LogGroup of the log
/// @param fmt Format string, printf style
/// @param ... Parameters required by format
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define CAMX_LOG_INFO(group, fmt, ...)                                     \
    if (g_log_level >= CamxLogInfo)                                  \
    {                                                                      \
        CAMX_LOG(CamxLogInfo, "[ INFO]", group, fmt, ##__VA_ARGS__)        \
    }
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Macro for printing verbose logs to the system
/// @param group LogGroup of the log
/// @param fmt Format string, printf style
/// @param ... Parameters required by format
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define CAMX_LOG_VERBOSE(group, fmt, ...)                                  \
    if (g_log_level >= CamxLogVerbose)                               \
    {                                                                      \
        CAMX_LOG(CamxLogInfo, "[ VERB]", group, fmt, ##__VA_ARGS__)        \
    }

#endif

#endif

#endif


#ifdef __cplusplus
}
#endif

#endif /* __DEFOG_INTERNAL_H__ */
