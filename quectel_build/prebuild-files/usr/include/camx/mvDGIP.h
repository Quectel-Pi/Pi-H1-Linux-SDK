#ifndef __MV_DG_IP_H__
#define __MV_DG_IP_H__

////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2016, 2019, 2022 Qualcomm Technologies, Inc.
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
    ///     Set a group of Transformation matrices for one camera frame.
    /// @param pObj
    ///     Pointer to DG object
    /// @param rTransfArray
    ///     Transformation matrices for camera frame specified by frameid.
    //------------------------------------------------------------------------------
    void MV_API mvDG_SetFrameTransfMatrix(mvDG* pObj, mvDGTransfMatrixArrayInfo* rTransfArray);


    //------------------------------------------------------------------------------
    /// @brief
    ///     Set image data content for camera frame.
    /// @param pObj
    ///     Pointer to DG object
    /// @param rTransfArray
    ///     Transformation matrices for camera frame specified by frameid. Caller is
    ///     responsible for memory management for rTransfArray.
    ///     Maximum number of matrices is the line count of output image.
    //------------------------------------------------------------------------------
    void MV_API mvDG_SetFrameContent(mvDG* pObj, mvDGCameraFrameContentInfo* rImgDataInfo);

    //------------------------------------------------------------------------------
    /// DEPRECATED!!!
    /// @brief
    ///     Get the stabilized image by finishing processing of all queued data and
    ///     images. Caller must first call the addAttitudes() and addImage() before calling
    ///     GetImage, And only mvDG lib receive all the attitudes info for a frame's [sof, eof],
    ///     can it generate the correct output frame.
    /// @param pObj
    ///     Pointer to DG object
    /// @param time
    ///     Timestamp of incoming image that output corresponds to
    /// @param pxlNV12
    ///     Pre-allocated array for image of outPxlWidth x outPxlHeight pixels in
    ///     NV12 (YUV420sp v12).  Enough memory (up to 64 bytes) for padding
    ///     between the Y and UV planes should be provided.
    /// @param frameid
    ///     frame id that output image correspond to. Caller can release the input frame whith frame id eauql or smaller than this frameid
    /// @param ionFD
    ///     Optional file handle if memory is ION memory so that GPU can be memory
    ///     map it.
    /// @return
    ///     0 = SUCCESS, the time_usec (SOF) indicates the input buffer that is associated with this SOF is returned to caller.
    ///                  a valid frame id is returned, caller can free the input frame with frame id equal or smaller than this value.
    ///   > 0, DELAYED PROCESSING. The mvDG is not ready to produce an output buffer, the corresponding input buffer is still held by
    ///    mvDG, framid valud is not valid under this situation.
    ///   < 0, PROCESSING ERROR,  the time_usec (SOF) indicates the input buffer that is associated with this SOF is returned to caller
    ///         if frameid >0, caller can free the input frame with frame id equal or smaller than this value. if the frameid <=0, it means
    ///         the frameid is not valid,caller should not free any input frames.
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_GetImage(mvDG* pObj, int64_t* time_usec, int32_t * frameid, uint8_t* pxlNV12,
        uint32_t* ionFD);

    //------------------------------------------------------------------------------
    /// @brief
    ///     Get the stabilized image by finishing processing of all queued data and
    ///     images. Caller must first call the SetFrameTransfMatrix() and
    ///     SetFrameContent() before calling GetOutputImage().
    /// @param pObj
    ///     Pointer to DG object
    /// @param frameid
    ///     frame id that output image correspond to.
    /// @param outImgInfo
    ///     reference of pointer to output image buffer, frame id and optional Ion buffer
    /// @return
    ///     0 = SUCCESS.
    ///   > 0, DELAYED PROCESSING. The mvDG is not ready to produce an output buffer,
    ///        the corresponding input buffer is still held by mvDG.
    ///   < 0, PROCESSING ERROR.
    //------------------------------------------------------------------------------
    int32_t MV_API mvDG_GetOutputImage(mvDG* pObj, int32_t frameid, mvDGCameraFrameContentInfo* outImgInfo);

#ifdef __cplusplus
}
#endif

#endif