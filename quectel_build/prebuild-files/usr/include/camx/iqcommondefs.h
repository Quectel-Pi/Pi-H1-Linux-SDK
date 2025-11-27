// NOWHINE NC009 <- Shared file with system team so uses non-CamX file naming
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2017-2022, 2024 Qualcomm Technologies, Inc.
// All Rights Reserved.
// Confidential and Proprietary - Qualcomm Technologies, Inc.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @file  iqcommondefs.h
/// @brief IQ Interface Input declarations
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef IQCOMMONDEFS_H
#define IQCOMMONDEFS_H

#include "camxincs.h"
#include "abf_3_4_0.h"
#include "abf_4_0_0.h"
#include "aidenoiser_1_0_0.h"
#include "aidenoiser_2_0_0.h"
#include "bpcabf_4_1_0.h"
#include "anr_1_0_0.h"
#include "asf_3_0_0.h"
#include "asf_3_2_0.h"
#include "bds_1_0.h"
#include "bincorr_1_0_0.h"
#include "bls_1_2_0.h"
#include "bpcbcc_5_0_0.h"
#include "cac_2_2_0.h"
#include "cac_2_3_0.h"
#include "cc_1_2_0.h"
#include "cc_1_3_0.h"
#include "cst_1_2_0.h"
#include "cs_2_0_0.h"
#include "cv_1_2_0.h"
#include "cvp_1_0_0.h"
#include "demosaic_3_6_0.h"
#include "demosaic_3_7_0.h"
#include "demux_1_3_0.h"
#include "ds4to1_1_0_0.h"
#include "ds4to1_1_1_0.h"
#include "dsx_1_0_0.h"
#include "hdr_2_0_0.h"
#include "hdr_3_0_0.h"
#include "ica_1_0_0.h"
#include "ica_2_0_0.h"
#include "ica_3_3_0.h"
#include "gamma_1_5_0.h"
#include "gamma_1_6_0.h"
#include "gic_3_0_0.h"
#include "gra_1_0_0.h"
#include "gtm_1_0_0.h"
#include "gtm_1_3_0.h"
#include "hdr_2_2_0.h"
#include "hdr_2_3_0.h"
#include "hdr_4_0_0.h"
#include "ica_1_0_0.h"
#include "iqsettingutil.h"
#include "ldc_1_1_0.h"
#include "hnr_1_0_0.h"
#include "linearization_3_3_0.h"
#include "linearization_3_4_0.h"
#include "lsc_3_4_0.h"
#include "lsc_3_5_0.h"
#include "lsc_4_0_0.h"
#include "ltm_1_3_0.h"
#include "ltm_1_4_0.h"
#include "ltm_1_5_0.h"
#include "ltm_1_6_0.h"
#include "nradj_1_0_0.h"
#include "nrm_1_0_0.h"
#include "pdpc_1_1_0.h"
#include "pdpc_2_0_0.h"
#include "pdpc_3_0_0.h"
#include "pedestal_1_3_0.h"
#include "qll_1_0_0.h"
#include "sce_1_1_0.h"
#include "shdr_1_1_0.h"
#include "swmfnr_2_0_0.h"
#include "swmfnr_3_0_0.h"
#include "swmctf_1_0_0.h"
#include "swmctf_2_0_0.h"
#include "tdl_1_0_0.h"
#include "tintless_2_0_0.h"
#include "tf_1_0_0.h"
#include "tf_2_0_0.h"
#include "tmc_1_0_0.h"
#include "tmc_1_1_0.h"
#include "tmc_1_2_0.h"
#include "tmc_1_3_0.h"
#include "upscale_2_0_0.h"
#include "ica_3_0_0.h"
#include "lenr_1_0_0.h"
#include "cvp_1_0_0.h"
#include "GeoLib.h"
#include "wnr_2_4_0.h"
#include "demuxblklevel_1_4_0.h"
#include "bds_1_0.h"
#include "gtm_1_2_0.h"
#include "csmce_1_1_0.h"
#include "sat_1_0_0.h"
#include "video_1_1_0.h"
#include "preference_1_0_0.h"

#include "chitintlessinterface.h"

/// @brief: This enumerator is for gamma look up tables
enum GammaLUTChannel
{
    GammaLUTChannel0 = 0, ///< G channel
    GammaLUTChannel1 = 1, ///< B channel
    GammaLUTChannel2 = 2, ///< R channel
    GammaLUTMax      = 3, ///< Max LUTs
};

/// @brief Camera Titan family Information
enum ISPVersion
{
    ISP175 = 0x00010705,    ///< Titan version
    ISP170 = 0x00010700,    ///< Titan version
    ISP160 = 0x00010600,    ///< Titan version
    ISP150 = 0x00010500,    ///< Titan version
    ISP480 = 0x00040800,    ///< Spectrum 480
    ISP540 = 0x00050400,    ///< Spectrum 540
};

enum FlashStatus
{
    NonFlash  = 0,      ///< No Flash
    PreFlash  = 1,      ///< PreFlash
    MainFlash = 2,      ///< MainFlash
};

static const UINT32 LinearizationMaxLUT              = 1;
static const UINT32 BPSLinearization34LUTEntries     = 36;    ///< 9 slopes for each of the 4 channels (R/RG/BG/B)
static const UINT32 BPSLinearization34LUTLengthDword = (BPSLinearization34LUTEntries);
static const UINT32 BPSLinearization34LUTLengthBytes = BPSLinearization34LUTLengthDword * sizeof(UINT32);

static const UINT32 NumberOfGammaEntriesPerLUT = 64;  // Number of Gamma entries per LUT

static const UINT32 GammaSizeLUTInBytes    = NumberOfGammaEntriesPerLUT * sizeof(UINT32);
static const UINT32 GammalLUTBufferSize    = GammaSizeLUTInBytes * GammaLUTMax;

static const UINT32 MaxGICNoiseLUT           = 1;
static const UINT32 BPSGIC30DMILengthDword   = 64;
static const UINT32 BPSGICNoiseLUTBufferSize = BPSGIC30DMILengthDword * sizeof(UINT32);

static const UINT32 GTM10LUT                 = 0x1;
static const UINT32 MaxGTM10LUT              = 1;
static const UINT32 BPSGTM10LUTTableSize     = 64; // DMI table size
static const UINT32 BPSGTM10DMILengthDword   = (BPSGTM10LUTTableSize * sizeof(UINT64)) / sizeof(UINT32);
static const UINT32 BPSGTM10DMILengthInBytes = (BPSGTM10DMILengthDword * sizeof(UINT32));

static const UINT32 GTM13LUT                 = 0x1;
static const UINT32 MaxGTM13LUT              = 1;
static const UINT32 IPEGTM13LUTTableSize     = 256; // DMI table size
static const UINT32 IPEGTM13DMILengthDword   = (IPEGTM13LUTTableSize * sizeof(UINT64)) / sizeof(UINT32);
static const UINT32 IPEGTM13DMILengthInBytes = (IPEGTM13DMILengthDword * sizeof(UINT32));

static const UINT32 MaxABFLUT                    = 4;
static const UINT32 BPSABF40NoiseLUTSizeDword    = 64;  // 64 entries in the DMI table
static const UINT32 BPSABF40NoiseLUTSize         = (BPSABF40NoiseLUTSizeDword * sizeof(UINT32));
static const UINT32 BPSABF40ActivityLUTSizeDword = 32;  // 32 entries in the DMI table
static const UINT32 BPSABF40ActivityLUTSize      = (BPSABF40ActivityLUTSizeDword * sizeof(UINT32));
static const UINT32 BPSABF40DarkLUTSizeDword     = 42;  // 48 entries in the DMI table
static const UINT32 BPSABF40DarkLUTSize          = (BPSABF40DarkLUTSizeDword * sizeof(UINT32));

static const UINT32 H_GRID                              = 25;
static const UINT32 S_GRID                              = 16;
static const UINT32 GammaMask                           = 0x3FF; // last 10 bits are 1s
static const UINT32 MAX_NUM_PASSES                      = 4;

static const UINT32 MAX_NR_Y_ADJUST_RANGE               = 4096; // max auto noise reduction ajust Y range
static const UINT32 MAX_NR_CBCR_ADJUST_RANGE            = 1024; // max auto noise reduction ajust CbCr range

// CC Default Saturation
static const UINT32 DefaultSaturation                   = 5;

static const UINT32 ANRMaxNonLeafNode                   = 255;

static const UINT32 TFMaxNonLeafNode                    = 255;

static const UINT32 Linearization33MaxmiumNode          = 91; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2 + 24 * 2)
static const UINT32 Linearization33MaxmiumNonLeafNode   = 43; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2)
static const UINT32 Linearization33InterpolationLevel   = 6;

static const UINT32 ABF34MaxmiumNode                    = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2);
static const UINT32 ABF34MaxmiumNonLeafNode             = 7;  // (1 + 1 * 2 + 2 * 2);
static const UINT32 ABF34InterpolationLevel             = 4;

static const UINT32 BPCABF41MaxmiumNode                 = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2);
static const UINT32 BPCABF41MaxmiumNonLeafNode          = 7;  // (1 + 1 * 2 + 2 * 2);
static const UINT32 BPCABF41InterpolationLevel          = 4;

static const UINT32 BPCBCC50MaxmiumNode                 = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2);
static const UINT32 BPCBCC50MaxmiumNonLeafNode          = 7;  // (1 + 1 * 2 + 2 * 2);
static const UINT32 BPCBCC50InterpolationLevel          = 4;

static const UINT32 CC12MaxmiumNode                     = 91; // (1 + 1 * 2 + 2 * 2 +  4 * 3 + 12 * 2 + 24 * 2)
static const UINT32 CC12MaxmiumNonLeafNode              = 43; // (1 + 1 * 2 + 2 * 2 +  4 * 3 + 12 * 2)
static const UINT32 CC12InterpolationLevel              = 6;

static const UINT32 HDR20MaxmiumNode                    = 7;  // (1 + 1 * 2 + 2 * 2);
static const UINT32 HDR20MaxmiumNoNLeafNode             = 3;  // (1 + 1 * 2);
static const UINT32 HDR20InterpolationLevel             = 3;

static const UINT32 HDR23MaxmiumNode                    = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 HDR23MaxmiumNonLeafNode             = 3;  // (1 + 1 * 2)
static const UINT32 HDR23InterpolationLevel             = 3;

static const UINT32 MAX_ADRC_LUT_KNEE_LENGTH_TMC10      = 4;
static const UINT32 MAX_ADRC_LUT_KNEE_LENGTH_TMC11      = 5;
static const UINT32 MAX_ADRC_LUT_KNEE_LENGTH_TMC12      = 5;
static const UINT32 MAX_ADRC_LUT_KNEE_LENGTH_TMC13      = 5;

static const UINT32 MaxADRCLUTKneeSize                  = 5;

static const UINT32 MAX_ADRC_LUT_COEF_SIZE              = 6;
static const UINT32 MAX_ADRC_LUT_PCHIP_COEF_SIZE        = 9;
static const UINT32 MAX_ADRC_CONTRAST_CURVE             = 1024;
static const UINT32 MAX_ADRC_HIST_BIN_NUM               = 1024;

static const UINT32 TMC13MaxInputs                      = 2;

static const UINT32 PDPC11MaxmiumNode                   = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 PDPC11MaxmiumNonLeafNode            = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 PDPC11InterpolationLevel            = 4;

static const UINT32 TDL10MaxNode                        = 91; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2 + 24 * 2)
static const UINT32 TDL10MaxNonLeafNode                 = 43; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2)
static const UINT32 TDL10InterpolationLevel             = 6;

static const UINT32 CS20MaxmiumNode                     = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 CS20MaxmiumNonLeafNode              = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 CS20InterpolationLevel              = 4;

static const UINT32 ASF30MaxNode                        = 31; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2)
static const UINT32 ASF30MaxNoLeafNode                  = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 ASF30InterpolationLevel             = 5;  // Core->TotalScaleRatio->DRCGAIN->HDRAEC->AEC

static const UINT32 ASF32MaxNode                        = 31; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2)
static const UINT32 ASF32MaxNoLeafNode                  = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 ASF32InterpolationLevel             = 5;  // Core->TotalScaleRatio->DRCGAIN->HDRAEC->AEC

static const UINT32 BLS12MaxmiumNode                    = 91; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2 + 24 * 2);
static const UINT32 BLS12MaxmiumNonLeafNode             = 43; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2);
static const UINT32 BLS12InterpolationLevel             = 6;

static const UINT32 ABF40MaxmiumNode                    = 15; // (1 + 1 * 2 +  2 * 2 + 4 * 2);
static const UINT32 ABF40MaxmiumNoLeafNode              = 7;  // (1 + 1 * 2 +  2 * 2);
static const UINT32 ABF40InterpolationLevel             = 4;

static const UINT32 GIC30MaxmiumNode                    = 7;  // (1 + 1 * 2 + 2 * 2);
static const UINT32 GIC30MaxmiumNonLeafNode             = 3;  // (1 + 1 * 2);
static const UINT32 GIC30InterpolationLevel             = 3;

static const UINT32 HDR22MaxmiumNode                    = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 HDR22MaxmiumNonLeafNode             = 3;  // (1 + 1 * 2)
static const UINT32 HDR22InterpolationLevel             = 3;

static const UINT32 HDR30MaxmiumNode                    = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 HDR30MaxmiumNonLeafNode             = 3;  // (1 + 1 * 2)
static const UINT32 HDR30InterpolationLevel             = 3;
static const UINT32 HDR40MaxmiumNode                    = 15; // (1 + 1 * 2 + 2 * 2)
static const UINT32 HDR40MaxmiumNonLeafNode             = 7;  // (1 + 1 * 2)
static const UINT32 HDR40InterpolationLevel             = 4;  // core->t1t2->t2t3->aec

static const UINT32 BC10MaxmiumNode                     = 7;  // (1 + 1 * 2 + 2 * 2);
static const UINT32 BC10MaxmiumNonLeafNode              = 3;  // (1 + 1 * 2);
static const UINT32 BC10InterpolationLevel              = 3;

static const UINT32 Linearization34MaxmiumNode          = 91; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2 + 24 * 2)
static const UINT32 Linearization34MaxmiumNonLeafNode   = 43; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2)
static const UINT32 Linearization34InterpolationLevel   = 6;

static const UINT32 PDPC20MaxNode                       = 15; // (1 + 1 * 2 + 2 * 2 +  4 * 2)
static const UINT32 PDPC20MaxNonLeafNode                = 7;  // (1 + 1 * 2 +  2 * 2)
static const UINT32 PDPC20InterpolationLevel            = 4;

static const UINT32 PDPC30MaxNode                       = 15; // (1 + 1 * 2 + 2 * 2 +  4 * 2)
static const UINT32 PDPC30MaxNonLeafNode                = 7;  // (1 + 1 * 2 +  2 * 2)
static const UINT32 PDPC30InterpolationLevel            = 4;

static const UINT32 CAC22MaxNode                        = 7;  // (1 + 1 * 2 + 2 * 2);
static const UINT32 CAC22MaxNoLeafNode                  = 3;  // (1 + 1 * 2);
static const UINT32 CAC22InterpolationLevel             = 3;  // Core->TotalScaleRatio->AEC

static const UINT32 CAC23MaxNode                        = 7;  // (1 + 1 * 2 + 2 * 2);
static const UINT32 CAC23MaxNoLeafNode                  = 3;  // (1 + 1 * 2);
static const UINT32 CAC23InterpolationLevel             = 3;  // Core->TotalScaleRatio->AEC

static const UINT32 CC13MaxNode                         = 91; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2 + 24 * 2)
static const UINT32 CC13MaxNonLeafNode                  = 43; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2)
static const UINT32 CC13InterpolationLevel              = 6;

static const UINT32 CV12MaxNode                         = 91; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2 + 24 * 2)
static const UINT32 CV12MaxNonLeafNode                  = 43; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2)
static const UINT32 CV12InterpolationLevel              = 6;  // Core->DRCGain->HDRAEC->LED->AEC->CCT

static const UINT32 Demosaic36MaximumNode               = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 Demosaic36MaximumNonLeafNode        = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 Demosaic36InterpolationLevel        = 4;

static const UINT32 Demosaic37MaximumNode               = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 Demosaic37MaximumNonLeafNode        = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 Demosaic37InterpolationLevel        = 4;

static const UINT32 DSX10MaxmiumNode                    = 3; ///< (1 + 1 * 2)
static const UINT32 DSX10MaxmiumNonLeafNode             = 1;
static const UINT32 DSX10InterpolationLevel             = 2;

static const UINT32 Gamma15MaxNode                      = 91; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2 + 24 * 2)
static const UINT32 Gamma15MaxNonLeafNode               = 43; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2)
static const UINT32 Gamma15InterpolationLevel           = 6;  // Core->DRCGain->HDRAEC->LED->AEC->CCT

static const UINT32 Gamma16MaxNode                      = 91; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2 + 24 * 2)
static const UINT32 Gamma16MaxNonLeafNode               = 43; // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2)
static const UINT32 Gamma16InterpolationLevel           = 6;  // Core->DRCGain->HDRAEC->LED->AEC->CCT

static const UINT32 GRA10MaxNode                        = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 GRA10MaxNonLeafNode                 = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 GRA10InterpolationLevel             = 4;

static const UINT32 GTM10MaxmiumNode                    = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2);
static const UINT32 GTM10MaxmiumNonLeafNode             = 7;  // (1 + 2 + 4);
static const UINT32 GTM10InterpolationLevel             = 4;

static const UINT32 GTM12MaxmiumNode                    = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2);
static const UINT32 GTM12MaxmiumNonLeafNode             = 7;  // (1 + 2 + 4);
static const UINT32 GTM12InterpolationLevel             = 4;

static const UINT32 GTM13MaxmiumNode                    = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2);
static const UINT32 GTM13MaxmiumNonLeafNode             = 7;  // (1 + 2 + 4);
static const UINT32 GTM13InterpolationLevel             = 4;

static const UINT32 HNR10MaxmiumNode                    = 31; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2);
static const UINT32 HNR10MaxmiumNonLeafNode             = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 HNR10InterpolationLevel             = 5;

static const UINT32 ICAMaxNode                          = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2);
static const UINT32 ICAMaxNoLeafNode                    = 7;  // (1 + 1 * 2 + 2 * 2);
static const UINT32 ICAInterpolationLevel               = 4;  // Core->LensPosition->LensZoom->AEC

static const UINT32 LDC11MaximumNode                    = 7;
static const UINT32 LDC11MaximumNonLeafNode             = 3;
static const UINT32 LDC11InterpolationLevel             = 3;

static const UINT32 LSC34MaxmiumNode                    = 183; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 3 + 24 * 2 + 48 * 2)
static const UINT32 LSC34MaxmiumNonLeafNode             = 87;  // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 3 + 24 * 2)
static const UINT32 LSC34InterpolationLevel             = 7;

static const UINT32 LSC35MaxmiumNode                    = 183; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 3 + 24 * 2 + 48 * 2)
static const UINT32 LSC35MaxmiumNonLeafNode             = 87;  // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 3 + 24 * 2)
static const UINT32 LSC35InterpolationLevel             = 7;

static const UINT32 LSC40MaxmiumNode                    = 183; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 3 + 24 * 2 + 48 * 2)
static const UINT32 LSC40MaxmiumNonLeafNode             = 87;  // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 3 + 24 * 2)
static const UINT32 LSC40InterpolationLevel             = 7;

static const UINT32 LTM13MaxNode                        = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 LTM13MaxNoLeafNode                  = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 LTM13InterpolationLevel             = 4;

static const UINT32 LTM14MaxNode                        = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 LTM14MaxNoLeafNode                  = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 LTM14InterpolationLevel             = 4;

static const UINT32 LTM15MaxNode                        = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 LTM15MaxNoLeafNode                  = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 LTM15InterpolationLevel             = 4;

static const UINT32 LTM16MaxNode                        = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 LTM16MaxNoLeafNode                  = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 LTM16InterpolationLevel             = 4;

static const UINT32 Pedestal13MaxmiumNode               = 63; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2 + 16 * 2)
static const UINT32 Pedestal13MaxmiumNonLeafNode        = 31; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2)
static const UINT32 Pedestal13InterpolationLevel        = 6;

static const UINT32 SCE11MaxNode                        = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 SCE11MaxNoLeafNode                  = 3;  // (1 + 1 * 2)
static const UINT32 SCE11InterpolationLevel             = 3;

static const UINT32 TF10MaxNode                         = 511; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2 + 32 + 64 + 128 + 256)
static const UINT32 TF10MaxNonLeafNode                  = 255; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2 + 16 * 2 + 32 * 2 + 64 * 2)
static const UINT32 TF10InterpolationLevel              = 9;   // Root->LensPos->LensZoom->PostScaleRatio->PreScaleRatio
                                                               // ->DRCGain->HDRAEC->AEC->CCT

static const UINT32 TF20MaxNode                         = 511; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2 + 32 + 64 + 128 + 256)
static const UINT32 TF20MaxNonLeafNode                  = 255; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2 + 16 * 2 + 32 * 2 + 64 * 2)
static const UINT32 TF20InterpolationLevel              = 9;   // Root->LensPos->LensZoom->PostScaleRatio->PreScaleRatio
                                                               // ->DRCGain->HDRAEC->AEC->CCT

static const UINT32 TMC11MaximumNode                    = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 TMC11MaximumNonLeafNode             = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 TMC11InterpolationLevel             = 4;  // Root->DRC->HDRAEC->AEC

static const UINT32 TMC12MaximumNode                    = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 TMC12MaximumNonLeafNode             = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 TMC12InterpolationLevel             = 4;  // Root->DRC->HDRAEC->AEC

static const UINT32 TMC13MaximumNode                    = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 TMC13MaximumNonLeafNode             = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 TMC13InterpolationLevel             = 4;  // Root->DRC->HDRAEC->AEC

static const UINT32 NRM10MaximumNode                    = 7;  // (1 + 1 * 2 + 2 * 2)
static const UINT32 NRM10MaximumNonLeafNode             = 3;  // (1 + 1 * 2)
static const UINT32 NRM10InterpolationLevel             = 3;  // Root->HDRAEC->AEC

static const UINT32 Upscale20MaxNode                    = 7;   // (1 + 1 * 2 + 2 * 2)
static const UINT32 Upscale20MaxNoLeafNode              = 3;   // (1 + 1 * 2)
static const UINT32 Upscale20InterpolationLevel         = 3;   // Core->TotalScaleRatio->AEC

// constants for table sizes
static const UINT32 NOISE_PARAM_YTB_SIZE   = 17;  ///< Noise parameter YTB table size of Y / CB / CR Channels
static const UINT32 NOISE_PARAM_CTB_SIZE   = 8;   ///< Noise parameter CTB table size of Y / CB / CR channels
static const UINT32 FS_TO_A1_A4_MAP_SIZE   = 9;   ///< Sparse mapping between FS values [0:8:64] and a1/a4 blending factors
static const UINT32 LNR_LUT_SIZE           = 16;  ///< LNR LUT size for Luma / Chroma channels
static const UINT32 FS_DECISION_PARAM_SIZE = 9;   ///< Multiplication factor used in Luma/Chroma thresholds calculation to
                                                  ///< form weak threshold (multiplies basic noise threshold).

static const UINT32 LENR10MaxmiumNode                   = 31;  // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2);
static const UINT32 LENR10MaxmiumNonLeafNode            = 15;  // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 LENR10InterpolationLevel            = 5;

static const UINT32 CVP10MaxmiumNode                    = 15; // (1 + 1 * 2 +  2 * 2 + 4 * 2);
static const UINT32 CVP10MaxmiumNonLeafNode             = 7;  // (1 + 1 * 2 +  2 * 2);
static const UINT32 CVP10InterpolationLevel             = 4;

static const UINT32 CVP20MaxmiumNode                    = 15; // (1 + 1 * 2 +  2 * 2 + 4 * 2);
static const UINT32 CVP20MaxmiumNonLeafNode             = 7;  // (1 + 1 * 2 +  2 * 2);
static const UINT32 CVP20InterpolationLevel             = 4;

static const UINT32 VIDEO11InterpolationLevel           = 6;
static const UINT32 VIDEO11MaximumNode                  = 63; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2 + 16 * 2)
static const UINT32 VIDEO11MaximumNonLeafNode           = 31; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 *2

static const UINT32 DSX_LUMA_PADDING_WEIGHTS   = 24;
static const UINT32 DSX_CHROMA_PADDING_WEIGHTS = 8;
static const UINT32 DSX_LUMA_KERNAL_WEIGHTS    = 192;
static const UINT32 DSX_CHROMA_KERNAL_WEIGHTS  = 96;

static const UINT32 WNR24MaximumNode                    = 127; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2 + 32 + 64)
static const UINT32 WNR24MaximumNonLeafNode             = 63;  // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2 + 32 )
static const UINT32 WNR24InterpolationLevel             = 7;   // (Root->PostScaleRatio->Prescaleratio->TotalScaleRatio->
                                                               //    DRCGain->HDRAEC->AEC)

static const UINT32 BDS10MaxmiumNode        = 31; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2);
static const UINT32 BDS10MaxmiumNonLeafNode = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2);
static const UINT32 BDS10InterpolationLevel = 5;  // (Root->Pre Scale Ratio-> DRCGain-> HDRAEC-> AEC)

static const UINT32 DemuxBlkLevel14MaxmiumNode          = 91;  // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2 + 24 * 2);
static const UINT32 DemuxBlkLevel14MaxmiumNonLeafNode   = 43;  // (1 + 1 * 2 + 2 * 2 + 4 * 3 + 12 * 2);
static const UINT32 DemuxBlkLevel14InterpolationLevel   = 6;

static const UINT32 CSMCE11MaximumNode                  = 15;  // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 CSMCE11MaximumNonLeafNode           = 7;   // (1 + 1 * 2 + 2 * 2)
static const UINT32 CSMCE11InterpolationLevel           = 4;   // Root->DRC->HDRAEC->AEC

static const UINT32 SWMFNR20MaxmiumNode                 = 31;  // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2);
static const UINT32 SWMFNR20MaxmiumNonLeafNode          = 15;  // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 SWMFNR20InterpolationLevel          = 5;   // Root->Scaleratio->DRC->AEC->GAIN

static const UINT32 SWMFNR30MaximumNode                 = 31;
static const UINT32 SWMFNR30MaximumNonLeafNode          = 15;
static const UINT32 SWMFNR30InterpolationLevel          = 5;

static const UINT32 Preference10InterpolationLevel      = 5;
static const UINT32 Preference10MaximumNode             = 31;
static const UINT32 Preference10MaximumNonLeafNode      = 15;
static const UINT32 SWMCTF10MaxmiumNode                 = 31; // (1 + 1 * 2 + 2 * 2 + 4 * 2 + 8 * 2);
static const UINT32 SWMCTF10MaxmiumNonLeafNode          = 15; // (1 + 1 * 2 + 2 * 2 + 4 * 2)
static const UINT32 SWMCTF10InterpolationLevel          = 5;// Root->Scaleratio->DRC->AEC->GAIN

static const UINT32 SWMCTF20MaximumNode                 = 31;
static const UINT32 SWMCTF20MaximumNonLeafNode          = 15;
static const UINT32 SWMCTF20InterpolationLevel          = 5;

static const UINT32 AIDENOISER10MaximumNode             = 31;
static const UINT32 AIDENOISER10MaximumNonLeafNode      = 15;
static const UINT32 AIDENOISER10InterpolationLevel      = 5;

static const UINT32 AIDENOISER20MaximumNode             = 31;
static const UINT32 AIDENOISER20MaximumNonLeafNode      = 15;
static const UINT32 AIDENOISER20InterpolationLevel      = 5;

static const UINT32 IPEICAUsecaseMCTF               = 1 << 0;
static const UINT32 IPEICAUsecaseMCTFLookahead      = 1 << 1;
static const UINT32 IPEICAUsecaseSLDC               = 1 << 2;
static const UINT32 IPEICAUsecaseSAT                = 1 << 3;
static const UINT32 IPEICAUsecaseEIS2               = 1 << 4;
static const UINT32 IPEICAUsecaseEIS3               = 1 << 5;
static const UINT32 IPEICAUsecaseMFNR               = 1 << 6;
static const UINT32 IPEICAUsecaseMFHDR2EXP          = 1 << 7;
static const UINT32 IPEICAUsecaseMFHDR3EXP          = 1 << 8;
static const UINT32 IPEICAUsecaseMFHDR2EXPSnapshot  = 1 << 9;
static const UINT32 IPEICAUsecaseMFHDR3EXPSnapshot  = 1 << 10;
static const UINT32 IPEICAUsecaseSHDR2EXP           = 1 << 11;
static const UINT32 IPEICAUsecaseOEMSETTING         = 1 << 12;

static const UINT32 ANRThresholdPerY  = 17; // Number of ANR parameter threshold per Y
static const UINT32 TFParamYtabSize   = 17; // Number of TF Noise parameter YTB table size of Y/CB/CR Channels
static const UINT32 YRatioGTMSize     = 64; // Number of GTM Y ratio
static const UINT32 IPEPassesMax      = 4;  // Number of multi pass

static const UINT32 SHDR11InterpolationLevel        = 4;
static const UINT32 SHDR11MaximumNode               = 15;
static const UINT32 SHDR11MaximumNonLeafNode        = 7;

/// @brief SWAIDenoiser Version
enum SWAIDenoiserVersion
{
    AIDenoiserInvalid, ///< AIDenoiser version invalid
    AIDenoiser10,      ///< AIDenoiser version 1.0
    AIDenoiser20,      ///< AIDenoiser version 2.0
};

/// @brief ISP TMC Version
enum SWTMCVersion
{
    TMCInvalid, ///< TMC version invalid
    TMC10,      ///< TMC version 1.0
    TMC11,      ///< TMC version 1.1
    TMC12,      ///< TMC version 1.2
    TMC13,      ///< TMC version 1.3
};

/// @brief ISP NRM Version
enum SWNRMVersion
{
    NRMInvalid, ///< NRM version invalid
    NRM10,      ///< NRM version 1.0
};

/// @brief SW MFNR Version
enum SWMFNRVersion
{
    SWMFNRInvalid, ///< SWMFNR version invalid
    SWMFNR20,      ///< SWMFNR version 2.0
    SWMFNR30,      ///< SWMFNR version 3.0
};

/// @brief SW MCTF Version
enum SWMCTFVersion
{
    SWMCTFInvalid, ///< SWMCTF version invalid
    SWMCTF10,      ///< SWCTF version 1.0
    SWMCTF20,      ///< SWCTF version 2.0
};

/// @brief SW SHDR Version
enum SWSHDRVersion
{
    SWSHDRInvalid, ///< SWSHDR version invalid
    SWSHDR10,      ///< SWSHDR version 1.0
    SWSHDR11,      ///< SWSHDR version 1.1
};

/// @brief ISP Use Case
enum UseCaseMode
{
    CONFIG_STILL = 0,     ///< Use Case STILL Image
    CONFIG_PREVIEW,       ///< Use Case Preview
    CONFIG_VIDEO,         ///< Use Case Video
};

/// @brief ISP Multi-Frame Config
enum ConfigMFMode
{
    MF_CONFIG_NONE = 0,       ///< MF Case: Config None
    MF_CONFIG_PREFILT,        ///< MF Case: Config Prefiltering
    MF_CONFIG_TEMPORAL,       ///< MF Case: Config Temporal Filtering
    MF_CONFIG_POSTPROCESS,    ///< MF Case: Config Post Processing
    MF_CONFIG_QLLPOSTFUSION,  ///< MF Case: Config QLL Post Fusion
};

/// @brief: This enumerator is for Upscale12 Vertical pixel offset
enum UpscaleVericalPixelOffset
{
    UpscaleVericalPixelOffsetPaddingTop   = 0,     ///< HW will do vertical padding at the top
    UpscaleVericalPixelOffsetNoPaddingTop,         ///< HW will not do vertical padding at the top
};

/// @brief: This enumerator is for Upscale12 Horizontal pixel offset
enum UpscaleHorizontalPixelOffset
{
    UpscaleHorizontalPixelOffsetPaddingTop = 0,     ///< HW will do horizontal padding at the left edge
    UpscaleHorizontalPixelOffsetNoPaddingTop,       ///< HW will not do horizontal padding at the left edge
};

/// @brief: This enumerator is for Upscale12 Dithering Mode
enum UpscaleInputDitheringDisable
{
    UpscaleDitheringDisable = 0,  ///< dithering is enabled,conversion happens based on dithering mode
    UpscaleDitheringEnable,       ///< dithering is disabled,truncation will happen, MSB 8bit will be sent
};

/// @brief: This enumerator is for Upscale12 Dithering mode
enum UpscaleInputDitheringMode
{
    UpscaleRegularRound     = 0, ///< Regular Rounding
    UpscaleCheckerBoardABBA,     ///< checkerboard ABBA Pattern
    UpscaleCheckerBoardBAAB,     ///< checkerboard BAAB Pattern
    UpscaleRoundHalfToEven,      ///< Round to Nearest Even
};

/// @brief: This enumerator is for Upscale12 FIR scale algorithm
enum UpscaleFirAlgoInterpolationMode
{
    UpscaleBiLinearInterpolation = 0, ///< bilinear interoplation
    UpscaleBiCubicInterpolation,      ///< bicubic interoplation
    UpscaleNearestNeighbourhood,      ///< nearest neighborhood interoplation
};

/// @brief ISP ICA Config Mode
enum ConfigICAMode
{
    ICA_CONFIG_NONE = 0,                                ///< Config None
    ICA_CONFIG_MFNR_TEMPORAL,                           ///< MFNR temporal mode with aggrgated matrix
    ICA_CONFIG_MFNR_TEMPORAL_ANCHOR_AGGREGATE,          ///< MFNR with  indivdual frame alignment
    ICA_CONFIG_MCTF,                                    ///< MCTF usecase without ICA1 activaltion
    ICA_CONFIG_MFHDR,                                   ///< MFHDR usecase
    ICA_CONFIG_POST_MFHDR,                              ///< Post MFHDR mode
    ICA_CONFIG_POST_MFHDR_MCTF,                         ///< Post MFHDR MCTF activation
};

/// @brief HDR40 two exposure combination
enum TwoExposureHDRCombination
{
    T2T3 = 0,   ///< Med/Short
    T1T3,       ///< Long/Shot
};

/// @brief Sensor PDAF Coordinates
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct PDPixelCoordinates
{
    UINT32  PDXCoordinate;  ///< PD pixel X coordinate
    UINT32  PDYCoordinate;  ///< PD pixel Y coordinate
};

/// @brief Union of Knee points for ADRC.
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
enum class GammaPipelineType
{
    IFE,    ///< IFE HW Pipeline
    BPS,    ///< BPS HW Pipeline
    IPE,    ///< IPE HW Pipeline
    OPE,    ///< OPE HW Pipeline
    TFE,    ///< TFE HW Pipeline
};

/// @ brief enumeration defining IPE path
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
enum class IPEGammaPath : UINT
{
    INPUT = 0,    ///< Input
    REFERENCE,    ///< Reference
    CVPICA,       ///< CVP ICA
    MFHDR_0,      ///< MFHDR_0
    MFHDR_1,      ///< MFHDR_1
    MFHDR_2,      ///< MFHDR_2
    MFHDR_3,      ///< MFHDR_3
};

/// @brief Union of Knee points for ADRC.
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct KneePoints
{
    FLOAT  kneeX[MaxADRCLUTKneeSize];                ///< knee X TMC13
    FLOAT  kneeY[MaxADRCLUTKneeSize];                ///< knee Y TMC13
};

/// @brief Input Data to all GTM and LTM Module for ADRC.
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct ADRCData
{
    UINT32     enable;                                              ///< global enable
    UINT32     version;                                             ///< TMC version
    UINT32     chipsetVersion;                                      ///< Titan version
    UINT32     gtmEnable;                                           ///< gtm enable
    UINT32     ltmEnable;                                           ///< ltm enable
    KneePoints kneePoints;                                          ///< knee points
    FLOAT      drcGainDark;                                         ///< drcGainDark
    FLOAT      gtmPercentage;                                       ///< GtmPercentage
    FLOAT      ltmPercentage;                                       ///< LtmPercentage

    // Valid for TMC version 1.0
    FLOAT      coefficient[MAX_ADRC_LUT_COEF_SIZE];                 ///< coefficient

    // Valid for TMC version 1.1 / 1.2
    // TMC1.1 and 1.2 support to calculate the enhance curve with input stats and two curve models.
    // In addition, TMC1.2 also supports to have its calculated output from previous frame for interpolation
    // and have more smoooth curve.
    UINT32     curveModel;                                          ///< curve model
    FLOAT      pchipCoeffficient[MAX_ADRC_LUT_PCHIP_COEF_SIZE];     ///< PCHIP curve
    FLOAT      contrastEnhanceCurve[MAX_ADRC_CONTRAST_CURVE];       ///< contrast enhance Curve
    FLOAT      contrastHEBright;                                    ///< contrast HE bright
    FLOAT      contrastHEDark;                                      ///< contrast HE Dark

    // Valid for TMC version 1.2
    FLOAT      currentCalculatedHistogram[MAX_ADRC_HIST_BIN_NUM];   ///< Output histogram of current frame
    FLOAT      currentCalculatedCDF[MAX_ADRC_HIST_BIN_NUM];         ///< Output CDF of current frame
    FLOAT      previousCalculatedHistogram[MAX_ADRC_HIST_BIN_NUM];  ///< Output histogram of previous frame
    FLOAT      previousCalculatedCDF[MAX_ADRC_HIST_BIN_NUM];        ///< Output CDF of previous frame

    // Valid for TMC version 1.3
    FLOAT      previousCalculatedFaceLevel;                         ///< Previous calculated face level
    FLOAT      currentCalculatedFaceLevel;                          ///< Output facelevel of current frame
};

/// @brief output Data to AIDenoise IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct AIDenoiserOutputData
{
    FLOAT  denoisingStrength;      ///< Denoise strength for noise reduction. Default 0.5
    FLOAT  colorSaturation;        ///< Control color saturation. Default value 0
    FLOAT  tone;                   ///< Control contrast enhancement. Default value 0.0
    FLOAT  detailEnhancement;      ///< Control details enhancement. Default value 0
};

const UINT32 GeoLibFrameConfigQueueDepth = 2;

/// @brief mask for identifying node for dumping geo lib  output
enum GeoLibMaskForNode
{
    GeoLibIFE   = 0x1,    ///< Mask for IFE
    GeoLibIPE   = 0x2,    ///< Mask for IPE
    GeoLibBPS   = 0x4,    ///< Mask for BPS
    GeoLibCVP   = 0x8,    ///< Mask for CVP
    GeoLibEIS2  = 0x10,   ///< Mask for EIS2
    GeoLibEIS3  = 0x20,   ///< Mask for EIS3
};

/// @brief data represeting realtime geolib perframe and init outputs
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct realtimeGeolibdata
{
    GeoLibVideoFlowMode         flowMode;                                       ///< geolib flow mode
    GeoLibVideoIfeStreamCfg     videoIfeStreamCfg;                              ///< geolib video stream configuration for IFE
    GeoLibVideoIpeStreamCfg     videoIpeStreamCfg;                              ///< geolib video stream configuration for IPE
    GeoLibVideoCvpStreamCfg     videoCvpStreamCfg;                              ///< geolib video stream configuration for CVP
    GeoLibVideoIfeFrameCfg      videoIfeFrameCfg;                               ///< geolib video frame configuration for IFE
    GeoLibVideoEisFrameCfg      videoEisFrameCfg;                               ///< geolib video frame configuration for EIS
    GeoLibIpeParams             ipeParams;                                      ///< ipeoutputs
    GeoLibVideoCvpFrameCfg      videoCvpFrameCfg[GeoLibFrameConfigQueueDepth];  ///< geolib video frame configuration for CVP
    GeoLibVideoIpeFrameCfg      videoIpeFrameCfg[GeoLibFrameConfigQueueDepth];  ///< geolib video frame configuration for IPE
};

/// @brief data represeting non realtime geolib perframe and init outputs
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct nonrealtimeGeolibdata
{
    GeoLibStillFrameConfig      stillFrameConfig;    ///< geo lib still frame configuration
};

/// @brief data represeting geolib perframe and init outputs  in realtime and non realtime
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct GeoLibParameters
{
    BOOL modeRealtime;         ///< real time or non realtime 1 is realtime 0 is non realtime
    BOOL srEnable;             ///< srEnable flag
    union GeolibStreamData
    {
        realtimeGeolibdata        rtGeolibdata;       ///< realtime geolib data
        nonrealtimeGeolibdata     nonRTgeolibData;    ///< non realtime geolib data
    } geoLibStreamData;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Structure for Preference config data
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct PreferenceConfigData
{
    FLOAT   bad_pixel_correction;
    FLOAT   Luma_noise_correction;
    FLOAT   chroma_noise_correction;
    FLOAT   multiframe_denoising;
    FLOAT   ghost_reduction;
    FLOAT   brightness;
    FLOAT   contrast;
    FLOAT   color;
    FLOAT   sharpness;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Structure for Preference Configurations
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct  PreferenceConfigs
{
    BOOL                   preferenceModuleEnable;
    BOOL                   preferenceDataChanged;
    PreferenceConfigData   preferenceCfgData;
};


/// @brief Input Data to all ISP IQ Module
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct ISPIQTriggerData
{
    FLOAT                       AECexposureTime;                          ///< Input trigger value:  exposure time ratio
    FLOAT                       AECexposureGainRatio;                     ///< Input trigger value:  exposure gain ratio
    FLOAT                       AECexposureGainRatioT1T2;                 ///< Input trigger value:  L/M exposure gain ratio
    FLOAT                       AECexposureGainRatioT2T3;                 ///< Input trigger value:  M/S exposure gain ratio
    FLOAT                       AECexposureGainRatioT1T3;                 ///< Input trigger value:  L/S exposure gain ratio
    FLOAT                       AECexposureTimeRatioT1T2;                 ///< Input trigger value:  L/M exposure time ratio
    FLOAT                       AECexposureTimeRatioT2T3;                 ///< Input trigger value:  M/S exposure time ratio
    FLOAT                       AECexposureTimeRatioT1T3;                 ///< Input trigger value:  L/S exposure time ratio
    FLOAT                       AECexposureSensitivityRatioT1T2;          ///< Input trigger value:  L/M sensitivity ratio
    FLOAT                       AECexposureSensitivityRatioT2T3;          ///< Input trigger value:  M/S sensitivity ratio
    FLOAT                       AECexposureSensitivityRatioT1T3;          ///< Input trigger value:  L/S sensitivity ratio
    FLOAT                       AECSensitivity;                           ///< Input trigger value:  AEC sensitivity data
    FLOAT                       AECGain;                                  ///< Input trigger value:  AEC Gain Value
    FLOAT                       AECShortGain;                             ///< Input trigger value:  AEC Short Gain Value
    FLOAT                       AECLuxIndex;                              ///< Input trigger value:  Lux index
    FLOAT                       AWBleftGGainWB;                           ///< Input trigger value:  AWB G channel gain
    FLOAT                       AWBleftBGainWB;                           ///< Input trigger value:  AWB B channel gain
    FLOAT                       AWBleftRGainWB;                           ///< Input trigger value:  AWB R channel gain
    FLOAT                       AWBColorTemperature;                      ///< Input Trigger value:  AWB CCT value
    FLOAT                       AWBSAOuptutConf[AWBAlgoSAOutputConfSize]; ///< Input Trigger value:  AWB SA output confidence
    FLOAT                       DRCGain;                                  ///< Input trigger value:  DRCGain
    FLOAT                       DRCGainDark;                              ///< Input trigger value:  DRCGainDark
    FLOAT                       lensPosition;                             ///< Input trigger value:  Lens position
    FLOAT                       lensZoom;                                 ///< Input trigger value:  Lens Zoom
    FLOAT                       postScaleRatio;                           ///< Input trigger value:  Post scale ratio
    FLOAT                       preScaleRatio;                            ///< Input trigger value:  Pre scale ratio
    FLOAT                       dsxSRScaleRatio;                          ///< Input trigger value:  DSX Super Resolution
    FLOAT                       totalScaleRatio;                          ///< Input trigger value:  Total scale ratio
    FLOAT                       AECPrevLuxIndex;                          ///< Lux index of previous frame
    FLOAT                       LEDSensitivity;                           ///< LED Sensitivity
    FLOAT                       LEDFirstEntryRatio;                       ///< Dual LED First Entry Ratio
    FLOAT                       AECYHistStretchClampOffset;               ///< Y Hist Clamp Offset
    FLOAT                       AECYHistStretchScaleFactor;               ///< Y Hist Scale Factor
    FLOAT                       overrideDarkBoostOffset;                  ///< Override dark boost offset for user control
    FLOAT                       overrideFourthToneAnchor;                 ///< Override fourth tone anchor for user control
    FLOAT                       previousCalculatedFaceLevel;              ///< Previous calculated face level for TMC13
    FLOAT                       predictiveGain;                           ///< digital gain from predictive convergence
                                                                          ///  algorithm, to be applied to ISP module (AWB)
    UINT8                       bayerPattern;                             ///< Bayer pattern
    UINT32                      sensorImageWidth;                         ///< Current Sensor Image Output Width
    UINT32                      sensorImageHeight;                        ///< Current Sensor Image Output Height
    UINT32                      CAMIFWidth;                               ///< Width of CAMIF Output
    UINT32                      CAMIFHeight;                              ///< Height of CAMIF Output
    UINT32                      sensorFullResolutionWidth;                ///< Sensor active array width
    UINT32                      sensorFullResolutionHeight;               ///< Sensor active array height
    UINT32                      fullInputWidth;                           ///< full input width, HNR input stream width
    UINT32                      fullInputHeight;                          ///< full input height, HNR input stream height
    UINT32                      ds4InputWidth;                            ///< LENR input stream width
    UINT32                      ds4InputHeight;                           ///< LENR input stream height
    UINT16                      numberOfLED;                              ///< Number of LED
    UINT32                      sensorOffsetX;                            ///< Current Sensor Image Output horizontal offset
    UINT32                      sensorOffsetY;                            ///< Current Sensor Image Output vertical offset
    UINT32                      blackLevelOffset;                         ///< Black level offset
    UINT32                      maxPipelineDelay;                         ///< Maximum pipeline delay
    UINT32                      MFHDRNumExposures;                        ///< Input number of exposures
    BOOL                        zzHDRModeEnable;                          ///< Indicates if BPS HDR is enabled
    BOOL                        enableAECYHistStretching;                 ///< Enable/Disable AEC Y Histogram Stretching
    SWTMCVersion                enabledTMCversion;                        ///< Indicates which version of TMC is enabled.
                                                                          ///  Default is TMC10
    SWNRMVersion                enabledNRMversion;                        ///< Indicates which version of NRM is enabled.
                                                                          ///  Default is NRM10
    VOID*                       pOEMTrigger[1];                           ///< OEM Trigger Pointer Array, Place Holder
    VOID*                       pLibInitialData;                          ///< Customtized Library initial data block
    ParsedBHistStatsOutput*     pParsedBHISTStats;                        ///< Pointer to parsed BHIST stats
    ParsedHDRBHistStatsOutput*  pParsedHDRBHISTStats;                     ///< Pointer to parsed HDR BHIST stats
    ParsedHDRBEStatsOutput*     pParsedHDRBEStats;                        ///< Pointer to parsed HDR BE stats
    ADRCData*                   pADRCData;                                ///< Pointer to ADRC data
    NRMConfigs*                 pNRMConfigs;                              ///< NRM (noise reduction module) configurations
    AIDenoiserOutputData*       pAIDenoiseData;                           ///< Pointer to AIDenoise data
    FLOAT*                      pPreviousCalculatedHistogram;             ///< Pointer to previous calculated histogram
                                                                          ///  for TMC12
    FLOAT*                      pPreviousCalculatedCDF;                   ///< Pointer to previous calculated CDF for TMC12
    FLOAT                       shortBlendRatio;                          ///< Short Blend Ratio
    PreferenceConfigs*          pPrefConfigs;                             ///< Preference configurations
};

/// @ Input FD Data to IQ modules
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct FDData
{
    UINT32                           numberOfFace;                 ///< Number of faces
    UINT32                           faceCenterX[MAX_FACE_NUM];    ///< Face cordinates X
    UINT32                           faceCenterY[MAX_FACE_NUM];    ///< Face coordinates Y
    UINT32                           faceRadius[MAX_FACE_NUM];     ///< Face Radius
};

// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct ICANcLibOutputData
{
    UINT32 ICARegSize;          ///< ICA register size for NClib
    UINT32 ICAChromatixSize;    ///< ICA chromatix size for nclib
};

// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct ANRNcLibOutputData
{
    UINT32 ANR10ChromatixSize;    ///< ANR chromatix size for nclib
};

// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct TFNcLibOutputData
{
    UINT32 TFChromatixSize;    ///< TF chromatix size for nclib
};

// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct DSXNcLibOutputData
{
    UINT32 DSX10OutputDataSize;  ///< DSX Output data size
    UINT32 DSX10ChromatixSize;   ///< Dsx NC lib chromatix size
};

struct _ImageDimension;
struct _WindowRegion;

/// @brief Structure to hold generic data buffer information
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct ISPIQTuningDataBuffer
{
    SIZE_T  size;       ///< Size of the buffer
    VOID*   pBuffer;    ///< Buffer pointer
};

/// @brief Input Data to AIDENOISER10 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct AIDENOISER10InputData
{
    aidenoiser_1_0_0::chromatix_aidenoiser10Type*   pChromatix;         ///< Input Chromatix tunning data
    BOOL                                            moduleEnable;       ///< Module enable flag
    FLOAT                                           totalScaleRatio;    ///< Input trigger value:  totalScaleRatio
    FLOAT                                           exposureTime;       ///< Input trigger value:  exposureTime
    FLOAT                                           AECSensitivity;     ///< Input trigger value:  AECSensitivity
    FLOAT                                           exposureGainRatio;  ///< Input trigger value:  exposureGainRatio
    FLOAT                                           luxIndex;           ///< Input trigger value:  luxIndex
    FLOAT                                           AECGain;            ///< Input trigger value:  AECGain
    FLOAT                                           DRCGain;            ///< Input trigger value:  DRCGain
};

/// @brief Input Data to AIDENOISER20 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct AIDENOISER20InputData
{
    aidenoiser_2_0_0::chromatix_aidenoiser20Type*   pChromatix;         ///< Input Chromatix tunning data
    BOOL                                            moduleEnable;       ///< Module enable flag
    FLOAT                                           totalScaleRatio;    ///< Input trigger value:  totalScaleRatio
    FLOAT                                           exposureTime;       ///< Input trigger value:  exposureTime
    FLOAT                                           AECSensitivity;     ///< Input trigger value:  AECSensitivity
    FLOAT                                           exposureGainRatio;  ///< Input trigger value:  exposureGainRatio
    FLOAT                                           luxIndex;           ///< Input trigger value:  luxIndex
    FLOAT                                           AECGain;            ///< Input trigger value:  AECGain
    FLOAT                                           DRCGain;            ///< Input trigger value:  DRCGain
};

/// @brief This structures encapsulate noise adjustment configure type
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct NRAdjustConfigureType
{
    BOOL   anr_adj_enable;              ///< Enable flag for ANR adjustment
    BOOL   hnr_adj_enable;              ///< Enable flag for HNR adjustment
    BOOL   tf_adj_enable;               ///< Enable flag for TF adjustment
};

/// @brief This structures encapsulate noise adjustment for ANR
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct NRAdjust_anr
{
    FLOAT luma_y_threshold_per_y[ANRThresholdPerY];   ///< luma_y_threshold_per_y array used by IQ calculation
    FLOAT luma_u_threshold_per_y[ANRThresholdPerY];   ///< luma_u_threshold_per_y array used by IQ calculation
    FLOAT luma_v_threshold_per_y[ANRThresholdPerY];   ///< luma_v_threshold_per_y array used by IQ calculation
    FLOAT chroma_y_threshold_per_y[ANRThresholdPerY]; ///< chroma_y_threshold_per_y array used by IQ calculation
    FLOAT chroma_u_threshold_per_y[ANRThresholdPerY]; ///< chroma_u_threshold_per_y array used by IQ calculation
    FLOAT chroma_v_threshold_per_y[ANRThresholdPerY]; ///< chroma_v_threshold_per_y array used by IQ calculation
};

/// @brief This structures encapsulate noise adjustment for TF
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct NRAdjust_tf
{
    FLOAT noise_params_y_ytb[TFParamYtabSize];        ///< noise_params_y_ytb array used by IQ calculation
    FLOAT noise_params_cb_ytb[TFParamYtabSize];       ///< noise_params_cb_ytb array used by IQ calculation
    FLOAT noise_params_cr_ytb[TFParamYtabSize];       ///< noise_params_cr_ytb array used by IQ calculation
};

/// @brief This structures encapsulate ANR noise adjustment data array
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct NRAdjust_ANRData
{
    NRAdjust_anr anr_adj_data[IPEPassesMax];          ///< ANR adjustment data
};

/// @brief This structures encapsulate TF noise adjustment data array
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct NRAdjust_TFData
{
    NRAdjust_tf tf_adj_data[IPEPassesMax];            ///< TF adjustment data
};

/// @brief This structures encapsulate IFE /BPS GTM Info output
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct GTMInfo
{
    UINT64   yRatioGtm[YRatioGTMSize];      ///< yRatioGtm array data used by IQ calculation
    BOOL     enabledIFEGTMAdjust;           ///< Enable flag for noise adjustment based on IFE GTM
    BOOL     enabledBPSGTMAdjust;           ///< Enable flag for noise adjustment based on BPS GTM
};

/// @brief Input Data to BPCBCC50 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct BPCBCC50InputData
{
    VOID*                                 pHWSetting;          ///< Pointer to the HW Setting Object
    bpcbcc_5_0_0::chromatix_bpcbcc50Type* pChromatix;          ///< Input Chromatix tunning data
    BOOL                                  moduleEnable;        ///< Module enable flag
    BOOL                                  symbolIDChanged;     ///< Symbol IOD changed, reload triggers
    UINT32                                blackLevelOffset;    ///< Black Level Offset
    FLOAT                                 DRCGain;             ///< Input trigger value:  DRCGain
    FLOAT                                 exposureTime;        ///< Input trigger value:  exposure time
    FLOAT                                 exposureGainRatio;   ///< Input trigger value:  exposure gain ratio
    FLOAT                                 AECSensitivity;      ///< Input trigger value:  AEC sensitivity data
    FLOAT                                 luxIndex;            ///< Input trigger value:  Lux index
    FLOAT                                 realGain;            ///< Input trigger value:  Real Gain Value
    FLOAT                                 digitalGain;         ///< Input trigger value:  Digital Gain Value
    FLOAT                                 leftGGainWB;         ///< G channel gain to adjust white balance
    FLOAT                                 leftBGainWB;         ///< B channel gain to adjust white balance
    FLOAT                                 leftRGainWB;         ///< R channel gain to adjust white balance
    FLOAT                                 nonHdrMultFactor;    ///< Non HDR MultFactor
    VOID*                                 pInterpolationData;  ///< input memory for interpolation data
    NRMConfigData                         nrmCfgData;          ///< NRM config data
    FLOAT                                 bad_pixel_correction;///< Slider Based Tuning
};

/// @brief Input Data to CC12 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct CC12InputData
{
    VOID*                         pHWSetting;         ///< Pointer to the HW Setting Object
    cc_1_2_0::chromatix_cc12Type* pChromatix;         ///< Chromatix data for CC12 module
    FLOAT                         DRCGain;            ///< Input trigger value:  DRCGain
    FLOAT                         exposureTime;       ///< Input trigger value:  exposure time
    FLOAT                         exposureGainRatio;  ///< Input trigger value:  exposure gain ratio
    FLOAT                         AECSensitivity;     ///< Input trigger value:  AEC sensitivity data
    FLOAT                         luxIndex;           ///< Input trigger value:  Lux index
    FLOAT                         AECGain;            ///< Input trigger value:  Aec Gain Value
    FLOAT                         colorTemperature;   ///< Input trigger value:  AWB colorTemperature
    FLOAT                         LEDTrigger;         ///< Input trigger value:  LED Index
    FLOAT                         LEDFirstEntryRatio; ///< Ratio of Dual LED
    UINT16                        numberOfLED;        ///< Number of LED
    VOID*                         pInterpolationData; ///< input memory for interpolation data
};

/// @brief CCM Manual Setting Data
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct ManualSetting
{
    BOOL                          manualCCMEnable;        ///< Enable Manual Control CCM
    BOOL                          manualAWBCCMEnable;     ///< Enable AWB CCM
    FLOAT*                        pManualCCM;             ///< Manual CCM Buffer
    UINT                          arraySize;              ///< Used to convert ManualCCM from 2D Array to 1D Array
    AWBCCMParams*                 pAWBCCM;                ///< AWB CCM Buffer
    cc_1_3_0::chromatix_cc13Type* pChromatix;             ///< Chromatix pointer to handle the q factor
};

/// @brief Input Data to CC13 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct CC13InputData
{
    VOID*                         pHWSetting;           ///< Pointer to the HW Setting Object
    cc_1_3_0::chromatix_cc13Type* pChromatix;           ///< Chromatix data for CC12 module
    FLOAT                         DRCGain;              ///< Input trigger value:  drcGain
    FLOAT                         exposureTime;         ///< Input trigger value:  exposure time
    FLOAT                         exposureGainRatio;    ///< Input trigger value:  exposure gain ratio
    FLOAT                         AECSensitivity;       ///< Input trigger value:  AEC sensitivity data
    FLOAT                         luxIndex;             ///< Input trigger value:  Lux index
    FLOAT                         AECGain;              ///< Input trigger value:  AEC Gain Value
    FLOAT                         CCTTrigger;           ///< Input trigger value:  AWB colorTemperature
    FLOAT                         LEDTrigger;           ///< Input trigger value:  LED Index
    FLOAT                         LEDFirstEntryRatio;   ///< Ratio of Dual LED
    UINT16                        numberOfLED;          ///< Number of LED
    FLOAT                         effectsMatrix[3][3];  ///< Matrix to hold effects values
    INT32                         saturation;           ///< Saturation effect value
    VOID*                         pInterpolationData;   ///< input memory for interpolation data
};

/// @brief Input Data to CST12 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct CST12InputData
{
    VOID*                           pHWSetting;         ///< Pointer to the HW Setting Object
    cst_1_2_0::chromatix_cst12Type* pChromatix;         ///< Input Chromatix tunning data
    UINT32                          chipsetVersion;     ///< Titan version
};

/// @brief Input Data to Linearization33 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct Linearization33InputData
{
    VOID*                                                pHWSetting;         ///< Pointer to the HW Setting Object
    linearization_3_3_0::chromatix_linearization33Type*  pChromatix;         ///< Input Chromatix tunning data
    BOOL                                                 symbolIDChanged;    ///< Symbol IOD changed, reload triggers
    FLOAT                                                LEDTrigger;         ///< LED Value
    FLOAT                                                LEDFirstEntryRatio; ///< Ratio of Dual LED
    UINT16                                               numberOfLED;        ///< Number of LED
    FLOAT                                                CCTTrigger;         ///< CCT Value
    FLOAT                                                DRCGain;            ///< DRCGain
    FLOAT                                                exposureTime;       ///< exposure time
    FLOAT                                                exposureGainRatio;  ///< Input trigger value:  exposure gain ratio
    FLOAT                                                AECSensitivity;     ///< AEC sensitivity data
    FLOAT                                                luxIndex;           ///< Lux index
    FLOAT                                                realGain;           ///< Linear Gain Value
    UINT16                                               lutBankSel;         ///< LUT bank need for this frame.
    BOOL                                                 pedestalEnable;     ///< pedestal is enabled or not
    UINT8                                                bayerPattern;       ///< Bayer Pattern
    VOID*                                                pInterpolationData; ///< input memory for interpolation data
};

/// @brief Input Data to BLS12 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct BLS12InputData
{
    VOID*                           pHWSetting;         ///< Pointer to the HW Setting Object
    bls_1_2_0::chromatix_bls12Type* pChromatix;         ///< Input Chromatix tunning data
    BOOL                            symbolIDChanged;    ///< Symbol IOD changed, reload triggers
    FLOAT                           DRCGain;            ///< Input trigger value:  DRCGain
    FLOAT                           exposureTime;       ///< Input trigger value:  exposure time
    FLOAT                           exposureGainRatio;  ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;     ///< Input trigger value:  AEC sensitivity data
    FLOAT                           realGain;           ///< Input trigger value:  Digital Gain Value
    FLOAT                           LEDTrigger;         ///< Input trigger value:  Led Index
    FLOAT                           LEDFirstEntryRatio; ///< Ratio of Dual LED
    UINT16                          numberOfLED;        ///< Number of LED
    FLOAT                           CCTTrigger;         ///< Input trigger value:  CCT
    FLOAT                           digitalGain;        ///< Digital gain from Sensor
    FLOAT                           luxIndex;           ///< Input trigger value:  Lux index
    UINT8                           bayerPattern;       ///< Bayer Pattern
    UINT16                          blackResOffset;     ///< Black Residual Offset
    VOID*                           pInterpolationData; ///< input memory for interpolation data
    UINT32                          chipsetVersion;     ///< Titan version
};

/// @brief LSC Calibration Table from EEPROM
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct LSC34CalibrationDataTable
{
    FLOAT* pRGain;    ///< R gain table
    FLOAT* pGRGain;   ///< GR gain table
    FLOAT* pGBGain;   ///< GB gain table
    FLOAT* pBGain;    ///< B gain table
};

/// @brief LSC stripe output
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct LSC34Stripe
{
    UINT32   lx_start;         ///< Start block x index, 6u
    UINT32   bx_start;         ///< Start subgrid x index within start block, 3u
    UINT32   bx_d1;            ///< x coordinate of top left pixel in start block/subgrid, 9u
};

/// @brief LSC Tintles ratio
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct LSC34TintlessRatio
{
    UINT32 isValid;   ///< Indicates if contents are valid
    FLOAT* pRGain;    ///< rgain tintless ratio
    FLOAT* pGrGain;   ///< grgain tintless ratio
    FLOAT* pGbGain;   ///< gbgain tintless ratio
    FLOAT* pBGain;    ///< bgain tintless ratio
};

/// @brief Input Data to LSC34 IQ Algorithem
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct LSC34InputData
{
    VOID*                                     pHWSetting;           ///< Pointer to the HW Setting Object
    lsc_3_4_0::chromatix_lsc34Type*           pChromatix;           ///< Input Chromatix tunning data
    BOOL                                      symbolIDChanged;      ///< Symbol IOD changed, reload triggers
    UINT32                                    fullResWidth;         ///< Current Sensor Image Max Output Width
    UINT32                                    fullResHeight;        ///< Current Sensor Image Max Output Height
    UINT32                                    imageWidth;           ///< Current Sensor Image Output Width
    UINT32                                    imageHeight;          ///< Current Sensor Image Output Height
    UINT32                                    offsetX;              ///< Current Sensor Image Output Width
    UINT32                                    offsetY;              ///< Current Sensor Image Output Height
    UINT32                                    scalingFactor;        ///< Scaling factor from sensor
    FLOAT                                     DRCGain;              ///< Input trigger value:  DRCGain
    FLOAT                                     exposureTime;         ///< Input trigger value:  exposure time
    FLOAT                                     exposureGainRatio;    ///< Input trigger value:  exposure gain ratio
    FLOAT                                     AECSensitivity;       ///< Input trigger value:  AEC sensitivity data
    FLOAT                                     luxIndex;             ///< Input trigger value:  Lux index
    FLOAT                                     realGain;             ///< Input trigger value:  real Gain Value
    FLOAT                                     LEDTrigger;           ///< Input trigger value:  LED Value
    FLOAT                                     LEDFirstEntryRatio;   ///< Ratio of Dual LED
    UINT16                                    numberOfLED;          ///< Number of LED
    FLOAT                                     lensPosTrigger;       ///< Input trigger value:  LensPosition Value
    FLOAT                                     CCTTrigger;           ///< Input trigger value:  CCT Value
    FLOAT                                     leftGGainWB;          ///< G channel gain to adjust white balance
    FLOAT                                     leftBGainWB;          ///< B channel gain to adjust white balance
    FLOAT                                     leftRGainWB;          ///< R channel gain to adjust white balance
    UINT16                                    bankSelect;           ///< Bank select for LUT table
    UINT8                                     numTables;            ///< Number of calibration tables
    LSC34CalibrationDataTable*                pCalibrationTable;    ///< pointer to the Calibration tables
    UINT32                                    numberOfEEPROMTable;  ///< number of tables in EEPROM
    BOOL                                      toCalibrate;          ///< Indicates we need to calibrate
    LSC34Stripe                               stripeOut;            ///< LSC changed values from striping lib
    BOOL                                      fetchSettingOnly;     ///< Flag to indicate to fetch settings for striping lib
    BOOL                                      enableCalibration;    ///< Indicates if calibration should be enabled or not
    tintless_2_0_0::chromatix_tintless20Type* pTintlessChromatix;   ///< Pointer to tintless Chromatix
    TintlessConfig*                           pTintlessConfig;      ///< Pointer to the Tintless Algo Configurations
    ParsedTintlessBGStatsOutput*              pTintlessStats;       ///< Pointer to Parsed Tintless data
    CHITintlessAlgorithm*                     pTintlessAlgo;        ///< Pointer to the Tintless Algo resource
    VOID*                                     pInterpolationData;   ///< input memory for interpolation data
    BOOL                                      enableTintless;       ///< Indicates if tintless algo
    LSC34TintlessRatio*                       pTintlessRatio;       ///< Store tintless ratio, this will be used if tinless
                                                                    ///  algo is skipped
    UINT8                                      flashStatus;         ///< To indicate flash OFF, PreFlash, Flash status
    UINT32                                     cameraID;            ///< Sensor ID Number
    UINT32                                     sensorAR;            ///< for 4:3 senosr mode is FULLMODE(0)
                                                                    ///  other mode NON_FULLMODE(1)
};

// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct LSC35CalibrationDataTable
{
    FLOAT* pRGain;    ///< R gain table
    FLOAT* pGRGain;   ///< GR gain table
    FLOAT* pGBGain;   ///< GB gain table
    FLOAT* pBGain;    ///< B gain table
};

/// @brief LSC stripe output
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct LSC35Stripe
{
    UINT32   lx_start_l;         ///< Start block x index, 6u
    UINT32   bx_start_l;         ///< Start subgrid x index within start block, 3u
    UINT32   bx_d1_l;            ///< x coordinate of top left pixel in start block/subgrid, 9u
};


typedef LSC34TintlessRatio LSC35TintlessRatio;
/// @brief Input Data to LSC35 IQ Algorithem
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct LSC35InputData
{
    VOID*                                     pHWSetting;           ///< Pointer to the HW Setting Object
    lsc_3_5_0::chromatix_lsc35Type*           pChromatix;           ///< Input Chromatix tunning data
    BOOL                                      symbolIDChanged;      ///< Symbol IOD changed, reload triggers
    UINT32                                    fullResWidth;         ///< Current Sensor Image Max Output Width
    UINT32                                    fullResHeight;        ///< Current Sensor Image Max Output Height
    UINT32                                    imageWidth;           ///< Current Sensor Image Output Width
    UINT32                                    imageHeight;          ///< Current Sensor Image Output Height
    UINT32                                    offsetX;              ///< Current Sensor Image Output Width
    UINT32                                    offsetY;              ///< Current Sensor Image Output Height
    UINT32                                    scalingFactor;        ///< Scaling factor from sensor
    FLOAT                                     DRCGain;              ///< Input trigger value:  DRCGain
    FLOAT                                     exposureTime;         ///< Input trigger value:  exposure time
    FLOAT                                     exposureGainRatio;         ///< Input trigger value:  exposure Gain Ratio
    FLOAT                                     AECSensitivity;       ///< Input trigger value:  AEC sensitivity data
    FLOAT                                     luxIndex;             ///< Input trigger value:  Lux index
    FLOAT                                     realGain;             ///< Input trigger value:  real Gain Value
    FLOAT                                     LEDTrigger;           ///< Input trigger value:  LED Value
    FLOAT                                     LEDFirstEntryRatio;   ///< Ratio of Dual LED
    UINT16                                    numberOfLED;          ///< Number of LED
    FLOAT                                     lensPosTrigger;       ///< Input trigger value:  LensPosition Value
    FLOAT                                     CCTTrigger;           ///< Input trigger value:  CCT Value
    FLOAT                                     leftGGainWB;          ///< G channel gain to adjust white balance
    FLOAT                                     leftBGainWB;          ///< B channel gain to adjust white balance
    FLOAT                                     leftRGainWB;          ///< R channel gain to adjust white balance
    UINT16                                    bankSelect;           ///< Bank select for LUT table
    UINT8                                     numTables;            ///< Number of calibration tables
    LSC35CalibrationDataTable*                pCalibrationTable;    ///< pointer to the Calibration tables
    UINT32                                    numberOfEEPROMTable;  ///< number of tables in EEPROM
    BOOL                                      toCalibrate;          ///< Indicates we need to calibrate
    LSC35Stripe                               stripeOut;            ///< LSC changed values from striping lib
    BOOL                                      fetchSettingOnly;     ///< Flag to indicate to fetch settings for striping lib
    BOOL                                      enableCalibration;    ///< Indicates if calibration should be enabled or not
    tintless_2_0_0::chromatix_tintless20Type* pTintlessChromatix;   ///< Pointer to tintless Chromatix
    TintlessConfig*                           pTintlessConfig;      ///< Pointer to the Tintless Algo Configurations
    ParsedTintlessBGStatsOutput*              pTintlessStats;       ///< Pointer to Parsed Tintless data
    CHITintlessAlgorithm*                     pTintlessAlgo;        ///< Pointer to the Tintless Algo resource
    VOID*                                     pInterpolationData;   ///< input memory for interpolation data
    BOOL                                      enableTintless;       ///< Indicates if tintless algo
    LSC35TintlessRatio                        tintlessConfig;       ///< Store tintless ratio, this will be used if tinless
                                                                    ///< algo is skipped
    UINT8                                      bayerPattern;         ///< Bayer Pattern
    UINT32                                     blackLevelOffsetIn;  ///< Black level Offset to be Subracted before processing
    UINT32                                     blackLevelOffsetOut; ///< Black Level Offset to be Added after processing
    BOOL                                       enableTableScaling;  ///< To scale LSC table after tintless in case of input crop
    UINT8                                      flashStatus;         ///< To indicate flash OFF, PreFlash, Flash status
    UINT32                                     cameraID;            ///< Sensor ID Number
    UINT32                                     sensorAR;            ///< for 4:3 senosr mode is FULLMODE(0)
                                                                    ///  other mode NON_FULLMODE(1)
};

typedef LSC34TintlessRatio          LSC40TintlessRatio;
typedef LSC34CalibrationDataTable   LSC40CalibrationDataTable;
typedef LSC34Stripe                 LSC40Stripe;

/// @brief: Rollof table type
enum RollOffTables
{
    MeshTable = 0, ///< MeshTable
    GridGain,      ///< GridGain
    GridMean,      ///< GridGain
};

/// @brief Input Data to LSC40 IQ Algorithem
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct LSC40InputData
{
    VOID*                                     pHWSetting;           ///< Pointer to the HW Setting Object
    lsc_4_0_0::chromatix_lsc40Type*           pChromatix;           ///< Input Chromatix tunning data
    BOOL                                      symbolIDChanged;      ///< Symbol IOD changed, reload triggers
    UINT32                                    fullResWidth;         ///< Current Sensor Image Max Output Width
    UINT32                                    fullResHeight;        ///< Current Sensor Image Max Output Height
    UINT32                                    imageWidth;           ///< Current Sensor Image Output Width
    UINT32                                    imageHeight;          ///< Current Sensor Image Output Height
    UINT32                                    offsetX;              ///< Current Sensor Image Output Width
    UINT32                                    offsetY;              ///< Current Sensor Image Output Height
    UINT32                                    scalingFactor;        ///< Scaling factor from sensor
    FLOAT                                     DRCGain;              ///< Input trigger value:  DRCGain
    FLOAT                                     exposureTime;         ///< Input trigger value:  exposure time
    FLOAT                                     exposureGainRatio;    ///< Input trigger value:  exposure gain ratio
    FLOAT                                     AECSensitivity;       ///< Input trigger value:  AEC sensitivity data
    FLOAT                                     luxIndex;             ///< Input trigger value:  Lux index
    FLOAT                                     realGain;             ///< Input trigger value:  real Gain Value
    FLOAT                                     LEDTrigger;           ///< Input trigger value:  LED Value
    FLOAT                                     LEDFirstEntryRatio;   ///< Ratio of Dual LED
    UINT16                                    numberOfLED;          ///< Number of LED
    FLOAT                                     lensPosTrigger;       ///< Input trigger value:  LensPosition Value
    FLOAT                                     CCTTrigger;           ///< Input trigger value:  CCT Value
    UINT16                                    bankSelect;           ///< Bank select for LUT table
    UINT8                                     numTables;            ///< Number of calibration tables
    LSC40CalibrationDataTable*                pCalibrationTable;    ///< pointer to the Calibration tables
    UINT32                                    numberOfEEPROMTable;  ///< number of tables in EEPROM
    BOOL                                      toCalibrate;          ///< Indicates we need to calibrate
    LSC40Stripe                               stripeOut;            ///< LSC changed values from striping lib
    BOOL                                      fetchSettingOnly;     ///< Flag to indicate to fetch settings for striping lib
    BOOL                                      enableCalibration;    ///< Indicates if calibration should be enabled or not
    UINT16                                    first_pixel;          ///< From crc1.1 module
    UINT16                                    last_pixel;           ///< From crc1.1 module
    UINT16                                    first_line;           ///< From crc1.1 module
    UINT16                                    last_line;            ///< From crc1.1 module
    tintless_2_0_0::chromatix_tintless20Type* pTintlessChromatix;   ///< Pointer to tintless Chromatix
    TintlessConfig*                           pTintlessConfig;      ///< Pointer to the Tintless Algo Configurations
    ParsedTintlessBGStatsOutput*              pTintlessStats;       ///< Pointer to Parsed Tintless data
    ParsedAWBBGStatsOutput*                   pAWBBGStats;          ///< AWB BG stats output pointer
    CHITintlessAlgorithm*                     pTintlessAlgo;        ///< Pointer to the Tintless Algo resource
    CHIALSCAlgorithm*                         pALSCAlgo;            ///< ALSC algorithm pointer
    VOID*                                     pInterpolationData;   ///< input memory for interpolation data
    BOOL                                      enableTintless;       ///< Indicates if tintless algo
    LSC40TintlessRatio                        tintlessConfig;       ///< Store tintless ratio, this will be used if tinless
                                                                    ///< algo is skipped
    UINT8                                     bayerPattern;         ///< Bayer pattern
    UINT32*                                   pALSCBuffer;          ///< ALSC Scratch Buffer
    UINT32                                    ALSCBufferSize;       ///< ALSC Scratch Buffer size in DWord
    BOOL                                      enableTableScaling;   ///< MFSR/InSensor 2x zoom, to scale tintless
                                                                    ///< tables/ALSC tables
    BOOL                                      isTLBGStatsBinned;    ///< TinglessBG Stats is generated using CSID binned frame
    UINT8                                     flashStatus;          ///< To indicate flash OFF, PreFlash, Flash status
    UINT32                                    cameraID;             ///< Sensor ID Number
    UINT32                                    sensorAR;             ///< for 4:3 senosr mode is FULLMODE(0)
                                                                    ///  other mode NON_FULLMODE(1)
};

/// @brief Tintless20 Interpolation Input
// NOWHINE NC004c: Share code with system team
struct Tintless20InterpolationInput
{
    tintless_2_0_0::chromatix_tintless20Type* pTintlessChromatix;   ///< Pointer to tintless Chromatix
    FLOAT                                     exposureTime;         ///< Input trigger value:  exposure time
    FLOAT                                     exposureGainRatio;   ///< Input trigger value:  exposure gain ratio
    FLOAT                                     AECSensitivity;       ///< Input trigger value:  AEC sensitivity data
    FLOAT                                     luxIndex;             ///< Input trigger value:  Lux index
    FLOAT                                     realGain;             ///< Input trigger value:  real Gain Value
};


/// @brief Input Data to Gamma16 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct Gamma16InputData
{
    VOID*                               pHWSetting;         ///< Pointer to the HW Setting Object
    gamma_1_6_0::chromatix_gamma16Type* pChromatix;         ///< Input Chromatix tunning data
    BOOL                                symbolIDChanged;    ///< Symbol ID changed, reload triggers
    FLOAT                               DRCGain;            ///< Input trigger value:  DRCGain
    FLOAT                               exposureTime;       ///< Input trigger value:  exposure time
    FLOAT                               exposureGainRatio;  ///< Input trigger value:  exposure gain ratio
    FLOAT                               AECSensitivity;     ///< Input trigger value:  AEC sensitivity data
    FLOAT                               LEDTrigger;         ///< Input trigger value:  Led Index
    FLOAT                               LEDFirstEntryRatio; ///< Ratio of Dual LED
    FLOAT                               luxIndex;           ///< Input trigger value:  Lux index
    FLOAT                               realGain;           ///< Input trigger value:  Real Gain Value
    FLOAT                               CCTTrigger;         ///< Input trigger value:  CCT
    FLOAT                               AECGain;            ///< Input trigger value:  Digital Gain Value
    UINT8                               contrastLevel;      ///< contrast level pass down by APP
    UINT16                              LUTBankSel;         ///< LUT bank used for the frame
    UINT16                              numberOfLED;        ///< Number of LED
    VOID*                               pLibData;           ///< Pointer to the library specific data
    VOID*                               pInterpolationData; ///< input memory for interpolation data
    UINT                                MFHDRIPEPath;       ///< IPE path if MFHDR pre hdr/ post HDR gamma for inv gamma calc
};

/// @brief Input Data to ABF34 IQ Algorithem
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct ABF34InputData
{
    VOID*                            pHWSetting;          ///< Pointer to the HW Setting Object
    abf_3_4_0::chromatix_abf34Type*  pChromatix;          ///< Input Chromatix tunning data
    BOOL                             moduleEnable;        ///< Module enable flag
    FLOAT                            DRCGain;             ///< Input trigger value:  DRCGain
    FLOAT                            exposureTime;        ///< Input trigger value:  exposure time
    FLOAT                            exposureGainRatio;   ///< Input trigger value:  exposure gain ratio
    FLOAT                            AECSensitivity;      ///< Input trigger value:  AEC sensitivity data
    FLOAT                            luxIndex;            ///< Input trigger value:  Lux index
    FLOAT                            realGain;            ///< Input trigger value:  Digital Gain Value
    UINT8                            LUTBankSel;          ///< Lut bank
    UINT32                           CAMIFWidth;          ///< Input CAMIF width
    UINT32                           CAMIFHeight;         ///< Input CAMIF Height
    UINT32                           blackResidueOffset;  ///< Black Level Offset
    UINT32                           sensorOffsetX;       ///< Current Sensor Image Output horizontal offset
    UINT32                           sensorOffsetY;       ///< Current Sensor Image Output vertical offset
    VOID*                            pInterpolationData;  ///< input memory for chromatix interpolation data
    NRMConfigData                    nrmCfgData;          ///< NRM config data
    FLOAT                            luma_denoising;	  ///< Slider Based Tuning
};

/// @brief Input Data to ABF41 IQ Algorithem
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct BPCABF41InputData
{
    VOID*                                 pHWSetting;          ///< Pointer to the HW Setting Object
    bpcabf_4_1_0::chromatix_bpcabf41Type* pChromatix;          ///< Input Chromatix tunning data
    BOOL                                  moduleEnable;        ///< Module enable flag
    BOOL                                  prevStateEnable;     ///< Previous enable flag
    FLOAT                                 DRCGain;             ///< Input trigger value:  DRCGain
    FLOAT                                 exposureTime;        ///< Input trigger value:  exposure time
    FLOAT                                 AECSensitivity;      ///< Input trigger value:  AEC sensitivity data
    FLOAT                                 luxIndex;            ///< Input trigger value:  Lux index
    FLOAT                                 realGain;            ///< Input trigger value:  Digital Gain Value
    UINT8                                 LUTBankSel;          ///< Lut bank
    UINT32                                CAMIFWidth;          ///< Input CAMIF width
    UINT32                                CAMIFHeight;         ///< Input CAMIF Height
    UINT32                                blackResidueOffset;  ///< Black Level Offset
    UINT32                                sensorOffsetX;       ///< Current Sensor Image Output horizontal offset
    UINT32                                sensorOffsetY;       ///< Current Sensor Image Output vertical offset
    BOOL                                  symbolIDChanged;     ///< Symbol IOD changed, reload triggers
    UINT32                                blackLevelOffset;    ///< Black Level Offset
    FLOAT                                 digitalGain;         ///< Input trigger value:  Digital Gain Value
    FLOAT                                 nonHdrMultFactor;    ///< Non HDR MultFactor
    VOID*                                 pInterpolationData;  ///< input memory for chromatix interpolation data
    UINT8                                 bayerPattern;        ///< bayer Pattern
    FLOAT                                 exposureGainRatio;     ///< exposure Gain Ratio
};


/// @ bried Input Data to BDS10 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct BDS10InputData
{
    VOID*                               pHWSetting;           ///< Pointer to the HW Setting Object
    bds_1_0::chromatix_bds10Type*       pChromatix;
    BOOL                                moduleEnable;         ///< Enable BDS Module
    FLOAT                               DRCGain;              ///< Input Trigger Value: DRC Gain
    FLOAT                               exposureTime;         ///< Input Trigger Value: Exposure Time
    FLOAT                               exposureGainRatio;    ///< Input Trigger Value: Exposure gain Ratio
    FLOAT                               AECSensitivity;       ///< Input Trigger Value: AecSensitivity
    FLOAT                               luxIndex;             ///< Input Trigger Value: Lux Index
    FLOAT                               digitalGain;          ///< Input Trigger Value: Digital Gain Value
    BOOL                                symbolIDChanged;      ///< Symbol ID changed, reload Triggers
    FLOAT                               leftGGainWB;          ///< G channel gain to adjust white balance
    FLOAT                               leftBGainWB;          ///< B channel gain to adjust white balance
    FLOAT                               leftRGainWB;          ///< R channel gain to adjust white balance
    UINT8                               bayerPattern;         ///< Bayer pattern
    FLOAT                               predictiveGain;       ///< Predictive Gain
    UINT32                              blackLevelResOffset;  ///< Black Level Residual Ofset
    UINT16                              inputWidth;           ///< Input Width
    VOID*                               pInterpolationData;   ///< input Memory for Chromatix Interpolation Data
    FLOAT                               preScaleRatio;        ///< Pre Scale Ratio
};

/// @ bried Input Data to WNR24 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct WNR24InputData
{
    VOID*                               pHWSetting;           ///< Pointer to the HW Setting Object
    wnr_2_4_0::chromatix_wnr24Type*     pChromatix;           ///< Input Chromatix Tuning Data
    BOOL                                moduleEnable;         ///< Enable WNR Module
    FLOAT                               DRCGain;              ///< Input Trigger Value: DRC Gain
    FLOAT                               exposureTime;         ///< Input Trigger Value: Exposure Time
    FLOAT                               exposureGainRatio;         ///< Input Trigger Value: Exposure Gain Ratio
    FLOAT                               AECSensitivity;       ///< Input Trigger Value: AecSensitivity
    FLOAT                               luxIndex;             ///< Input Trigger Value: Lux Index
    FLOAT                               totalScaleRatio;      ///< Input Trigger Value: Total Scale Ratio
    FLOAT                               preScaleRatio;       ///< Input Trigger Value: PreScale Ratio
    FLOAT                               postScaleRatio;       ///< Input Trigger Value: PostScale Ratio
    FLOAT                               realGain;             ///< Input Trigger Value: Digital Gain Value
    INT32                               imageWidth;           ///< Image Width
    INT32                               imageHeight;          ///< Image Height
    UINT8                               LUTBankSel;           ///< LUT bank
    BOOL                                symbolIDChanged;      ///< Symbol ID changed, reload Triggers
    VOID*                               pInterpolationData;   ///< input Memory for Chromatix Interpolation Data
    UINT8                               bayerPattern;         ///< Bayer Pattern
    FLOAT                               blendConfidence;      ///< Blend confidence to account for failure case of MFNR
    FLOAT                               adjustmentFactor;     ///< Adjustment factor to account for denoising strength in MFNR
    INT                                 opeCds;               ///< opeCDS enabled or disabled
};

static const UINT32 MAX_CAMIF_PDAF_PIXELS   = 256;
static const UINT32 BPSPDPC20DMILengthDword = 64;

/// @brief Input Data to pdpc11 Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct PDPC11InputData
{
    VOID*                             pHWSetting;                             ///< Pointer to the HW Setting Object
    pdpc_1_1_0::chromatix_pdpc11Type* pChromatix;                             ///< Input Chromatix tunning data
    SensorType                        sensorType;                             ///< Sensor type
    UINT32                            imageWidth;                             ///< Current Sensor Image Output Width
    UINT32                            imageHeight;                            ///< Current Sensor Image Output Height
    UINT32                            PDAFPixelCount;                         ///< PD pixel count
    UINT32                            PDAFBlockWidth;                         ///< Current Sensor PDAF block Width
    UINT32                            PDAFBlockHeight;                        ///< Current Sensor PDAF block Height
    UINT16                            PDAFGlobaloffsetX;                      ///< Current Sensor PDAF horizontal offset
    UINT16                            PDAFGlobaloffsetY;                      ///< Current Sensor PDAF vertical offset
    UINT16                            blackLevelOffset;                       ///< Black Level Offset
    UINT16                            zzHDRFirstRBEXP;                        ///< HDR First RB Exposure
    UINT16                            zzHDRPattern;                           ///< ZZ HDR pattern
    UINT8                             HDRMode;                                ///< ISP or Sensor HDR mode
    FLOAT                             DRCGain;                                ///< Input trigger value:  DRCGain
    FLOAT                             exposureTime;                           ///< Input trigger value:  exposure time
    FLOAT                             exposureGainRatio;                      ///< Input trigger value:  exposure gain ratio
    FLOAT                             AECSensitivity;                         ///< Input trigger value:  AEC sensitivity data
    FLOAT                             luxIndex;                               ///< Input trigger value:  Lux index
    FLOAT                             realGain;                               ///< Input trigger value:  AEC Gain Value
    FLOAT                             leftGGainWB;                            ///< G channel gain to adjust white balance
    FLOAT                             leftBGainWB;                            ///< B channel gain to adjust white balance
    FLOAT                             leftRGainWB;                            ///< R channel gain to adjust white balance
    PDPixelCoordinates                PDAFPixelCoords[MAX_CAMIF_PDAF_PIXELS]; ///< Pixel coordinates
    VOID*                             pInterpolationData;                     ///< input memory for interpolation data
    BOOL                              isPrepareStripeInputContext;            ///< Flag to indicate if prepare striping context
};

/// @brief Input Data to GTM10 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct GTM10InputData
{
    VOID*                           pHWSetting;             ///< Pointer to the HW Setting Object
    gtm_1_0_0::chromatix_gtm10Type* pChromatix;             ///< Input Chromatix tunning data
    BOOL                            symbolIDChanged;        ///< Symbol IOD changed, reload triggers
    FLOAT                           DRCGain;                ///< Input trigger value:  drcGain
    FLOAT                           exposureTime;           ///< Input trigger value:  exposure time
    FLOAT                           exposureGainRatio;      ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;         ///< Input trigger value:  AEC sensitivity data
    FLOAT                           AECSensitivityShort;    ///< Input trigger value:  AEC sensitivity data
    FLOAT                           realGain;               ///< Input trigger value:  Digital Gain Value
    FLOAT                           digitalGain;            ///< Digital gain from Sensor
    FLOAT                           luxIndex;               ///< Input trigger value:  Lux index
    UINT8                           LUTBankSel;             ///< LUT bank need for this frame
    /// @todo (CAMX-1843) Need to populate below parameters from Meta data/calculated values
    UINT32*                         pGRHist;                ///< BHIST GR stats
    FLOAT                           HDRExpTimeRatio;        ///< HDR exposure time ratio
    UINT16                          HDRMSBAlign;            ///< HDR MSB alignment
    UINT16                          HDRExpRatio;            ///< HDR exposure ratio
    UINT16                          HDRBlackIn;             ///< Black level input
    UINT32                          totalNumFrames;         ///< Total number of frames
    UINT32                          frameNum;               ///< Current frame number
    FLOAT                           keyPrevious;            ///< Key from previous frame
    FLOAT                           alphaPrevious;          ///< Alpha from previous frame
    INT32                           maxVHistPrevious;       ///< Maximum value in histogram from previous frame
    INT32                           minVHistPrevious;       ///< Minimum value in histogram from previous frame
    VOID*                           pLibData;               ///< Pointer to the library specific data
    ADRCData*                       pAdrcInputData;         ///< Pointer to the input data for adrc algo calculation
    UINT64                          BHistRequestID;         ///< RequestID to which the BHIST stats correspond to
    UINT32                          maxPipelineDelay;       ///< Maximum pipeline delay
    VOID*                           pInterpolationData;     ///< input memory for interpolation data
    FLOAT                           gamma15Output[257];     ///< Gamma output from gamma15 module
    ParsedHDRBHistStatsOutput*      pParsedHDRBHistStats;   ///< HDR BHIST stats
    ParsedHDRBEStatsOutput*         pParsedHDRBEStats;      ///< HDR BE stats
};


/// @brief Input Data to GTM12 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct GTM12InputData
{
    VOID*                           pHWSetting;             ///< Pointer to the HW Setting Object
    gtm_1_2_0::chromatix_gtm12Type* pChromatix;             ///< Input Chromatix tunning data
    BOOL                            symbolIDChanged;        ///< Symbol IOD changed, reload triggers
    FLOAT                           DRCGain;                ///< Input trigger value:  drcGain
    FLOAT                           exposureTime;           ///< Input trigger value:  exposure time
    FLOAT                           exposureGainRatio;           ///< Input trigger value:  exposure gain Ratio
    FLOAT                           AECSensitivity;         ///< Input trigger value:  AEC sensitivity data
    FLOAT                           AECSensitivityShort;    ///< Input trigger value:  AEC sensitivity data
    FLOAT                           realGain;               ///< Input trigger value:  Digital Gain Value
    FLOAT                           digitalGain;            ///< Digital gain from Sensor
    FLOAT                           luxIndex;               ///< Input trigger value:  Lux index
    UINT8                           LUTBankSel;             ///< LUT bank need for this frame
                                                            /// @todo (CAMX-1843) Need to populate below parameters
                                                            ///        from Meta data/calculated values
    UINT32*                         pGRHist;                ///< BHIST GR stats
    FLOAT                           HDRExpTimeRatio;        ///< HDR exposure time ratio
    UINT16                          HDRMSBAlign;            ///< HDR MSB alignment
    UINT16                          HDRExpRatio;            ///< HDR exposure ratio
    UINT16                          HDRBlackIn;             ///< Black level input
    UINT32                          totalNumFrames;         ///< Total number of frames
    UINT32                          frameNum;               ///< Current frame number
    FLOAT                           keyPrevious;            ///< Key from previous frame
    FLOAT                           alphaPrevious;          ///< Alpha from previous frame
    INT32                           maxVHistPrevious;       ///< Maximum value in histogram from previous frame
    INT32                           minVHistPrevious;       ///< Minimum value in histogram from previous frame
    VOID*                           pLibData;               ///< Pointer to the library specific data
    ADRCData*                       pAdrcInputData;         ///< Pointer to the input data for adrc algo calculation
    UINT64                          BHistRequestID;         ///< RequestID to which the BHIST stats correspond to
    UINT32                          maxPipelineDelay;       ///< Maximum pipeline delay
    VOID*                           pInterpolationData;     ///< input memory for interpolation data
    UINT32                          chipsetVersion;         ///< Titan version
};

/// @brief Input Data to GTM13 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct GTM13InputData
{
    VOID*                           pHWSetting;             ///< Pointer to the HW Setting Object
    gtm_1_3_0::chromatix_gtm13Type* pChromatix;             ///< Input Chromatix tunning data
    BOOL                            symbolIDChanged;        ///< Symbol IOD changed, reload triggers
    FLOAT                           DRCGain;                ///< Input trigger value:  drcGain
    FLOAT                           exposureTime;           ///< Input trigger value:  exposure time
    FLOAT                           exposureGainRatio;      ///< Input trigger value:  exposure gain Ratio
    FLOAT                           AECSensitivity;         ///< Input trigger value:  AEC sensitivity data
    FLOAT                           AECSensitivityShort;    ///< Input trigger value:  AEC sensitivity data
    FLOAT                           realGain;               ///< Input trigger value:  Digital Gain Value
    FLOAT                           digitalGain;            ///< Digital gain from Sensor
    FLOAT                           luxIndex;               ///< Input trigger value:  Lux index
    UINT8                           LUTBankSel;             ///< LUT bank need for this frame
    /// @todo (CAMX-1843) Need to populate below parameters from Meta data/calculated values
    UINT32*                         pGRHist;                ///< BHIST GR stats
    FLOAT                           HDRExpTimeRatio;        ///< HDR exposure time ratio
    UINT16                          HDRMSBAlign;            ///< HDR MSB alignment
    UINT16                          HDRExpRatio;            ///< HDR exposure ratio
    UINT16                          HDRBlackIn;             ///< Black level input
    UINT32                          totalNumFrames;         ///< Total number of frames
    UINT32                          frameNum;               ///< Current frame number
    FLOAT                           keyPrevious;            ///< Key from previous frame
    FLOAT                           alphaPrevious;          ///< Alpha from previous frame
    INT32                           maxVHistPrevious;       ///< Maximum value in histogram from previous frame
    INT32                           minVHistPrevious;       ///< Minimum value in histogram from previous frame
    VOID*                           pLibData;               ///< Pointer to the library specific data
    ADRCData*                       pAdrcInputData;         ///< Pointer to the input data for adrc algo calculation
    UINT64                          BHistRequestID;         ///< RequestID to which the BHIST stats correspond to
    UINT32                          maxPipelineDelay;       ///< Maximum pipeline delay
    VOID*                           pInterpolationData;     ///< input memory for interpolation data
};

/// @brief Input Data to HNR10 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct HNR10InputData
{
    VOID*                               pHWSetting;                ///< Pointer to the HW Setting Object
    hnr_1_0_0::chromatix_hnr10Type*     pChromatix;                ///< Input Chromatix tunning data
    nradj_1_0_0::chromatix_nradj10Type* pNRAjust10Chromatix;       ///< Input Chromatix tunning data for NR adjust
    BOOL                                moduleEnable;              ///< Module enable flag
    FLOAT                               totalScaleRatio;           ///< Input trigger value:  TotalScaleRatio
    FLOAT                               DRCGain;                   ///< Input trigger value:  drcGain
    FLOAT                               exposureTime;              ///< Input trigger value:  exporsure time
    FLOAT                               exposureGainRatio;         ///< Input trigger value:  exposure gain ratio
    FLOAT                               AECSensitivity;            ///< Input trigger value:  AEC sensitivity data
    FLOAT                               luxIndex;                  ///< Input trigger value:  Lux index
    FLOAT                               realGain;                  ///< Input trigger value:  Digital Gain Value
    UINT32                              streamWidth;               ///< Stream Width
    UINT32                              streamHeight;              ///< Stream height
    UINT32                              horizontalOffset;          ///< Horizontal Offset
    UINT32                              verticalOffset;            ///< Vertical Offset
    struct FDData*                      pFDData;                   ///< Face data
    UINT8                               LUTBankSel;                ///< LUT bank need for this frame
    VOID*                               pInterpolationData;        ///< input memory for interpolation data
    BOOL                                enableNRAdjust;            ///< Enable NR adjust flag from Chromatix
    VOID*                               pGTMInfo;                  ///< Pointer to GTM info from IFE/BPS
};

/// @brief Input Data to Demux13 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct Demux13InputData
{
    VOID*                               pHWSetting;            ///< Pointer to the HW Setting Object
    demux_1_3_0::chromatix_demux13Type* pChromatixInput;       ///< Input Chromatix tuning data
    UINT8                               bayerPattern;          ///< Bayer pattern
    UINT32                              demuxInConfigPeriod;   ///< Configration period
    UINT32                              demuxInConfigConfig;   ///< Determine config for even or odd channel
    FLOAT                               digitalGain;           ///< Digital Gain of in the input stream
    FLOAT                               stretchGainRed;        ///< Stretch Gain of Red Channel
    FLOAT                               stretchGainGreenEven;  ///< Stretch Gain of Green Channel (Even)
    FLOAT                               stretchGainGreenOdd;   ///< Stretch Gain of Green Channel (Odd)
    FLOAT                               stretchGainBlue;       ///< Stretch Gain of Blue Channel
    UINT32                              blackLevelOffset;      ///< Black Level Offset
};

/// @brief Input Data to Demux14 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct Demux14InputData
{
    VOID*                                                 pHWSetting;            ///< Pointer to the HW Setting Object
    demuxblklevel_1_4_0::chromatix_demuxblklevel14Type*   pChromatixInput;       ///< Input Chromatix tuning data
    UINT8                                                 bayerPattern;          ///< Bayer pattern
    UINT32                                                demuxInConfigPeriod;   ///< Configration period
    UINT32                                                demuxInConfigConfig;   ///< Determine config for even or odd channel
    FLOAT                                                 luxIndex;              ///< Lux Index
    FLOAT                                                 realGain;              ///< Real Gain
    FLOAT                                                 AECSensitivity;        ///< AEC Sensitivity
    FLOAT                                                 exposureTime;          ///< Exposure Time
    FLOAT                                                 exposureGainRatio;     ///< Exposure Gain Ratio
    FLOAT                                                 CCTTrigger;            ///< CCT Trigger
    FLOAT                                                 LEDFirstEntryRatio;    ///< First Entry Ratio
    FLOAT                                                 LEDTrigger;            ///< LED Trigger
    UINT32                                                numberOfLED;           ///< Number of LED
    FLOAT                                                 digitalGain;           ///< Digital Gain of in the input stream
    FLOAT                                                 DRCGain;               ///< DRC Gain
    FLOAT                                                 blackLevelOffset;      ///< Black level offset
    FLOAT                                                 stretchGainRed;        ///< Stretch Gain Red
    FLOAT                                                 stretchGainGreenEven;  ///< Stretch Gain Green
    FLOAT                                                 stretchGainGreenOdd;   ///< Stretch Gain Green
    FLOAT                                                 stretchGainBlue;       ///< stretch Gain Blue
    VOID*                                                 pInterpolationData;    ///< input memory for interpolation data
    VOID*                                                 pBLSInterpolationData; ///< input memory for BLS interpolation data
    UINT32                                                chipsetVersion;        ///< Titan version
};

/// @brief Input Data to Demosaic36 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct Demosaic36InputData
{
    VOID*                                     pHWSetting;           ///< Pointer to the HW Setting Object
    demosaic_3_6_0::chromatix_demosaic36Type* pChromatix;           ///< Input Chromatix tuning data
    BOOL                                      symbolIDChanged;      ///< Symbol ID changed, reload triggers
    UINT32                                    imageWidth;           ///< Current Sensor Image Output Width
    UINT32                                    imageHeight;          ///< Current Sensor Image Output Height
    FLOAT                                     DRCGain;              ///< Input trigger value:  DRCGain
    FLOAT                                     exposureTime;         ///< Input trigger value:  exporsure time
    FLOAT                                     exposureGainRatio;    ///< Input trigger value:  exposure gain ratio
    FLOAT                                     AECSensitivity;       ///< Input trigger value:  AEC sensitivity data
    FLOAT                                     luxIndex;             ///< Input trigger value:  Lux index
    FLOAT                                     AECGain;              ///< Input trigger value:  AEC Gain Value
    VOID*                                     pInterpolationData;   ///< input memory for interpolation data
};

/// @brief Input Data to Demosaic37 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct Demosaic37InputData
{
    VOID*                                     pHWSetting;           ///< Pointer to the HW Setting Object
    demosaic_3_7_0::chromatix_demosaic37Type* pChromatix;           ///< Input Chromatix tuning data
    BOOL                                      symbolIDChanged;      ///< Symbol ID changed, reload triggers
    UINT32                                    imageWidth;           ///< Current Sensor Image Output Width
    UINT32                                    imageHeight;          ///< Current Sensor Image Output Height
    FLOAT                                     DRCGain;              ///< Input trigger value:  DRCGain
    FLOAT                                     exposureTime;         ///< Input trigger value:  exporsure time
    FLOAT                                     exposureGainRatio;    ///< Input trigger value:  exposure gain ratio
    FLOAT                                     AECSensitivity;       ///< Input trigger value:  AEC sensitivity data
    FLOAT                                     luxIndex;             ///< Input trigger value:  Lux index
    FLOAT                                     AECGain;              ///< Input trigger value:  AEC Gain Value
    VOID*                                     pInterpolationData;   ///< input memory for interpolation data
};

/// @brief Input Data to DS410 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct DS410InputData
{
    VOID*                                     pHWSetting;           ///< Pointer to the HW Setting Object
    ds4to1_1_0_0::chromatix_ds4to1v10Type*    pDS4Chromatix;        ///< Chromatix pointer for DS4
    ds4to1_1_0_0::chromatix_ds4to1v10Type*    pDS16Chromatix;       ///< Chromatix pointer for DS16
    UINT32                                    stream_height;        ///< Stream Height in pixels
    UINT                                      modulePath;           ///< IFE pipeline path for module
};

/// @brief Input Data to DS410 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct DS411InputData
{
    VOID*                                     pHWSetting;           ///< Pointer to the HW Setting Object
    ds4to1_1_1_0::chromatix_ds4to1v11Type*    pDS4Chromatix;        ///< Chromatix pointer for DS4
    ds4to1_1_1_0::chromatix_ds4to1v11Type*    pDS16Chromatix;       ///< Chromatix pointer for DS16
    UINT32                                    streamWidth;          ///< Stream Height in pixels
    UINT32                                    streamHeight;         ///< Stream Height in pixels
    UINT                                      modulePath;           ///< IFE pipeline path for module
};

/// @brief Input Data to DS410 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct DSX10InputData
{
    VOID*                                  pHWSetting;                  ///< Pointer to the HW Setting Object
    dsx_1_0_0::chromatix_dsx10Type*        pChromatix;                  ///< Input Chromatix tuning data
    ds4to1_1_1_0::chromatix_ds4to1v11Type* pDS4to1Chromatix;            ///< Input Chromatix tuning data
    FLOAT                                  scaleRatio;                  ///< Input trigger value
    UINT32                                 inW;                         ///< Input width [16, 8190]
    UINT32                                 inH;                         ///< Input hight [16, 16384]
    UINT32                                 outW;                        ///< Output width [2, 1696]
    UINT32                                 outH;                        ///< Output width [2, 13108]
    FLOAT                                  downscaleX;                  ///< Step size [1.25, 4]
    FLOAT                                  downscaleY;                  ///< Step size [1.25, 4]
    FLOAT                                  offsetX;                     ///< input offset X [0, 0.8*inW]
    FLOAT                                  offsetY;                     ///< input offset Y [0, 0.8*inH]
    UINT32                                 numPasses;                   ///< Pass number
    VOID*                                  pDSXChromatix;               ///< Chromatix Input pointer
    VOID*                                  pInterpolationData;          ///< input memory for interpolation data
    VOID*                                  pUnpackedfield;              ///< input memory Unpacked filed
    UINT                                   modulePath;                  ///< IFE pipeline path for module
    BOOL                                   isPrepareStripeInputContext; ///< Flag to indicate prepare striping
    VOID*                                  pICAInterpolationData;       ///< ICA DATA
    NCLIB_DSX_HOST_TYPE                    host;                        ///< host for dsx: IFE_DSX or IPE_DSX
    VOID*                                  pICA30Chromatix;             ///< ICA 30 Chromatix
    VOID*                                  pICA30InterpolationData;     ///< input memory for interpolation data
    FLOAT                                  luxIndex;                    ///< Input trigger : Lux index
    FLOAT                                  digitalGain;                 ///< Input trigger : Digital Gain Value
    BOOL                                   isGridFromChromatixEnabled;  ///< Flag to check if grid from chromatixis Enabled
    UINT32                                 titanVersion;                ///< Titan Version
    UINT64                                 preDSXPhaseInitLuma;         ///< Left Stripe DSX luma_start_location_x for Luma
    UINT64                                 preDSXPhaseInitChroma;       ///< Left Stripe DSX luma_art_location_x for Chroma
    BOOL                                   updatePreDSXPhaseInit;       ///< flag to signify update preDSX_phase_init param
};

/// @brief Input Data to GIC30 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct GIC30InputData
{
    VOID*                           pHWSetting;             ///< Pointer to the HW Setting Object
    gic_3_0_0::chromatix_gic30Type* pChromatix;             ///< Input Chromatix tunning data
    BOOL                            moduleEnable;           ///< Module enable flag
    BOOL                            symbolIDChanged;        ///< Symbol ID changed, reload triggers
    FLOAT                           DRCGain;                ///< Input trigger value:  drcGain
    FLOAT                           exposureTimeTrigger;    ///< Input trigger value:  exporsure time
    FLOAT                           exposureGainRatio;      ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;         ///< Input trigger value:  AEC sensitivity data
    FLOAT                           luxIndex;               ///< Input trigger value:  Lux index
    FLOAT                           realGain;               ///< Input trigger value:  Real Gain Value
    UINT8                           LUTBankSel;             ///< LUT bank used for the frame
    UINT32                          imageWidth;             ///< Current Sensor Image Output Width
    UINT32                          imageHeight;            ///< Current Sensor Image Output Height
    UINT32                          sensorOffsetX;          ///< Current Sensor Image Output horizontal offset
    UINT32                          sensorOffsetY;          ///< Current Sensor Image Output vertical offset
    VOID*                           pInterpolationData;     ///< input memory for interpolation data
};

/// @brief Input Data to HDR20 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct HDR20InputData
{
    VOID*                           pHWSetting;           ///< Pointer to the HW Setting Object
    hdr_2_0_0::chromatix_hdr20Type* pChromatix;           ///< Input Chromatix tunning data
    BOOL                            moduleMacEnable;      ///< Module HDR MAC enable flag
    BOOL                            moduleReconEnable;    ///< Module HDR Recon enable flag
    FLOAT                           exposureTime;         ///< Input trigger value:  exposure time
    FLOAT                           exposureGainRatio;    ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;       ///< Input trigger value:  AEC sensitivity data
    FLOAT                           luxIndex;             ///< Input trigger value:  Lux index
    FLOAT                           AECGain;              ///< Input trigger value:  Digital Gain Value
    FLOAT                           leftGGainWB;          ///< G channel gain to adjust white balance
    FLOAT                           leftBGainWB;          ///< B channel gain to adjust white balance
    FLOAT                           leftRGainWB;          ///< R channel gain to adjust white balance
    UINT16                          RECONFirstField;      ///< HDR First field 0=T1, 1=T2
    UINT16                          ZZHDRMode;            ///< ZZHDR mode or IHDR mode
    UINT16                          ZZHDRPattern;         ///< 0~3, Bayer starting R1: 0=R1G1G2B1, 1=R1G1G2B2,
                                                          ///<                         2=R1G2G1B1, 3=R1G2G1B2
    UINT16                          ZZHDRFirstRBEXP;      ///< ZZHDR first R/B Exp,  0=T1, 1=T2
    UINT16                          HDRZigzagReconSel;    ///< ZZHDR ReconSel
    UINT32                          blackLevelOffset;     ///< Black Level Offset
    VOID*                           pInterpolationData;   ///< input memory for interpolation data
};

/// @brief Input Data to HDR23 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct HDR23InputData
{
    VOID*                           pHWSetting;           ///< Pointer to the HW Setting Object
    hdr_2_3_0::chromatix_hdr23Type* pChromatix;           ///< Input Chromatix tunning data
    BOOL                            moduleMacEnable;      ///< Module HDR MAC enable flag
    BOOL                            moduleReconEnable;    ///< Module HDR Recon enable flag
    FLOAT                           exposureTime;         ///< Input trigger value:  exposure time
    FLOAT                           exposureGainRatio;    ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;       ///< Input trigger value:  AEC sensitivity data
    FLOAT                           luxIndex;             ///< Input trigger value:  Lux index
    FLOAT                           AECGain;              ///< Input trigger value:  Digital Gain Value
    FLOAT                           leftGGainWB;          ///< G channel gain to adjust white balance
    FLOAT                           leftBGainWB;          ///< B channel gain to adjust white balance
    FLOAT                           leftRGainWB;          ///< R channel gain to adjust white balance
    UINT16                          RECONFirstField;      ///< HDR First field 0=T1, 1=T2
    UINT16                          ZZHDRMode;            ///< ZZHDR mode or IHDR mode
    UINT16                          ZZHDRPattern;         ///< 0~3, Bayer starting R1: 0=R1G1G2B1, 1=R1G1G2B2,
                                                          ///<                         2=R1G2G1B1, 3=R1G2G1B2
    UINT16                          ZZHDRFirstRBEXP;      ///< ZZHDR first R/B Exp,  0=T1, 1=T2
    UINT16                          HDRZigzagReconSel;    ///< ZZHDR ReconSel
    UINT32                          blackLevelOffset;     ///< Black Level Offset
    VOID*                           pInterpolationData;   ///< input memory for interpolation data
};

/// @brief Input Data to Pedestal13 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct Pedestal13InputData
{
    VOID*                                     pHWSetting;             ///< Pointer to the HW Setting Object
    pedestal_1_3_0::chromatix_pedestal13Type* pChromatix;             ///< Input Chromatix tunning data
    BOOL                                      symbolIDChanged;        ///< Symbol ID changed, reload triggers
    FLOAT                                     DRCGain;                ///< Input trigger value:  drcGain
    FLOAT                                     exposureTimeTrigger;    ///< Input trigger value:  exporsure time
    FLOAT                                     exposureGainRatio;      ///< Input trigger value:  exposure gain ratio
    FLOAT                                     AECSensitivity;         ///< Input trigger value:  AEC sensitivity data
    FLOAT                                     LEDTrigger;             ///< Input trigger value:  Led Index
    FLOAT                                     LEDFirstEntryRatio;     ///< Ratio of Dual LED
    UINT16                                    numberOfLED;            ///< Number of LED
    FLOAT                                     luxIndex;               ///< Input trigger value:  Lux index
    FLOAT                                     CCTTrigger;             ///< Input trigger value:  CCT
    FLOAT                                     AECGain;                ///< Input trigger value:  Digital Gain Value
    UINT16                                    LUTBankSel;             ///< LUT bank used for the frame
    UINT8                                     bayerPattern;           ///< Bayer Pattern
    UINT32                                    imageWidth;             ///< Current Sensor Image Output Width
    UINT32                                    imageHeight;            ///< Current Sensor Image Output Height
    VOID*                                     pInterpolationData;     ///< input memory for interpolation data
};

/// @brief Input Data to Linearization34 IQ Algorithm
/// NOWHINE NC004c: Share code with system team
struct Linearization34IQInput
{
    VOID*                                                pHWSetting;             ///< Pointer to the HW Setting Object
    linearization_3_4_0::chromatix_linearization34Type*  pChromatix;             ///< Input Chromatix tunning data
    BOOL                                                 symbolIDChanged;        ///< Symbol IOD changed, reload triggers
    FLOAT                                                DRCGain;                ///< Input trigger value: DRCGain
    FLOAT                                                exposureTimeTrigger;    ///< Input trigger value: exporsure time
    FLOAT                                                exposureGainRatio;      ///< Input trigger value:  exposure gain ratio
    FLOAT                                                AECSensitivity;         ///< Input trigger value: AEC sensitivity data
    FLOAT                                                LEDTrigger;             ///< Input trigger value: Led Index
    UINT16                                               numberOfLED;            ///< Number of LED
    FLOAT                                                LEDFirstEntryRatio;     ///< Ratio of Dual LED
    FLOAT                                                luxIndexTrigger;        ///< Input trigger value: Lux index
    FLOAT                                                CCTTrigger;             ///< Input trigger value: CCT
    FLOAT                                                realGain;               ///< Input trigger value: Real Gain Value
    UINT16                                               LUTBankSel;             ///< LUT bank need for this frame
    UINT8                                                bayerPattern;           ///< Bayer pattern
    BOOL                                                 pedestalEnable;         ///< pedestal is enabled or not
    VOID*                                                pInterpolationData;     ///< input memory for interpolation data
};

/// @brief Input Data to WhiteBalance12 IQ Algorithem
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct WB12InputData
{
    VOID* pHWSetting;      ///< Pointer to the HW Setting Object
    FLOAT leftGGainWB;     ///< G channel gain to adjust white balance
    FLOAT leftBGainWB;     ///< B channel gain to adjust white balance
    FLOAT leftRGainWB;     ///< R channel gain to adjust white balance
    FLOAT predictiveGain;  ///< Predictive Gain
};

/// @brief Input Data to WhiteBalance13 IQ Algorithem
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct WB13InputData
{
    VOID*  pHWSetting;      ///< Pointer to the HW Setting Object
    FLOAT  leftGGainWB;     ///< G channel gain to adjust white balance
    FLOAT  leftBGainWB;     ///< B channel gain to adjust white balance
    FLOAT  leftRGainWB;     ///< R channel gain to adjust white balance
    FLOAT  predictiveGain;  ///< Predictive Gain
    UINT16 inputWidth;      ///< Input Image Width
};

/// @brief Input Data to WhiteBalance14 IQ Algorithem
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct WB14InputData
{
    VOID*  pHWSetting;          ///< Pointer to the HW Setting Object
    FLOAT  leftGGainWB;         ///< G channel gain to adjust white balance
    FLOAT  leftBGainWB;         ///< B channel gain to adjust white balance
    FLOAT  leftRGainWB;         ///< R channel gain to adjust white balance
    UINT8  bayerPattern;        ///< Bayer pattern
    FLOAT  predictiveGain;      ///< Predictive Gain
    UINT32 blackLevelResOffset; ///< Black Level Residual Ofset
    UINT16 inputWidth;          ///< Input Image Width
};

/// @brief Input Data to lrme IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct LRMEInputData
{
    VOID*                            pHWSetting;           ///< Pointer to the HW Setting Object
    VOID*                            pCapability;          ///< LRME capabilities
    UINT32                           tarWidth;             ///< Target port width
    UINT32                           tarHeight;            ///< Target port height
    UINT32                           refWidth;             ///< Reference port width
    UINT32                           refHeight;            ///< Reference port height
    BOOL                             isRefValid;           ///< flag for reference validity
    BOOL                             isDS2Enabled;         ///< flag for DS2 enablement
    UINT32                           tarFormat;            ///< Target port format
    UINT32                           refFormat;            ///< Reference port format
    UINT32                           vectorOutputFormat;   ///< Output port format
    VOID*                            pLRMECmdBuffer;       ///< Pointer LRME command buffer
    UINT32                           frameNum;             ///< Current frame number
};

/// @brief Input Data to ABF40 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct ABF40InputData
{
    VOID*                           pHWSetting;                 ///< Pointer to the HW Setting Object
    abf_4_0_0::chromatix_abf40Type* pChromatix;                 ///< Input Chromatix tunning data
    BOOL                            moduleEnable;               ///< Module enable flag
    FLOAT                           DRCGain;                    ///< Input trigger value:  DRCGain
    FLOAT                           exposureTime;               ///< Input trigger value:  exporsure time
    FLOAT                           exposureGainRatio;          ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;             ///< Input trigger value:  AEC sensitivity data
    FLOAT                           luxIndex;                   ///< Input trigger value:  Lux index
    FLOAT                           realGain;                   ///< Input trigger value:  Real Gain Value
    FLOAT                           leftGGainWB;                ///< G channel gain to adjust white balance
    FLOAT                           leftBGainWB;                ///< B channel gain to adjust white balance
    FLOAT                           leftRGainWB;                ///< R channel gain to adjust white balance
    UINT8                           LUTBankSel;                 ///< Lut Bank
    BLS12InputData                  BLSData;                    ///< Black Level is clubbed with ABF40 in BPS
    UINT32                          sensorOffsetX;              ///< Sensor X offset used for RNR
    UINT32                          sensorOffsetY;              ///< Sensor y offset used for RNR
    UINT32                          sensorWidth;                ///< Sensor output width
    UINT32                          sensorHeight;               ///< Sensor output Height
    VOID*                           pInterpolationData;         ///< input memory for interpolation data
    BOOL                            bilateralEnable;            ///< enable bilateral filtering
    BOOL                            minmaxEnable;               ///< enable built-in min-max pixel filter
    BOOL                            directionalSmoothEnable;    ///< enable directional smoothing filter
    BOOL                            BLSEnable;                  ///< enable BLS
    BOOL                            crossPlaneEnable;           ///< enable cross plane
    BOOL                            activityAdjustEnable;       ///< enable activity adjustment
    NRMConfigData                   nrmCfgData;                 ///< NRM config data
    FLOAT                           luma_denoising;             ///< Slider Based Tuning
};

/// @brief Input Data to CS20 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct CS20InputData
{
    VOID*                          pHWSetting;          ///< Pointer to the HW Setting Object
    cs_2_0_0::chromatix_cs20Type*  pChromatix;          ///< Input Chromatix tunning data
    FLOAT                          DRCGain;             ///< Input trigger value:  DRCGain
    FLOAT                          exposureTime;        ///< Input trigger value:  exposure time
    FLOAT                          exposureGainRatio;   ///< Input trigger value:  exposure gain ratio
    FLOAT                          AECSensitivity;      ///< Input trigger value:  AEC sensitivity data
    FLOAT                          luxIndex;            ///< Input trigger value:  Lux index
    FLOAT                          linearGain;          ///< Input trigger value:  Digital Gain Value
    VOID*                          pInterpolationData;  ///< input memory for interpolation data
};

/// @brief Input Data to BPSHDR22 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct HDR22InputData
{
    VOID*                           pHWSetting;             ///< Pointer to the HW Setting Object
    hdr_2_2_0::chromatix_hdr22Type* pChromatix;             ///< Input Chromatix tunning data
    BOOL                            moduleMacEnable;        ///< Module enable flag
    BOOL                            moduleReconEnable;      ///< Module enable flag
    FLOAT                           exposureTime;           ///< Input trigger value:  exposure time
    FLOAT                           exposureGainRatio;      ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;         ///< Input trigger value:  AEC sensitivity data
    FLOAT                           luxIndex;               ///< Input trigger value:  Lux index
    FLOAT                           AECGain;                ///< Input trigger value:  Digital Gain Value
    FLOAT                           leftGGainWB;            ///< G channel gain to adjust white balance
    FLOAT                           leftBGainWB;            ///< B channel gain to adjust white balance
    FLOAT                           leftRGainWB;            ///< R channel gain to adjust white balance
    UINT32                          blackLevelOffset;       ///< Black Level Offset
    UINT16                          RECONFirstField;        ///< HDR First field 0=T1, 1=T2
    UINT16                          ZZHDRMode;              ///< ZZHDR mode or IHDR mode
    UINT16                          ZZHDRPattern;           ///< 0~3 Bayer starting R1: 0=R1G1G2B1, 1=R1G1G2B2,
                                                            ///< 2=R1G2G1B1, 3=R1G2G1B2
    UINT16                          ZZHDRFirstRBEXP;        ///< ZZHDR first r/B Exp, 0=T1, 1=T2
    UINT8                           bayerPattern;           ///< Bayer pattern
    VOID*                           pInterpolationData;     ///< input memory for interpolation data
};

/// @brief Input Data to BC10 IQ Algorithm
/// NOWHINE NC004c: Share code with system team
struct BC10InputData
{
    bincorr_1_0_0::chromatix_bincorr10Type* pChromatixBC;           ///< Pointer to BC10 chromatix
    BOOL                                    moduleEnable;           ///< BC10 module enable
    FLOAT                                   postScaleRatio;         ///< Input trigger value:  Post scale ratio
    FLOAT                                   preScaleRatio;          ///< Input trigger value:  Pre scale ratio
    VOID*                                   pBC10InterpolationData; ///< input memory for interpolation data
};

/// @brief Input Data to HDR30 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct HDR30InputData
{
    VOID*                                   pHWSetting;             ///< Pointer to the HW Setting Object
    hdr_3_0_0::chromatix_hdr30Type*         pChromatix;             ///< Input Chromatix HDR tunning data
    BOOL                                    prevMacStateEnable;     ///< Previous enable flag
    BOOL                                    moduleReconEnable;      ///< Module enable flag
    BOOL                                    prevReconStateEnable;   ///< Previous enable flag
    FLOAT                                   exposureTime;           ///< Input trigger value:  exposure time
    FLOAT                                   exposureGainRatio;      ///< Input trigger value:  exposure gain ratio
    FLOAT                                   AECSensitivity;         ///< Input trigger value:  AEC sensitivity data
    FLOAT                                   luxIndex;               ///< Input trigger value:  Lux index
    FLOAT                                   AECGain;                ///< Input trigger value:  Digital Gain Value
    FLOAT                                   leftGGainWB;            ///< G channel gain to adjust white balance
    FLOAT                                   leftBGainWB;            ///< B channel gain to adjust white balance
    FLOAT                                   leftRGainWB;            ///< R channel gain to adjust white balance
    UINT32                                  blackLevelOffset;       ///< Black Level Offset
    UINT16                                  RECONFirstField;        ///< HDR First field 0=T1, 1=T2
    UINT16                                  ZZHDRMode;              ///< ZZHDR mode or IHDR mode
    UINT16                                  ZZHDRPattern;           ///< 0~3 Bayer starting R1: 0=R1G1G2B1, 1=R1G1G2B2,
                                                                    ///<                        2=R1G2G1B1, 3=R1G2G1B2
    UINT16                                  ZZHDRFirstRBEXP;        ///< ZZHDR first r/B Exp, 0=T1, 1=T2
    UINT8                                   bayerPattern;           ///< Bayer pattern
    VOID*                                   pInterpolationData;     ///< input memory for interpolation data
    BC10InputData                           bc10Data;               ///< Bin correction input data
};

/// @brief Input Data to HDR40 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct HDR40InputData
{
    VOID*                                   pHWSetting;                     ///< Pointer to the HW Setting Object
    hdr_4_0_0::chromatix_hdr40Type*         pChromatix;                     ///< Input Chromatix HDR tunning data
    BOOL                                    hdr_enable;                     ///< Module enable flag
    BOOL                                    realtimeFlag;                   ///< flag to indicate realtime usecase or not
    BOOL                                    isAFDControlEnable;             ///< 3A AFD Enable control
    FLOAT                                   exposureGainRatio;              ///< Input trigger value:  exposure gain ratio
    FLOAT                                   exposureGainRatioT1T2;          ///< Input trigger value:  L/M exposure gain ratio
    FLOAT                                   exposureGainRatioT2T3;          ///< Input trigger value:  M/S exposure gain ratio
    FLOAT                                   exposureGainRatioT1T3;          ///< Input trigger value:  L/S exposure gain ratio
    FLOAT                                   exposureTimeRatioT1T2;          ///< Input trigger value:  L/M exposure time ratio
    FLOAT                                   exposureTimeRatioT2T3;          ///< Input trigger value:  M/S exposure time ratio
    FLOAT                                   exposureTimeRatioT1T3;          ///< Input trigger value:  L/S exposure time ratio
    FLOAT                                   exposureSensitivityRatioT1T2;   ///< Input trigger value:  L/M sensitivity ratio
    FLOAT                                   exposureSensitivityRatioT2T3;   ///< Input trigger value:  M/S sensitivity ratio
    FLOAT                                   exposureSensitivityRatioT1T3;   ///< Input trigger value:  L/S sensitivity ratio
    FLOAT                                   BGMidStatsSatRatio;             ///< BG Mid Stats Ratio
    FLOAT                                   BGMidStatsDarkRatio;            ///< BG Mid Stats Ratio
    FLOAT                                   luxIndex;                       ///< Input trigger value:  Lux index
    FLOAT                                   AECGain;                        ///< Input trigger value:  Digital Gain Value
    FLOAT                                   leftGGainWB;                    ///< G channel gain to adjust white balance
    FLOAT                                   leftBGainWB;                    ///< B channel gain to adjust white balance
    FLOAT                                   leftRGainWB;                    ///< R channel gain to adjust white balance
    VOID*                                   pInterpolationData;             ///< input memory for interpolation data
    UINT32                                  expCount;                       ///< exposure count
    UINT32                                  QLLEnable;                      ///< QLL enable flag
    FLOAT                                   shortBlendRatio;                ///< Short Blend Ratio
  };

/// @brief Input Data to PDPC20 IQ Algorithm
/// NOWHINE NC004c: Share code with system team
struct PDPC20IQInput
{
    VOID*                             pHWSetting;                               ///< Pointer to the HW Setting Object
    pdpc_2_0_0::chromatix_pdpc20Type* pChromatix;                               ///< Input Chromatix tunning data
    BOOL                              moduleEnable;                             ///< Module enable flag
    BOOL                              zzHDRModeEnable;                          ///< ISP or Sensor HDR mode
    SensorType                        sensorType;                               ///< Sensor type
    UINT32                            imageWidth;                               ///< Current Sensor Image Output Width
    UINT32                            imageHeight;                              ///< Current Sensor Image Output Height
    UINT32                            PDAFBlockWidth;                           ///< Current Sensor Image Output Width
    UINT32                            PDAFBlockHeight;                          ///< Current Sensor Image Output Height
    UINT32                            blackLevelOffset;                         ///< Black Level Offset
    UINT32                            PDAFPixelCount;                           ///< PD pixel count
    UINT32                            ZZHDRPattern;                             ///< zzHDR Pattern
    UINT16                            zzHDRFirstRBEXP;                          ///< zzHDR First RBEXP
    UINT16                            PDAFGlobaloffsetX;                        ///< Current Sensor Image Output Width
    UINT16                            PDAFGlobaloffsetY;                        ///< Current Sensor Image Output Height
    UINT16                            LUTBankSel;                               ///< LUT bank need for this frame.
    UINT8                             bayerPattern;                             ///< Bayer pattern
    FLOAT                             DRCGain;                                  ///< Input trigger value:  drcGain
    FLOAT                             exposureGainRatio;                        ///< Input trigger value:  exporsure gain Ratio
    FLOAT                             exposureTime;                             ///< Input trigger value:  exporsure time
    FLOAT                             AECSensitivity;                           ///< Input trigger value:  AEC sensitivity data
    FLOAT                             luxIndex;                                 ///< Input trigger value:  Lux index
    FLOAT                             realGain;                                 ///< Input trigger value:  Digital Gain Value
    FLOAT                             digitalGain;                              ///< Input trigger value:  Digital Gain Value
    FLOAT                             leftGGainWB;                              ///< G channel gain to adjust white balance
    FLOAT                             leftBGainWB;                              ///< B channel gain to adjust white balance
    FLOAT                             leftRGainWB;                              ///< R channel gain to adjust white balance
    PDPixelCoordinates                PDAFPixelCoords[MAX_CAMIF_PDAF_PIXELS];   ///< Pixel coordinates
    VOID*                             pInterpolationData;                       ///< input memory for interpolation data
    FLOAT                             bad_pixel_correction;                     ///< Slider Based Tuning
};


static const UINT32 BPSPDPC30DMILengthPDAF  = 64;                               ///< This value should match the size for
                                                                                ///< PDAFPDMask defined gicbpspdpc30setting.h
static const UINT32 BPSPDPC30DMILengthNoise = 64;                               ///< This value should match the size for
                                                                                ///< noiseStdLUTLevel0 defined in
                                                                                ///< gicbpspdpc30setting.h

/// @brief Input Data to PDPC30 IQ Algorithm
/// NOWHINE NC004c: Share code with system team
struct PDPC30IQInput
{
    VOID*                             pHWSetting;                               ///< Pointer to the HW Setting Object
    pdpc_3_0_0::chromatix_pdpc30Type* pChromatix;                               ///< Input Chromatix tunning data
    BOOL                              isPrepareStripeInputContext;              ///< Flag to indicate prepare striping
    BOOL                              moduleEnable;                             ///< Module enable flag
    BOOL                              bpcEnable;                                ///< BPC Module enable flag
    BOOL                              directionalBPCEnable;                     ///< directional BPC enable flag
    BOOL                              gicEnable;                                ///< GIC Module enable flag
    BOOL                              pdpcEnable;                               ///< PDPC Module enable flag
    BOOL                              zzHDRModeEnable;                          ///< ISP or Sensor HDR mode
    BOOL                              prevStateEnable;                          ///< Previous enable flag
    SensorType                        sensorType;                               ///< Sensor type
    UINT32                            blackLevelOffset;                         ///< Black Level Offset
    UINT32                            imageWidth;                               ///< Current Sensor Image Output Width
    UINT32                            imageHeight;                              ///< Current Sensor Image Output Height
    UINT32                            PDAFBlockWidth;                           ///< Current Sensor Image Output Width
    UINT32                            PDAFBlockHeight;                          ///< Current Sensor Image Output Height
    UINT16                            PDAFGlobaloffsetX;                        ///< Current Sensor Image Output Width
    UINT16                            PDAFGlobaloffsetY;                        ///< Current Sensor Image Output Height
    UINT16                            LUTBankSel;                               ///< LUT bank need for this frame.
    UINT16                            zzHDRFirstRBEXP;                          ///< zzHDR First RB EXP
    UINT16                            ZZHDRPattern;                             ///< zzHDR Pattern
    UINT8                             bayerPattern;                             ///< Bayer pattern
    FLOAT                             DRCGain;                                  ///< Input trigger value:  drcGain
    FLOAT                             exposureTime;                             ///< Input trigger value:  exporsure time
    FLOAT                             exposureGainRatio;                        ///< Input trigger value:  exposure gain ratio
    FLOAT                             AECSensitivity;                           ///< Input trigger value:  AEC sensitivity data
    FLOAT                             luxIndex;                                 ///< Input trigger value:  Lux index
    FLOAT                             realGain;                                 ///< Input trigger value:  Digital Gain Value
    FLOAT                             digitalGain;                              ///< Input trigger value:  Digital Gain Value
    FLOAT                             leftGGainWB;                              ///< G channel gain to adjust white balance
    FLOAT                             leftBGainWB;                              ///< B channel gain to adjust white balance
    FLOAT                             leftRGainWB;                              ///< R channel gain to adjust white balance
    PDPixelCoordinates                PDAFPixelCoords[MAX_CAMIF_PDAF_PIXELS];   ///< Pixel coordinates
    UINT32                            PDAFPixelCount;                           ///< PD pixel count
    VOID*                             pInterpolationData;                       ///< input memory for interpolation data
    NRMConfigData                     nrmCfgData;                               ///< NRM config data
};
static const UINT32 PDPC30DMILengthDword = 64;

/// @brief Input Data to TDL10 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct TDL10InputData
{
    VOID*                           pHWSetting;          ///< Pointer to the HW Setting Object
    tdl_1_0_0::chromatix_tdl10Type* pChromatix;          ///< Input Chromatix tunning data
    FLOAT                           DRCGain;             ///< Input trigger value:  drcGain
    FLOAT                           exposureTime;        ///< Input trigger value:  exposure time
    FLOAT                           exposureGainRatio;   ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;      ///< Input trigger value:  AEC sensitivity data
    FLOAT                           LEDTrigger;          ///< Input trigger value:  LED Trigger
    FLOAT                           LEDFirstEntryRatio;  ///< Ratio of Dual LED
    UINT16                          numberOfLED;         ///< Number of LED
    FLOAT                           luxIndex;            ///< Input trigger value:  Lux index
    FLOAT                           CCTTrigger;          ///< Input trigger value:  CCT
    FLOAT                           realGain;            ///< Input trigger value:  real Gain Value
    FLOAT                           AECShortSensitivity; ///< AEC short sensitivity
    VOID*                           pInterpolationData;  ///< input memory for interpolation data
};

/// @brief Input Data ASF30 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct ASF30InputData
{
    VOID*                           pHWSetting;                  ///< Pointer to the HW Setting Object
    asf_3_0_0::chromatix_asf30Type* pChromatix;                  ///< Input Chromatix tunning data
    BOOL                            moduleEnable;                ///< Module enable flag
    FLOAT                           totalScaleRatio;             ///< Input trigger value:  Totalscale ratio
    FLOAT                           DRCGain;                     ///< Input trigger value:  DRC Gain
    FLOAT                           exposureGainRatio;           ///< exposure Gain Ratio
    FLOAT                           exposureTime;                ///< Input trigger value:  exporsure time
    FLOAT                           AECSensitivity;              ///< Input trigger value:  AEC sensitivity data
    FLOAT                           luxIndex;                    ///< Input trigger value:  Lux index
    FLOAT                           digitalGain;                 ///< Input trigger value:  Digital Gain Value
    UINT16                          edgeAlignEnable;             ///< edge alignment enable flag
    UINT16                          layer1Enable;                ///< layer1 enable flag
    UINT16                          layer2Enable;                ///< layer2 enable flag
    UINT16                          contrastEnable;              ///< contrast enable flag
    UINT16                          radialEnable;                ///< rnr enable flag
    UINT32                          chYStreamInWidth;            ///< Sensor output Channel Y stream in width
    UINT32                          chYStreamInHeight;           ///< Sensor output Channel Y stream in height
    struct FDData*                  pFDData;                      ///< Pointer to FD Data
    UINT16                          faceHorzOffset;              ///< face horizontal offset
    UINT16                          faceVertOffset;              ///< face vertical offset
    UINT8                           specialEffectAbsoluteEnable; ///< special effect; 0 for Emboss,
                                                                 ///< 1: for Sketch when negAbsY1 is 1
                                                                 ///< 1: for Neon when negAbsY1 is 0
    UINT8                           negateAbsoluteY1;            ///< special effect; see comments for spEffAbsen
    UINT8                           specialEffectEnable;         ///< special effect enable flag; 1 to enable special effect
    UINT8                           specialPercentage;           ///< special percentage
    UINT8                           smoothPercentage;            ///< smooth percentage
    UINT8                           nonZero[8];                  ///< non-zero flag; 0x1A90 for special effect
    UINT32                          sensorOffsetX;               ///< Current Sensor Image Output horizontal offset
    UINT32                          sensorOffsetY;               ///< Current Sensor Image Output vertical offset
    UINT32                          edgeMode;                    ///< sharpness control
    FLOAT                           sharpness;                   ///< sharpness strength
    VOID*                           pInterpolationData;          ///< input memory for interpolation data
    VOID*                           pWarpGeometriesOutput;       ///< Pointer to Warp Geometries Output
    BOOL                            sliderEnable;                ///< Slider Based Tuning
    FLOAT                           sharpness_sl;                ///< Slider Based Tuning
    VOID*                           pDefogInterpolationData;     ///< Defog interpolation data
};

/// @brief Input Data ASF30 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct ASF32InputData
{
    VOID*                           pHWSetting;                  ///< Pointer to the HW Setting Object
    asf_3_2_0::chromatix_asf32Type* pChromatix;                  ///< Input Chromatix tunning data
    BOOL                            moduleEnable;                ///< Module enable flag
    BOOL                            prevStateEnable;             ///< Previous enable flag
    FLOAT                           totalScaleRatio;             ///< Input trigger value:  Totalscale ratio
    FLOAT                           DRCGain;                     ///< Input trigger value:  DRC Gain
    FLOAT                           exposureTime;                ///< Input trigger value:  exporsure time
    FLOAT                           exposureGainRatio;                ///< Input trigger value:  exporsure gain Ratio
    FLOAT                           AECSensitivity;              ///< Input trigger value:  AEC sensitivity data
    FLOAT                           luxIndex;                    ///< Input trigger value:  Lux index
    FLOAT                           digitalGain;                 ///< Input trigger value:  Digital Gain Value
    UINT16                          contrastEnable;              ///< contrast enable flag
    UINT16                          radialEnable;                ///< rnr enable flag
    UINT32                          chYStreamInWidth;            ///< Sensor output Channel Y stream in width
    UINT32                          chYStreamInHeight;           ///< Sensor output Channel Y stream in height
    UINT8                           specialEffectAbsoluteEnable; ///< special effect; 0 for Emboss,
                                                                 ///< 1: for Sketch when negAbsY1 is 1
                                                                 ///< 1: for Neon when negAbsY1 is 0
    UINT8                           negateAbsoluteY1;            ///< special effect; see comments for spEffAbsen
    UINT8                           specialEffectEnable;         ///< special effect enable flag; 1 to enable special effect
    UINT8                           specialPercentage;           ///< special percentage
    UINT8                           smoothPercentage;            ///< smooth percentage
    UINT8                           nonZero[8];                  ///< non-zero flag; 0x1A90 for special effect
    UINT32                          sensorOffsetX;               ///< Current Sensor Image Output horizontal offset
    UINT32                          sensorOffsetY;               ///< Current Sensor Image Output vertical offset
    UINT32                          edgeMode;                    ///< sharpness control
    FLOAT                           sharpness;                   ///< sharpness strength
    VOID*                           pInterpolationData;          ///< input memory for interpolation data
    VOID*                           pWarpGeometriesOutput;       ///< Pointer to Warp Geometries Output Structure
};
/// @brief Input Data to CV12 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct CV12InputData
{
    VOID*                               pHWSetting;         ///< Pointer to the HW Setting Object
    cv_1_2_0::chromatix_cv12Type*       pChromatix;          ///< Input Chromatix tunning data
    FLOAT                               DRCGain;             ///< Input trigger value:  DRCGain
    FLOAT                               exposureTime;        ///< Input trigger value:  exporsure time
    FLOAT                               exposureGainRatio;   ///< Input trigger value:  exposure gain ratio
    FLOAT                               AECSensitivity;      ///< Input trigger value:  AEC sensitivity data
    FLOAT                               luxIndex;            ///< Input trigger value:  Lux index
    FLOAT                               LEDTrigger;;         ///< Input trigger value:  LED Trigger
    FLOAT                               LEDFirstEntryRatio;  ///< Ratio of Dual LED
    UINT16                              numberOfLED;         ///< Number of LED
    FLOAT                               colorTemperature;    ///< Input trigger value:  cct
    FLOAT                               digitalGain;         ///< Input trigger value:  Digital Gain Value
    VOID*                               pInterpolationData;  ///< input memory for interpolation data
    FLOAT                               stretch_factor;      ///< Input trigger value:  Aec Histogram scale factor
    FLOAT                               clamp;               ///< Input trigger value:  Aec Histogram offset
    UINT32                              chipsetVersion;      ///< Titan version
    FLOAT                               saturation_sl;       ///< Slider Based Tuning
    VOID*                               pDefogInterpolationData;     ///< Defog interpolation data
};

/// @brief Input Data to Gamma15 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct Gamma15InputData
{
    VOID*                               pHWSetting;                ///< Pointer to the HW Setting Object
    gamma_1_5_0::chromatix_gamma15Type* pChromatix;                ///< Input Chromatix tunning data
    FLOAT                               DRCGain;                   ///< Input trigger value:  drcGain
    FLOAT                               exposureTime;              ///< Input trigger value:  exporsure time
    FLOAT                               exposureGainRatio;         ///< Input trigger value:  exposure gain ratio
    FLOAT                               AECSensitivity;            ///< Input trigger value:  AEC sensitivity data
    FLOAT                               luxIndex;                  ///< Input trigger value:  Lux index
    FLOAT                               LEDTrigger;                ///< Input trigger value:  LED Trigger
    FLOAT                               LEDFirstEntryRatio;        ///< Ratio of Dual LED
    UINT16                              numberOfLED;               ///< Number of LED
    FLOAT                               colorTemperature;          ///< Input trigger value:  AWB colorTemperature
    FLOAT                               digitalGain;               ///< Input trigger value:  Digital Gain Value
    FLOAT                               tone;                      ///< Variable to control DynamicGamma. Default value 0.0
    VOID*                               pLibData;                  ///< Pointer to the library specific data
    UINT8                               contrastLevel;             ///< contrast level pass down by APP
    BOOL                                isHDR10Mode;               ///< HDR10 mode check
    UseCaseMode                         usecaseMode;               ///< Use case Mode
    VOID*                               pInterpolationData;        ///< input memory for interpolation data
    UINT32                              chipsetVersion;            ///< Titan version
    BOOL                                isAIDEenabled;             ///< Flag to indicate AIDE is enabled/Disabled
    VOID*                               pDefogInterpolationData;   ///< defog interpolation data
    VOID*                               pGammaChannelData;          ///< Gamma channel data
};

/// @brief Input Data to SCE11 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct SCE11InputData
{
    VOID*                           pHWSetting;          ///< Pointer to the HW Setting Object
    sce_1_1_0::chromatix_sce11Type* pChromatix;          ///< Input Chromatix tunning data
    FLOAT                           DRCGain;             ///< Input trigger value:  DRCGain
    FLOAT                           exposureTime;        ///< Input trigger value:  exporsure time
    FLOAT                           exposureGainRatio;   ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;      ///< Input trigger value:  AEC sensitivity data
    FLOAT                           luxIndex;            ///< Input trigger value:  Lux index
    FLOAT                           realGain;            ///< Input trigger value:  Real Gain Value
    FLOAT                           leftGGainWB;         ///< G channel gain to adjust white balance
    FLOAT                           leftBGainWB;         ///< B channel gain to adjust white balance
    FLOAT                           leftRGainWB;         ///< R channel gain to adjust white balanc
    FLOAT                           CCTTrigger;          ///< Input trigger value:  AWB colorTemperature
    VOID*                           pInterpolationData;  ///< input memory for interpolation data
};

/// @brief Input Data to CAC22 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct CAC22InputData
{
    VOID*                           pHWSetting;         ///< Pointer to the HW Setting Object
    cac_2_2_0::chromatix_cac22Type* pChromatix;         ///< Input Chromatix tunning data
    FLOAT                           totalScaleRatio;    ///< Input trigger value:  Total Scale Ratio
    FLOAT                           luxIndex;           ///< Input trigger value:  Lux index
    FLOAT                           digitalGain;        ///< Input trigger value:  Digital Gain Value
    UINT32                          enableSNR;          ///< SNR enable flag
    UINT32                          resolution;         ///< 0 - 420 proessing 1 - 422 processing
    VOID*                           pInterpolationData; ///< input memory for interpolation data
};

/// @brief Input Data to CAC23 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct CAC23InputData
{
    VOID*                           pHWSetting;         ///< Pointer to the HW Setting Object
    cac_2_3_0::chromatix_cac23Type* pChromatix;         ///< Input Chromatix tunning data
    FLOAT                           totalScaleRatio;    ///< Input trigger value:  Total Scale Ratio
    FLOAT                           luxIndex;           ///< Input trigger value:  Lux index
    FLOAT                           digitalGain;        ///< Input trigger value:  Digital Gain Value
    UINT32                          enableSNR;          ///< SNR enable flag
    UINT32                          resolution;         ///< 0 - 420 proessing 1 - 422 processing
    VOID*                           pInterpolationData; ///< input memory for interpolation data
};

/// @brief Input Data to TF10 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct TF10InputData
{
    VOID*                         pHWSetting;                   ///< Pointer to the HW Setting Object
    tf_1_0_0::chromatix_tf10Type* pChromatix;                   ///< Chromatix data for TF10 module
    BOOL                          moduleEnable;                 ///< Module enable flag
    BOOL                          bypassMode;                   ///< Bypass ANR and TF
    FLOAT                         lensPosition;                 ///< Input trigger value:  Lens position
    FLOAT                         lensZoom;                     ///< Input trigger value:  Lens Zoom
    FLOAT                         postScaleRatio;               ///< Input trigger value:  Post scale ratio
    FLOAT                         preScaleRatio;                ///< Input trigger value:  Pre scale ratio
    FLOAT                         DRCGain;                      ///< Input trigger value:  drcGain
    FLOAT                         exposureTime;                 ///< Input trigger value:  exposure time
    FLOAT                         exposureGainRatio;            ///< Input trigger value:  exposure gain ratio
    FLOAT                         AECSensitivity;               ///< Input trigger value:  AEC sensitivity data
    FLOAT                         luxIndex;                     ///< Input trigger value:  Lux index
    FLOAT                         AECGain;                      ///< Input trigger value:  AEC Gain Value
    FLOAT                         CCTTrigger;                   ///< Input trigger value:  AWB colorTemperature
    FLOAT                         upscalingFactorMFSR;          ///< MFSR upscaling factor
    FLOAT                         tfIntensity;                 ///< Input trigger value:  TNR Strength
    FLOAT                         tfMotionSensitivity;         ///< Input trigger value:  TNR Sensitivity
    FLOAT                         tfBlendStrength;             ///< Input trigger value:  TNR BlendStrength
    UINT32                        fullPassIcaOutputFrameWidth;  ///< Full Pass ICA Output Frame width
    UINT32                        fullPassIcaOutputFrameHeight; ///< Full Pass ICA Ouput Frame Height
    UINT32                        maxUsedPasses;                ///< Maximum passes used in the request
    UINT32                        frameNum;                     ///< Current frame number
    UINT32                        mfFrameNum;                   ///< MFNR/MFSR blending frame number
    UINT32                        numOfFrames;                  ///< Number of frames for MFNR/MFSR use case
    UINT32                        perspectiveConfidence;        ///< Transform confidence
    UINT32                        digitalZoomStartX;            ///< Digital zoom horizontal start offset
    UINT32                        digitalZoomStartY;            ///< Digital zoom vertical start offset
    UINT8                         hasTFRefInput;                ///< 1: has reference input, 0: no reference input
    UINT8                         isDigitalZoomEnabled;         ///< 1: digital zoom enable, 0: disable
    UINT8                         useCase;                      ///< UseCase: Video, Still
    UINT8                         configMF;                     ///< Multiframe Config: Prefilter or Temporal
    VOID*                         pWarpGeometriesOutput;        ///< Pointer to Warp Geometries Output Structure
    VOID*                         pRefinementParameters;        ///< Pointer to refinement parameters
    VOID*                         pTFParameters;                ///< Pointer to TF parameters
    VOID*                         pLMCParameters;               ///< Pointer to TF LMC parameters
    VOID*                         pInterpolationData;           ///< input memory  for chromatix interpolation data
    VOID*                         pNCChromatix;                 ///< Chromatix Input pointer
    UINT32                        maxSupportedPassesForUsecase; ///< Maximum supported passes for usecase
    NRMConfigData                 nrmCfgData;                   ///< NRM config data
    BOOL			              sliderEnable;			///< Slider Based Tuning
    FLOAT                         multi_frame;                  ///< Slider Based Tuning
    FLOAT                         ghost_removal;                ///< Slider Based Tuning
    VOID*                         pLEFInterpolationData;        ///< input memory for chromatix interpolation data of LEF
    VOID*                         pAdaptiveTF;                  ///< Adaptive TF module
    gamma_1_6_0::gamma16_rgn_dataType    interpolatedBPSGamma;  ///< Interpolated gamma value in BPS
    gamma_1_5_0::gamma15_rgn_dataType    interpolatedIPEGamma;  ///< Interpolated gamma value in IPE
    ParsedHDRBHistStatsOutput    BPSHDRBhist;                   ///< BHist statistics from HDR
};

/// @brief Input Data to TF20 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct TF20InputData
{
    VOID*                               pHWSetting;                   ///< Pointer to the HW Setting Object
    tf_2_0_0::chromatix_tf20Type*       pChromatix;                   ///< Chromatix data for TF10 module
    nradj_1_0_0::chromatix_nradj10Type* pNRAjust10Chromatix;          ///< Input Chromatix tunning data for NR adjust
    BOOL                                moduleEnable;                 ///< Module enable flag
    BOOL                                bypassMode;                   ///< Bypass ANR and TF
    FLOAT                               lensPosition;                 ///< Input trigger value:  Lens position
    FLOAT                               lensZoom;                     ///< Input trigger value:  Lens Zoom
    FLOAT                               postScaleRatio;               ///< Input trigger value:  Post scale ratio
    FLOAT                               preScaleRatio;                ///< Input trigger value:  Pre scale ratio
    FLOAT                               DRCGain;                      ///< Input trigger value:  drcGain
    FLOAT                               exposureTime;                 ///< Input trigger value:  exposure time
    FLOAT                               exposureGainRatio;            ///< Input trigger value:  exposure gain ratio
    FLOAT                               AECSensitivity;               ///< Input trigger value:  AEC sensitivity data
    FLOAT                               luxIndex;                     ///< Input trigger value:  Lux index
    FLOAT                               AECGain;                      ///< Input trigger value:  AEC Gain Value
    FLOAT                               CCTTrigger;                   ///< Input trigger value:  AWB colorTemperature
    FLOAT                               upscalingFactorMFSR;          ///< MFSR upscaling factor
    FLOAT                               tfIntensity;                 ///< Input trigger value:  TNR Strength
    FLOAT                               tfMotionSensitivity;         ///< Input trigger value:  TNR Sensitivity
    FLOAT                               tfBlendStrength;             ///< Input trigger value:  TNR BlendStrength
    UINT32                              fullPassIcaOutputFrameWidth;  ///< Full Pass ICA Output Frame width
    UINT32                              fullPassIcaOutputFrameHeight; ///< Full Pass ICA Ouput Frame Height
    UINT32                              maxUsedPasses;                ///< Maximum passes used in the request
    UINT32                              frameNum;                     ///< Current frame number
    UINT32                              mfFrameNum;                   ///< MFNR/MFSR blending frame number
    UINT32                              numOfFrames;                  ///< Number of frames for MFNR/MFSR use case
    UINT32                              perspectiveConfidence;        ///< Transform confidence
    UINT32                              digitalZoomStartX;            ///< Digital zoom horizontal start offset
    UINT32                              digitalZoomStartY;            ///< Digital zoom vertical start offset
    UINT8                               hasTFRefInput;                ///< 1: has reference input, 0: no reference input
    UINT8                               isDigitalZoomEnabled;         ///< 1: digital zoom enable, 0: disable
    UINT8                               useCase;                      ///< UseCase: Video, Still
    UINT8                               configMF;                     ///< Multiframe Config: Prefilter or Temporal
    VOID*                               pWarpGeometriesOutput;        ///< Pointer to Warp Geometries Output Structure
    VOID*                               pRefinementParameters;        ///< Pointer to refinement parameters
    VOID*                               pTFParameters;                ///< Pointer to TF parameters
    VOID*                               pInterpolationData;           ///< input memory  for chromatix interpolation data
    VOID*                               pNCChromatix;                 ///< Chromatix Input pointer
    VOID*                               pLMCParameters;               ///< Pointer to Lmc parameterps
    UINT32                              maxSupportedPassesForUsecase; ///< Maximum supported passes for usecase
    BOOL                                enableNRAdjust;               ///< Enable NR adjust flag from Chromatix
    VOID*                               pGTMInfo;                     ///< Pointer to GTM info from IFE/BPS
    NRAdjust_TFData*                    pNRadjustPrev;                ///< Pointer to previous parameters of noise adjustment
    NRAdjust_TFData*                    pNRadjustCurr;                ///< Pointer to current parameters of noise adjustment
    BOOL                                runExtraPass;                 ///< Flag to determine if extrapass needs to be run
    BOOL                                runExtraPassWithBlend;        ///< Flag to determine if extrapass with blending needed
    UINT8                               extraPassBlendFactor;         ///< Extra pass blend factor
    NRMConfigData                       nrmCfgData;                   ///< NRM config data
    VOID*                               pLEFInterpolationData;        ///< input memory for chromatix interpolation data of LEF
    VOID*                               pAdaptiveTF;                  ///< Adaptive TF module
    gamma_1_6_0::gamma16_rgn_dataType   interpolatedBPSGamma;         ///< Interpolated gamma value in BPS
    gamma_1_5_0::gamma15_rgn_dataType   interpolatedIPEGamma;         ///< Interpolated gamma value in IPE
    ParsedHDRBHistStatsOutput           BPSHDRBhist;                  ///< BHist statistics from HDR
};

/// @brief Input Data ICA IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct ICAInputData
{
    VOID*                            pHWSetting;                          ///< Pointer to the HW Setting Object
    UINT32                           titanVersion;                        ///< titan version
    UINT32                           hwVersion;                           ///< hardware version
    UINT32                           icaVersion;                          ///< ICA version
    UINT32                           instanceID;                          ///< instance ID
    UINT32                           opticalCenterX;                      ///< optical center X
    UINT32                           opticalCenterY;                      ///< optical center Y
    UINT64                           frameNum;                            ///< frame number
    UINT                             IPEPath;                             ///< IPE path indicating input or reference.
    UINT                             configMode;                          ///< ICA Configuration Mode
    UINT                             ICAIntransformConfidence;            ///< Transform Confidence
    UINT                             ICAReftransformConfidence;           ///< Transform Confidence
    BOOL                             byPassAlignmentMatrixAdjustement;    ///< Flag to byPass Alignment Matrix Adjustement
    ConfigICAMode                    ICAMode;                             ///< ICA mode
    BOOL                             dumpICAOut;                          ///< Dump for ICA transforms
    BOOL                             isGridFromChromatixEnabled;          ///< Flag to check if grid from chromatix is enabled
    FLOAT                            luxIndex;                            ///< Input trigger value:  Lux index
    FLOAT                            digitalGain;                         ///< Input trigger value:  Digital Gain Value
    FLOAT                            lensZoomRatio;                       ///< Lens Zoom ratio
    FLOAT                            lensPosition;                        ///< Lens Position
    struct FDData*                   pFDData;                             ///< Pointer to FD Data
    struct _ImageDimensions*         pImageDimensions;                    ///< Input dimension
    struct _ImageDimensions*         pMarginDimensions;                   ///< Margin related dimension
    struct _ImageDimensions*         pFullImageDimensions;                ///< Full Input Image dimension
    struct _WindowRegion*            pZoomWindow;                         ///< Zoom Window
    struct _WindowRegion*            pIFEZoomWindow;                      ///< IFE Zoom Window
    VOID*                            pNCChromatix;                        ///< Chromatix pointer required for NCLib
    VOID*                            pNCChromatixGrid;                    ///< Chromatix grid pointer required for NcLib
    VOID*                            pLdcChromatix;                       ///< LDC Chromatix pointer
    VOID*                            pNCRegData;                          ///< ICA Reg Pointer

    VOID*                            pCurrICAInData;                      ///< ICA input current frame Data
    VOID*                            pPrevICAInData;                      ///< ICA input previous frame Data
    VOID*                            pCurrICAInStabData;                  ///< ICA input stabilize current frame Data
    VOID*                            pPrevICAInStabData;                  ///< ICA input previous stabilize frame Data
    VOID*                            pCurWarpAssistStabData;              ///< Current stabilize Warp assist data
    VOID*                            pPrevWarpAssistStabData;             ///< Previous stabilize Warp assist data

    VOID*                            pCurrICARefData;                     ///< ICA reference current frame Data
    VOID*                            pAlignmentFusionInputData;           ///< MCTF alignement fusion input data
    VOID*                            pAlignmentFusionOutputData;          ///< MCTF alignement fusion output data
    VOID*                            pICATempWarpData;                    ///< ICA temp warp filled by ica setting

    VOID*                            pWarpGeomOut;                        ///< Warp Geometry Output buffer pointer
    VOID*                            pInterpolationParamters;             ///< Interpolation Parameters
    VOID*                            pInterpolationData;                  ///< input memory for interpolation data
    VOID*                            pLdcInterpolationData;               ///< input memory for interpolation data
    VOID*                            pChromatix;                          ///< Input Chromatix tunning data
    VOID*                            pIcaGeolibInputData;                 ///< Input data for geolib
    VOID*                            pCVPICAIntermediateBuffer;           ///< Intermediate buffer used for cvp ica calculation
    NcLibWarpImageDomain             imageMCTFAlignmentMatrixDomain;      ///< Image MCTF alignment domain
    NcLibWarpImageDomain             gyroMCTFAlignmentMatrixDomain;       ///< Gyro MCTF alignment domain
    NcLibWarpImageDomain             faceDetectionDomain;                 ///< Face detection domain
    BOOL                             gmoStatsCalcNeeded;                  ///< Flag for whether GMO statics need calculation
};

/// @brief Input Data to ANR10 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct ANR10InputData
{
    // Need to modify it as ANR Input data.
    VOID*                               pHWSetting;                   ///< Pointer to the HW Setting Object
    anr_1_0_0::chromatix_anr10Type*     pChromatix;                   ///< Chromatix data for ANR10 module
    nradj_1_0_0::chromatix_nradj10Type* pNRAjust10Chromatix;          ///< Input Chromatix tunning data for NR adjust
    FLOAT                               lensPosition;                 ///< Input trigger value:  Lens position
    FLOAT                               lensZoom;                     ///< Input trigger value:  Lens Zoom
    FLOAT                               postScaleRatio;               ///< Input trigger value:  Post scale ratio
    FLOAT                               preScaleRatio;                ///< Input trigger value:  Pre scale ratio
    FLOAT                               DRCGain;                      ///< Input trigger value:  drcGain
    FLOAT                               exposureTime;                 ///< Input trigger value:  exposure time
    FLOAT                               exposureGainRatio;            ///< Input trigger value:  exposure gain ratio
    FLOAT                               AECSensitivity;               ///< Input trigger value:  AEC sensitivity data
    FLOAT                               luxIndex;                     ///< Input trigger value:  Lux index
    FLOAT                               AECGain;                      ///< Input trigger value:  AEC Gain Value
    FLOAT                               CCTTrigger;                   ///< Input trigger value:  AWB colorTemperature
    FLOAT                               anrIntensity;                 ///< Input trigger value:  ANR Strength
    FLOAT                               anrMotionSensitivity;         ///< Input trigger value:  ANR Sensitivity
    VOID*                               pWarpGeometriesOutput;        ///< Pointer to Warp Geometries Output Structure
    UINT32                              numPasses;                    ///< Pass number
    UINT64                              frameNum;                     ///< Current frame number
    UINT32                              bitWidth;                     ///< format bitwidth
    UINT32                              opticalCenterX;               ///< Optical center X
    UINT32                              opticalCenterY;               ///< Optical Center Y
    struct FDData*                      pFDData;                      ///< Pointer to FD Data
    struct _ImageDimensions*            pImageDimensions;             ///< Input dimension
    struct _ImageDimensions*            pMarginDimensions;            ///< Margin related dimension
    VOID*                               pANRParameters;               ///< Pointer to ANR parameters
    VOID*                               pInterpolationData;           ///< input memory  for chromatix interpolation data
    VOID*                               pNCChromatix;                 ///< Chromatix Input pointer
    BOOL                                validateANRSettings;          ///< Validate ANR register settings
    BOOL                                realtimeFlag;                 ///< Validate ANR register settings
    UINT32                              maxSupportedPassesForUsecase; ///< Maximum supported passes for usecase
    BOOL                                enableNRAdjust;               ///< Enable NR adjust flag from Chromatix
    VOID*                               pGTMInfo;                     ///< Pointer to ANR parameters
    NRAdjust_ANRData*                   pNRadjustPrev;                ///< Pointer to previous parameters of noise adjustment
    NRAdjust_ANRData*                   pNRadjustCurr;                ///< Pointer to current parameters of noise adjustment
    NRMConfigData                       nrmCfgData;                   ///< NRM config data
    BOOL                                sliderEnable;                 ///< Slider Based tuning
    FLOAT                               luma_denoising;               ///< Slider Based Tuning
    FLOAT                               chroma_denoising;             ///< Slider Based Tuning
    VOID*                               pDefogANRdata;                ///< Defog ANR data
    VOID*                               pLEFInterpolationData;        ///< input memory for chromatix interpolation data of LEF
    VOID*                               pAdaptiveANR;                 ///< Adaptive ANR module
    gamma_1_6_0::gamma16_rgn_dataType   interpolatedBPSGamma;         ///< Interpolated gamma value in BPS
    gamma_1_5_0::gamma15_rgn_dataType   interpolatedIPEGamma;         ///< Interpolated gamma value in IPE
};

/// @brief Input Data to LTM13 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct LTM13InputData
{
    VOID*                           pHWSetting;                    ///< Pointer to the HW Setting Object
    ltm_1_3_0::chromatix_ltm13Type* pChromatix;                    ///< Input Chromatix tunning data
    UINT32                          imageWidth;                    ///< Current Sensor Image Output Width
    UINT32                          imageHeight;                   ///< Current Sensor Image Output Height
    FLOAT                           DRCGain;                       ///< Input trigger value: DRCGain
    FLOAT                           DRCGainDark;                   ///< Input trigger value: DRCGainDark
    FLOAT                           exposureTime;                  ///< Input trigger value: exporsure time
    FLOAT                           exposureGainRatio;             ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;                ///< Input trigger value: AEC sensitivity data
    FLOAT                           luxIndex;                      ///< Input trigger value: Lux index
    FLOAT                           exposureIndex;                 ///< Exposure Index, Same as Lux index for non ADRC cases
    FLOAT                           prevExposureIndex;             ///< Exposure Index of Previous Frame
    FLOAT                           realGain;                      ///< Input trigger value: Real Gain Value
    FLOAT                           ltmDarkBoostStrength;          ///< Input trigger value: Dark Boost Strength
    FLOAT                           ltmBrightSupressStrength;      ///< Input trigger value: Bright Supress Strength
    FLOAT                           ltmLceStrength;                ///< Input trigger value: Local Contrast Strength
    INT32*                          pGammaPrev;                    ///< Ptr to cached version of InverseGamma() input gamma
    INT32*                          pIGammaPrev;                   ///< Ptr to cached version of InverseGamma() output igamma
    FLOAT                           gammaOutput[65];               ///< Gamma ouput from gamma15 module
    ADRCData*                       pAdrcInputData;                ///< Pointer to the input data for adrc algo calculation.
    VOID*                           pInterpolationData;            ///< input memory for interpolation data
    FLOAT                           gamma15Output[257];            ///< Gamma output from gamma15 module
    ParsedHDRBHistStatsOutput*      pParsedHDRBHistStats;          ///< HDR BHIST stats
    ParsedHDRBEStatsOutput*         pParsedHDRBEStats;             ///< HDR BE stats
    FLOAT                           contrast;                      ///< Slider Based Tuning
    VOID*                           pDefogInterpolationData;       ///< defog interpolation data
};

/// @brief Input Data to LTM14 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct LTM14InputData
{
    VOID*                           pHWSetting;                    ///< Pointer to the HW Setting Object
    ltm_1_4_0::chromatix_ltm14Type* pChromatix;                    ///< Input Chromatix tunning data
    UINT32                          imageWidth;                    ///< Current Sensor Image Output Width
    UINT32                          imageHeight;                   ///< Current Sensor Image Output Height
    FLOAT                           DRCGain;                       ///< Input trigger value: DRCGain
    FLOAT                           DRCGainDark;                   ///< Input trigger value: DRCGainDark
    FLOAT                           exposureTime;                  ///< Input trigger value: exporsure time
    FLOAT                           exposureGainRatio;             ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;                ///< Input trigger value: AEC sensitivity data
    FLOAT                           luxIndex;                      ///< Input trigger value: Lux index
    FLOAT                           exposureIndex;                 ///< Exposure Index, Same as Lux index for non ADRC cases
    FLOAT                           prevExposureIndex;             ///< Exposure Index of Previous Frame
    FLOAT                           realGain;                      ///< Input trigger value: Real Gain Value
    INT32*                          pGammaPrev;                    ///< Ptr to cached version of InverseGamma() input gamma
    INT32*                          pIGammaPrev;                   ///< Ptr to cached version of InverseGamma() output igamma
    FLOAT                           gammaOutput[65];               ///< Gamma ouput from gamma15 module
    ADRCData*                       pAdrcInputData;                ///< Pointer to the input data for adrc algo calculation.
    VOID*                           pInterpolationData;            ///< input memory for interpolation data
    UINT16                          first_pixel;                   ///< Pre-crop parameters
    UINT16                          last_pixel;                    ///< Pre-crop parameters
    UINT16                          first_line;                    ///< Pre-crop parameters
    UINT16                          last_line;                     ///< Pre-crop parameters
    BOOL                            LTM_data_collect_enable;       ///< DC enable
    INT16                           LTM_Dc_Pass;                   ///< DC pass
    FLOAT                           gamma15Output[257];            ///< Gamma output from gamma15 module
    ParsedHDRBHistStatsOutput*      pParsedHDRBHistStats;          ///< HDR BHIST stats
    ParsedHDRBEStatsOutput*         pParsedHDRBEStats;             ///< HDR BE stats
};

/// @brief Input Data to LTM15 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct LTM15InputData
{
    VOID*                           pHWSetting;                    ///< Pointer to the HW Setting Object
    ltm_1_5_0::chromatix_ltm15Type* pChromatix;                    ///< Input Chromatix tunning data
    UINT32                          imageWidth;                    ///< Current Sensor Image Output Width
    UINT32                          imageHeight;                   ///< Current Sensor Image Output Height
    FLOAT                           DRCGain;                       ///< Input trigger value: DRCGain
    FLOAT                           DRCGainDark;                   ///< Input trigger value: DRCGainDark
    FLOAT                           exposureTime;                  ///< Input trigger value: exporsure time
    FLOAT                           exposureGainRatio;                  ///< Input trigger value: exporsure gain ratio
    FLOAT                           AECSensitivity;                ///< Input trigger value: AEC sensitivity data
    FLOAT                           luxIndex;                      ///< Input trigger value: Lux index
    FLOAT                           exposureIndex;                 ///< Exposure Index, Same as Lux index for non ADRC cases
    FLOAT                           prevExposureIndex;             ///< Exposure Index of Previous Frame
    FLOAT                           realGain;                      ///< Input trigger value: Real Gain Value
    INT32*                          pGammaPrev;                    ///< Ptr to cached version of InverseGamma() input gamma
    INT32*                          pIGammaPrev;                   ///< Ptr to cached version of InverseGamma() output igamma
    FLOAT                           gammaOutput[65];               ///< Gamma ouput from gamma15 module
    ADRCData*                       pAdrcInputData;                ///< Pointer to the input data for adrc algo calculation.
    VOID*                           pInterpolationData;            ///< input memory for interpolation data
    UINT16                          first_pixel;                   ///< Pre-crop parameters
    UINT16                          last_pixel;                    ///< Pre Crop Parameters
    UINT16                          first_line;                    ///< Pre Crop Parameters
    UINT16                          last_line;                     ///< Pre Crop Parameters
    BOOL                            LTM_data_collect_enable;       ///< LTM data collect enable
    INT16                           LTM_Dc_Pass;                   ///< LTM DC pass
};

/// @brief Input Data to LTM16 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct LTM16InputData
{
    VOID*                           pHWSetting;                    ///< Pointer to the HW Setting Object
    ltm_1_6_0::chromatix_ltm16Type* pChromatix;                    ///< Input Chromatix tunning data
    UINT32                          imageWidth;                    ///< Current Sensor Image Output Width
    UINT32                          imageHeight;                   ///< Current Sensor Image Output Height
    FLOAT                           DRCGain;                       ///< Input trigger value: DRCGain
    FLOAT                           DRCGainDark;                   ///< Input trigger value: DRCGainDark
    FLOAT                           exposureTime;                  ///< Input trigger value: exporsure time
    FLOAT                           exposureGainRatio;             ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;                ///< Input trigger value: AEC sensitivity data
    FLOAT                           luxIndex;                      ///< Input trigger value: Lux index
    FLOAT                           exposureIndex;                 ///< Exposure Index, Same as Lux index for non ADRC cases
    FLOAT                           prevExposureIndex;             ///< Exposure Index of Previous Frame
    FLOAT                           realGain;                      ///< Input trigger value: Real Gain Value
    INT32*                          pGammaPrev;                    ///< Ptr to cached version of InverseGamma() input gamma
    INT32*                          pIGammaPrev;                   ///< Ptr to cached version of InverseGamma() output igamma
    FLOAT                           gammaOutput[65];               ///< Gamma ouput from gamma15 module
    ADRCData*                       pAdrcInputData;                ///< Pointer to the input data for adrc algo calculation.
    VOID*                           pInterpolationData;            ///< input memory for interpolation data
    UINT16                          first_pixel;                   ///< Pre-crop parameters
    UINT16                          last_pixel;                    ///< Pre-crop parameters
    UINT16                          first_line;                    ///< Pre-crop parameters
    UINT16                          last_line;                     ///< Pre-crop parameters
    BOOL                            LTM_data_collect_enable;       ///< DC enable
    INT16                           LTM_Dc_Pass;                   ///< DC pass
};

// Constant values that are used by TMC
static const UINT32 Gamma15TableSize    = 257;
static const FLOAT  GAMMA15_MAX_VALUE   = 1024.0f;

/// @brief Input Data to TMC13 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct TMC13InputData
{
    tmc_1_3_0::chromatix_tmc13Type* pChromatix;                         ///< Input Chromatix tunning data
    FLOAT                           DRCGain;                            ///< Input trigger value:  DRCGain
    FLOAT                           DRCGainDark;                        ///< Input trigger value:  drcGainDark
    FLOAT                           realGain;                           ///< Input trigger value:  Real Gain Value
    FLOAT                           luxIndex;                           ///< Input trigger value:  Lux index
    FLOAT                           exposureTime;                       ///< Input trigger value:  exporsure time
    FLOAT                           exposureGainRatio;                  ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;                     ///< Input trigger value:  AEC sensitivity data
    FLOAT                           prevluxIndex;                       ///< Lux index of previous frame
    FLOAT*                          pPreviousCalculatedHistogram;       ///< calculated histogram for next frame
    FLOAT*                          pPreviousCalculatedCDF;             ///< calculated CDF for next frame
    UINT32                          frameNumber;                        ///< Frame number
    UINT32*                         pGRHist;                            ///< BHIST GR stats
    ADRCData*                       pAdrcOutputData;                    ///< Pointer to the input data for adrc algo calculation
    FLOAT                           IPEGammaOutput[Gamma15TableSize];   ///< Gamma output from IPE gamma15 module
                                                                        ///  which is an input for TMC calculation
    FLOAT                           overrideDarkBoostOffset;            ///< Override dark boost offset for user control
    FLOAT                           overrideFourthToneAnchor;           ///< Override fourth tone anchor for user control
    FLOAT                           previousCalculatedFaceLevel;        ///< Previous calculated face level
    FLOAT                           faceLevel;                          ///< Face level
    FDData*                         pFDDataTMC;                         ///< Face Detection data for TMC

    // Note: that short strength is determined by (1 - long_strength - medium_strength)
    FLOAT                           out_stats_fusion_long_range;        ///< Fusion long range
    FLOAT                           out_stats_fusion_long_str;          ///< Fusion long strength
    FLOAT                           out_stats_fusion_medium_range;      ///< Fusion medium range
    FLOAT                           out_stats_fusion_medium_str;        ///< Fusion medium strength
    FLOAT                           out_stats_fusion_short_range;       ///< Fusion short range

    UINT32                          imageWidth;                         ///< Image width (CAMIF dimension)
    UINT32                          imageHeight;                        ///< Image height (CAMIF dimension)
    BOOL                            sliderEnable;                       ///< Slider Based Tuning
    FLOAT                           tone;                               ///< Slider Based Tuning
};

/// @brief Input Data to TMC12 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct TMC12InputData
{
    tmc_1_2_0::chromatix_tmc12Type* pChromatix;                         ///< Input Chromatix tunning data
    FLOAT                           DRCGain;                            ///< Input trigger value:  DRCGain
    FLOAT                           DRCGainDark;                        ///< Input trigger value:  drcGainDark
    FLOAT                           realGain;                           ///< Input trigger value:  Real Gain Value
    FLOAT                           luxIndex;                           ///< Input trigger value:  Lux index
    FLOAT                           exposureTime;                       ///< Input trigger value:  exporsure time
    FLOAT                           exposureGainRatio;                  ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;                     ///< Input trigger value:  AEC sensitivity data
    FLOAT                           prevluxIndex;                       ///< Lux index of previous frame
    FLOAT*                          pPreviousCalculatedHistogram;       ///< calculated histogram for next frame
    FLOAT*                          pPreviousCalculatedCDF;             ///< calculated CDF for next frame
    UINT32                          frameNumber;                        ///< Frame number
    UINT32*                         pGRHist;                            ///< BHIST GR stats
    ADRCData*                       pAdrcOutputData;                    ///< Pointer to the input data for adrc algo calculation
    FLOAT                           gammaInput[Gamma15TableSize];       ///< Gamma input for TMC/ADRC
    FLOAT                           overrideDarkBoostOffset;            ///< Override dark boost offset for user control
    FLOAT                           overrideFourthToneAnchor;           ///< Override fourth tone anchor for user control
    UINT32                          chipsetVersion;                     ///< Titan version
};

/// @brief Input Data to TMC11 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct TMC11InputData
{
    tmc_1_1_0::chromatix_tmc11Type* pChromatix;           ///< Input Chromatix tunning data
    FLOAT                           DRCGain;              ///< Input trigger value:  DRCGain
    FLOAT                           DRCGainDark;          ///< Input trigger value:  drcGainDark
    FLOAT                           realGain;             ///< Input trigger value:  Real Gain Value
    FLOAT                           luxIndex;             ///< Input trigger value:  Lux index
    FLOAT                           exposureTime;         ///< Input trigger value:  exporsure time
    FLOAT                           exposureGainRatio;    ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;       ///< Input trigger value:  AEC sensitivity data
    UINT32*                         pGRHist;              ///< BHIST GR stats
    ADRCData*                       pAdrcOutputData;      ///< Pointer to the input data for adrc algo calculation.
    FLOAT                           IPEGammaOutput[Gamma15TableSize];  ///< IPE gamma output from IPE gamma15 module
};

/// @brief Input Data to TMC10 IQ Algorithm
// NOWHINE NC004c: Share code with system team
struct TMC10InputData
{
    tmc_1_0_0::chromatix_tmc10Type* pChromatix;      ///< Input Chromatix tunning data
    FLOAT                           drcGain;         ///< Input trigger value:  DRCGain
    FLOAT                           drcGainDark;     ///< Input trigger value:  drcGainDark
    FLOAT                           realGain;        ///< Input trigger value:  Real Gain Value
    FLOAT                           luxIndex;        ///< Input trigger value:  Lux index
    BOOL                            adrcGTMEnable;   ///< GTM is enabled or not for ADRC
    BOOL                            adrcLTMEnable;   ///< LTM is enabled or not for ADRC
    ADRCData*                       pAdrcOutputData; ///< Pointer to the input data for adrc algo calculation.
};

// @brief Input Data to NRM10 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct NRAdjust10InputData
{
    nradj_1_0_0::chromatix_nradj10Type* pChromatix;   ///< Input Chromatix tunning data
    BOOL                                moduleEnable; ///< Module enable flag
};

/// @brief Input Data to NRM10 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct NRM10InputData
{
    nrm_1_0_0::chromatix_nrm10Type* pChromatix;             ///< Input Chromatix tunning data
    VOID*                           pInterpolationData;     ///< input memory for interpolation data
    BOOL                            moduleEnable;           ///< Module enable flag
    UINT32                          frameExposureType;      ///< frame exposure type
    UINT32                          frameNumber;            ///< Frame number
    UINT32                          chipsetVersion;         ///< Titan version
    FLOAT                           exposureTime;           ///< Input trigger value:  exposure time
    FLOAT                           exposureGainRatio;      ///< Input trigger value:  exposure gain ratio
    FLOAT                           AECSensitivity;         ///< Input trigger value:  AEC sensitivity data
    FLOAT                           AECSensitivityShort;    ///< Input trigger value:  AEC sensitivity data
    FLOAT                           realGain;               ///< Input trigger value:  Digital GainValue
    FLOAT                           digitalGain;            ///< Digital gain from Sensor
    FLOAT                           luxIndex;               ///< Input trigger value:  Lux index
};

/// @brief Input Data to Upscale12 and Chroma Upsample IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct Upscale12InputData
{
    VOID*                                   pHWSetting;                    ///< Pointer to the HW Setting Object
    FLOAT                                   luxIndex;                      ///< Input trigger value:  Lux index
    FLOAT                                   AECGain;                       ///< Input trigger value:  Digital Gain Value
    UINT16                                  lumaVScaleFirAlgorithm;        ///< Vertical scale FIR algo
    UINT16                                  lumaHScaleFirAlgorithm;        ///< Horizontal scale FIR algo
    UINT16                                  lumaInputDitheringDisable;     ///< Input Dithering disable
    UINT16                                  lumaInputDitheringMode;        ///< Input Dithering Mode
    UINT16                                  chromaVScaleFirAlgorithm;      ///< Vertical scale FIR algo
    UINT16                                  chromaHScaleFirAlgorithm;      ///< Horizontal scale FIR algo
    UINT16                                  chromaInputDitheringDisable;   ///< Input Dithering disable
    UINT16                                  chromaInputDitheringMode;      ///< Input Dithering Mode
    UINT16                                  chromaRoundingModeV;           ///< Chroma rounding mode vertical
    UINT16                                  chromaRoundingModeH;           ///< Chroma rounding mode horizontal
};

/// @brief Input Data to LDC11 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct LDC11InputData
{
    ldc_1_1_0::chromatix_ldc11Type*   pChromatix;             ///< Input Chromatix tunning data
    FLOAT                             lensZoom;               ///< Input trigger value:  lensZoom
    FLOAT                             lensPosition;           ///< Input trigger value:  Lens position
};

/// @brief Input Data to Upscale20 and Chroma Upsample IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct Upscale20InputData
{
    VOID*                                   pHWSetting;         ///< Pointer to the HW Setting Object
    upscale_2_0_0::chromatix_upscale20Type* pChromatix;         ///< Input Chromatix tunning data
    FLOAT                                   totalScaleRatio;    ///< Input trigger value:  Total ScaleRatio
    FLOAT                                   luxIndex;           ///< Input trigger value:  Lux index
    FLOAT                                   AECGain;            ///< Input trigger value:  Digital Gain Value
    UINT16                                  cosited;            ///< 0: interstitial; 1: cosited
    UINT16                                  evenOdd;            ///< 0: even; 1: odd
    UINT16                                  enableHorizontal;   ///< 1: enable horizontal chroma upsample; 0: disable
    UINT16                                  enableVertical;     ///< 1: enable vertical chroma upsample; 0: disable
    UINT32                                  ch0InputWidth;      ///< Input image width for channel 0
    UINT32                                  ch0InputHeight;     ///< Input image height for channel 0
    UINT32                                  ch1InputWidth;      ///< Input image width for channel 1
    UINT32                                  ch1InputHeight;     ///< Input image height for channel 1
    UINT32                                  ch2InputWidth;      ///< Input image width for channel 2
    UINT32                                  ch2InputHeight;     ///< Input image height for channel 2
    VOID*                                   pInterpolationData; ///< input memory for interpolation data
    UINT16                                  outWidth;           ///< added module output width
    UINT16                                  outHeight;          ///< added module output height
};

/// @brief Input Data to GRA10 IQ Algorithm
/// NOWHINE NC004c: Share code with system team
struct GRA10IQInput
{
    VOID*                           pHWSetting;         ///< Pointer to the HW Setting Object
    gra_1_0_0::chromatix_gra10Type* pChromatix;         ///< Input Chromatix tunning data
    BOOL                            moduleEnable;       ///< Module enable flag
    UINT32                          frameNum;           ///< Input Frame number for algorithm
    FLOAT                           colorTemperature;   ///< Input trigger value:  AWB color Temperature
    FLOAT                           luxIndex;           ///< Input trigger value:  Lux index
    FLOAT                           linearGain;         ///< Input trigger value:  linear Gain Value
    FLOAT                           preScaleRatio;      ///< Input trigger value:  Prescale ratio Mode
    VOID*                           pInterpolationData; ///< input memory for interpolation data
};

/// @brief Input Data to LENR10 IQ Algorithm
/// NOWHINE NC004c: Share code with system team
struct LENR10InputData
{
    VOID*                              pHWSetting;                      ///< Pointer to the HW Setting Object
    lenr_1_0_0::chromatix_lenr10Type*  pChromatix;                      ///< Input Chromatix tunning data
    BOOL                               moduleEnable;                    ///< Module enable flag
    BOOL                               prevStateEnable;                 ///< Previous enable flag
    FLOAT                              totalScaleRatio;                 ///< Input trigger value:  TotalScaleRatio
    FLOAT                              DRCGain;                         ///< Input trigger value:  drcGain
    FLOAT                              exposureTime;                    ///< Input trigger value:  exporsure time
    FLOAT                              AECSensitivity;                  ///< Input trigger value:  AEC sensitivity data
    FLOAT                              exposureGainRatio;               ///< Input trigger value:  Exposure Gain Ratio
    FLOAT                              luxIndex;                        ///< Input trigger value:  Lux index
    FLOAT                              realGain;                        ///< Input trigger value:  Digital Gain Value
    UINT32                             streamWidth;                     ///< Stream Width
    UINT32                             streamHeight;                    ///< Stream height
    UINT32                             cropFrameBoundaryPixAlignRight;  ///< 0: 4-pix align, 1: 2-pix align
    UINT32                             horizontalOffset;                ///< Horizontal Offset
    UINT32                             verticalOffset;                  ///< Vertical Offset
    struct FDData*                     pFDData;                         ///< Face data
    VOID*                              pInterpolationData;              ///< input memory for interpolation data
    FLOAT                              ica_zoom_factor;                 ///< icaZoomFactor
    FLOAT                              sensorScaling;                   ///< sensorScaling
};

/// @brief Input Data to CVP10 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct CVPInputData
{
    VOID*                            pChromatix;           ///< Input Chromatix tunning data
    FLOAT                            preScaleRatio;        ///< Pre Scale Ratio
    FLOAT                            exposureTime;         ///< Input trigger value:  exporsure time
    FLOAT                            exposureGainRatio;    ///< Input trigger value:  exporsure Gain Ratio
    FLOAT                            AECSensitivity;       ///< Input trigger value:  AEC sensitivity data
    FLOAT                            luxIndex;             ///< Input trigger value:  Lux index
    FLOAT                            realGain;             ///< Real Gain
    FLOAT                            totalScaleRatio;      ///< Input trigger value:  Real Gain Value
    UINT32                           frameNum;             ///< Current frame number
    UINT32                           numOfFrames;          ///< Number of frames for MFNR use case
    UINT8                            hasCVPRefInput;       ///< 1: has reference input, 0: no reference input
    VOID*                            pInterpolationData;   ///< input memory  for chromatix interpolation data
    VOID*                            pUnpackedfield;       ///< input memory Unpacked filed
    UINT32                           ifeOutFrameWidth;     ///< IFE Out Frame Width
    UINT32                           ifeOutFrameHeight;    ///< IFE Out Frame Height
    UINT8                            ifeAreDsImagesPD;     ///< IFE DS is PD
    UINT8                            ifeAreDsImages10Bps;  ///< IFE DS image are 10bit
    VOID*                            pCVPICAFrameCfgData;  ///< CVP ICA frame configuraion data
};

/// @brief Input Data to CSMCE11 IQ Algorithm
/// NOWHINE NC004c: Share code with system team
struct CSMCE11InputData
{
    VOID*                                   pHWSetting;         ///< Pointer to the HW Setting Object
    csmce_1_1_0::chromatix_csmce11Type *    pChromatix;         ///< Input chromatix tuning data
    BOOL                                    moduleEnble;        ///< Module enable flag
    FLOAT                                   DRCGain;            ///< Input trigger value:  DRCGain
    FLOAT                                   realGain;           ///< Input trigger value:  Real Gain Value
    FLOAT                                   luxIndex;           ///< Input trigger value:  Lux index
    FLOAT                                   exposureTime;       ///< Input trigger value:  exporsure time
    FLOAT                                   exposureGainRatio;  ///< Input trigger value:  exporsure gain Ratio
    FLOAT                                   AECSensitivity;     ///< Input trigger value:  AEC sensitivity data
    VOID*                                   pInterpolationData; ///< input memory for interpolation data

};

/// @brief Input Data to SWMFNR20 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct SWMFNR20InputData
{
    swmfnr_2_0_0::chromatix_swmfnr20Type* pChromatix;                ///< Input Chromatix tunning data
    BOOL                                  moduleEnable;              ///< Module enable flag
    BOOL                                  prevStateEnable;           ///< Previous enable flag
    FLOAT                                 totalScaleRatio;           ///< Input trigger value:  TotalScaleRatio
    FLOAT                                 DRCGain;                   ///< Input trigger value:  drcGain
    FLOAT                                 exposureTime;              ///< Input trigger value:  exporsure time
    FLOAT                                 exposureGainRatio;         ///< Input trigger value:  exposure gain ratio
    FLOAT                                 AECSensitivity;            ///< Input trigger value:  AEC sensitivity data
    FLOAT                                 luxIndex;                  ///< Input trigger value:  Lux index
    FLOAT                                 realGain;                  ///< Input trigger value:  Digital Gain Value
    UINT32                                streamWidth;               ///< Stream Width
    UINT32                                streamHeight;              ///< Stream height
    UINT32                                horizontalOffset;          ///< Horizontal Offset
    UINT32                                verticalOffset;            ///< Vertical Offset
    struct FDData*                        pFDData;                   ///< Face data
    UINT8                                 LUTBankSel;                ///< LUT bank need for this frame
    VOID*                                 pInterpolationData;        ///< input memory for interpolation data
    FLOAT                                 ica_zoom_factor;           ///< ICA Zoom Factor
    FLOAT                                 sensorScaling;             ///< Sensor scaling factor
};

/// @brief Input Data to SWMFNR30 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct SWMFNR30InputData
{
    swmfnr_3_0_0::chromatix_swmfnr30Type* pChromatix;                ///< Input Chromatix tunning data
    BOOL                                  moduleEnable;              ///< Module enable flag
    BOOL                                  prevStateEnable;           ///< Previous enable flag
    FLOAT                                 totalScaleRatio;           ///< Input trigger value:  TotalScaleRatio
    FLOAT                                 DRCGain;                   ///< Input trigger value:  drcGain
    FLOAT                                 exposureTime;              ///< Input trigger value:  exposure time
    FLOAT                                 exposureGainRatio;         ///< Input trigger value:  exposure gain ratio
    FLOAT                                 AECSensitivity;            ///< Input trigger value:  AEC sensitivity data
    FLOAT                                 AECGain;                   ///< Input trigger value:  AEC Gain Value
    FLOAT                                 luxIndex;                  ///< Input trigger value:  Lux index
    UINT32                                streamWidth;               ///< Stream Width
    UINT32                                streamHeight;              ///< Stream height
    UINT32                                horizontalOffset;          ///< Horizontal Offset
    UINT32                                verticalOffset;            ///< Vertical Offset
    UINT8                                 LUTBankSel;                ///< LUT bank need for this frame
    VOID*                                 pInterpolationData;        ///< input memory for interpolation data
    FLOAT                                 sensorScaling;             ///< Sensor scaling factor
};

/// @brief Input Data to SWMCTF10 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct SWMCTF10InputData
{
    swmctf_1_0_0::chromatix_swmctf10Type* pChromatix;                ///< Input Chromatix tunning data
    BOOL                                  moduleEnable;              ///< Module enable flag
    // BOOL                               prevStateEnable;           ///< Previous enable flag
    FLOAT                                 totalScaleRatio;           ///< Input trigger value:  TotalScaleRatio
    FLOAT                                 DRCGain;                   ///< Input trigger value:  drcGain
    FLOAT                                 exposureTime;              ///< Input trigger value:  exporsure time
    FLOAT                                 exposureGainRatio;         ///< Input trigger value:  exposure gain ratio
    FLOAT                                 AECSensitivity;            ///< Input trigger value:  AEC sensitivity data
    FLOAT                                 luxIndex;                  ///< Input trigger value:  Lux index
    FLOAT                                 realGain;                  ///< Input trigger value:  Digital Gain Value
    UINT32                                streamWidth;               ///< Stream Width
    UINT32                                streamHeight;              ///< Stream height
    UINT32                                horizontalOffset;          ///< Horizontal Offset
    VOID*                                 pInterpolationData;        ///< input memory for interpolation data
    UINT32                                verticalOffset;            ///< Vertical Offset
    FLOAT                                 sensorScaling;             ///< Sensor scaling factor
};

struct PREFERENCE10InputData
{
    preference_1_0_0::chromatix_preference10Type* pChromatix;              ///< Input Chromatix tuning data
    VOID*                                         pInterpolationData;      ///< input memory for interpolation data
    BOOL                                          moduleEnable;            ///< Module enable flag
    UINT32                                        frameNumber;             ///< Frame number
    UINT32                                        chipsetVersion;          ///< Titan version
    FLOAT                                         AECGain;                 ///< Input trigger value:  AEC Gain Value
    FLOAT                                         luxIndex;                ///< Input trigger value:  Lux index
    FLOAT                                         CCTTrigger;              ///< Input trigger value:  Lux index
    FLOAT                                         bad_pixel_correction;    ///< BPC Slider data
    FLOAT                                         Luma_noise_correction;   ///< Luma noise correction Slider data
    FLOAT                                         chroma_noise_correction; ///< Chroma noise correction Slider data
    FLOAT                                         multiframe_denoising;    ///< Multiframe denoising Slider data
    FLOAT                                         ghost_reduction;         ///< Ghost reduction Slider data
    FLOAT                                         brightness;              ///< Brightness adjustment Slider data
    FLOAT                                         contrast;                ///< Contrast Adjustment Slider data
    FLOAT                                         color;                   ///< Color  Slider data
    FLOAT                                         sharpness;               ///< Sharpness Slider data
};
/// @brief Input Data to SWMCTF20 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct SWMCTF20InputData
{
    swmctf_2_0_0::chromatix_swmctf20Type* pChromatix;                ///< Input Chromatix tunning data
    BOOL                                  moduleEnable;              ///< Module enable flag
    BOOL                                  prevStateEnable;           ///< Previous enable flag
    FLOAT                                 totalScaleRatio;           ///< Input trigger value:  TotalScaleRatio
    FLOAT                                 DRCGain;                   ///< Input trigger value:  drcGain
    FLOAT                                 exposureTime;              ///< Input trigger value:  exporsure time
    FLOAT                                 exposureGainRatio;         ///< Input trigger value:  exposure gain ratio
    FLOAT                                 AECSensitivity;            ///< Input trigger value:  AEC sensitivity data
    FLOAT                                 AECGain;                   ///< Input trigger value:  AEC Gain Value
    FLOAT                                 luxIndex;                  ///< Input trigger value:  Lux index
    UINT32                                streamWidth;               ///< Stream Width
    UINT32                                streamHeight;              ///< Stream height
    UINT32                                horizontalOffset;          ///< Horizontal Offset
    VOID*                                 pInterpolationData;        ///< input memory for interpolation data
    UINT32                                verticalOffset;            ///< Vertical Offset
    FLOAT                                 sensorScaling;             ///< Sensor scaling factor
};

/// @brief Input Data to QLL10 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct QLL10InputData
{
    qll_1_0_0::chromatix_qll10Type* pChromatix;             ///< Input Chromatix tunning data
    UINT32                          moduleEnable;           ///< Enable flag
    FLOAT                           DRCGain;                ///< Input trigger value: DRC Gain data
    FLOAT                           exposureTime;           ///< Input trigger value: exposure time
    FLOAT                           AECSensitivity;         ///< Input trigger value: AEC sensitivity data
    FLOAT                           exposureGainRatio;      ///< Input trigger value: exposure gain ratio
    FLOAT                           luxIndex;               ///< Input trigger value: Lux index
    FLOAT                           AECGain;                ///< Input trigger value: QEC Gain data
};

/// @brief Input Data to Video11 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct Video11InputData
{
    video_1_1_0::chromatix_video11Type*   pChromatix;             ///< Input Chromatix tunning data
    video_1_1_0::chromatix_video11Type*   pPrevChromatix;         ///< Prev Input Chromatix tunning data
    BOOL                                  moduleEnable;           ///< Module enable flag
    FLOAT                                 lensZoom;               ///< Input trigger value:  lensZoom
    FLOAT                                 exposureTime;           ///< Input trigger value:  exposure time
    FLOAT                                 exposureGainRatio;      ///< Input trigger value:  exposure gain ratio
    FLOAT                                 AECSensitivity;         ///< Input trigger value:  AEC sensitivity data
    FLOAT                                 lensPosition;           ///< Input trigger value:  Lens position
    FLOAT                                 preScaleRatio;          ///< Input trigger value:  Pre scale ratio
    FLOAT                                 luxIndex;               ///< Input trigger value:  Lux index
    FLOAT                                 AECGain;                ///< Input trigger value:  AEC Gain
};

/// @brief Input Data to SHDR11 IQ Algorithm
// NOWHINE NC004c : Shared file with system team so uses non-CamX file naming
struct SHDR11InputData
{
    shdr_1_1_0::chromatix_shdr11Type*   pChromatix;                 ///< Input Chromatix tunning data
    FLOAT                               totalScaleRatio;            ///< Input trigger value:  TotalScaleRatio
    FLOAT                               DRCGain;                    ///< Input trigger value:  drcGain
    FLOAT                               exposureTime;               ///< Input trigger value:  exporsure time
    FLOAT                               exposureGainRatio;          ///< Input trigger value:  exposure gain ratio
    FLOAT                               AECSensitivity;             ///< Input trigger value:  AEC sensitivity data
    FLOAT                               luxIndex;                   ///< Input trigger value:  Lux index
    FLOAT                               AECGain;                    ///< Input trigger value:  AEC Gain Value
    BOOL                                moduleEnable;               ///< Module enable flag
};

#endif // IQCOMMONDEFS_H
