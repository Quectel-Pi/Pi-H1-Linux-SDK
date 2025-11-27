#ifndef __MV_DG_TC_H__
#define __MV_DG_TC_H__

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
#include "mvDGCommon26.h"

//==============================================================================
// Declarations
//==============================================================================

#ifdef __cplusplus
extern "C"
{
#endif

    //------------------------------------------------------------------------------
    /// @brief
    ///     Pass multiple Gyroscope data to the DG object
    /// @param pObj
    ///     Pointer to DG object
    /// @param  numGyro
    ///     Number of gyros in gyro list
    /// @param pGyroList
    ///     Pointer to multiple gyro data
    /// @param  gyroSamplingRate
    ///     Sampling rate of gyro data.
    /// @param  type
    ///     IMU type. Source of current data.
    //------------------------------------------------------------------------------
    void MV_API mvDG_AddMultiGyro(mvDG* pObj, uint32_t numGyro, mvGyro *pGyroList, uint32_t gyroSamplingRate, mvDG_IMU_TYPE type);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Pass multiple Accelerometer data to the DG object
    /// @param pObj
    ///     Pointer to DG object
    /// @param  numAccel
    ///     Number of Accels in Accelerometer list
    /// @param pAccelList
    ///     Pointer to multiple Accelerometer data
    /// @param  accelSamplingRate
    ///     Sampling rate of accel data.
    /// @param  type
    ///     IMU type. Source of current data.
    //------------------------------------------------------------------------------
    void MV_API mvDG_AddMultiAccel(mvDG* pObj, uint32_t numAccel, mvAccel *pAccelList, uint32_t accelSamplingRate, mvDG_IMU_TYPE type); // receive

    //------------------------------------------------------------------------------
    /// @brief
    ///     Pass external pose from application to the DG object
    /// @param pObj
    ///     Pointer to DG object
    /// @param  pExtPose
    ///     Pointer to external pose data
    /// @param need_coord_conversion
    ///     Flag for coordinate definition conversion
    /// @param is_quaternion
    ///     Flag for coordinate presentation conversion
    //------------------------------------------------------------------------------
    void MV_API mvDG_AddExtPose(mvDG* pObj, mvExtPose *pExtPose, int32_t cnt, mvDG_COORD_DEFINE need_coord_conversion, mvDG_COORD_PRESENTATION is_quaternion);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Pass multiple external pose from application to the DG object
    /// @param pObj
    ///     Pointer to DG object
    /// @param  pExtPose
    ///     Pointer to external pose data
    /// @param need_coord_conversion
    ///     Flag for coordinate definition conversion
    /// @param is_quaternion
    ///     Flag for coordinate presentation conversion
    //------------------------------------------------------------------------------
    void MV_API mvDG_AddMultiExtPose(mvDG* pObj, mvExtPose* pExtPose, int32_t cnt);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Pass libSensorFusion_custom function pointer to the DG object
    /// @param pObj
    ///     Pointer to DG object
    /// @param  init_func_ptr
    ///     Function Pointer to libSensorFusion_custom init function
    /// @param  func_ptr
    ///     Function Pointer to libSensorFusion_custom process function
    /// @param  deinit_func_ptr
    ///     Function Pointer to libSensorFusion_custom deinit function
    //------------------------------------------------------------------------------
    void MV_API mvDG_libSensorFusion_custom_regist_API(
        void* pObj,
        void* (*init_func_ptr)(void),
        void(*func_ptr)(mvDG_libsensor_fusion_proc_t* _proc_param, void* private_data),
        void(*deinit_func_ptr)(void* private_data)
    );

    //------------------------------------------------------------------------------
    /// @brief
    ///     Pass meta data to the DG object
    /// @param pObj
    ///     Pointer to DG object
    /// @param  IMUTemp
    ///     Temperature data of imu
    /// @param  imuTempSamplingRate
    ///     Sampling rate of imu
    /// @param  imuTempCnt
    ///     Count of temperature data from imu in this frame
    /// @param  type
    ///     IMU type. Source of current data.
    //------------------------------------------------------------------------------
    void MV_API mvDG_AddIMUTemp(mvDG* pObj, mvTemp* IMUTemp, uint32_t imuTempSamplingRate, uint32_t imuTempCnt, mvDG_IMU_TYPE type);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Get AE ROI weighting data
    /// @param pObj
    ///     Pointer to DG object
    /// @param pAEWeighting
    ///     Pointer to AE ROI weighting data
    //------------------------------------------------------------------------------
    void MV_API mvDG_GetAEWeighting(mvDG* pObj, mvAECWeighting *pAEWeighting);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Pass camera frame timing information to the DG object.
    /// @param pObj
    ///     Pointer to DG object
    /// @param mvDGVideoFrameTimeInfo
    ///     Timing information of image data frame
    //------------------------------------------------------------------------------
    void MV_API mvDG_AddFrameTimeInfo(mvDG* pObj, mvDGCameraFrameTimeInfo* rVidTimeInfo);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Get the number of Transformation matrices for one camera frame.
    ///     Caller must call this function before GetFrameTransfMatrix.
    ///     Note: actual number of nodes on the grids of transf matrices is
    ///     (nTransfArrayW+1) * (nTransfArrayH+1).
    /// @return
    ///   = 0, SUCCESS. a valid camera frame id is returned as well as the matrices
    ///     of transformation for the frame.
    ///   > 0, DELAYED PROCESSING. The mvDG is not ready to produce an output,
    ///     returned framid valud is not valid under this situation.
    ///   < 0, PROCESSING ERROR.
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_GetFrameTransfMatrixNumber(mvDG* pObj, int32_t frameid, int32_t* nTransfArrayW, int32_t* nTransfArrayH);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Get a group of Transformation matrices for one camera frame.
    ///     Caller must first call addMultiplGyro() before calling this function.
    ///     Only after mvDG lib receive all the attitudes info for a frame's
    //      [sof, eof], can it generate the correct transformation matrices.
    /// @param pObj
    ///     Pointer to DG object
    /// @param frameid
    ///     Camera frame id that caller wants to retrieve.
    /// @param rTransfArray
    ///     Transformation matrices for camera frame specified by frameid and matTye.
    ///     Caller is responsible for memory management for rTransfArray.
    ///     Maximum number of matrices is max(line count of input image, 128x96)
    /// @return
    ///   = 0, SUCCESS. a valid camera frame id is returned as well as the matrices
    ///     of transformation for the frame.
    ///   < 0, PROCESSING ERROR.
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_GetFrameTransfMatrix(mvDG* pObj, int32_t frameid, mvDGTransfMatrixArrayInfo** rTransfArray);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Get a Transformation matrix of global frame alignment for one camera frame.
    ///     Caller must first call mvDG_GetFrameTransfMatrix() before calling
    ///     this function.
    /// @param rTransfArray
    ///     Transformation matrx of global frame alignment for camera frame specified
    ///     by frameid and matTye. Caller is responsible for memory management for
    ///     rTransfArray. Maximum number of matrices is max(line count of input image, 128x96)
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_GetFrameTransfMatrix_GFA(mvDG* pObj, int32_t frameid, mvDGTransfMatrixArrayInfo* rTransfArray);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Get a Transformation matrix of shdr alignment for one camera frame.
    ///     Caller must first call mvDG_GetFrameTransfMatrix() before calling
    ///     this function.
    /// @param rTransfArray
    ///     Transformation matrx of shdr alignment for camera frame specified by frameid and matTye.
    ///     Caller is responsible for memory management for rTransfArray.
    ///     Maximum number of matrices is max(line count of input image, 128x96)
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_GetFrameTransfMatrix_SHA(mvDG * pObj, int32_t frameid, mvDGTransfMatrixArrayInfo** rTransfArray);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Pass each camera exposure frame timing information to the DG object.
    /// @param pObj
    ///     Pointer to DG object
    /// @param mvDGVideoFrameTimeInfo
    ///     Timing information of image data frame
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_AddFrameTimeInfo_SHA(mvDG* pObj, mvDGCameraFrameTimeInfo** rVidTimeInfo);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Get a alpha mesh for camera frame.
    ///     Caller must first call mvDG_GetFrameTransfMatrix() before calling
    ///     this function.
    /// @param rTransfArray
    ///     Caller is responsible for memory management for rTransfArray.
    ///     Maximum number of matrices is max(line count of input image, 128x96)
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_GetFrameAlpha(int32_t frameid, mvDGAlphaArrayInfo** rAlphaArray);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Get extended mesh entries for UWM mesh format
    /// @param rTransfArray_fbis
    ///     Extended mesh entries of FBIS meshes
    /// @param rTransfArray_gfa
    ///     Extended mesh entries of GFA mesh
    /// @param rTransfArray_sha
    ///     Extended mesh entries of SHA meshes
    /// @return
    ///   = 0, SUCCESS.
    ///   = -1 PROCESSING ERROR. mvDGTransfMatrixArrayInfo null pointer.
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_GetFrameTransfMatrix_Ext(
        mvDG* pObj,
        int32_t frameid,
        mvDGTransfMatrixArrayInfo** rTransfArray_fbis,
        mvDGTransfMatrixArrayInfo* rTransfArray_gfa,
        mvDGTransfMatrixArrayInfo** rTransfArray_sha);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Set sensor, camif and VFE window crop/rescale information to mvDGTC
    /// @param pObj
    ///     Pointer to DG object
    /// @param sensor_win
    ///     sensor readout window crop and rescale information.
    /// @param camif_win
    ///     camif crop information.
    /// @param vfe_win
    ///     vfe crop and rescale information.
    /// @return
    ///   = 0, SUCCESS.
    ///   = -1 window information doesn't make sense.
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_Adapt_window(mvDG* pObj, mvDGWindowInfo* sensor_win, mvDGWindowInfo* ife_win, mvDGWindowInfo* isp_win);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Set view offset
    /// @param pObj
    ///     Pointer to DG object
    /// @param view_offset
    ///     view offset for three axis, order: pitch, yaw, roll
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_SetViewOffset(mvDG* pObj, float32_t* view_offset);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Set camera pose
    /// @param pObj
    ///     Pointer to DG object
    /// @param pose
    ///     Pointer to camera pose (const float array[3])
    //------------------------------------------------------------------------------
    void MV_API mvDG_SetPose(mvDG* pObj, const float32_t* pose);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Get camera pose
    /// @param pObj
    ///     Pointer to DG object
    /// @param pose
    ///     Pointer to camera pose (float array[3])
    //------------------------------------------------------------------------------
    void MV_API mvDG_GetPose(mvDG* pObj, float32_t* pose);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Set bias estimation
    /// @param pObj
    ///     Pointer to DG object
    /// @param bias_est
    ///     Pointer to bias estimation (const float array[3])
    //------------------------------------------------------------------------------
    void MV_API mvDG_SetBiasEst(mvDG* pObj, const float32_t* bias_est);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Get bias estimation
    /// @param pObj
    ///     Pointer to DG object
    /// @param bias_est
    ///     Pointer to bias estimation (float array[3])
    //------------------------------------------------------------------------------
    void MV_API mvDG_GetBiasEst(mvDG* pObj, float32_t* bias_est);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Set FBIS margin
    /// @param pObj
    ///     Pointer to DG object
    /// @param fbis_margin
    ///     FBIS margin to be set (const float)
    /// @return
    ///     = 0, SUCCESS
    ///     = non-zero, FAIL
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_SetFbisMargin(mvDG* pObj, const float32_t fbis_margin);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Tell the presents of main IMU and aux IMU
    /// @param pObj
    ///     Pointer to DG object
    /// @param isMainImuPresent
    ///     Main IMU present (uint32_t)
    /// @param isAuxImuPresent
    ///     Aux IMU present (uint32_t)
    /// @return
    ///     void.
    //------------------------------------------------------------------------------
    void MV_API mvDG_SetAuxIMU(mvDG* pObj, uint32_t isMainImuPresent, uint32_t isAuxImuPresent);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Set optical zoom focal length
    /// @param pObj
    ///     Pointer to DG object
    /// @param focal_length
    ///     Focal length in float to be set
    /// @return
    ///     = 0, SUCCESS
    ///     = non-zero, FAIL
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_SetOptiZoomFocalLength(mvDG* pObj, float32_t focal_length);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Set LDC look-up table and LDC look-up table size. mvDG_Adapt_window()
    ///     should be called after calling this function.
    /// @param pObj
    ///     Pointer to DG object
    /// @param ldc_table
    ///     Pointer to LDC look-up table to be set
    /// @param ldc_r2_lut_size
    ///     ldc_r2_lut_size to be set
    /// @param partial_ldc_table
    ///     Pointer to partial LDC look-up table to be set
    /// @param partial_ldc_lut_size
    ///     partial_ldc_lut_size to be set
    /// @return
    ///     = 0, SUCCESS
    ///     = non-zero, FAIL
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_SetLDC(mvDG* pObj, float32_t* ldc_r2_lut, float32_t ldc_r2_lut_size, float32_t* partial_ldc_lut, float32_t partial_ldc_lut_size);

#ifdef __cplusplus
}
#endif

#endif
