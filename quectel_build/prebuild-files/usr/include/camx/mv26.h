////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014-2016, 2019-2022 Qualcomm Technologies, Inc.
// All Rights Reserved.
// Confidential and Proprietary - Qualcomm Technologies, Inc.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef MV_H
#define MV_H

/**=============================================================================
@mainpage Machine Vision API

@version 0.8-rc0

@section Overview Overview

The release numbering scheme follows the conventions of http://www.semver.org/
=============================================================================**/

#ifdef _WIN32
#define MV_2_EXPORTS
#endif


#ifdef __GNUC__
#ifdef BUILDING_SO
/// MACRO enables function to be visible in shared-library case.
#define MV_API __attribute__ ((visibility ("default")))
#else
/// MACRO empty for non-shared-library case.
#define MV_API
#endif
#else

#ifdef MV_2_EXPORTS
/// MACRO enables function to be visible in shared-library case.
#define MV_API __declspec(dllexport)
#else
/// MACRO empty for non-shared-library case.
#define MV_API
#endif
#endif

#define stringGetter(str) #str
#define functionNameToString(str) stringGetter(str)

//==============================================================================
// Includes
//==============================================================================

#include <stddef.h>
#include <stdbool.h>
#include "mesh_fusion26.h"

#ifdef __ARM_NEON__
#include <arm_neon.h>
typedef float  float32_t;
typedef double float64_t;
#else
#include <stdint.h>
typedef float  float32_t;
typedef double float64_t;
#endif

//==============================================================================
// Declarations
//==============================================================================

#ifdef __cplusplus
extern "C"
{
#endif


//------------------------------------------------------------------------------
/// @brief
///     Preference of tradeoffs (e.g., speed vs quality)
//------------------------------------------------------------------------------
typedef enum
{
    MV_MODE_SPEED = 0,
    MV_MODE_QUALITY = 1,
    MV_MODE_GPU = 2,
    MV_MODE_GPU_SIM = 3
} MV_MODE;


//------------------------------------------------------------------------------
/// @brief
///     Tracking state quality
//------------------------------------------------------------------------------
typedef enum
{
    MV_TRACKING_STATE_FAILED = -2,
    MV_TRACKING_STATE_INITIALIZING = -1,
    MV_TRACKING_STATE_HIGH_QUALITY = 0,
    MV_TRACKING_STATE_LOW_QUALITY = 1
} MV_TRACKING_STATE;


//------------------------------------------------------------------------------
/// @brief
///     Camera calibration parameters.
///
/// @remark
///     There is a tool provided with the SDK for generating this information.
///
/// @param distortion
///    Distortion coefficients.  All unused array elements must be set to 0.
///
/// @param
///     pixelWidth: Input image width
///     pixelHeight: Input image height
//------------------------------------------------------------------------------
typedef struct
{
    /// Input Image size:
    uint32_t pixelWidth, pixelHeight;
} mvCameraConfiguration;


//------------------------------------------------------------------------------
/// @brief
///     Configuration of stereo rig.
//------------------------------------------------------------------------------
typedef struct
{
    float32_t translation[3], rotation[3];  // Relative between cameras
    mvCameraConfiguration camera[2];        // Left/right camera calibrations
    float32_t correctionFactors[4];         // Distance correction
} mvStereoConfiguration;


//------------------------------------------------------------------------------
/// @brief
///     3-DOF pose information in rotation matrix form.
//------------------------------------------------------------------------------
typedef mesh_one_entry_t mvPose3DR;

//------------------------------------------------------------------------------
/// @brief
///     6-DOF pose information in Rotation-Translation matrix form.
//------------------------------------------------------------------------------
typedef struct
{
    float32_t matrix[3][4];  // [ R | T ] rotation matrix + translation col. vec.
} mvPose6DRT;


//------------------------------------------------------------------------------
/// @brief
///     Pose information in Euler-Translation (Tait-Bryan convention) form.
//------------------------------------------------------------------------------
typedef struct
{
    float32_t translation[3];       // Translation parameters
    float32_t euler[3];             // Euler angles
} mvPose6DET;


//------------------------------------------------------------------------------
/// @brief
///     Pose information along with a quality indicator.
//------------------------------------------------------------------------------
typedef struct
{
    mvPose6DRT pose;                // Pose
    MV_TRACKING_STATE poseQuality;  // Quality of the pose
} mvTrackingPose;


//------------------------------------------------------------------------------
/// @brief
///     Return string of version information.
//------------------------------------------------------------------------------
MV_API const char* mvVersion( void );


//------------------------------------------------------------------------------
/// @brief
///     Convert Euler-Translation pose to Rotation-Translation.
//------------------------------------------------------------------------------
MV_API void mvPose6DETto6DRT( mvPose6DET* pose, mvPose6DRT* mvPose );


//------------------------------------------------------------------------------
/// @brief
///     Convert Rotation-Translation pose to Euler-Translation.
//      Follows Tait-Bryan convention so that:
//      euler[0] = rotation about x-axis
//      euler[1] = rotation about y-axis
//      euler[2] = rotation about z-axis (defined from y-axis)
//------------------------------------------------------------------------------
MV_API void mvPose6DRTto6DET( mvPose6DRT* pose, mvPose6DET* mvPose );


//------------------------------------------------------------------------------
/// @brief
///     multiply two mvPose6DRT, computes out = A * B
//------------------------------------------------------------------------------
MV_API void mvMultiplyPose6DRT( const mvPose6DRT* A, const mvPose6DRT* B,
                                mvPose6DRT* out );


//------------------------------------------------------------------------------
/// @brief
///     invert mvPose6RT in place, computes pose = pose^-1
//------------------------------------------------------------------------------
MV_API void mvInvertPose6DRT( mvPose6DRT* pose );


//------------------------------------------------------------------------------
/// @brief
///     OpenGL helper function.
/// @param transpose
///     Flag of whether transpose is needed
//------------------------------------------------------------------------------
MV_API void mvGetGLProjectionMatrix( mvCameraConfiguration* camera,
                                     float64_t nearClip, float64_t farClip,
                                     float64_t* mat, bool transpose );

//------------------------------------------------------------------------------
/// @brief
///     Get Yaw, Pitch and Roll of camera pose (assuming NIDEC coordinate system) Z up, Y right, X out of target
///     and camera system is x right, y down and z out of camera
/// @param pose
///     Pose to calculate angles from
/// @param yaw
///     Results of yaw calculation, rotation of x axis direction y (in x/y plane) (target coord)
/// @param pitch
///     Results of pitch calculation, rotation of z axis direction x (in z/x plane) (target coord)
/// @param roll
///     Results of roll calculation, rotation of z axis direction y (in z/y plane) (target coord)
//------------------------------------------------------------------------------
MV_API void mvPoseAngles( mvPose6DRT* pose, float* yaw, float* pitch, float* roll );


#ifdef __cplusplus
}
#endif


#endif
