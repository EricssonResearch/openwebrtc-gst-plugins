/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifndef android_video_capture_device_h
#define android_video_capture_device_h


// Video Capture Device (VCD) module

#include "include/android_video_capture_device_android_definitions.h"
#include "include/android_video_capture_device_errors.h"
#include "include/gst_android_video_source_common.h"

#include <jni.h>
#include <stdint.h>

#define PREFER_FRONT_CAMERA_DEFAULT TRUE
#define USE_CAMERA_INDEX_DEFAULT -1

typedef void* VCD_handle;

//
// VCD methods
//
VCD_handle VCD_open(int logInterval);
int VCD_close(VCD_handle);
int VCD_start(VCD_handle);
int VCD_stop(VCD_handle);
int VCD_prepare(VCD_handle);
int VCD_unprepare(VCD_handle);
int VCD_unfixateMediaType(VCD_handle);
int VCD_supportsMediaFormat(VCD_handle, int);
int VCD_supportsMediaSize(VCD_handle, int width, int height);
int VCD_supportsMediaFramerate(VCD_handle, int framerate);
int VCD_fixateMediaType(VCD_handle, int format, int width, int height, int framerate);
int VCD_getBufferSize(VCD_handle, int format, int width, int height);
int VCD_preferFrontFacingCamera(int preferFrontFacingCamera);
int VCD_useCameraWithIndex(int index);
void VCD_checkChangeCamera(VCD_handle);

//
// Camera class methods
//
void VCD_addCallbackBuffer(VCD_handle, jobject buffer);
int VCD_getNumberOfCameras(); // Static method. No handle required.
void VCD_getCameraInfo(int cameraId, jobject camInfo); // Static method. No handle required.
void VCD_startPreview(VCD_handle);
void VCD_stopPreview(VCD_handle);
void VCD_setPreviewCallbackWithBuffer(VCD_handle, jobject previewCallback);
void VCD_setPreviewDisplay(VCD_handle, jobject surfaceHolder);
void VCD_setDisplayOrientation(VCD_handle, int displayOrientation);
int VCD_read(VCD_handle, uint8_t ** pp_data, int bufsize);
void VCD_release();
int VCD_gatherMediaSupport(VCD_handle);
int VCD_getMediaSupportFmtLen(VCD_handle);
int* VCD_getMediaSupportFmt(VCD_handle);
int VCD_GetMinResolution(VCD_handle, int *p_minWidth, int *p_minHeight);
int VCD_GetMaxResolution(VCD_handle, int *p_maxWidth, int *p_maxHeight);
int VCD_GetWidestFpsRange(VCD_handle, int* p_minFps, int* p_maxFps);

// returns
// --> on success: TRUE
// ----> on error: FALSE
int VCD_init();

#endif // android_video_capture_device_h
