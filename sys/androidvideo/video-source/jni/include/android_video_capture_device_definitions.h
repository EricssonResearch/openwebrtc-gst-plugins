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

#ifndef android_video_capture_device_definitions_h
#define android_video_capture_device_definitions_h


#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#define VCD_MIN_ANDROID_SDK_VERSION 9
#define VCD_ANDROID_SDK_VERSION_WITH_SURFACE_TEXTURE 11

#define VCD_JNI_VERSION JNI_VERSION_1_6

#define VCD_UNUSED(param) while (0) { (void) (param); };
#define VCD_NELEM(x) ((int) (sizeof(x) / sizeof((x)[0])))

typedef enum {
    VCD_STATE_NULL,
    VCD_STATE_OPENED,
    VCD_STATE_PREPARED,
    VCD_STATE_STOPPED,
    VCD_STATE_RUNNING
} VCD_State;

typedef enum {
    NO_BUF_HAS_DATA,
    DATA_IN_BUF_ONE,
    DATA_IN_BUF_TWO
} VCD_DataFlag;

typedef enum {
    BUF_ID_NOT_SET,
    BUF_ID_ONE,
    BUF_ID_TWO
} VCD_PreviewBufferID;

typedef struct {
    int camIsBackFacing;
    int *fmt;
    int fmtLen;
    int **size;
    int sizeLen;
    int **fps;
    int fpsLen;
} VCD_CamMediaSupport;

enum { WIDTH_POS = 0, HEIGHT_POS = 1};
enum { MIN_FPS_POS = 0, MAX_FPS_POS = 1};

struct _VCD_VideoCaptureDevice {
    jobject m_javaCameraObj; // Camera obj, global reference
    jobject m_javaBufOneObj; // jbyteArray obj, global reference
    jobject m_javaBufTwoObj; // jbyteArray obj, global reference
    jobject m_javaPreviewCbObj; // preview callback handler obj, global reference
    jobject m_javaSurfaceTextureObj; // surface texture obj, global reference
    pthread_mutex_t m_bufOneMutex;
    pthread_mutex_t m_bufTwoMutex;
    VCD_DataFlag m_dataFlag;
    pthread_mutex_t m_dataFlagMutex;
    pthread_cond_t m_dataFlagCondition;
    VCD_PreviewBufferID m_currPreviewBuf;
    int m_hasStoppedFlag;
    pthread_mutex_t m_hasStoppedMutex;
    pthread_cond_t m_hasStoppedCondition;
    VCD_State m_state;
    VCD_CamMediaSupport** m_camMediaSupport;
    int m_numberOfCameras;
    int m_mediaSupportKnown;
    int m_mediaTypeIsFixed;
    int m_fixedFormat;
    int m_fixedFramerate;
    int m_fixedWidth;
    int m_fixedHeight;
    int m_fixedTrueWidth;
    int m_fixedTrueHeight;
    int m_logInterval;
    struct timeval m_onPreviewFrameTimer;
    struct timeval m_readTimer;
};
typedef struct _VCD_VideoCaptureDevice VCD_VideoCaptureDevice;

typedef enum {
    // Camera methods
    VCD_METHODID_CAMERA_ADDCALLBACKBUFFER,
    VCD_METHODID_CAMERA_GETCAMERAINFO,
    VCD_METHODID_CAMERA_GETNUMBEROFCAMERAS,
    VCD_METHODID_CAMERA_GETPARAMETERS,
    VCD_METHODID_CAMERA_LOCK,
    VCD_METHODID_CAMERA_OPEN_DEFAULT,
    VCD_METHODID_CAMERA_OPEN,
    VCD_METHODID_CAMERA_RELEASE,
    VCD_METHODID_CAMERA_SETDISPLAYORIENTATION,
    VCD_METHODID_CAMERA_SETPARAMETERS,
    VCD_METHODID_CAMERA_SETPREVIEWCALLBACKWITHBUFFER,
    VCD_METHODID_CAMERA_SETPREVIEWDISPLAY,
    VCD_METHODID_CAMERA_SETPREVIEWTEXTURE, // API Level 11
    VCD_METHODID_CAMERA_STARTPREVIEW, // dovstam: preview can be started with a preview display set to null, but "recording" cannot be started
    VCD_METHODID_CAMERA_STOPPREVIEW,
    VCD_METHODID_CAMERA_UNLOCK,
    // CameraInfo methods
    VCD_METHODID_CAMERAINFO_CONSTRUCTOR,
    // Camera Parameter methods
    VCD_METHODID_CAM_PARAMS_SETPREVIEWSIZE,
    VCD_METHODID_CAM_PARAMS_SETPREVIEWFORMAT,
    VCD_METHODID_CAM_PARAMS_SETPREVIEWFPSRANGE,
    VCD_METHODID_CAM_PARAMS_GETSUPPORTEDPREVIEWFPSRANGE,
    VCD_METHODID_CAM_PARAMS_GETSUPPORTEDPREVIEWFORMATS,
    VCD_METHODID_CAM_PARAMS_GETSUPPORTEDPREVIEWSIZES,
    VCD_METHODID_CAM_PARAMS_SETPREVIEWFRAMERATE, // NOTE: Deprecated!
    // preview callback methods
    VCD_METHODID_PREVIEW_CB_HANDLER_CONSTRUCTOR,
    VCD_METHODID_PREVIEW_CB_HANDLER_TEST,
    // ClassLoader methods
    // VCD_METHODID_CLASSLOADER_LOADCLASS,
    // surface texture methods
    VCD_METHODID_SURFACE_TEXTURE_CONSTRUCTOR,
    // List class methods
    VCD_METHODID_LIST_GET,
    VCD_METHODID_LIST_SIZE,
    // Integer class methods
    VCD_METHODID_INTEGER_INTVALUE
} VCD_MethodId;


#endif // android_video_capture_device_definitions_h
