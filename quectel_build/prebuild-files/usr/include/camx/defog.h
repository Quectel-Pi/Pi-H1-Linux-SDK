////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2019 Qualcomm Technologies, Inc.
// All Rights Reserved.
// Confidential and Proprietary - Qualcomm Technologies, Inc.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @file  defog.h
/// @brief DefogLib class declarations
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef __DEFOG_H__
#define __DEFOG_H__

#include "camxdefs.h"
#include "iqcommondefs.h"
#include "camxchinodeutil.h"
#include "chiiqmoduledefines.h"
#include "properties.h"
//#include <log.h>
//#include "shdr3.h"
//#include "shdr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEFOG_CE_VERSION "2.5.0"
#define DISABLE_CE_MODULE
//#define DEFOG_FOR_DEMO3

//remove this after shdr3 addition
#define TM_RESERVED_TABLE_SIZE (32)

	/// @brief The group tag for defog input update
	typedef UINT32 DefogInputTypeGroup;
	static const DefogInputTypeGroup DefogInputTypeGroupNone                    = (1 << 0);     ///< Generic group
	static const DefogInputTypeGroup DefogInputTypeGroupAECStats                = (1 << 1);     ///< AEC Stats
	static const DefogInputTypeGroup DefogInputTypeGroupAWBStats                = (1 << 2);     ///< AWB Stats
	static const DefogInputTypeGroup DefogInputTypeGroupSHDRStats               = (1 << 3);     ///< SHDR Stats
	static const DefogInputTypeGroup DefogInputTypeGroupIHistStats              = (1 << 4);     ///< iHist Stats
    static const DefogInputTypeGroup DefogInputTypeGroupBHistStats              = (1 << 5);     ///< HDR-BHist Stats (For SHDRv2)
	static const DefogInputTypeGroup DefogInputTypeGroupInterGLUT               = (1 << 6);     ///< BPS/IFE Gamma
	static const DefogInputTypeGroup DefogInputTypeGroupDS4                     = (1 << 7);     ///< DS4
	static const DefogInputTypeGroup DefogInputTypeGroupSHDR                    = (1 << 8);     ///< SHDR3
	static const DefogInputTypeGroup DefogInputTypeGroupInterANR                = (1 << 9);     ///< Interpolation IPE ANR
	static const DefogInputTypeGroup DefogInputTypeGroupInterLTM                = (1 << 10);     ///< Interpolation IPE LTM
	static const DefogInputTypeGroup DefogInputTypeGroupInterGamma              = (1 << 11);     ///< Interpolation IPE Gamma
	static const DefogInputTypeGroup DefogInputTypeGroupInterCV                 = (1 << 12);    ///< Interpolation CV
	static const DefogInputTypeGroup DefogInputTypeGroupInterASF                = (1 << 13);    ///< Interpolation ASF
	static const UINT32              DefogInputTypeGroupNum                     = 14;

    /// @brief Defog algorithm
    typedef enum
    {
        DefogAlgoInvalid                                =  -1,
        DefogAlgoHQDefog                                =   0,
        DefogAlgoLPDefog                                =   1,
        DefogAlgoManualHQDefog                          =   2,
        DefogAlgoManualLPDefog                          =   3,
        DefogAlgoHQDefogManualLPDefog                   =   4,
        DefogAlgoLpDefogManualHQDefog                   =   5,
        DefogAlgoMax                                    =   6,
    } DefogAlgoMode;

    /// @brief LP algorithm decision mode
    typedef enum
    {
        LPAlgoDecisionModeInvalid                       =  -1,
        LPAlgoDecisionModeMin                           =   0,
        LPAlgoDecisionModeAvg                           =   1,
        LPAlgoDecisionModeMax                           =   2,
    } LPAlgoDecisionMode;

    /// @brief Scene type of DNR trigger for fog scene detection
    typedef enum
    {
        FogSceneDetectionDNRTriggerModeInvalid          =  -1,
        FogSceneDetectionDNRTriggerModeFlatScene        =   0,
        FogSceneDetectionDNRTriggerModeFogScene         =   1,
        FogSceneDetectionDNRTriggerModeNormalScene      =   2,
        FogSceneDetectionDNRTriggerModeMax              =   3,
    } FogSceneDetectionDNRTriggerMode;

    /// @brief Scene type of Lux trigger for fog scene detection
    typedef enum
    {
        FogSceneDetectionLuxTriggerModeInvalid          =  -1,
        FogSceneDetectionLuxTriggerModeDayLight         =   0,
        FogSceneDetectionLuxTriggerModeNormalLight      =   1,
        FogSceneDetectionLuxTriggerModeLowLight         =   2,
        FogSceneDetectionLuxTriggerModeMax              =   3,
    } FogSceneDetectionLuxTriggerMode;

    /// @brief Scene type of CCT trigger for fog scene detection
    typedef enum
    {
        FogSceneDetectionCCTTriggerModeInvalid          =  -1,
        FogSceneDetectionCCTTriggerModeLow              =   0,
        FogSceneDetectionCCTTriggerModeIndoorAndOutdoor =   1,
        FogSceneDetectionCCTTriggerModeOutdoorAndFog    =   2,
        FogSceneDetectionCCTTriggerModeHigh             =   3,
        FogSceneDetectionCCTTriggerModeMax              =   4,
    } FogSceneDetectionCCTTriggerMode;

    /// @brief Scene type of DRC gain trigger for fog scene detection
	typedef enum
	{
		FogSceneDetectionDRCTriggerModeInvalid          =  -1,
		FogSceneDetectionDRCTriggerModeLowDRCGain       =   0,
		FogSceneDetectionDRCTriggerModeHighDRCGain      =   1,
		FogSceneDetectionDRCTriggerModeMax              =   2,
	} FogSceneDetectionDRCTriggerMode;

    /// @brief Scene type of HDR ratio trigger for fog scene detection
	typedef enum
	{
		FogSceneDetectionHDRTriggerModeInvalid          =  -1,
		FogSceneDetectionHDRTriggerModeLowHDRRatio      =   0,
		FogSceneDetectionHDRTriggerModeHighHDRRatio     =   1,
		FogSceneDetectionHDRTriggerModeMax              =   2,
	} FogSceneDetectionHDRTriggerMode;

    /// @brief Scene type of gain trigger for CE scene detection
	typedef enum
	{
		CESceneDetectionGainTriggerModeInvalid          =  -1,
		CESceneDetectionGainTriggerModeLowGain          =   0,
		CESceneDetectionGainTriggerModeNormalGain       =   1,
		CESceneDetectionGainTriggerModeHighGain         =   2,
		CESceneDetectionGainTriggerModeMax              =   3,
	} CESceneDetectionGainTriggerMode;

    /// @brief Defog/CE convergence mode
	typedef enum
	{
		DefogCEConvModeInvalid                          =  -1,
		DefogCEConvModeStartUp                          =   0,
		DefogCEConvModeSerial                           =   1,
		DefogCEConvModeParallel                         =   2,
		DefogCEDefogConvModeMax                         =   3,
	} DefogCEConvMode;

    /// @brief Defog/CE convergence state
	typedef enum
	{
		DefogCEConvStateInvalid                         =  -1,
		DefogCEConvStateInactive                        =   0,
		DefogCEConvStateConverging                      =   1,
		DefogCEConvStateConverged                       =   2,
		DefogCEConvStateMax                             =   3,
	} DefogCEConvState;
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Defog/CE version information
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	struct SoftwareVersionInfoType
	{
		UINT8  majorVersion;                    ///< Major version number
		UINT8  minorVersion;                    ///< Minor version number
		UINT8  patchVersion;                    ///< Patch version number
		UINT8  newFeatureIndicator;             ///< New feature indicator: 0x01: 2AInfo, 0x02: CE, 0x04: ADRC, 0x08: SHDR3
	};



//remove this after shdr3 addition
	typedef struct {
		float gamma; // for method 1 & 2
		float gamma_region; // for method 2
		float alpha; // for method 3 & 5
		float highlight_compression_strength; // for method 4
		float boost_strength; // for method 6
		float factor;
		float max_total_gain;
		float pre_gamma;
		unsigned int method;
		float max_gain;
		float min_gain;
		float desat_th;
		float MaxVal;
		float detail_smoothing_weight_pass_full;
		float detail_smoothing_weight_pass_dc4;
		float reserved[TM_RESERVED_TABLE_SIZE];
	} shdr3_tuning_tm_t;

//remove this after shdr3 addition
	typedef struct {
		unsigned int enable_detail_enhancement;
		float luma_adapation_a;
		float luma_adapation_b;
		float luma_adapation_c;
		float regulation_fat_thin;
		float regulation_center;
	} shdr3_tuning_de_t;

    /// @brief Defog/CE handle
	typedef VOID* StatsAlgorithmHandle;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Trigger information
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	struct TriggerParams
	{
		FLOAT start;                            ///< start trigger point
		FLOAT end;                              ///< end trigger point
		UINT32 p;                               ///< probability
	};

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Fog scene detection trigger information
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	typedef struct FogSceneDetectionParams
	{
		TriggerParams dnr_trigger[3];           ///< [0]: flat_scene, [1]: fog_scene, [2]: normal_scene, range: 0.0 - 8.0 EV
		TriggerParams lux_trigger[3];           ///< [0]: daylight, [1]: normal light, [2]: low light, range: 0.0 - 1000.0 lux index
		TriggerParams cct_trigger[4];           ///< [0]: low CCT, [1]: indoor/outdoor CCT, [2]: outdoor/fog CCT, [3]: high CCT
		TriggerParams drc_trigger[2];           ///< [0]: low DRC gain, [1] high DRC gain
		TriggerParams hdr_trigger[2];           ///< [0]: low HDR ratio, [1] high HDR ratio
	} FOG_SCENE_DETECTION_PARAMS;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief CE scene detection trigger information
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	typedef struct CESceneDetectionParams
	{
		TriggerParams gain_trigger[3];          ///< [0]: low gain, [1]: normal gain, [2]: high gain, range: 1.0 - 4096.0
	} CE_SCENE_DETECTION_PARAMS;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Defog/ATES Control information
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    struct DefogConfigDefogParams
    {
        BOOL  defog_en;                                ///< 0: disable defog, 1: enable defog                                                                     (default: 1)
        INT8  strength;                                ///< [-100:100], 0: Defog strength based on tuning XML, -100: no Defog effect, 100: strongest Defog effect (default: 0)
    };

    struct DefogConfigATESParams
    {
        INT8  strength;                                ///< [-100:100], 0: ATES strength based on tuning XML, -100: no ATES effect, 100: strongest ATES effect    (default: 0) 
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief DS4 structure
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    struct DS4Data
    {
        UINT32                                                      ds4_format;
        UINT32                                                      ds4_stride;
        UINT32                                                      ds4_width;
        UINT32                                                      ds4_height;
        UINT8                                                       *buf;
        INT32                                                       fd;
        VOID*                                                       pDS4Data;
    };

	/// @briefDefog DS4 Formatt
	typedef enum DS4BufferFormat
	{
		DEFOG_DS4_FORMAT_BLOB = 0,
	} SHDRBAYERPATTERN;

    typedef struct LinearModeFsdTuningParams
    {
		BOOL   fsd_en;                                  ///< 0: disable FSD(fog scene detector), 1: enable FSD                       (default: 1)
		BOOL   fsd_2a_en;                               ///< 0: FSD without 2A info, 1: FSD with 2A info                             (default: 1)
        FOG_SCENE_DETECTION_PARAMS fsd_trig;            ///< Fog scene detection trigger information
    } LINEAR_MODE_FSD_TUNING_PARAMS;

    typedef struct LinearModeDefogTuningParams
    {
		BOOL   defog_en;                                ///< 0: disable defog, 1: enable defog                                       (default: 1)
		UINT32 speed;                                   ///< [0:10], convergence speed, 0: slowest, 10: fastest                      (default: 10)
		UINT32 convergence_mode;                        ///< 1: serial mode, 2: parallel mode                                        (default: 1)
		UINT32 algo;                                    ///< 0: Gamma Defog, 1: CV Defog, 2: Manual Gamma Defog, 3: Manual CV Defog  (default: 0)
		FLOAT  strength;                                ///< [0.0:10.0], 0.0: no defog effect, 10.0: strongest defog effect          (default: 1.0)
		UINT32 lp_algo_decision_mode;                   ///< 0: min bin decision, 1: avg bin decision, 2: max bin decision           (default: 0)
		FLOAT  lp_algo_color_comp_gain;                 ///< 0.0: disable color compensation, 1.0: 1x color compensation gain        (default: 1.0)
		BOOL   abc_en;                                  ///< 0: disable ABC(adaptive brightness compensation), 1: enable ABC         (default: 1)
		BOOL   acc_en;                                  ///< 0: disable ACC(advanced contrast control), 1: enable ACC                (default: 1)
		UINT32 dark_thres;                              ///< [1:256]  (default: 20): the two parameters are for defog reprocess. if defog status is settled and dark/bright bins are
		UINT32 bright_thres;                            ///< [1:256]  (default: 80):   within the range of defog_bright_thres and defog_dark_thres, it will keep the previous defog settings.
		UINT32 dark_limit;                              ///< [0:255]: limit max internal defog strength for dark regions             (default: 255)
		UINT32 bright_limit;                            ///< [0:255]: limit max internal defog strength for bright regions           (default: 0)
		UINT32 dark_preserve;                           ///< [0:255]: move dark bin to preserve dark details                         (default: 10)
		UINT32 bright_preserve;                         ///< [0:255]: move bright bin to preserve bright details                     (default: 50)
		FLOAT  abc_str;                                 ///< [0.0:4.0]: strength for adaptive brightness compensation                (default: 2.0)
		FLOAT  acc_dark_str;                            ///< [0.0:4.0]: ACC strength for dark region                                 (default: 2.0)
		FLOAT  acc_bright_str;                          ///< [0.0:4.0]: ACC strength for bright region                               (default: 0.5)
    } LINEAR_MODE_DEFOG_TUNING_PARAMS;

    typedef struct LinearModeCeTuningParams
    {
		BOOL    ce_en;                                  ///< 0: disable CE(contrast enahancement), 1: enable CE                      (default: 0)
		BOOL    guc_en;                                 ///< 0: disable GUC(Gain-up control), enable GUC                             (default: 1)
		BOOL    dcc_en;                                 ///< 0: disable DCC(dynamic contrast control), 1: enable DCC                 (default: 1)
		FLOAT   guc_str;                                ///< [0.0:5.0]: GUC strength on CE mode                                      (default: 1.0)
		FLOAT   dcc_dark_str;                           ///< [0.0:4.0]: DCC strength for dark region on CE mode                      (default: 1.0)
		FLOAT   dcc_bright_str;                         ///< [0.0:4.0]: DCC strength for bright region on CE mode                    (default: 1.0)
    } LINEAR_MODE_CE_TUNING_PARAMS;

    typedef CE_SCENE_DETECTION_PARAMS LINEAR_MODE_CESD_TUNING_PARAMS;

    typedef struct SHDRModeFsdTuningParams
    {
		BOOL   fsd_en;                                  ///< 0: disable FSD(fog scene detector), 1: enable FSD                       (default: 1)
		BOOL   fsd_2a_en;                               ///< 0: FSD without 2A info, 1: FSD with 2A info                             (default: 1)
        FOG_SCENE_DETECTION_PARAMS fsd_trig;            ///< Fog scene detection trigger information
    } SHDR_MODE_FSD_TUNING_PARAMS;

    typedef struct SHDRModeCommonTuningParams
    {
		UINT32 speed;                                   ///< [0:10], convergence speed, 0: slowest, 10: fastest                      (default: 10)
		FLOAT  abc_str;                                 ///< [0.0:4.0]: strength for adaptive brightness compensation                (default: 2.0)
		FLOAT  acc_dark_str;                            ///< [0.0:4.0]: ACC strength for dark region                                 (default: 2.0)
		FLOAT  acc_bright_str;                          ///< [0.0:4.0]: ACC strength for bright region                               (default: 0.5)
    } SHDR_MODE_COMMON_TUNING_PARAMS;

    typedef struct SHDRModeAtesDefogTuningParams
    {
        BOOL   enable;                                  ///< 0: disable Defog, 1: enable Defog (Obsoleted for ATES)
		FLOAT  strength;                                ///< [0.0:10.0], 0.0: no defog/ATES effect, 10.0: strongest defog/ATES effect(default: 1.0)
		UINT32 dark_thres;                              ///< [1:256]  (default: 20): the two parameters are for defog/ATES reprocess. if defog/ATES status is settled and dark/bright bins are
		UINT32 bright_thres;                            ///< [1:256]  (default: 80):   within the range of bright_thres and dark_thres, it will keep the previous defog settings.
		UINT32 dark_limit;                              ///< [0:255]: limit max internal defog strength for dark regions             (default: 255)
		UINT32 bright_limit;                            ///< [0:255]: limit max internal defog strength for bright regions           (default: 0)
		UINT32 dark_preserve;                           ///< [0:255]: move dark bin to preserve dark details                         (default: 10)
		UINT32 bright_preserve;                         ///< [0:255]: move bright bin to preserve bright details                     (default: 50)
    } SHDR_MODE_ATES_DEFOG_TUNING_PARAMS;

    typedef struct SHDRModeCeTuningParams
    {
		BOOL    ce_en;                                  ///< 0: disable CE(contrast enahancement), 1: enable CE                      (default: 0)
		BOOL    guc_en;                                 ///< 0: disable GUC(Gain-up control), enable GUC                             (default: 1)
		BOOL    dcc_en;                                 ///< 0: disable DCC(dynamic contrast control), 1: enable DCC                 (default: 1)
		FLOAT   guc_str;                                ///< [0.0:5.0]: GUC strength on CE mode                                      (default: 1.0)
		FLOAT   dcc_dark_str;                           ///< [0.0:4.0]: DCC strength for dark region on CE mode                      (default: 1.0)
		FLOAT   dcc_bright_str;                         ///< [0.0:4.0]: DCC strength for bright region on CE mode                    (default: 1.0)
    } SHDR_MODE_CE_TUNING_PARAMS;

    typedef struct SHDRModeCesdTuningParams
    {
		TriggerParams gain_trigger[3];                  ///< [0]: low gain, [1]: normal gain, [2]: high gain, range: 1.0 - 4096.0
    } SHDR_MODE_CESD_TUNING_PARAMS;

    typedef struct LinearModeTuningParams
    {
        LINEAR_MODE_DEFOG_TUNING_PARAMS     defog_params;
        LINEAR_MODE_CE_TUNING_PARAMS        ce_params;      ///< (Obsoleted)
        LINEAR_MODE_FSD_TUNING_PARAMS       fsd_params;     ///< Define DNR/LuxIdx/CCT triggers for fog scene detection
        LINEAR_MODE_CESD_TUNING_PARAMS      cesd_params;    ///< (Obsoleted) Define Gain triggers for ce scene detection
    } LINEAR_MODE_TUNING_PARAMS;

    typedef struct DefogSHDRModeTuningParams
    {
        SHDR_MODE_CE_TUNING_PARAMS          ce_params;      ///< (Obsoleted)
        SHDR_MODE_COMMON_TUNING_PARAMS      common_params;
        SHDR_MODE_ATES_DEFOG_TUNING_PARAMS  ates_params;
        SHDR_MODE_ATES_DEFOG_TUNING_PARAMS  defog_params;
        SHDR_MODE_FSD_TUNING_PARAMS         fsd_params;     ///< Define DNR/LuxIdx/CCT triggers for fog scene detection
    } SHDR_MODE_TUNING_PARAMS;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Represents ATES/Defog/CE XML parameter to the algorithm
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    typedef struct XMLTuningParams
    {
        LINEAR_MODE_TUNING_PARAMS   linear_mode;
        SHDR_MODE_TUNING_PARAMS     shdr_mode;
    } TUNING_XML_PARAMS;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Represents ATES/Defog/CE input parameter to the algorithm
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	struct DefogInputData
	{
		UINT32                                                      frameNum;                   ///< Frame number for current request
		UINT32                                                      DefogInputType;             ///< Notify input data type
		AECFrameControl*                                            AECStatsdata;               ///< AEC info
		AWBFrameControl*                                            AWBStatsdata;               ///< AWB info

        /* SHDRv3 input stats */
        ParsedIHistStatsOutput*                                     pIHistStatsOutput;          ///< IHistStats with Y/R/G/B Hist Stats, Y Hist stats will be overrided by SHDR30/35 lib on SHDR mode.
		ParsedIHistStatsOutput*                                     pLefIHistStatsOutput;       ///< IHistStats of long exposure frame(SEF) from the other IFE for SHDR38.
		ParsedIHistStatsOutput*                                     pSefIHistStatsOutput;       ///< IHistStats of short exposure frame(SEF) from the other IFE for SHDR38.

        /* SHDRv2 input stats */
        ParsedHDRBHistStatsOutput*                                  pHDRBHistStatsOutput;       ///< BPS HDR-BHistStats for SHDRv2
        ParsedHDRBHistStatsOutput*                                  pLefHDRBHistStatsOutput;    ///< SW HDR-BHistStats of long exposure frame from SHDRv2 library
        ParsedHDRBHistStatsOutput*                                  pSefHDRBHistStatsOutput;    ///< SW HDR-BHistStats of short exposure frame from SHDRv2 library

        gamma_1_6_0::mod_gamma16_channel_dataType*                  pInterpolationGamma16Data;  ///< IFE/BPS GLUT (reserved for new feature. store Gamma info for YUV sensor)
		DS4Data*                                                    pDS4Data;                   ///< DS4 frame buffer pointer
		anr_1_0_0::mod_anr10_cct_dataType::cct_dataStruct*          pInterpolationANR10Data;    ///< IPE ANR      (reserved for new feature. noise improvement)
		ltm_1_3_0::ltm13_rgn_dataType*                              pInterpolationLTM13Data;    ///< IPE LTM      (reserved for new feature. image tone improvement)
		gamma15_cct_dataType::cct_dataStruct*                       pInterpolationGamma15Data;  ///< IPE Gamma
		cv_1_2_0::cv12_rgn_dataType*                                pInterpolationCV12Data;	    ///< IPE CV
		asf_3_0_0::asf30_rgn_dataType*                              pInterpolationASF30Data;    ///< IPE ASF      (reserved for new feature. detail improvement)

		/* More AEC/AWB info */
		BOOL                                                        isAECSettled;               ///< AWC settle flag
		BOOL                                                        isAWBSettled;               ///< AWB settle flag
		FLOAT                                                       frameRate;                  ///< current fps

		/* ADRC info */
		BOOL                                                        isADRCEnabled;
		FLOAT                                                       DRCGain;
		FLOAT                                                       ltm_percentage;             ///< Percentage of current DRC gain to be applied to LTM
		ltm_1_3_0::chromatix_ltm13_reserveType*                     pLTM13ReserveData;          ///< IPE LTM reverve data, used for ADRC LTM curve calculation

		/* SHDR2.0 & 3.0 stats info */
		BOOL                                                        isSHDREnabled;              ///< SHDR enable status for both SHDR2.0 and SHDR3.0
		BOOL                                                        isYUVSHDREnabled;           ///< SHDR3 enable status (0 for SHDR2)
		BOOL                                                        isSHDRSettled;              ///< SHDR3 settle status
		FLOAT                                                       sHDRRatio;                  ///< SHDR3 ratio
		SoftwareVersionInfoType                                     sHDRVer;                    ///< SHDR3 version
		shdr3_tuning_tm_t*                                          pInterpolationTMData;       ///< SHDR3 GTM and LTM data
		shdr3_tuning_de_t*                                          pInterpolationDEData;       ///< SHDR3.0 DE   (reserved for new feature. detail enahancement on SHDR3.0 mode)
	};



    /// @brief DS4 frame buffer
    DS4Data                                                         m_DS4_buffer;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Represents Defog/CE output from the algorithm
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	struct DefogOutputData
	{
		UINT32                                                      version;                    ///< Defog Version(reserved for notification between differnt libs or apps)
		UINT32                                                      isSettled;                  ///< Defog Status
		UINT32                                                      fog_scene_probability;      ///< Fog Scene Probability
		anr_1_0_0::mod_anr10_cct_dataType::cct_dataStruct*          pProcessedANR10Data;        ///< IPE ANR      (reserved for new feature. noise improvement)
		ltm_1_3_0::ltm13_rgn_dataType*								pProcessedLTM13Data;	    ///< IPE LTM      (reserved for new feature. image tone improvement)
		gamma15_cct_dataType::cct_dataStruct*						pProcessedGamma15Data;	    ///< IPE Gamma
		cv_1_2_0::cv12_rgn_dataType*								pProcessedCV12Data;		    ///< IPE CV
		asf_3_0_0::asf30_rgn_dataType*                              pProcessedASF30Data;        ///< IPE ASF      (reserved for new feature. detail improvement)
		/* processed data to SHDR3.0 lib */
		//FLOAT                                                     shdrDarkBoost;              ///< Boost brightness on dark region (reserved for new feature. use SHDR3.0 for dark boost)
		shdr3_tuning_de_t*                                          pProcessedDEData;           ///< SHDR3.0 DE   (reserved for new feature. use SHDR3.0 for contrast enhancement)
	};

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Represents Defog/CE input and output data for the algorithm
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    struct DefogData
    {
        DefogInputData                                              defogInputData;             ///< Defog input data
        DefogOutputData                                             defogOutputData;            ///< Defog output data
    };

    CAMX_VISIBILITY_PUBLIC StatsAlgorithmHandle DefogInitialize();
    CAMX_VISIBILITY_PUBLIC UINT32 DefogReadXML(StatsAlgorithmHandle handler, TUNING_XML_PARAMS *pTuningXMLParams);
    CAMX_VISIBILITY_PUBLIC UINT32 DefogGetDefogParams(StatsAlgorithmHandle handler, DefogConfigDefogParams *pDefogConfigDefogData);
    CAMX_VISIBILITY_PUBLIC UINT32 DefogGetATESParams(StatsAlgorithmHandle handler, DefogConfigATESParams *pDefogConfigATESData);
    CAMX_VISIBILITY_PUBLIC UINT32 DefogSetDefogParams(StatsAlgorithmHandle handler, DefogConfigDefogParams *pDefogConfigDefogData);
    CAMX_VISIBILITY_PUBLIC UINT32 DefogSetATESParams(StatsAlgorithmHandle handler, DefogConfigATESParams *pDefogConfigATESData);
    CAMX_VISIBILITY_PUBLIC UINT32 DefogProcess(StatsAlgorithmHandle handler, DefogData *pDefogData);
    CAMX_VISIBILITY_PUBLIC UINT32 DefogUninitialize(StatsAlgorithmHandle handler);

#ifdef __cplusplus
}
#endif

#endif /* __DEFOG_H__ */
