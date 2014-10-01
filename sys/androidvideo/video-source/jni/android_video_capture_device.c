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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "include/android_video_capture_device.h"

#include "include/android_video_capture_device_android_definitions.h" // dependencies on Android, e.g. constants
#include "include/android_video_capture_device_definitions.h"

#include "include/gst_android_video_source_common.h"
#include "include/gst_android_video_source_config.h"
#include "include/gst_android_video_source_log.h"
#include "include/jni_utils.h"

#include <assert.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int s_VCD_AndroidSdkVersion = VCD_MIN_ANDROID_SDK_VERSION;

// If gl_VCD_useCameraWithIndex is set to a valid camera index, that camera will be chosen (overriding any setting of gl_VCD_preferFrontFacingCamera).
// If gl_VCD_useCameraWithIndex is < 0 or is >= 0 but not a valid camera index, the first front facing camera found will be chosen if
// gl_VCD_preferFrontFacingCamera = TRUE, or the first back facing camera if gl_VCD_preferFrontFacingCamera = FALSE
int gl_VCD_preferFrontFacingCamera = PREFER_FRONT_CAMERA_DEFAULT;
int gl_VCD_useCameraWithIndex = USE_CAMERA_INDEX_DEFAULT;
int gl_VCD_prevPreferredFrontFacingCamera = -1;
int gl_VCD_currentCameraIndex = -1;

static const char* VCD_CLASS_CAMERA_STR = "android/hardware/Camera";
static const char* VCD_CLASS_CAM_PARAMS_STR = "android/hardware/Camera$Parameters"; // Parameters is a nested class within android.hardware.Camera (hence "$" rather than "/")
static const char* VCD_CLASS_CAM_SIZE_STR = "android/hardware/Camera$Size"; // Size is a nested class within android.hardware.Camera (hence "$" rather than "/")
static const char* VCD_CLASS_CAM_INFO_STR = "android/hardware/Camera$CameraInfo"; // CameraInfo is a nested (static) class within android.hardware.Camera (hence "$" rather than "/")
static const char* VCD_CLASS_PREVIEW_CB_HANDLER_STR = "com.ericsson.gstreamer.VideoCaptureDevicePreviewCallbackHandler";
static const char* VCD_CLASS_SURFACE_TEXTURE_STR = "android/graphics/SurfaceTexture";
static const char* VCD_CLASS_OS_BUILD_VERSION_STR = "android/os/Build$VERSION"; // VERSION is a nested class within android.os.Build (hence "$" rather than "/")
static const char* VCD_CLASS_LIST_STR = "java/util/List";
static const char* VCD_CLASS_INTEGER_STR = "java/lang/Integer";
static const char* VCD_MODULE_NAME_STR = "androidvideosrc";

static int s_VCD_is_initialized = FALSE;
static int s_VCD_is_native_methods_registered = FALSE;
static int s_VCD_are_classes_cached = FALSE;
static JavaVM *s_VCD_javaVM = NULL;
static pthread_key_t s_VCD_jniEnvKey = 0;

static jobject s_VCD_classCamera = NULL; // global reference to the Camera class
static jobject s_VCD_classPreviewCbHandler = NULL; // global reference to the preview callback handler class
static jobject s_VCD_classCamParams = NULL; // global reference to the Camera parameter class
static jobject s_VCD_classCamSize = NULL; // global reference to the Camera size class
static jobject s_VCD_classCamInfo = NULL; // glocal reference to the Camera info class
static jobject s_VCD_classSurfaceTexture = NULL; // global reference to the Surface Texture class
static jobject s_VCD_classList = NULL; // global reference to the List class
static jobject s_VCD_classInteger = NULL; // global reference to the Integer class

// Global VCD_handle to be used to access VCD from native_onPreviewFrame
static VCD_handle s_VCD_handle = NULL;

// static jmethodIDs - Camera
static jmethodID s_mId_addCallbackBuffer = NULL;
static jmethodID s_mId_getCameraInfo = NULL;
static jmethodID s_mId_getNumberOfCameras = NULL;
static jmethodID s_mId_getParameters = NULL;
static jmethodID s_mId_lock = NULL;
static jmethodID s_mId_open_default = NULL;
static jmethodID s_mId_open = NULL;
static jmethodID s_mId_release = NULL;
static jmethodID s_mId_setDisplayOrientation = NULL;
static jmethodID s_mId_setParameters = NULL;
static jmethodID s_mId_setPreviewCallbackWithBuffer = NULL;
static jmethodID s_mId_setPreviewDisplay = NULL;
static jmethodID s_mId_setPreviewTexture = NULL; // API Level 11
static jmethodID s_mId_startPreview = NULL;
static jmethodID s_mId_stopPreview = NULL;
static jmethodID s_mId_unlock = NULL;
// static jmethodIDs - CameraInfo
static jmethodID s_mId_camInfoConstructor = NULL;
// static jmethodIDs - Camera Parameters
static jmethodID s_mId_camParams_setPreviewSize = NULL;
static jmethodID s_mId_camParams_setPreviewFormat = NULL;
static jmethodID s_mId_camParams_setPreviewFpsRange = NULL;
static jmethodID s_mId_camParams_getSupportedPreviewFpsRange = NULL;
static jmethodID s_mId_camParams_getSupportedPreviewFormat = NULL;
static jmethodID s_mId_camParams_getSupportedPreviewSizes = NULL;
static jmethodID s_mId_camParams_setPreviewFrameRate = NULL; // NOTE: Deprecated!
// static jmethodIDs - preview callback
static jmethodID s_mId_previewCbHandlerConstructor = NULL;
static jmethodID s_mId_previewCbHandlerTest = NULL;
// static jmethodIDs - surface texture
static jmethodID s_mId_surfaceTextureConstructor = NULL;
// static jmethodIDs - List class
static jmethodID s_mId_list_get = NULL;
static jmethodID s_mId_list_size = NULL;
// static jmethodIDs - Integer class
static jmethodID s_mId_integer_intValue = NULL;

// Prototypes
static jmethodID getMethodId(VCD_MethodId VCD_id);

// Util prototypes
void create_wait_time(struct timespec* p_time, int seconds_to_wait);
int time_diff_usec(struct timeval* p_timeA, struct timeval* p_timeB);
int time_diff_sec(struct timeval* p_timeA, struct timeval* p_timeB);
int getAndroidSdkVersion(JNIEnv* p_env);

//
// VCD_getEnv
//
static JNIEnv* VCD_getEnv()
{
#ifdef GA_TRACES_ENABLED
    if (!s_VCD_javaVM) {
        GA_LOGERROR("%s %s ERROR: no Java VM defined, has VCD_init() been called?", __FILE__, __FUNCTION__);
    }
#endif
    assert(s_VCD_javaVM);

    JNIEnv *env = pthread_getspecific(s_VCD_jniEnvKey);
    if (!env) {
        // attach the current thread to the Java VM
        JavaVMAttachArgs thr_args;
        thr_args.version = VCD_JNI_VERSION;
        thr_args.name = NULL;
        thr_args.group = NULL;
        int err = (*s_VCD_javaVM)->AttachCurrentThread(s_VCD_javaVM, &env, &thr_args);
        if (!err) {
            pthread_setspecific(s_VCD_jniEnvKey, env);
            GA_LOGINFO("%s attached thread(%ld) to Java VM (env= %p)", __FUNCTION__, pthread_self(), env);
        } else {
            GA_LOGERROR("%s ERROR: attach thread(%ld) to Java VM failed, terminates...", __FILE__, pthread_self());
            exit(EXIT_FAILURE); // terminates the process
        }
    }

    assert(env);
    return env;
}


//
// VCD_cacheCameraClass
//
// caches android/hardware/Camera class (plus parameter, size and CameraInfo inner classes) and
// makes sure that it is always loaded in the process
//
static int VCD_cacheCameraClass(JNIEnv *p_env)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    jclass classCamera = NULL;
    jclass classCamParams = NULL;
    jclass classCamSize = NULL;
    jclass classCamInfo = NULL;

    assert(p_env);

    if (s_VCD_classCamera || s_VCD_classCamParams || s_VCD_classCamSize || s_VCD_classCamInfo) {
        GA_LOGWARN("%s WARNING: s_VCD_classCamera and/or s_VCD_classCamParams and/or s_VCD_classCamSize and/or s_VCD_classCamInfo is already cached, is re-caching really intended?", __FILE__);
    }

    classCamera = (*p_env)->FindClass(p_env, VCD_CLASS_CAMERA_STR);
    AV_CHECK_NULL(classCamera, VCD_cacheCameraClass_err_find_class_camera);

    s_VCD_classCamera = (*p_env)->NewGlobalRef(p_env, classCamera);
    (*p_env)->DeleteLocalRef(p_env, classCamera);
    AV_CHECK_NULL(s_VCD_classCamera, VCD_cacheCameraClass_err_new_global_ref_camera);

    classCamParams = (*p_env)->FindClass(p_env, VCD_CLASS_CAM_PARAMS_STR);
    AV_CHECK_NULL(classCamParams, VCD_cacheCameraClass_err_find_class_cam_params);

    s_VCD_classCamParams = (*p_env)->NewGlobalRef(p_env, classCamParams);
    (*p_env)->DeleteLocalRef(p_env, classCamParams);
    AV_CHECK_NULL(s_VCD_classCamParams, VCD_cacheCameraClass_err_new_global_ref_cam_params);

    classCamSize = (*p_env)->FindClass(p_env, VCD_CLASS_CAM_SIZE_STR);
    AV_CHECK_NULL(classCamSize, VCD_cacheCameraClass_err_find_class_cam_size);

    s_VCD_classCamSize = (*p_env)->NewGlobalRef(p_env, classCamSize);
    (*p_env)->DeleteLocalRef(p_env, classCamSize);
    AV_CHECK_NULL(s_VCD_classCamSize, VCD_cacheCameraClass_err_new_global_ref_cam_size);

    classCamInfo = (*p_env)->FindClass(p_env, VCD_CLASS_CAM_INFO_STR);
    AV_CHECK_NULL(classCamInfo, VCD_cacheCameraClass_err_find_class_cam_info);

    s_VCD_classCamInfo = (*p_env)->NewGlobalRef(p_env, classCamInfo);
    (*p_env)->DeleteLocalRef(p_env, classCamInfo);
    AV_CHECK_NULL(s_VCD_classCamInfo, VCD_cacheCameraClass_err_new_global_ref_cam_info);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return VCD_NO_ERROR;

    /*
     * propagate unhandled errors
     */
VCD_cacheCameraClass_err_find_class_camera:
    {
        GA_LOGERROR("%s ERROR: find class %s failed", __FUNCTION__, VCD_CLASS_CAMERA_STR);
        return VCD_ERR_GENERAL;
    }
VCD_cacheCameraClass_err_new_global_ref_camera:
    {
        GA_LOGERROR("%s ERROR: NewGlobalRef failed (camera) => out of memory", __FUNCTION__);
        return VCD_ERR_NO_MEMORY;
    }
VCD_cacheCameraClass_err_find_class_cam_params:
    {
        GA_LOGERROR("%s ERROR: find class %s failed", __FUNCTION__, VCD_CLASS_CAM_PARAMS_STR);
        return VCD_ERR_GENERAL;
    }
VCD_cacheCameraClass_err_new_global_ref_cam_params:
    {
        GA_LOGERROR("%s ERROR: NewGlobalRef failed (cam params) => out of memory", __FUNCTION__);
        return VCD_ERR_NO_MEMORY;
    }
VCD_cacheCameraClass_err_find_class_cam_size:
    {
        GA_LOGERROR("%s ERROR: find class %s failed", __FUNCTION__, VCD_CLASS_CAM_SIZE_STR);
        return VCD_ERR_GENERAL;
    }
VCD_cacheCameraClass_err_new_global_ref_cam_size:
    {
        GA_LOGERROR("%s ERROR: NewGlobalRef failed (cam size) => out of memory", __FUNCTION__);
        return VCD_ERR_NO_MEMORY;
    }
VCD_cacheCameraClass_err_find_class_cam_info:
    {
        GA_LOGERROR("%s ERROR: find class %s failed", __FUNCTION__, VCD_CLASS_CAM_INFO_STR);
        return VCD_ERR_GENERAL;
    }
VCD_cacheCameraClass_err_new_global_ref_cam_info:
    {
        GA_LOGERROR("%s ERROR: NewGlobalRef failed (cam info) => out of memory", __FUNCTION__);
        return VCD_ERR_NO_MEMORY;
    }
}


//
// VCD_cacheSurfaceTextureClass
//
// caches android/graphics/SurfaceTexture class and
// makes sure that it is always loaded in the process
//
static int VCD_cacheSurfaceTextureClass(JNIEnv *p_env)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    jclass classSurfaceTexture = NULL;

    assert(p_env);

    if (s_VCD_classSurfaceTexture) {
        GA_LOGWARN("%s WARNING: s_VCD_classSurfaceTexture is already cached, is re-caching really intended?", __FILE__);
    }

    classSurfaceTexture = (*p_env)->FindClass(p_env, VCD_CLASS_SURFACE_TEXTURE_STR);
    AV_CHECK_NULL(classSurfaceTexture, VCD_cacheSurfaceTextureClass_err_find_class);

    s_VCD_classSurfaceTexture = (*p_env)->NewGlobalRef(p_env, classSurfaceTexture);
    (*p_env)->DeleteLocalRef(p_env, classSurfaceTexture);
    AV_CHECK_NULL(s_VCD_classSurfaceTexture, VCD_cacheSurfaceTextureClass_err_new_global_ref);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return VCD_NO_ERROR;

    /*
     * propagate unhandled errors
     */
VCD_cacheSurfaceTextureClass_err_find_class:
    {
        GA_LOGERROR("%s ERROR: find class %s failed", __FUNCTION__, VCD_CLASS_SURFACE_TEXTURE_STR);
        return VCD_ERR_GENERAL;
    }
VCD_cacheSurfaceTextureClass_err_new_global_ref:
    {
        GA_LOGERROR("%s ERROR: NewGlobalRef failed => out of memory", __FUNCTION__);
        return VCD_ERR_NO_MEMORY;
    }
}


//
// VCD_cacheListClass
//
// caches java/util/List class and
// makes sure that it is always loaded in the process
//
static int VCD_cacheListClass(JNIEnv *p_env)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    jclass classList = NULL;

    assert(p_env);

    if (s_VCD_classList) {
        GA_LOGWARN("%s WARNING: s_VCD_classList is already cached, is re-caching really intended?", __FILE__);
    }

    classList = (*p_env)->FindClass(p_env, VCD_CLASS_LIST_STR);
    AV_CHECK_NULL(classList, VCD_cacheListClass_err_find_class);

    s_VCD_classList = (*p_env)->NewGlobalRef(p_env, classList);
    (*p_env)->DeleteLocalRef(p_env, classList);
    AV_CHECK_NULL(s_VCD_classList, VCD_cacheListClass_err_new_global_ref);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return VCD_NO_ERROR;

    /*
     * propagate unhandled errors
     */
VCD_cacheListClass_err_find_class:
    {
        GA_LOGERROR("%s ERROR: find class %s failed", __FUNCTION__, VCD_CLASS_LIST_STR);
        return VCD_ERR_GENERAL;
    }
VCD_cacheListClass_err_new_global_ref:
    {
        GA_LOGERROR("%s ERROR: NewGlobalRef failed => out of memory", __FUNCTION__);
        return VCD_ERR_NO_MEMORY;
    }
}


//
// VCD_cacheIntegerClass
//
// caches java/lang/Integer class and
// makes sure that it is always loaded in the process
//
static int VCD_cacheIntegerClass(JNIEnv *p_env)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    jclass classInteger= NULL;

    assert(p_env);

    if (s_VCD_classInteger) {
        GA_LOGWARN("%s WARNING: s_VCD_classInteger is already cached, is re-caching really intended?", __FILE__);
    }

    classInteger = (*p_env)->FindClass(p_env, VCD_CLASS_INTEGER_STR);
    AV_CHECK_NULL(classInteger, VCD_cacheIntegerClass_err_find_class);

    s_VCD_classInteger = (*p_env)->NewGlobalRef(p_env, classInteger);
    (*p_env)->DeleteLocalRef(p_env, classInteger);
    AV_CHECK_NULL(s_VCD_classInteger, VCD_cacheIntegerClass_err_new_global_ref);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return VCD_NO_ERROR;

    /*
     * propagate unhandled errors
     */
VCD_cacheIntegerClass_err_find_class:
    {
        GA_LOGERROR("%s ERROR: find class %s failed", __FUNCTION__, VCD_CLASS_INTEGER_STR);
        return VCD_ERR_GENERAL;
    }
VCD_cacheIntegerClass_err_new_global_ref:
    {
        GA_LOGERROR("%s ERROR: NewGlobalRef failed => out of memory", __FUNCTION__);
        return VCD_ERR_NO_MEMORY;
    }
}


//
// VCD_getCachedIntegerClass
//
static jobject VCD_getCachedIntegerClass()
{
    if (!s_VCD_classInteger) {
        GA_LOGWARN("%s WARNING: cached s_VCD_classInteger = NULL", __FILE__);
    }

    return s_VCD_classInteger;
}

//
// VCD_getCachedListClass
//
static jobject VCD_getCachedListClass()
{
    if (!s_VCD_classList) {
        GA_LOGWARN("%s WARNING: cached s_VCD_classList = NULL", __FILE__);
    }

    return s_VCD_classList;
}

//
// VCD_getCachedSurfaceTextureClass
//
static jobject VCD_getCachedSurfaceTextureClass()
{
    if (!s_VCD_classSurfaceTexture) {
        GA_LOGWARN("%s WARNING: cached s_VCD_classSurfaceTexture = NULL", __FILE__);
    }

    return s_VCD_classSurfaceTexture;
}

//
// VCD_getCachedPreviewCbHandlerClass
//
static jobject VCD_getCachedPreviewCbHandlerClass()
{
    if (!s_VCD_classPreviewCbHandler) {
        GA_LOGWARN("%s WARNING: cached s_VCD_classPreviewCbHandler = NULL", __FILE__);
    }

    return s_VCD_classPreviewCbHandler;
}


//
// VCD_getCachedCameraClass
//
static jobject VCD_getCachedCameraClass()
{
    if (!s_VCD_classCamera) {
        GA_LOGWARN("%s WARNING: cached s_VCD_classCamera = NULL", __FILE__);
    }

    return s_VCD_classCamera;
}

//
// VCD_getCachedCamParamsClass
//
static jobject VCD_getCachedCamParamsClass()
{
    if (!s_VCD_classCamParams) {
        GA_LOGWARN("%s WARNING: cached s_VCD_classCamParams = NULL", __FILE__);
    }

    return s_VCD_classCamParams;
}

//
// VCD_getCachedCamSizeClass
//
static jobject VCD_getCachedCamSizeClass()
{
    if (!s_VCD_classCamSize) {
        GA_LOGWARN("%s WARNING: cached s_VCD_classCamSize = NULL", __FILE__);
    }

    return s_VCD_classCamSize;
}

//
// VCD_getCachedCamInfoClass
//
static jobject VCD_getCachedCamInfoClass()
{
    if (!s_VCD_classCamInfo) {
        GA_LOGWARN("%s WARNING: cached s_VCD_classCamInfo = NULL", __FILE__);
    }

    return s_VCD_classCamInfo;
}


//
// VCD_cacheIntegerMethodIds
//
static int VCD_cacheIntegerMethodIds(JNIEnv *p_env)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    assert(p_env);

    s_mId_integer_intValue = (*p_env)->GetMethodID(p_env, VCD_getCachedIntegerClass(), "intValue", "()I");

    int err = !s_mId_integer_intValue;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return err ? VCD_ERR_GENERAL : VCD_NO_ERROR;
}

//
// VCD_cacheListMethodIds
//
static int VCD_cacheListMethodIds(JNIEnv *p_env)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    assert(p_env);

    s_mId_list_get = (*p_env)->GetMethodID(p_env, VCD_getCachedListClass(), "get", "(I)Ljava/lang/Object;");
    s_mId_list_size = (*p_env)->GetMethodID(p_env, VCD_getCachedListClass(), "size", "()I");

    int err = !s_mId_list_get || !s_mId_list_size;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return err ? VCD_ERR_GENERAL : VCD_NO_ERROR;
}

//
// VCD_cacheSurfaceTextureMethodIds
//
static int VCD_cacheSurfaceTextureMethodIds(JNIEnv *p_env)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    assert(p_env);

    s_mId_surfaceTextureConstructor = (*p_env)->GetMethodID(p_env, VCD_getCachedSurfaceTextureClass(), "<init>", "(I)V");

    int err = !s_mId_surfaceTextureConstructor;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return err ? VCD_ERR_GENERAL : VCD_NO_ERROR;
}

//
// VCD_cachePreviewCbHandlerMethodIds
//
// caches needed com/ericsson/gstreamer/VideoCaptureDevicePreviewCallbackHandler method ids
//
static int VCD_cachePreviewCbHandlerMethodIds(JNIEnv *p_env)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    assert(p_env);

    s_mId_previewCbHandlerConstructor = (*p_env)->GetMethodID(p_env, VCD_getCachedPreviewCbHandlerClass(), "<init>", "()V");
    s_mId_previewCbHandlerTest =        (*p_env)->GetMethodID(p_env, VCD_getCachedPreviewCbHandlerClass(), "test", "()V");

    int err = !s_mId_previewCbHandlerConstructor || !s_mId_previewCbHandlerTest;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return err ? VCD_ERR_GENERAL : VCD_NO_ERROR;
}

//
// VCD_cacheCameraMethodIds
//
// caches needed android/hardware/Camera method ids (plus cam parameters methods and others)
//
static int VCD_cacheCameraMethodIds(JNIEnv *p_env)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    assert(p_env);

    // FYI: The javap tool can be used to derive method signatures (like so: javap -s -p Camera (when Camera.class file exists in current dir) (Camera.class is obtained by compiling Camera.java file (e.g. using javac)))
    //      You can also figure them out yourself by e.g. looking here: http://journals.ecs.soton.ac.uk/java/tutorial/native1.1/implementing/method.html
    // Camera methods
    s_mId_addCallbackBuffer =                     (*p_env)->GetMethodID(p_env, VCD_getCachedCameraClass(), "addCallbackBuffer", "([B)V");
    s_mId_getCameraInfo =                         (*p_env)->GetStaticMethodID(p_env, VCD_getCachedCameraClass(), "getCameraInfo", "(ILandroid/hardware/Camera$CameraInfo;)V");
    s_mId_getNumberOfCameras =                    (*p_env)->GetStaticMethodID(p_env, VCD_getCachedCameraClass(), "getNumberOfCameras", "()I");
    s_mId_getParameters =                         (*p_env)->GetMethodID(p_env, VCD_getCachedCameraClass(), "getParameters", "()Landroid/hardware/Camera$Parameters;");
    s_mId_lock =                                  (*p_env)->GetMethodID(p_env, VCD_getCachedCameraClass(), "lock", "()V");
    s_mId_open_default =                          (*p_env)->GetStaticMethodID(p_env, VCD_getCachedCameraClass(), "open", "()Landroid/hardware/Camera;");
    s_mId_open =                                  (*p_env)->GetStaticMethodID(p_env, VCD_getCachedCameraClass(), "open", "(I)Landroid/hardware/Camera;");
    s_mId_release =                               (*p_env)->GetMethodID(p_env, VCD_getCachedCameraClass(), "release", "()V");
    s_mId_setDisplayOrientation =                 (*p_env)->GetMethodID(p_env, VCD_getCachedCameraClass(), "setDisplayOrientation", "(I)V");
    s_mId_setParameters =                         (*p_env)->GetMethodID(p_env, VCD_getCachedCameraClass(), "setParameters", "(Landroid/hardware/Camera$Parameters;)V");
    s_mId_setPreviewCallbackWithBuffer =          (*p_env)->GetMethodID(p_env, VCD_getCachedCameraClass(), "setPreviewCallbackWithBuffer", "(Landroid/hardware/Camera$PreviewCallback;)V");
    s_mId_setPreviewDisplay =                     (*p_env)->GetMethodID(p_env, VCD_getCachedCameraClass(), "setPreviewDisplay", "(Landroid/view/SurfaceHolder;)V");
    if (s_VCD_AndroidSdkVersion >= VCD_ANDROID_SDK_VERSION_WITH_SURFACE_TEXTURE) {
        s_mId_setPreviewTexture =                 (*p_env)->GetMethodID(p_env, VCD_getCachedCameraClass(), "setPreviewTexture", "(Landroid/graphics/SurfaceTexture;)V"); // API Level 11
    }
    s_mId_startPreview =                          (*p_env)->GetMethodID(p_env, VCD_getCachedCameraClass(), "startPreview", "()V");
    s_mId_stopPreview =                           (*p_env)->GetMethodID(p_env, VCD_getCachedCameraClass(), "stopPreview", "()V");
    s_mId_unlock =                                (*p_env)->GetMethodID(p_env, VCD_getCachedCameraClass(), "unlock", "()V");
    // CameraInfo methods
    s_mId_camInfoConstructor =                    (*p_env)->GetMethodID(p_env, VCD_getCachedCamInfoClass(), "<init>", "()V");
    // Camera Parameter methods
    s_mId_camParams_setPreviewSize =              (*p_env)->GetMethodID(p_env, VCD_getCachedCamParamsClass(), "setPreviewSize", "(II)V");
    s_mId_camParams_setPreviewFormat =            (*p_env)->GetMethodID(p_env, VCD_getCachedCamParamsClass(), "setPreviewFormat", "(I)V");
    s_mId_camParams_setPreviewFpsRange =          (*p_env)->GetMethodID(p_env, VCD_getCachedCamParamsClass(), "setPreviewFpsRange", "(II)V");
    s_mId_camParams_getSupportedPreviewFpsRange = (*p_env)->GetMethodID(p_env, VCD_getCachedCamParamsClass(), "getSupportedPreviewFpsRange", "()Ljava/util/List;");
    s_mId_camParams_getSupportedPreviewFormat =   (*p_env)->GetMethodID(p_env, VCD_getCachedCamParamsClass(), "getSupportedPreviewFormats", "()Ljava/util/List;");
    s_mId_camParams_getSupportedPreviewSizes =    (*p_env)->GetMethodID(p_env, VCD_getCachedCamParamsClass(), "getSupportedPreviewSizes", "()Ljava/util/List;");
    s_mId_camParams_setPreviewFrameRate =         (*p_env)->GetMethodID(p_env, VCD_getCachedCamParamsClass(), "setPreviewFrameRate", "(I)V"); // NOTE: Deprecated!

    int err = !s_mId_addCallbackBuffer
        || !s_mId_getCameraInfo
        || !s_mId_getNumberOfCameras
        || !s_mId_getParameters
        || !s_mId_lock
        || !s_mId_open_default
        || !s_mId_open
        || !s_mId_release
        || !s_mId_setDisplayOrientation
        || !s_mId_setParameters
        || !s_mId_setPreviewCallbackWithBuffer
        || !s_mId_setPreviewDisplay
        || !s_mId_startPreview
        || !s_mId_stopPreview
        || !s_mId_unlock
        || !s_mId_camInfoConstructor
        || !s_mId_camParams_setPreviewSize
        || !s_mId_camParams_setPreviewFormat
        || !s_mId_camParams_setPreviewFpsRange
        || !s_mId_camParams_getSupportedPreviewFpsRange
        || !s_mId_camParams_getSupportedPreviewFormat
        || !s_mId_camParams_getSupportedPreviewSizes
        || !s_mId_camParams_setPreviewFrameRate;

    if (s_VCD_AndroidSdkVersion >= VCD_ANDROID_SDK_VERSION_WITH_SURFACE_TEXTURE) {
        err = err || !s_mId_setPreviewTexture;
    }

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return err ? VCD_ERR_GENERAL : VCD_NO_ERROR;
}

//
// getMethodId
//
static jmethodID getMethodId(VCD_MethodId VCD_id)
{
    jmethodID id = NULL;

    switch (VCD_id) {
    // Camera methods
    case VCD_METHODID_CAMERA_ADDCALLBACKBUFFER:
        id = s_mId_addCallbackBuffer;
        break;
    case VCD_METHODID_CAMERA_GETCAMERAINFO:
        id = s_mId_getCameraInfo;
        break;
    case VCD_METHODID_CAMERA_GETNUMBEROFCAMERAS:
        id = s_mId_getNumberOfCameras;
        break;
    case VCD_METHODID_CAMERA_GETPARAMETERS:
        id = s_mId_getParameters;
        break;
    case VCD_METHODID_CAMERA_LOCK:
        id = s_mId_lock;
        break;
    case VCD_METHODID_CAMERA_OPEN_DEFAULT:
        id = s_mId_open_default;
        break;
    case VCD_METHODID_CAMERA_OPEN:
        id = s_mId_open;
        break;
    case VCD_METHODID_CAMERA_RELEASE:
        id = s_mId_release;
        break;
    case VCD_METHODID_CAMERA_SETDISPLAYORIENTATION:
        id = s_mId_setDisplayOrientation;
        break;
    case VCD_METHODID_CAMERA_SETPARAMETERS:
        id = s_mId_setParameters;
        break;
    case VCD_METHODID_CAMERA_SETPREVIEWCALLBACKWITHBUFFER:
        id = s_mId_setPreviewCallbackWithBuffer;
        break;
    case VCD_METHODID_CAMERA_SETPREVIEWDISPLAY:
        id = s_mId_setPreviewDisplay; // API Level 11
        break;
    case VCD_METHODID_CAMERA_SETPREVIEWTEXTURE:
        id = s_mId_setPreviewTexture;
        break;
    case VCD_METHODID_CAMERA_STARTPREVIEW:
        id = s_mId_startPreview;
        break;
    case VCD_METHODID_CAMERA_STOPPREVIEW:
        id = s_mId_stopPreview;
        break;
    case VCD_METHODID_CAMERA_UNLOCK:
        id = s_mId_unlock;
        break;
    // CameraInfo methods
    case VCD_METHODID_CAMERAINFO_CONSTRUCTOR:
        id = s_mId_camInfoConstructor;
        break;
    // Camera Parameter methods
    case VCD_METHODID_CAM_PARAMS_SETPREVIEWSIZE:
        id = s_mId_camParams_setPreviewSize;
        break;
    case VCD_METHODID_CAM_PARAMS_SETPREVIEWFORMAT:
        id = s_mId_camParams_setPreviewFormat;
        break;
    case VCD_METHODID_CAM_PARAMS_SETPREVIEWFPSRANGE:
        id = s_mId_camParams_setPreviewFpsRange;
        break;
    case VCD_METHODID_CAM_PARAMS_GETSUPPORTEDPREVIEWFPSRANGE:
        id = s_mId_camParams_getSupportedPreviewFpsRange;
        break;
    case VCD_METHODID_CAM_PARAMS_GETSUPPORTEDPREVIEWFORMATS:
        id = s_mId_camParams_getSupportedPreviewFormat;
        break;
    case VCD_METHODID_CAM_PARAMS_GETSUPPORTEDPREVIEWSIZES:
        id = s_mId_camParams_getSupportedPreviewSizes;
        break;
    case VCD_METHODID_CAM_PARAMS_SETPREVIEWFRAMERATE:
        id = s_mId_camParams_setPreviewFrameRate;
        break;
    // preview callback methods
    case VCD_METHODID_PREVIEW_CB_HANDLER_CONSTRUCTOR:
        id = s_mId_previewCbHandlerConstructor;
        break;
    case VCD_METHODID_PREVIEW_CB_HANDLER_TEST:
        id = s_mId_previewCbHandlerTest;
        break;
    // surface texture methods
    case VCD_METHODID_SURFACE_TEXTURE_CONSTRUCTOR:
        id = s_mId_surfaceTextureConstructor;
        break;
    // List class methods
    case VCD_METHODID_LIST_GET:
        id = s_mId_list_get;
        break;
    case VCD_METHODID_LIST_SIZE:
        id = s_mId_list_size;
        break;
    // Integer class methods
    case VCD_METHODID_INTEGER_INTVALUE:
        id = s_mId_integer_intValue;
        break;
    default:
        GA_LOGERROR("%s ERROR: unknown method id %d", __FILE__, VCD_id);
        id = NULL;
        break;
    }

    return id;
}


//
// VCD_open
//
VCD_handle VCD_open(int logInterval)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    VCD_VideoCaptureDevice *p_obj = calloc(1, sizeof(*p_obj));
    AV_CHECK_NULL(p_obj, VCD_open_err_calloc_rec_device);

    // Some inits...
    p_obj->m_state = VCD_STATE_OPENED;
    p_obj->m_camMediaSupport = NULL;
    p_obj->m_mediaSupportKnown = FALSE;
    p_obj->m_mediaTypeIsFixed = FALSE;
    p_obj->m_fixedFormat = -1;
    p_obj->m_fixedFramerate = -1;
    p_obj->m_fixedWidth = -1;
    p_obj->m_fixedHeight = -1;
    p_obj->m_fixedTrueWidth = -1;
    p_obj->m_fixedTrueHeight = -1;

    s_VCD_handle = p_obj;

    p_obj->m_logInterval = logInterval;
    gettimeofday(&(p_obj->m_onPreviewFrameTimer), NULL);
    gettimeofday(&(p_obj->m_readTimer), NULL);

    gl_VCD_prevPreferredFrontFacingCamera = gl_VCD_preferFrontFacingCamera;

    p_obj->m_numberOfCameras = VCD_getNumberOfCameras();
    if (p_obj->m_numberOfCameras < 0) {
        goto VCD_open_err_camera_count_failed;
    }
    if (p_obj->m_numberOfCameras < 1) {
        goto VCD_open_err_no_cameras;
    }

    AV_CHECK_ERR(VCD_gatherMediaSupport(p_obj), VCD_open_err_gather_media_support);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return p_obj;

    /*
     * propagate unhandled errors
     */
VCD_open_err_calloc_rec_device:
    {
        GA_LOGERROR("%s: ERROR: allocate VCD_VideoCaptureDevice obj failed => out of memory. Could not open device.", __FUNCTION__);
        return NULL;
    }
VCD_open_err_gather_media_support:
    {
        GA_LOGERROR("%s: ERROR: Failed to gather media support for device. Could not open device.", __FUNCTION__);
        return NULL;
    }
VCD_open_err_camera_count_failed:
    {
        GA_LOGERROR("%s: ERROR: Could not count cameras on device. Could not open device.", __FUNCTION__);
        return NULL;
    }
VCD_open_err_no_cameras:
    {
        GA_LOGERROR("%s: ERROR: Device has no cameras. Cannot open device without cameras.", __FUNCTION__);
        return NULL;
    }
}

//
// VCD_close
//
int VCD_close(VCD_handle handle)
{
    int i;
    int cam;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;

    if (!p_obj) {
        // this might indicate a design/implementation problem
        // the normal usage is to close an open video capture device only once
        GA_LOGWARN("%s WARNING: handle = NULL", __FUNCTION__);

        return VCD_NO_ERROR;
    }

    if (p_obj->m_camMediaSupport) {
        for (cam = 0; cam < p_obj->m_numberOfCameras; cam++) {
            VCD_CamMediaSupport *p_camMediaSupport = p_obj->m_camMediaSupport[cam];
            if (p_camMediaSupport) {
                if (p_camMediaSupport->fmt) {
                    free(p_camMediaSupport->fmt);
                    p_camMediaSupport->fmt = NULL;
                }
                if (p_camMediaSupport->fps) {
                    for (i = 0; i < p_camMediaSupport->fpsLen; i++) {
                        free(p_camMediaSupport->fps[i]);
                        p_camMediaSupport->fps[i] = NULL;
                    }
                    free(p_camMediaSupport->fps);
                    p_camMediaSupport->fps = NULL;
                }
                if (p_camMediaSupport->size) {
                    for (i = 0; i < p_camMediaSupport->sizeLen; i++) {
                        free(p_camMediaSupport->size[i]);
                        p_camMediaSupport->size[i] = NULL;
                    }
                    free(p_camMediaSupport->size);
                    p_camMediaSupport->size = NULL;
                }
                free(p_obj->m_camMediaSupport[cam]); // free(p_camMediaSupport)
                p_obj->m_camMediaSupport[cam] = NULL; // p_camMediaSupport = NULL
            }
        }
        free(p_obj->m_camMediaSupport);
        p_obj->m_camMediaSupport = NULL;
    }

    if (p_obj->m_state != VCD_STATE_OPENED) {
        goto VCD_close_err_close_illegal_state;
    }
    s_VCD_handle = NULL;
    free(p_obj);
    p_obj = NULL;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return VCD_NO_ERROR;

    /*
     * propagate unhandled errors
     */
VCD_close_err_close_illegal_state:
    {
        GA_LOGERROR("%s %s ERROR: illegal state (%d)", __FILE__, __FUNCTION__, p_obj->m_state);
        return VCD_ERR_ILLEGAL_STATE;
    }
}

//
// VCD_start
//
int VCD_start(VCD_handle handle)
{
    int ret = 0;
    int vcd_ret = VCD_NO_ERROR;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    if (p_obj->m_state != VCD_STATE_PREPARED && p_obj->m_state != VCD_STATE_STOPPED) {
        goto VCD_start_err_start_illegal_state;
    }

    // Set no more data just to be sure
    AV_CHECK_ERR(pthread_mutex_lock(&(p_obj->m_dataFlagMutex)), VCD_start_err_start_mutex);
    p_obj->m_dataFlag = NO_BUF_HAS_DATA;
    AV_CHECK_ERR(ret = pthread_mutex_unlock(&(p_obj->m_dataFlagMutex)), VCD_start_err_start_mutex);

    // Take the first buffer by locking it and adding it to the preview queue
    AV_CHECK_ERR(pthread_mutex_lock(&(p_obj->m_bufOneMutex)), VCD_start_err_start_mutex);
    VCD_addCallbackBuffer(handle, p_obj->m_javaBufOneObj);
    p_obj->m_currPreviewBuf = BUF_ID_ONE;

    p_obj->m_hasStoppedFlag = FALSE;
    p_obj->m_state = VCD_STATE_RUNNING; // Has to be set before call to VCD_startPreview() since preview callback function checks this flag

    VCD_startPreview(handle);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return vcd_ret;

    /*
     * propagate unhandled errors
     */
VCD_start_err_start_illegal_state:
    {
        GA_LOGERROR("%s: ERROR: illegal state: %d", __FILE__, p_obj->m_state);
        return VCD_ERR_ILLEGAL_STATE;
    }
VCD_start_err_start_mutex:
    {
        GA_LOGERROR("%s: ERROR: Problems with mutex. Return value: %d", __FUNCTION__, ret);
        return VCD_ERR_GENERAL;
    }
}

//
// VCD_stop
//
int VCD_stop(VCD_handle handle)
{
    int ret;
    int vcd_ret = VCD_NO_ERROR;
    struct timespec time_to_wait;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    if (p_obj->m_state == VCD_STATE_STOPPED) {
        goto VCD_stop_err_stop_useless;
    }
    if (p_obj->m_state != VCD_STATE_RUNNING) {
        goto VCD_stop_err_stop_illegal_state;
    }

    // Signal preview callback function to stop by setting state
    p_obj->m_state = VCD_STATE_STOPPED;
    // Wait for preview callback to confirm this so that it has time to unlock its current buffer
    AV_CHECK_ERR(ret = pthread_mutex_lock(&(p_obj->m_hasStoppedMutex)), VCD_stop_err_stop_mutex);
    if (!p_obj->m_hasStoppedFlag) {
        GA_LOGINFO("%s: Waiting for the 'preview callback loop' to end...", __FUNCTION__);
        create_wait_time(&time_to_wait, DEFAULT_TIME_TO_WAIT_FOR_STOP_IN_SECONDS);
        ret = pthread_cond_timedwait(&(p_obj->m_hasStoppedCondition), &(p_obj->m_hasStoppedMutex), &time_to_wait);
        p_obj->m_hasStoppedFlag = TRUE;
        if (ret == ETIMEDOUT) {
            GA_LOGERROR("%s: ERROR: ...'preview callback loop' never signalled a confirmed stop. This likely means that at least one buffer mutex is still locked which will cause an error if/when the mutex is destroyed.", __FUNCTION__);
        } else if (ret) { // ret > 0 --> Error
            pthread_mutex_unlock(&(p_obj->m_hasStoppedMutex));
            goto VCD_stop_err_stop_mutex;
        }
    }
    AV_CHECK_ERR(ret = pthread_mutex_unlock(&(p_obj->m_hasStoppedMutex)), VCD_stop_err_stop_mutex);

    // Now we can safely call stopPreview which will "kill" the preview callback loop
    VCD_stopPreview(handle);

    // Set no more data and broadcast data flag to free up potentially blocked threads
    AV_CHECK_ERR(ret = pthread_mutex_lock(&(p_obj->m_dataFlagMutex)), VCD_stop_err_stop_mutex);
    p_obj->m_dataFlag = NO_BUF_HAS_DATA;
    AV_CHECK_ERR(pthread_cond_broadcast(&(p_obj->m_dataFlagCondition)), VCD_stop_err_stop_cond);
    AV_CHECK_ERR(pthread_mutex_unlock(&(p_obj->m_dataFlagMutex)), VCD_stop_err_stop_mutex);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return vcd_ret;

    /*
     * propagate unhandled errors
     */
VCD_stop_err_stop_illegal_state:
    {
        GA_LOGERROR("%s: ERROR: illegal state: %d", __FILE__, p_obj->m_state);
        return VCD_ERR_ILLEGAL_STATE;
    }
VCD_stop_err_stop_useless:
    {
        GA_LOGWARN("%s: already in stopped state --> doing nothing...", __FUNCTION__);
        return VCD_NO_ERROR;
    }
VCD_stop_err_stop_mutex:
    {
        GA_LOGERROR("%s: ERROR: Problems with mutex. Return value: %d", __FUNCTION__, ret);
        return VCD_ERR_GENERAL;
    }
VCD_stop_err_stop_cond:
    {
        GA_LOGERROR("%s: ERROR: Problems when broadcasting condition. Return value: %d", __FUNCTION__, ret);
        return VCD_ERR_GENERAL;
    }
}


//
// VCD_integer_intValue --> Integer class method
//
jint VCD_integer_intValue(jobject integer)
{
    // GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    assert(integer);
    JNIEnv *env = VCD_getEnv();
    jint value = (jint) (*env)->CallIntMethod(env, integer, getMethodId(VCD_METHODID_INTEGER_INTVALUE));
    // GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return value;
}

//
// VCD_list_get --> List class method
//
jobject VCD_list_get(jobject list, jint location)
{
    // GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    assert(list);
    JNIEnv *env = VCD_getEnv();
    jobject elem = (*env)->CallObjectMethod(env, list, getMethodId(VCD_METHODID_LIST_GET), location);
    // GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return elem;
}

//
// VCD_list_size --> List class method
//
jint VCD_list_size(jobject list)
{
    // GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    assert(list);
    JNIEnv *env = VCD_getEnv();
    // NOTE: Below is used the method ID "retrieved" from the clazz rather the method ID
    //       "retrieved" from doing GetMethodID() here on the actual instance. I thought
    //       the latter was (more) right, but it doesn't work, while this works, so.....
    jint size = (jint) (*env)->CallIntMethod(env, list, getMethodId(VCD_METHODID_LIST_SIZE));

    // GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return size;
}

//
// VCD_previewCbHandlerTest --> VideoCaptureDevicePreviewCallbackHandler class method
//
void VCD_previewCbHandlerTest(VCD_handle handle)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, p_obj->m_javaPreviewCbObj, VCD_getCachedPreviewCbHandlerClass(),
        getMethodId(VCD_METHODID_PREVIEW_CB_HANDLER_TEST));
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}

//
// VCD_camSize_getIntField --> Camera Size class int fields
//
jint VCD_camSize_getIntField(jobject camSize, const char *fieldName)
{
    // GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    assert(camSize);
    assert(fieldName);
    JNIEnv *env = VCD_getEnv();
    jfieldID fieldID = (*env)->GetFieldID(env, VCD_getCachedCamSizeClass(), fieldName, "I");
    if (!fieldID) {
        GA_LOGERROR("%s: FATAL ERROR: GetFieldID for '%s' for Camera Size class failed", __FUNCTION__, fieldName);
        assert(0);
        return VCD_ERR_GENERAL;
    }
    jint fieldValue = (*env)->GetIntField(env, camSize, fieldID);

    // GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return fieldValue;
}

//
// VCD_camInfo_getIntField --> Camera.CameraInfo class int fields
//
jint VCD_camInfo_getIntField(jobject camInfo, const char *fieldName, int isStatic)
{
    // GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    assert(camInfo);
    assert(fieldName);
    jfieldID fieldID;
    jint fieldValue;
    JNIEnv *env = VCD_getEnv();
    if (isStatic) {
        fieldID = (*env)->GetStaticFieldID(env, VCD_getCachedCamInfoClass(), fieldName, "I");
    } else {
        fieldID = (*env)->GetFieldID(env, VCD_getCachedCamInfoClass(), fieldName, "I");
    }
    if (!fieldID) {
        GA_LOGERROR("%s: FATAL ERROR: %s for '%s' for Camera.CameraInfo class failed", __FUNCTION__, isStatic ? "GetStaticFieldID" : "GetFieldID", fieldName);
        assert(0);
        return VCD_ERR_GENERAL;
    }
    if (isStatic) {
        fieldValue = (*env)->GetStaticIntField(env, VCD_getCachedCamInfoClass(), fieldID);
    } else {
        fieldValue = (*env)->GetIntField(env, camInfo, fieldID);
    }

    // GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return fieldValue;
}


//
// VCD_preferFrontFacingCamera
//
int VCD_preferFrontFacingCamera(int preferFrontFacingCamera)
{
    GA_LOGINFO("%s: Setting gl_VCD_preferFrontFacingCamera to %s. Previous value was %s", __FUNCTION__, preferFrontFacingCamera ? "TRUE" : "FALSE", gl_VCD_preferFrontFacingCamera ? "TRUE" : "FALSE");
    gl_VCD_preferFrontFacingCamera = preferFrontFacingCamera;
    return VCD_NO_ERROR;
}

//
// VCD_useCameraWithIndex
//
int VCD_useCameraWithIndex(int index)
{
    GA_LOGINFO("%s: Setting gl_VCD_useCameraWithIndex to %d. Previous value was %d", __FUNCTION__, index, gl_VCD_useCameraWithIndex);
    gl_VCD_useCameraWithIndex = index;
    return VCD_NO_ERROR;
}

//
// VCD_camIsBackFacing
//
int VCD_camIsBackFacing(int cameraId)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    int facing;
    int backFacing;
    int frontFacing;
    JNIEnv *env = VCD_getEnv();
    jobject camInfo = (*env)->NewObject(env, VCD_getCachedCamInfoClass(), getMethodId(VCD_METHODID_CAMERAINFO_CONSTRUCTOR));
    if ((*env)->ExceptionCheck(env)) {
        GA_LOGERROR("%s: Failed to create CameraInfo object", __FUNCTION__);
        assert(0);
        return VCD_ERR_GENERAL;
    }
    VCD_getCameraInfo(cameraId, camInfo);
    backFacing = (int) VCD_camInfo_getIntField(camInfo, "CAMERA_FACING_BACK", TRUE/*static field*/);
    frontFacing = (int) VCD_camInfo_getIntField(camInfo, "CAMERA_FACING_FRONT", TRUE/*static field*/);
    facing = (int) VCD_camInfo_getIntField(camInfo, "facing", FALSE/*non-static field*/);
    (*env)->DeleteLocalRef(env, camInfo);
    GA_LOGVERB("%s: Camera with ID %d has the 'facing' value = %d", __FUNCTION__, cameraId, facing);
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    if (facing == backFacing) {
        return TRUE;
    }
    if (facing == frontFacing) {
        return FALSE;
    }
    GA_LOGERROR("%s: Strange value from CameraInfo. Camera is not back facing nor is it front facing... where the hell is it facing...?", __FUNCTION__);
    return VCD_ERR_GENERAL;
}

//
// VCD_getIndexOfCameraToUse
//
int VCD_getIndexOfCameraToUse(VCD_handle handle)
{
    int i;
    int foundPreferredFacingCamera = FALSE;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld) (no EXIT is logged)", __FUNCTION__, pthread_self());

    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);

    if (p_obj->m_numberOfCameras < 2) {
        GA_LOGINFO("%s: Device has only one camera. Will use this one and only camera.", __FUNCTION__);
        return 0;
    }
    if (gl_VCD_useCameraWithIndex >= 0) {
        if (gl_VCD_useCameraWithIndex > p_obj->m_numberOfCameras - 1) {
            GA_LOGWARN("%s: Camera with chosen index (%d) does not exist. Choosing another camera...", __FUNCTION__, gl_VCD_useCameraWithIndex);
        } else {
            GA_LOGINFO("%s: Using camera with index %d. Camera is %s facing.", __FUNCTION__, gl_VCD_useCameraWithIndex, p_obj->m_camMediaSupport[gl_VCD_useCameraWithIndex]->camIsBackFacing ? "back" : "front");
            return gl_VCD_useCameraWithIndex;
        }
    }
    for (i = 0; i < p_obj->m_numberOfCameras; i++) {
        if (p_obj->m_camMediaSupport[i]->camIsBackFacing != gl_VCD_preferFrontFacingCamera) {
            foundPreferredFacingCamera = TRUE;
            break;
        }
    }
    if (foundPreferredFacingCamera) {
        GA_LOGINFO("%s: Found %s facing camera as preferred. Camera index is %d", __FUNCTION__, gl_VCD_preferFrontFacingCamera ? "front" : "back", i);
        return i;
    }
    GA_LOGWARN("%s: Did not find %s facing camera as preferred. Will instead choose the first %s facing camera (i.e. camera with index 0)", __FUNCTION__, gl_VCD_preferFrontFacingCamera ? "front" : "back", gl_VCD_preferFrontFacingCamera ? "back" : "front");
    return 0;
}


//
// VCD_camParams_setPreviewSize --> Camera Parameter class method
//
void VCD_camParams_setPreviewSize(jobject camParams, int width, int height)
{
    // GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    assert(camParams);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, camParams, VCD_getCachedCamParamsClass(),
        getMethodId(VCD_METHODID_CAM_PARAMS_SETPREVIEWSIZE), width, height);
    GA_LOGINFO("%s: called setPreviewSize() with %dx%d", __FUNCTION__, width, height);
    // GA_LOGTRACE("EXIT %s", __FUNCTION__);
}

//
// VCD_camParams_setPreviewFormat --> Camera Parameter class method
//
void VCD_camParams_setPreviewFormat(jobject camParams, int pixel_format)
{
    // GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    assert(camParams);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, camParams, VCD_getCachedCamParamsClass(),
        getMethodId(VCD_METHODID_CAM_PARAMS_SETPREVIEWFORMAT), pixel_format);
    GA_LOGINFO("%s: Called setPreviewFormat() with 0x%x", __FUNCTION__, pixel_format);
    // GA_LOGTRACE("EXIT %s", __FUNCTION__);
}

//
// VCD_camParams_setPreviewFpsRange --> Camera Parameter class method
//
void VCD_camParams_setPreviewFpsRange(jobject camParams, int min, int max)
{
    // GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    assert(camParams);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, camParams, VCD_getCachedCamParamsClass(),
        getMethodId(VCD_METHODID_CAM_PARAMS_SETPREVIEWFPSRANGE), min, max);
    GA_LOGINFO("%s: Called setPreviewFpsRange with [%d, %d]", __FUNCTION__, min, max);
    // GA_LOGTRACE("EXIT %s", __FUNCTION__);
}

//
// VCD_camParams_setPreviewFrameRate --> Camera Parameter class method
//
// NOTE: This method is deprecated!
//
void VCD_camParams_setPreviewFrameRate(jobject camParams, int fps)
{
    // GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    assert(camParams);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, camParams, VCD_getCachedCamParamsClass(),
        getMethodId(VCD_METHODID_CAM_PARAMS_SETPREVIEWFRAMERATE), fps);
    GA_LOGINFO("%s: Called setPreviewFrameRate with %d", __FUNCTION__, fps);
    // GA_LOGTRACE("EXIT %s", __FUNCTION__);
}

//
// VCD_camParams_getSupportedPreviewFpsRange --> Camera Parameter class method
//
void VCD_camParams_getSupportedPreviewFpsRange(jobject camParams, VCD_CamMediaSupport *p_camMediaSupport)
{
    int i;
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    assert(camParams);
    assert(p_camMediaSupport);
    JNIEnv *env = VCD_getEnv();
    jobject fpsRanges = (*env)->CallNonvirtualObjectMethod(
        env, camParams, VCD_getCachedCamParamsClass(),
        getMethodId(VCD_METHODID_CAM_PARAMS_GETSUPPORTEDPREVIEWFPSRANGE));
    int size = (int) VCD_list_size(fpsRanges);
    p_camMediaSupport->fps = (int**) malloc(sizeof(int*) * size);
    AV_CHECK_NULL(p_camMediaSupport->fps, VCD_camParams_getSupportedPreviewFpsRange_err_out_of_memory);
    GA_LOGVERB("%s: size of fpsRanges list: %d", __FUNCTION__, size);
    for (i = 0; i < size; i++) {
        jobject fpsObj = VCD_list_get(fpsRanges, i);
        jarray jminAndMaxFps = (jarray) fpsObj;
        int mamfSize = (int) (*env)->GetArrayLength(env, jminAndMaxFps); // According to Android SDK this array should always have length 2 and only length 2
        if (mamfSize != 2) {
            goto VCD_camParams_getSupportedPreviewFpsRange_err_array;
        }
        jint *cminAndMaxFps = (*env)->GetIntArrayElements(env, jminAndMaxFps, 0);
        GA_LOGVERB("%s:   ---> ranges for pos %d: [%d, %d]", __FUNCTION__, i, ((int*) cminAndMaxFps)[MIN_FPS_POS], ((int*) cminAndMaxFps)[MAX_FPS_POS]);
        int *minMax = (int*) malloc(sizeof(int) * 2);
        AV_CHECK_NULL(minMax, VCD_camParams_getSupportedPreviewFpsRange_err_out_of_memory);
        minMax[MIN_FPS_POS] = (int) cminAndMaxFps[MIN_FPS_POS];
        minMax[MAX_FPS_POS] = (int) cminAndMaxFps[MAX_FPS_POS];
        p_camMediaSupport->fps[i] = minMax;
        (*env)->ReleaseIntArrayElements(env, jminAndMaxFps, cminAndMaxFps, 0);
        (*env)->DeleteLocalRef(env, fpsObj);
    }
    (*env)->DeleteLocalRef(env, fpsRanges);
    p_camMediaSupport->fpsLen = size;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return;

    /*
     * propagate unhandled errors
     */
VCD_camParams_getSupportedPreviewFpsRange_err_out_of_memory:
    {
        GA_LOGERROR("%s: ERROR: Out of memory!", __FUNCTION__);
        assert(0);
        return;
    }
VCD_camParams_getSupportedPreviewFpsRange_err_array:
    {
        GA_LOGERROR("%s: FATAL ERROR: Array length of min and max framerate array != 2", __FUNCTION__);
        assert(0);
        return;
    }
}

//
// VCD_camParams_getSupportedPreviewFormats --> Camera Parameter class method
//
void VCD_camParams_getSupportedPreviewFormats(jobject camParams, VCD_CamMediaSupport *p_camMediaSupport)
{
    int i;
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    assert(camParams);
    assert(p_camMediaSupport);
    JNIEnv *env = VCD_getEnv();
    jobject prevFmts = (*env)->CallNonvirtualObjectMethod(
        env, camParams, VCD_getCachedCamParamsClass(),
        getMethodId(VCD_METHODID_CAM_PARAMS_GETSUPPORTEDPREVIEWFORMATS));
    int size = (int) VCD_list_size(prevFmts);
    GA_LOGVERB("%s: size of prevFmts list: %d", __FUNCTION__, size);
    p_camMediaSupport->fmt = (int*) malloc(sizeof(int) * size);
    AV_CHECK_NULL(p_camMediaSupport->fmt, VCD_camParams_getSupportedPreviewFormats_err_out_of_memory);
    for (i = 0; i < size; i++) {
        jobject fmtObj = VCD_list_get(prevFmts, i);
        int fmt = (int) VCD_integer_intValue(fmtObj);
        (*env)->DeleteLocalRef(env, fmtObj);
        GA_LOGVERB("%s:   ---> format for pos %d is: 0x%x", __FUNCTION__, i, fmt);
        p_camMediaSupport->fmt[i] = fmt;
    }
    (*env)->DeleteLocalRef(env, prevFmts);
    p_camMediaSupport->fmtLen = size;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return;

    /*
     * propagate unhandled errors
     */
VCD_camParams_getSupportedPreviewFormats_err_out_of_memory:
    {
        GA_LOGERROR("%s: ERROR: Out of memory!", __FUNCTION__);
        assert(0);
        return;
    }
}

//
// VCD_camParams_getSupportedPreviewSizes --> Camera Parameter class method
//
void VCD_camParams_getSupportedPreviewSizes(jobject camParams, VCD_CamMediaSupport *p_camMediaSupport)
{
    int i;
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    assert(camParams);
    assert(p_camMediaSupport);
    JNIEnv *env = VCD_getEnv();
    jobject prevSizes = (*env)->CallNonvirtualObjectMethod(
        env, camParams, VCD_getCachedCamParamsClass(),
        getMethodId(VCD_METHODID_CAM_PARAMS_GETSUPPORTEDPREVIEWSIZES));
    int size = (int) VCD_list_size(prevSizes);
    GA_LOGVERB("%s: size of prevSizes list: %d", __FUNCTION__, size);
    p_camMediaSupport->size = (int**) malloc(sizeof(int*) * size);
    AV_CHECK_NULL(p_camMediaSupport->size, VCD_camParams_getSupportedPreviewSizes_err_out_of_memory);
    for (i = 0; i < size; i++) {
        jobject sizeObj = VCD_list_get(prevSizes, i); // this object is of type Camera$Size
        int width, height;
        width = (int) VCD_camSize_getIntField(sizeObj, "width");
        height = (int) VCD_camSize_getIntField(sizeObj, "height");
        (*env)->DeleteLocalRef(env, sizeObj);
        GA_LOGVERB("%s:   ---> size of pos %d is: %dx%d", __FUNCTION__, i, width, height);
        int *widthAndHeight = (int*) malloc(sizeof(int) * 2);
        AV_CHECK_NULL(widthAndHeight, VCD_camParams_getSupportedPreviewSizes_err_out_of_memory);
        widthAndHeight[WIDTH_POS] = width;
        widthAndHeight[HEIGHT_POS] = height;
        p_camMediaSupport->size[i] = widthAndHeight;
    }
    (*env)->DeleteLocalRef(env, prevSizes);
    p_camMediaSupport->sizeLen = size;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return;

    /*
     * propagate unhandled errors
     */
VCD_camParams_getSupportedPreviewSizes_err_out_of_memory:
    {
        GA_LOGERROR("%s: ERROR: Out of memory!", __FUNCTION__);
        assert(0);
        return;
    }
}

//
// VCD_addCallbackBuffer --> Camera class method
//
void VCD_addCallbackBuffer(VCD_handle handle, jobject buffer)
{
    // GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, p_obj->m_javaCameraObj, VCD_getCachedCameraClass(),
        getMethodId(VCD_METHODID_CAMERA_ADDCALLBACKBUFFER), buffer);
    // GA_LOGTRACE("EXIT %s", __FUNCTION__);
}

//
// VCD_getCameraInfo --> Camera class method
//
void VCD_getCameraInfo(int cameraIndex, jobject camInfo)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    JNIEnv *env = VCD_getEnv();
    (*env)->CallStaticVoidMethod(
        env, VCD_getCachedCameraClass(),
        getMethodId(VCD_METHODID_CAMERA_GETCAMERAINFO), cameraIndex, camInfo);
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}

//
// VCD_getNumberOfCameras --> Camera class method
//
int VCD_getNumberOfCameras()
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    JNIEnv *env = VCD_getEnv();
    int numberOfCameras = (int) (*env)->CallStaticIntMethod(
        env, VCD_getCachedCameraClass(),
        getMethodId(VCD_METHODID_CAMERA_GETNUMBEROFCAMERAS));
    if (0 >= numberOfCameras) {
        goto err_numberOfCameras;
    }
    GA_LOGVERB("Camera.getNumberOfCameras returned: %d", numberOfCameras);
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return numberOfCameras;

    /*
     * propagate unhandled errors
     */
err_numberOfCameras:
    {
        GA_LOGERROR("%s %s ERROR: getting number of cameras for hardware (Camera class) failed. Returned: %d",
            __FILE__, __FUNCTION__, numberOfCameras);
        return VCD_ERR_GENERAL;
    }
}

//
// VCD_getParameters --> Camera class method
//
jobject VCD_getParameters(VCD_handle handle, jobject camera)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    VCD_UNUSED(p_obj);
    JNIEnv *env = VCD_getEnv();
    jobject paramsObj = (*env)->CallNonvirtualObjectMethod(
        env, camera, VCD_getCachedCameraClass(),
        getMethodId(VCD_METHODID_CAMERA_GETPARAMETERS));
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return paramsObj;
}

//
// VCD_setParameters --> Camera class method
//
void VCD_setParameters(VCD_handle handle, jobject paramsObj)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, p_obj->m_javaCameraObj, VCD_getCachedCameraClass(),
        getMethodId(VCD_METHODID_CAMERA_SETPARAMETERS), paramsObj);
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}

//
// VCD_startPreview --> Camera class method
//
void VCD_startPreview(VCD_handle handle)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, p_obj->m_javaCameraObj, VCD_getCachedCameraClass(),
        getMethodId(VCD_METHODID_CAMERA_STARTPREVIEW));
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


//
// VCD_stopPreview --> Camera class method
//
void VCD_stopPreview(VCD_handle handle)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, p_obj->m_javaCameraObj, VCD_getCachedCameraClass(),
        getMethodId(VCD_METHODID_CAMERA_STOPPREVIEW));
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


//
// VCD_setPreviewCallbackWithBuffer --> Camera class method
//
void VCD_setPreviewCallbackWithBuffer(VCD_handle handle, jobject previewCallback)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, p_obj->m_javaCameraObj, VCD_getCachedCameraClass(),
        getMethodId(VCD_METHODID_CAMERA_SETPREVIEWCALLBACKWITHBUFFER), previewCallback);
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


//
// VCD_setDisplayOrientation --> Camera class method
//
void VCD_setDisplayOrientation(VCD_handle handle, int displayOrientation)
{
    // GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, p_obj->m_javaCameraObj, VCD_getCachedCameraClass(),
        getMethodId(VCD_METHODID_CAMERA_SETDISPLAYORIENTATION), displayOrientation);
    GA_LOGVERB("%s: Called SetDisplayOrientation() with %d", __FUNCTION__, displayOrientation);
    // GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


//
// VCD_setPreviewDisplay --> Camera class method
//
void VCD_setPreviewDisplay(VCD_handle handle, jobject surfaceHolder)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, p_obj->m_javaCameraObj, VCD_getCachedCameraClass(),
        getMethodId(VCD_METHODID_CAMERA_SETPREVIEWDISPLAY), surfaceHolder);
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


//
// VCD_setPreviewTexture --> Camera class method (API Level 11)
//
void VCD_setPreviewTexture(VCD_handle handle, jobject surfaceTexture)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, p_obj->m_javaCameraObj, VCD_getCachedCameraClass(),
        getMethodId(VCD_METHODID_CAMERA_SETPREVIEWTEXTURE), surfaceTexture);
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


//
// VCD_release --> Camera class method
//
void VCD_release(VCD_handle handle)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    JNIEnv *env = VCD_getEnv();
    (*env)->CallNonvirtualVoidMethod(
        env, p_obj->m_javaCameraObj, VCD_getCachedCameraClass(),
        getMethodId(VCD_METHODID_CAMERA_RELEASE));
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


//
// Some getters...
//
// VCD_getMediaSupportFmtLen()
int VCD_getMediaSupportFmtLen(VCD_handle handle)
{
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    return p_obj->m_camMediaSupport[gl_VCD_currentCameraIndex]->fmtLen;
}
// VCD_getMediaSupportFmt()
int *VCD_getMediaSupportFmt(VCD_handle handle)
{
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    return p_obj->m_camMediaSupport[gl_VCD_currentCameraIndex]->fmt;
}


//
// VCD_GetMinResolution
//
int VCD_GetMinResolution(VCD_handle handle, int *p_minWidth, int *p_minHeight)
{
    assert(p_minWidth);
    assert(p_minHeight);

    *p_minWidth = 2;
    *p_minHeight = 2;

    return VCD_NO_ERROR;
}


//
// VCD_GetMaxResolution
//
int VCD_GetMaxResolution(VCD_handle handle, int *p_maxWidth, int *p_maxHeight)
{
    int i;
    int maxWidth = -1;
    int maxHeight = -1;
    int sizeLen;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    assert(p_maxWidth);
    assert(p_maxHeight);

    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);

    sizeLen = p_obj->m_camMediaSupport[gl_VCD_currentCameraIndex]->sizeLen;

    // Let width be the "limiter". I.e. first find max width, then find max height for that width
    for (i = 0; i < sizeLen; i++) {
        int width = p_obj->m_camMediaSupport[gl_VCD_currentCameraIndex]->size[i][WIDTH_POS];
        int height = p_obj->m_camMediaSupport[gl_VCD_currentCameraIndex]->size[i][HEIGHT_POS];
        if (width > maxWidth) {
            maxWidth = width;
            maxHeight = height;
        } else if (width == maxWidth) {
            // More than one resolution with max width exists, find the max height for this width
            if (height > maxHeight) {
                maxHeight = height;
            }
        }
    }

    GA_LOGINFO("%s: Result: %dx%d", __FUNCTION__, maxWidth, maxHeight);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);

    if (maxWidth < 0 || maxHeight < 0) {
        return VCD_ERR_GENERAL;
    }

    *p_maxWidth = maxWidth;
    *p_maxHeight = maxHeight;

    return VCD_NO_ERROR;
}


//
// VCD_GetWidestFpsRange
//
int VCD_GetWidestFpsRange(VCD_handle handle, int *p_minFps, int *p_maxFps)
{
    int i;
    int currMinFps = -1;
    int currMaxFps = -1;
    int fpsLen;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    assert(p_minFps);
    assert(p_maxFps);

    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);

    fpsLen = p_obj->m_camMediaSupport[gl_VCD_currentCameraIndex]->fpsLen;

    // Max
    for (i = 0; i < fpsLen; i++) {
        int fps = p_obj->m_camMediaSupport[gl_VCD_currentCameraIndex]->fps[i][MAX_FPS_POS];
        if (fps > currMaxFps) {
            currMaxFps = fps;
        }
    }

    // Min
    currMinFps = currMaxFps;
    for (i = 0; i < fpsLen; i++) {
        int fps = p_obj->m_camMediaSupport[gl_VCD_currentCameraIndex]->fps[i][MIN_FPS_POS];
        if (fps < currMinFps) {
            currMinFps = fps;
        }
    }

    *p_minFps = currMinFps;
    *p_maxFps = currMaxFps;

    GA_LOGINFO("%s: Result: [%d, %d]", __FUNCTION__, *p_minFps, *p_maxFps);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);

    if (currMinFps < 0 || currMaxFps < 0) {
        return VCD_ERR_GENERAL;
    }

    return VCD_NO_ERROR;
}


// VCD_supportsMediaFormat()
int VCD_supportsMediaFormat(VCD_handle handle, int format)
{
    int i;
    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);
    for (i = 0; i < p_obj->m_camMediaSupport[gl_VCD_currentCameraIndex]->fmtLen; i++) {
        if (format == p_obj->m_camMediaSupport[gl_VCD_currentCameraIndex]->fmt[i]) {
            return TRUE;
        }
    }
    return FALSE;
}

// VCD_supportsMediaSize()
int VCD_supportsMediaSize(VCD_handle handle, int width, int height)
{
    int minWidth, minHeight;
    int maxWidth, maxHeight;
    if (VCD_NO_ERROR != VCD_GetMinResolution(handle, &minWidth, &minHeight)) {
        return FALSE;
    }
    if (VCD_NO_ERROR != VCD_GetMaxResolution(handle, &maxWidth, &maxHeight)) {
        return FALSE;
    }
    if (width >= minWidth && width <= maxWidth && height >= minHeight && height <= maxHeight) {
        return TRUE;
    }
    return FALSE;
}

// VCD_supportsMediaFramerate()
int VCD_supportsMediaFramerate(VCD_handle handle, int framerate)
{
    int minFps;
    int maxFps;
    int trueMinFps;
    if (VCD_GetWidestFpsRange(handle, &minFps, &maxFps) != VCD_NO_ERROR) {
        return FALSE;
    }
#ifdef ACCEPT_FPS_CAPS_DOWN_TO_1FPS
    trueMinFps = 1000;
#else
    trueMinFps = minFps;
#endif
    if (framerate >= trueMinFps && framerate <= maxFps) {
        return TRUE;
    }
    return FALSE;
}


//
// VCD_AllocAndInitCamMediaSupport
//
VCD_CamMediaSupport *VCD_AllocAndInitCamMediaSupport()
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    VCD_CamMediaSupport *p_camMediaSupport = (VCD_CamMediaSupport*) malloc(sizeof(VCD_CamMediaSupport));
    if (!p_camMediaSupport) {
        return NULL;
    }
    p_camMediaSupport->camIsBackFacing = -1;
    p_camMediaSupport->fmt = NULL;
    p_camMediaSupport->fmtLen = 0;
    p_camMediaSupport->fps = NULL;
    p_camMediaSupport->fpsLen = 0;
    p_camMediaSupport->size = NULL;
    p_camMediaSupport->sizeLen = 0;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return p_camMediaSupport;
}


//
// VCD_gatherMediaSupport
//
int VCD_gatherMediaSupport(VCD_handle handle)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    int i;
    JNIEnv *p_env = VCD_getEnv();

    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    AV_CHECK_NULL(p_obj, VCD_gatherMediaSupport_err_null_handle);

    if (p_obj->m_mediaSupportKnown) {
        GA_LOGINFO("%s: Have already gathered media support. Will do nothing.", __FUNCTION__);
        return VCD_NO_ERROR;
    }

    if (p_obj->m_state != VCD_STATE_OPENED) {
        // Currently we don't support getting the media support unless we are in state opened
        goto VCD_gatherMediaSupport_err_wrong_state;
    }

    // Allocate new array with media support and a media support struct for each cam
    p_obj->m_camMediaSupport = (VCD_CamMediaSupport**) malloc(sizeof(VCD_CamMediaSupport*) * p_obj->m_numberOfCameras);
    if (!p_obj->m_camMediaSupport) {
        goto VCD_gatherMediaSupport_err_out_of_memory;
    }
    for (i = 0; i < p_obj->m_numberOfCameras; i++) {
        p_obj->m_camMediaSupport[i] = VCD_AllocAndInitCamMediaSupport();
        AV_CHECK_NULL(p_obj->m_camMediaSupport[i], VCD_gatherMediaSupport_err_out_of_memory);
    }

    for (i = 0; i < p_obj->m_numberOfCameras; i++) {
        GA_LOGINFO("%s: Creating camera with index %d", __FUNCTION__, i);
        p_obj->m_camMediaSupport[i]->camIsBackFacing = VCD_camIsBackFacing(i);
        if (p_obj->m_camMediaSupport[i]->camIsBackFacing < 0) {
            goto VCD_gatherMediaSupport_err_camera_facing;
        }
        jobject camera = camera = (*p_env)->CallStaticObjectMethod(
            p_env, VCD_getCachedCameraClass(),
            getMethodId(VCD_METHODID_CAMERA_OPEN), i);
        AV_CHECK_ERR((*p_env)->ExceptionCheck(p_env), VCD_gatherMediaSupport_err_create_camera);

        jobject paramsObj = VCD_getParameters(handle, camera);
        (*p_env)->CallNonvirtualVoidMethod(
            p_env, camera, VCD_getCachedCameraClass(),
            getMethodId(VCD_METHODID_CAMERA_RELEASE));
        (*p_env)->DeleteLocalRef(p_env, camera);
        AV_CHECK_NULL(paramsObj, VCD_gatherMediaSupport_err_null_params);
        VCD_camParams_getSupportedPreviewFormats(paramsObj, p_obj->m_camMediaSupport[i]);
        VCD_camParams_getSupportedPreviewSizes(paramsObj, p_obj->m_camMediaSupport[i]);
        VCD_camParams_getSupportedPreviewFpsRange(paramsObj, p_obj->m_camMediaSupport[i]);
        (*p_env)->DeleteLocalRef(p_env, paramsObj);
    }

    p_obj->m_mediaSupportKnown = TRUE;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return VCD_NO_ERROR;

    /*
     * propagate unhandled errors
     */
VCD_gatherMediaSupport_err_null_handle:
    {
        GA_LOGERROR("%s FATAL ERROR: handle = NULL", __FUNCTION__);
        return VCD_ERR_NULL_HANDLE;
    }
VCD_gatherMediaSupport_err_out_of_memory:
    {
        GA_LOGERROR("%s ERROR: Out of memory!", __FUNCTION__);
        p_obj->m_camMediaSupport = NULL;
        return VCD_ERR_NO_MEMORY;
    }
VCD_gatherMediaSupport_err_camera_facing:
    {
        GA_LOGERROR("%s ERROR: VCD_camIsBackFacing() failed", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
VCD_gatherMediaSupport_err_create_camera:
    {
        GA_LOGERROR("%s ERROR: Could not create camera", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
VCD_gatherMediaSupport_err_null_params:
    {
        GA_LOGERROR("%s ERROR: Got NULL when getting Camera parameters", __FUNCTION__);
        return VCD_ERR_NULL_PTR;
    }
VCD_gatherMediaSupport_err_wrong_state:
    {
        GA_LOGERROR("%s ERROR: wrong state, should be %d but was %d", __FUNCTION__, VCD_STATE_PREPARED, p_obj->m_state);
        return VCD_ERR_ILLEGAL_STATE;
    }
}


//
// VCD_getBufferSize
//
int VCD_getBufferSize(VCD_handle handle, int format, int width, int height)
{
    int size = -1;

    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    AV_CHECK_NULL(p_obj, VCD_getBufferSize_err_null_obj);

    if (format == VCD_PIXEL_FORMAT_RGB_565
        || format == VCD_PIXEL_FORMAT_YUY2
        || format == VCD_PIXEL_FORMAT_NV16/*??*/) {
            size = (width * height * 16) / 8;
    } else if (format == VCD_PIXEL_FORMAT_YV12
        || format == VCD_PIXEL_FORMAT_NV21) {
            size = (width * height * 12) / 8;
    } else {
        GA_LOGERROR("%s: ERROR: Invalid format: %d", __FUNCTION__, format);
    }

    if (size >= 0) {
        GA_LOGINFO("%s: Calculated buffer size: %d", __FUNCTION__, size);
    }

    return size;

    /*
     * propagate unhandled errors
     */
VCD_getBufferSize_err_null_obj:
    {
        GA_LOGERROR("%s ERROR: handle = NULL", __FUNCTION__);
        return VCD_ERR_NULL_HANDLE;
    }
}

//
// VCD_getBestFpsRange
//
// Find the fps range that fullfills the following properties:
// 1. includes framerate
// 2. has the minimum width
int VCD_getBestFpsRange(VCD_CamMediaSupport *p_camMediaSupport, int framerate, int *p_minFps, int *p_maxFps)
{
    int i;
    int minWidth = -1;

    assert(p_camMediaSupport);
    assert(p_minFps);
    assert(p_maxFps);

    // First loop once and get the minimum width for all the ranges including framerate
    for (i = 0; i < p_camMediaSupport->fpsLen; i++) {
        int minFps = p_camMediaSupport->fps[i][MIN_FPS_POS];
        int maxFps = p_camMediaSupport->fps[i][MAX_FPS_POS];
        if (framerate >= minFps && framerate <= maxFps) { // is framerate in this range?
            int width = maxFps - minFps;
            if (minWidth < 0 || width < minWidth) {
                minWidth = width;
            }
        }
    }
    if (minWidth >= 0) {
        // Now loop again and pick the first with minimum width that includes framerate
        for (i = 0; i < p_camMediaSupport->fpsLen; i++) {
            int minFps = p_camMediaSupport->fps[i][MIN_FPS_POS];
            int maxFps = p_camMediaSupport->fps[i][MAX_FPS_POS];
            if (framerate >= minFps && framerate <= maxFps) { // is framerate in this range?
                int width = maxFps - minFps;
                if (width == minWidth) {
                    *p_minFps = minFps;
                    *p_maxFps = maxFps;
                    GA_LOGINFO("%s: Best range found and is: [%d, %d].", __FUNCTION__, *p_minFps, *p_maxFps);
                    return VCD_NO_ERROR;
                }
            }
        }
    }

    // If we get here there was either an error or, ACCEPT_FPS_CAPS_DOWN_TO_1FPS is set
    // and the given framerate is smaller than the min fps of the ranges
#ifdef ACCEPT_FPS_CAPS_DOWN_TO_1FPS
    GA_LOGINFO("%s: No range found including framerate. However, ACCEPT_FPS_CAPS_DOWN_TO_1FPS is defined so will continue...", __FUNCTION__);
    int minFpsAllRanges = -1;
    // First loop once and get the minimum fps for all the ranges
    for (i = 0; i < p_camMediaSupport->fpsLen; i++) {
        int minFps = p_camMediaSupport->fps[i][MIN_FPS_POS];
        if (minFpsAllRanges < 0 || minFps < minFpsAllRanges) {
            minFpsAllRanges = minFps;
        }
    }
    if (minFpsAllRanges >= 0) {
        // Now loop again and get the minimum width for all ranges including minimum fps
        minWidth = -1;
        for (i = 0; i < p_camMediaSupport->fpsLen; i++) {
            int minFps = p_camMediaSupport->fps[i][MIN_FPS_POS];
            int maxFps = p_camMediaSupport->fps[i][MAX_FPS_POS];
            if (minFps == minFpsAllRanges) { // is the minimum framerate in this range?
                int width = maxFps - minFps;
                if (minWidth < 0 || width < minWidth) {
                    minWidth = width;
                }
            }
        }
        if (minWidth >= 0) {
            // Finally loop again and pick the first range with min fps and min width
            for (i = 0; i < p_camMediaSupport->fpsLen; i++) {
                int minFps = p_camMediaSupport->fps[i][MIN_FPS_POS];
                int maxFps = p_camMediaSupport->fps[i][MAX_FPS_POS];
                if (minFps ==  minFpsAllRanges) { // is min fps in this range?
                    int width = maxFps - minFps;
                    if (width == minWidth) {
                        *p_minFps = minFps;
                        *p_maxFps = maxFps;
                        GA_LOGINFO("%s: Best range found and is: [%d, %d].", __FUNCTION__, *p_minFps, *p_maxFps);
                        return VCD_NO_ERROR;
                    }
                }
            }
        }
    }
#endif

    GA_LOGERROR("%s: Didn't find best fps range. Shouldn't happen. Bailing out...", __FUNCTION__);
    return VCD_ERR_GENERAL;
}


//
// VCD_findBestMatchingSupportedResolution
//
int VCD_findBestMatchingSupportedResolution(VCD_CamMediaSupport *p_camMediaSupport, int wantedWidth, int wantedHeight, int *p_bestMatchWidth, int *p_bestMatchHeight)
{
    int i;
    int minPixels = -1;
    int bestMatchPos = -1;
    int sizeLen;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    assert(p_camMediaSupport);
    assert(p_bestMatchWidth);
    assert(p_bestMatchHeight);

    sizeLen = p_camMediaSupport->sizeLen;

    // First check if we can match wanted resolution exactly
    for (i = 0; i < sizeLen; i++) {
        int width = p_camMediaSupport->size[i][WIDTH_POS];
        int height = p_camMediaSupport->size[i][HEIGHT_POS];
        if (width == wantedWidth && height == wantedHeight) {
            bestMatchPos = i;
            GA_LOGINFO("%s: Found exact match for resolution %dx%d", __FUNCTION__, wantedWidth, wantedHeight);
            break;
        }
    }

    // If we did not find exakt match then find nearest match
    if (bestMatchPos < 0) {
        for (i = 0; i < sizeLen; i++) {
            int width = p_camMediaSupport->size[i][WIDTH_POS];
            int height = p_camMediaSupport->size[i][HEIGHT_POS];
            if (width >= wantedWidth && height >= wantedHeight) {
                if (minPixels == -1 || width*height < minPixels) {
                    minPixels = width*height;
                    bestMatchPos = i;
                }
            }
        }
    }

    if (bestMatchPos < 0) {
        GA_LOGTRACE("EXIT %s", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }

    *p_bestMatchWidth = p_camMediaSupport->size[bestMatchPos][WIDTH_POS];
    *p_bestMatchHeight = p_camMediaSupport->size[bestMatchPos][HEIGHT_POS];

    if (wantedWidth != *p_bestMatchWidth || wantedHeight != *p_bestMatchHeight) {
        GA_LOGWARN("%s: Will manage support of resolution %dx%d by cropping from resolution %dx%d", __FUNCTION__, wantedWidth, wantedHeight, *p_bestMatchWidth, *p_bestMatchHeight);
    }

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return VCD_NO_ERROR;
}


//
// VCD_fixateMediaType
//
int VCD_fixateMediaType(VCD_handle handle, int format, int width, int height, int framerate)
{
    int minFps;
    int maxFps;
    int bufSize;
    int trueWidth;
    int trueHeight;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    JNIEnv *p_env = VCD_getEnv();

    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    AV_CHECK_NULL(p_obj, VCD_fixateMediaType_err_null_obj);

    if (p_obj->m_mediaTypeIsFixed) {
        GA_LOGWARN("%s: Unexpected call since media type already has been fixed. Will do nothing.", __FUNCTION__);
        return VCD_NO_ERROR;
    }

    AV_CHECK_ERR(VCD_getBestFpsRange(p_obj->m_camMediaSupport[gl_VCD_currentCameraIndex], framerate, &minFps, &maxFps), VCD_fixateMediaType_err_fps_range);

    AV_CHECK_ERR(VCD_findBestMatchingSupportedResolution(p_obj->m_camMediaSupport[gl_VCD_currentCameraIndex], width, height, &trueWidth, &trueHeight), VCD_fixateMediaType_err_match_res);

    jobject cameraParams = VCD_getParameters(handle, p_obj->m_javaCameraObj);
    VCD_camParams_setPreviewFormat(cameraParams, format);
    VCD_camParams_setPreviewSize(cameraParams, trueWidth, trueHeight);
    VCD_camParams_setPreviewFpsRange(cameraParams, minFps, maxFps);
    VCD_setParameters(handle, cameraParams);
    (*p_env)->DeleteLocalRef(p_env, cameraParams);

    bufSize = VCD_getBufferSize(handle, format, trueWidth, trueHeight);
    if (bufSize < 0) {
        goto VCD_fixateMediaType_err_bufsize;
    }

    // Now allocate the buffers for the preview callback

    // Byte array object (buffer 1)
    GA_LOGTRACE("%s --> before first call to NewByteArray()", __FUNCTION__);
    // construct a Java byte array, which will be used as a first preview frame buffer
    jarray previewFrameBufOne = (*p_env)->NewByteArray(p_env, bufSize);
    AV_CHECK_NULL(previewFrameBufOne, VCD_fixateMediaType_err_new_byte_array);

    GA_LOGTRACE("%s --> before call to NewGlobalRef() for preview frame buffer one", __FUNCTION__);
    // create a global reference to the first buffer, this will also prevent it from being garbage collected
    p_obj->m_javaBufOneObj = (*p_env)->NewGlobalRef(p_env, previewFrameBufOne);
    (*p_env)->DeleteLocalRef(p_env, previewFrameBufOne);
    AV_CHECK_NULL(p_obj->m_javaBufOneObj, VCD_fixateMediaType_err_new_global_ref_frameBuf);

    // Byte array object (buffer 2)
    GA_LOGTRACE("%s --> before second call to NewByteArray()", __FUNCTION__);
    // construct a Java byte array, which will be used as a second preview frame buffer
    jarray previewFrameBufTwo = (*p_env)->NewByteArray(p_env, bufSize);
    AV_CHECK_NULL(previewFrameBufTwo, VCD_fixateMediaType_err_new_byte_array);

    GA_LOGTRACE("%s --> before call to NewGlobalRef() for preview frame buffer two", __FUNCTION__);
    // create a global reference to the second buffer, this will also prevent it from being garbage collected
    p_obj->m_javaBufTwoObj = (*p_env)->NewGlobalRef(p_env, previewFrameBufTwo);
    (*p_env)->DeleteLocalRef(p_env, previewFrameBufTwo);
    AV_CHECK_NULL(p_obj->m_javaBufTwoObj, VCD_fixateMediaType_err_new_global_ref_frameBuf);

    p_obj->m_mediaTypeIsFixed = TRUE;
    p_obj->m_fixedFormat = format;
    p_obj->m_fixedFramerate = framerate;
    p_obj->m_fixedWidth = width;
    p_obj->m_fixedHeight = height;
    p_obj->m_fixedTrueWidth = trueWidth;
    p_obj->m_fixedTrueHeight = trueHeight;

    GA_LOGINFO("%s: Fixation complete with: format=%d, framerate=%d, width=%d, height=%d, trueWidth=%d and trueHeight=%d", __FUNCTION__, format, framerate, width, height, trueWidth, trueHeight);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return VCD_NO_ERROR;

    /*
     * propagate unhandled errors
     */
VCD_fixateMediaType_err_null_obj:
    {
        GA_LOGERROR("%s: ERROR: handle = NULL", __FUNCTION__);
        return VCD_ERR_NULL_HANDLE;
    }
VCD_fixateMediaType_err_fps_range:
    {
        GA_LOGERROR("%s: ERROR: something is screewed up with the framerate", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
VCD_fixateMediaType_err_match_res:
    {
        GA_LOGERROR("%s: ERROR: something got wrong when trying to find best matching resolution", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
VCD_fixateMediaType_err_bufsize:
    {
        GA_LOGERROR("%s ERROR: Unknown problem with buf size", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
VCD_fixateMediaType_err_new_byte_array:
    {
        GA_LOGERROR("%s ERROR: NewByteArray failed (frameBuf)", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
VCD_fixateMediaType_err_new_global_ref_frameBuf:
    {
        GA_LOGERROR("%s ERROR: NewGlobalRef failed (frameBuf) => out of memory", __FUNCTION__);
        return VCD_ERR_NO_MEMORY;
    }
}


//
// VCD_unfixateMediaType
//
int VCD_unfixateMediaType(VCD_handle handle)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    AV_CHECK_NULL(p_obj, VCD_unfixateMediaType_err_null_obj);

    JNIEnv *p_env = VCD_getEnv();
    (*p_env)->DeleteGlobalRef(p_env, p_obj->m_javaBufOneObj); // unreferences the jbyteArray object
    (*p_env)->DeleteGlobalRef(p_env, p_obj->m_javaBufTwoObj); // unreferences the jbyteArray object

    p_obj->m_javaBufOneObj = NULL;
    p_obj->m_javaBufTwoObj = NULL;
    p_obj->m_mediaTypeIsFixed = FALSE;
    p_obj->m_fixedFormat = -1;
    p_obj->m_fixedFramerate = -1;
    p_obj->m_fixedWidth = -1;
    p_obj->m_fixedHeight = -1;
    p_obj->m_fixedTrueWidth = -1;
    p_obj->m_fixedTrueHeight = -1;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);

    return VCD_NO_ERROR;

    /*
     * propagate unhandled errors
     */
VCD_unfixateMediaType_err_null_obj:
    {
        GA_LOGERROR("%s ERROR: handle = NULL", __FUNCTION__);
        return VCD_ERR_NULL_HANDLE;
    }
}


//
// VCD_prepare
//
int VCD_prepare(VCD_handle handle)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    int ret;
    int textureName;

    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    AV_CHECK_NULL(p_obj, VCD_prepare_err_prepare_null_obj);

    p_obj->m_javaCameraObj = NULL;
    p_obj->m_javaBufOneObj = NULL;
    p_obj->m_javaBufTwoObj = NULL;
    p_obj->m_javaPreviewCbObj = NULL;
    p_obj->m_javaSurfaceTextureObj = NULL;

    GA_LOGTRACE("%s --> before call to VCD_getEnv()", __FUNCTION__);
    JNIEnv *env = VCD_getEnv();

    GA_LOGTRACE("%s construct Camera obj (and other objects as well)", __FUNCTION__);

    gl_VCD_currentCameraIndex = VCD_getIndexOfCameraToUse(handle);
    if (gl_VCD_currentCameraIndex < 0) {
        goto VCD_prepare_err_choosing_camera;
    }

    // Camera object
    // open a Camera Java object, can throw IllegalArgumentException ? or some other exception ?? FIXME: check what can be thrown here
    // if an exception is thrown, then catch, clear it and report error
    GA_LOGTRACE("%s --> before call to CallStaticObjectMethod()", __FUNCTION__);
    // jobject camera = (*env)->CallStaticObjectMethod(
    //     env, VCD_getCachedCameraClass(),
    //     getMethodId(VCD_METHODID_CAMERA_OPEN_DEFAULT));
    jobject camera = (*env)->CallStaticObjectMethod(
        env, VCD_getCachedCameraClass(),
        getMethodId(VCD_METHODID_CAMERA_OPEN), gl_VCD_currentCameraIndex);
    AV_CHECK_ERR((*env)->ExceptionCheck(env), VCD_prepare_err_open_camera);

    // create a global reference to the camera object, this will also prevent it from being garbage collected
    GA_LOGTRACE("%s --> before call to NewGlobalRef() for Camera", __FUNCTION__);
    p_obj->m_javaCameraObj = (*env)->NewGlobalRef(env, camera);
    (*env)->DeleteLocalRef(env, camera);
    AV_CHECK_NULL(p_obj->m_javaCameraObj, VCD_prepare_err_prepare_new_global_ref_camera);

    // VideoCaptureDevicePreviewCallbackHandler object
    GA_LOGTRACE("%s --> before call to NewObject() for preview callback handler", __FUNCTION__);
    // construct a VideoCaptureDevicePreviewCallbackHandler Java object, can throw IllegalArgumentException
    // if an exception is thrown, then catch, clear it and report error
    jobject cb = (*env)->NewObject(env, VCD_getCachedPreviewCbHandlerClass(), getMethodId(VCD_METHODID_PREVIEW_CB_HANDLER_CONSTRUCTOR));
    AV_CHECK_ERR((*env)->ExceptionCheck(env), VCD_prepare_err_construct_preview_cb_handler);

    GA_LOGTRACE("%s --> before call to NewGlobalRef() for preview callback handler", __FUNCTION__);
    // create a global reference to the preview callback handler, this will also prevent it from being garbage collected
    p_obj->m_javaPreviewCbObj = (*env)->NewGlobalRef(env, cb);
    (*env)->DeleteLocalRef(env, cb);
    AV_CHECK_NULL(p_obj->m_javaPreviewCbObj, VCD_prepare_err_prepare_new_global_ref_preview_cb);

    if (s_VCD_AndroidSdkVersion >= VCD_ANDROID_SDK_VERSION_WITH_SURFACE_TEXTURE) {
        // SurfaceTexture object
        GA_LOGVERB("%s --> before call to NewObject() for surface texture", __FUNCTION__);
        // construct a SurfaceTexture Java object, can throw IllegalArgumentException
        // if an exception is thrown, then catch, clear it and report error
        textureName = 1; // 42 ?
        jobject surfaceTexture = (*env)->NewObject(env, VCD_getCachedSurfaceTextureClass(), getMethodId(VCD_METHODID_SURFACE_TEXTURE_CONSTRUCTOR), textureName);
        AV_CHECK_ERR((*env)->ExceptionCheck(env), VCD_prepare_err_construct_surface_texture);

        GA_LOGVERB("%s --> before call to NewGlobalRef() for surface texture", __FUNCTION__);
        // create a global reference to the surface texture, this will also prevent it from being garbage collected
        p_obj->m_javaSurfaceTextureObj = (*env)->NewGlobalRef(env, surfaceTexture);
        (*env)->DeleteLocalRef(env, surfaceTexture);
        AV_CHECK_NULL(p_obj->m_javaSurfaceTextureObj, VCD_prepare_err_new_global_ref_surface_texture);
    }

    // Set up some camera stuff...
    VCD_setDisplayOrientation(handle, 0); // This does not affect the order or byte array in the buffer in the callback, so it is actually useless
    VCD_setPreviewDisplay(handle, NULL);
    if (s_VCD_AndroidSdkVersion >= VCD_ANDROID_SDK_VERSION_WITH_SURFACE_TEXTURE) {
        VCD_setPreviewTexture(handle, p_obj->m_javaSurfaceTextureObj);
    }
    VCD_setPreviewCallbackWithBuffer(handle, p_obj->m_javaPreviewCbObj); // should be matched by a "NULL-setting" in unprepare

    // Initialize mutexes and conditions
    p_obj->m_dataFlag = NO_BUF_HAS_DATA;
    AV_CHECK_ERR(ret = pthread_mutex_init(&(p_obj->m_bufOneMutex), NULL), VCD_prepare_err_mutex_init);
    AV_CHECK_ERR(ret = pthread_mutex_init(&(p_obj->m_bufTwoMutex), NULL), VCD_prepare_err_mutex_init);
    AV_CHECK_ERR(ret = pthread_mutex_init(&(p_obj->m_dataFlagMutex), NULL), VCD_prepare_err_mutex_init);
    AV_CHECK_ERR(ret = pthread_cond_init(&(p_obj->m_dataFlagCondition), NULL), VCD_prepare_err_cond_init);
    AV_CHECK_ERR(ret = pthread_mutex_init(&(p_obj->m_hasStoppedMutex), NULL), VCD_prepare_err_mutex_init);
    AV_CHECK_ERR(ret = pthread_cond_init(&(p_obj->m_hasStoppedCondition), NULL), VCD_prepare_err_cond_init);

    p_obj->m_state = VCD_STATE_PREPARED;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return VCD_NO_ERROR;

    /*
     * propagate unhandled errors
     */
VCD_prepare_err_prepare_null_obj:
    {
        GA_LOGERROR("%s ERROR: handle = NULL", __FUNCTION__);
        return VCD_ERR_NULL_HANDLE;
    }
VCD_prepare_err_choosing_camera:
    {
        GA_LOGERROR("%s ERROR: Could not find a suitable camera index. Device has no camera?", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
VCD_prepare_err_open_camera:
    {
        (*env)->ExceptionClear(env);
        GA_LOGERROR("%s ERROR: open Camera Java obj failed (IllegalArgumentException?)", __FUNCTION__);
        return VCD_ERR_ARGUMENT; // ??
    }
VCD_prepare_err_prepare_new_global_ref_camera:
    {
        GA_LOGERROR("%s ERROR: NewGlobalRef failed (camera) => out of memory", __FUNCTION__);
        return VCD_ERR_NO_MEMORY;
    }
VCD_prepare_err_construct_preview_cb_handler:
    {
        GA_LOGERROR("%s ERROR: constructing preview callback handler failed", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
VCD_prepare_err_prepare_new_global_ref_preview_cb:
    {
        GA_LOGERROR("%s ERROR: NewGlobalRef failed (previewCb) => out of memory", __FUNCTION__);
        return VCD_ERR_NO_MEMORY;
    }
VCD_prepare_err_construct_surface_texture:
    {
        GA_LOGERROR("%s ERROR: constructing surface texture failed", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
VCD_prepare_err_new_global_ref_surface_texture:
    {
        GA_LOGERROR("%s ERROR: NewGlobalRef failed (surface texture) => out of memory", __FUNCTION__);
        return VCD_ERR_NO_MEMORY;
    }
VCD_prepare_err_mutex_init:
    {
        GA_LOGERROR("%s ERROR: Failed to initialize mutex. Return value: %d", __FUNCTION__, ret);
        return VCD_ERR_GENERAL;
    }
VCD_prepare_err_cond_init:
    {
        GA_LOGERROR("%s ERROR: Failed to initialize condition. Return value: %d", __FUNCTION__, ret);
        return VCD_ERR_GENERAL;
    }
}

//
// VCD_unprepare
//
int VCD_unprepare(VCD_handle handle)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    int ret;

    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    AV_CHECK_NULL(p_obj, VCD_unprepare_err_unprepare_null_obj);

    if (p_obj->m_state != VCD_STATE_PREPARED && p_obj->m_state != VCD_STATE_STOPPED) {
        goto VCD_unprepare_err_unprepare_illegal_state;
    }

    if (p_obj->m_mediaTypeIsFixed) {
        GA_LOGWARN("%s: Media type is still fixed (hasn't been unfixed). This is strange. Will try to unfix now.", __FUNCTION__);
        (void) VCD_unfixateMediaType(handle);
    }

    JNIEnv *env = VCD_getEnv();
    VCD_setPreviewCallbackWithBuffer(handle, NULL);
    // release the Camera Java object
    VCD_release(handle);
    (*env)->DeleteGlobalRef(env, p_obj->m_javaCameraObj); // unreferences the Camera object
    (*env)->DeleteGlobalRef(env, p_obj->m_javaPreviewCbObj); // unreferences the preview callback handler object
    if (s_VCD_AndroidSdkVersion >= VCD_ANDROID_SDK_VERSION_WITH_SURFACE_TEXTURE) {
        (*env)->DeleteGlobalRef(env, p_obj->m_javaSurfaceTextureObj); // unreferences the surface texture object
    }
    // Destroy mutexes and conditions
    AV_CHECK_ERR(ret = pthread_mutex_destroy(&(p_obj->m_bufOneMutex)), VCD_unprepare_err_mutex_destroy);
    AV_CHECK_ERR(ret = pthread_mutex_destroy(&(p_obj->m_bufTwoMutex)), VCD_unprepare_err_mutex_destroy);
    AV_CHECK_ERR(ret = pthread_mutex_destroy(&(p_obj->m_dataFlagMutex)), VCD_unprepare_err_mutex_destroy);
    AV_CHECK_ERR(ret = pthread_cond_destroy(&(p_obj->m_dataFlagCondition)), VCD_unprepare_err_cond_destroy);
    AV_CHECK_ERR(ret = pthread_mutex_destroy(&(p_obj->m_hasStoppedMutex)), VCD_unprepare_err_mutex_destroy);
    AV_CHECK_ERR(ret = pthread_cond_destroy(&(p_obj->m_hasStoppedCondition)), VCD_unprepare_err_cond_destroy);

    // Reset appropriate parts of the VCD object
    p_obj->m_javaCameraObj = NULL;
    p_obj->m_javaPreviewCbObj = NULL;
    p_obj->m_javaSurfaceTextureObj = NULL;
    p_obj->m_dataFlag = NO_BUF_HAS_DATA;
    p_obj->m_currPreviewBuf = BUF_ID_NOT_SET;
    p_obj->m_hasStoppedFlag = FALSE;
    p_obj->m_state = VCD_STATE_OPENED;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return VCD_NO_ERROR;

    /*
     * propagate unhandled errors
     */
VCD_unprepare_err_unprepare_null_obj:
    {
        GA_LOGERROR("%s %s ERROR: handle= NULL", __FILE__, __FUNCTION__);
        return VCD_ERR_NULL_HANDLE;
    }
VCD_unprepare_err_unprepare_illegal_state:
    {
        GA_LOGERROR("%s %s ERROR: illegal state (%d)", __FILE__, __FUNCTION__, p_obj->m_state);
        return VCD_ERR_ILLEGAL_STATE;
    }
VCD_unprepare_err_mutex_destroy:
    {
        GA_LOGERROR("%s %s ERROR: Failed to destroy mutex. Return value: %d", __FILE__, __FUNCTION__, ret);
        return VCD_ERR_GENERAL;
    }
VCD_unprepare_err_cond_destroy:
    {
        GA_LOGERROR("%s %s ERROR: Failed to destroy condition. Return value: %d", __FILE__, __FUNCTION__, ret);
        return VCD_ERR_GENERAL;
    }
}



//
// VCD_checkChangeCamera
//
void VCD_checkChangeCamera(VCD_handle handle)
{
    int newCameraIndex;
    int currCameraIndex;
    int currFixedFormat;
    int currFixedWidth;
    int currFixedHeight;
    int currFixedFramerate;
    int newCamSupportsMediaType;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    if (gl_VCD_prevPreferredFrontFacingCamera == gl_VCD_preferFrontFacingCamera) {
        return;
    }

    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(p_obj);

    // Remember currently fixed media type before resetting things
    currFixedFormat = p_obj->m_fixedFormat;
    currFixedWidth = p_obj->m_fixedWidth;
    currFixedHeight = p_obj->m_fixedHeight;
    currFixedFramerate = p_obj->m_fixedFramerate;

    // Before stopping and trying to start a new camera, make sure the new camera supports the current media settings
    newCameraIndex = VCD_getIndexOfCameraToUse(handle);
    if (newCameraIndex == gl_VCD_currentCameraIndex) {
        // Nothing to do...
        return;
    }
    // Test new camera for media type...
    currCameraIndex = gl_VCD_currentCameraIndex;
    gl_VCD_currentCameraIndex = newCameraIndex;
    newCamSupportsMediaType = VCD_supportsMediaFormat(handle, currFixedFormat) && VCD_supportsMediaSize(handle, currFixedWidth, currFixedHeight) && VCD_supportsMediaFramerate(handle, currFixedFramerate);
    if (!newCamSupportsMediaType) {
        // Restore old camera index and simply return
        gl_VCD_currentCameraIndex = currCameraIndex;
        return;
    }

    // Stop, unfixate and unprepare
    (void) VCD_stop(handle);
    (void) VCD_unfixateMediaType(handle);
    (void) VCD_unprepare(handle);

    // Prepare, fixate and start with new camera
    (void) VCD_prepare(handle);
    (void) VCD_fixateMediaType(handle, currFixedFormat, currFixedWidth, currFixedHeight, currFixedFramerate);
    (void) VCD_start(handle);

    gl_VCD_prevPreferredFrontFacingCamera = gl_VCD_preferFrontFacingCamera;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


//
// VCD_crop
//
// Will crop centralized, dstWidth must be <= srcWidth and dstHeight must be <= srcHeight
// NOTE: This function supports ONLY YUV 4:2:0 color space (like YV12, NV21 and NV12)
//
void VCD_crop(uint8_t **pp_dst, uint8_t **pp_src, int srcWidth, int srcHeight, int dstWidth, int dstHeight)
{
    int i;

    assert(pp_dst);
    assert(pp_src);

    // Y
    int wStart = (srcWidth - dstWidth) / 2;
    int hStart = (srcHeight - dstHeight) / 2;
    int hEnd = hStart + dstHeight;
    int line = 0;
    for (i = hStart; i < hEnd; i++) {
        memcpy(*pp_dst + line*dstWidth, *pp_src + i*srcWidth + wStart, dstWidth);
        line += 1;
    }
    // U & V
    int uvStart = srcWidth * srcHeight;
    int uvStartDst = dstWidth * dstHeight;
    int uvWidth = srcWidth;
    int uvHeight = srcHeight / 2;
    int uvwStart = (uvWidth - dstWidth) / 2;
    int uvhStart = (uvHeight - dstHeight / 2) / 2;
    int uvhEnd = uvhStart + dstHeight / 2;
    line = 0;
    for (i = uvhStart; i < uvhEnd; i++) {
        memcpy(*pp_dst + uvStartDst + line*dstWidth, *pp_src + uvStart + i*uvWidth + uvwStart, dstWidth);
        line += 1;
    }
}


//
// VCD_read
//
int VCD_read(VCD_handle handle, uint8_t **pp_data, int bufsize)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    int ret = 0;
    void *p_frame_buf;
    static struct timespec time_to_wait;
    static struct timeval time_of_day;
    static struct timeval start_time;
    int timeDiffUsec;
    static int frameCount = 0;
    static int frameCountWindow = 0;
    static int waitCount = 0;

    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) handle;
    assert(handle);
    assert(pp_data);

    if (!frameCount) { // Only first time
        gettimeofday(&start_time, NULL);
        gettimeofday(&(p_obj->m_readTimer), NULL);
    }

    frameCount++;
    frameCountWindow++;

    gettimeofday(&time_of_day, NULL);
    timeDiffUsec = time_diff_usec(&time_of_day, &(p_obj->m_readTimer));
    if (timeDiffUsec > p_obj->m_logInterval || !p_obj->m_logInterval) {
        int framerate;
        int framerateWindow;
        int timeDiffSec;
        timeDiffSec = time_diff_sec(&time_of_day, &start_time);
        framerate = frameCount / (timeDiffSec > 0 ? timeDiffSec : 1);
        framerateWindow = frameCountWindow * 1000000 / timeDiffUsec;
        GA_LOGVERB("%s ------> has now been called %d times and had to wait for data %d times --GST--> framerate since start: %d fps, framerate last %d usec: %d fps", __FUNCTION__, frameCount, waitCount, framerate, timeDiffUsec, framerateWindow);
        gettimeofday(&(p_obj->m_readTimer), NULL);
        frameCountWindow = 0;
    }

    JNIEnv *env = VCD_getEnv();

    //
    // 1. Lock data flag mutex
    // 2. If no buf has data --> wait for data exists condition on data flag mutex
    // 3. Lock the buf that has data (i.e. one or two)
    // 4. Copy the data
    // 5. Unlock the buf that has data (i.e. the buf locked before copy)
    // 6. Set enum flag to NO_BUF_HAS_DATA
    // 7. Unlock data flag mutex
    //

    // 1. Lock data flag mutex
    AV_CHECK_ERR(ret = pthread_mutex_lock(&(p_obj->m_dataFlagMutex)), VCD_read_err_mutex);
    // 2. If no buf has data --> wait for data exists condition on data flag mutex
    if (p_obj->m_dataFlag == NO_BUF_HAS_DATA) {
        waitCount++;
        GA_LOGTRACE("%s: Has to wait for data...", __FUNCTION__);
        create_wait_time(&time_to_wait, DEFAULT_TIME_TO_WAIT_FOR_DATA_IN_SECONDS);
        // pthread_cond_timedwait() unlocks the mutex during wait and locks when wait is over (condition met)
        ret = pthread_cond_timedwait(&(p_obj->m_dataFlagCondition), &(p_obj->m_dataFlagMutex), &time_to_wait);
        // checking ret below is surprising to me... thought ETIMEDOUT would be set "in" errno... /KD
        if (ret == ETIMEDOUT) {
            GA_LOGWARN("%s: WARNING: Stopped waiting for data after %d seconds. Something must be wrong. Returning VCD_ERR_NO_DATA", __FUNCTION__, DEFAULT_TIME_TO_WAIT_FOR_DATA_IN_SECONDS);
            // We need to return here so that the GST element can "free up" the currently blocked streaming thread
            // Again I'm surprised. I did not think that m_dataFlagMutex would be locked when pthread_cond_timedwait() in fact returns an error
            AV_CHECK_ERR(ret = pthread_mutex_unlock(&(p_obj->m_dataFlagMutex)), VCD_read_err_mutex);
            return VCD_ERR_NO_DATA;
        }
        if (ret) {
            goto VCD_read_err_cond_wait;
        }
    }
    if (p_obj->m_dataFlag == NO_BUF_HAS_DATA) {
        // Check the flag again as customary... (e.g. "the system" may have incorrectly signaled the condition for some strange reason...)
        AV_CHECK_ERR(ret = pthread_mutex_unlock(&(p_obj->m_dataFlagMutex)), VCD_read_err_mutex);
        GA_LOGTRACE("%s: Data exists condition flagged, but no data in buffer. Returning VCD_ERR_NO_DATA.", __FUNCTION__);
        return VCD_ERR_NO_DATA;
    }
    jobject *p_javaBufWithDataObj;
    pthread_mutex_t *p_bufWithDataMutex;
    if (p_obj->m_dataFlag == DATA_IN_BUF_TWO) {
        p_javaBufWithDataObj = &(p_obj->m_javaBufTwoObj);
        p_bufWithDataMutex = &(p_obj->m_bufTwoMutex);
    } else {
        p_javaBufWithDataObj = &(p_obj->m_javaBufOneObj);
        p_bufWithDataMutex = &(p_obj->m_bufOneMutex);
    }
    // 3. Lock the buf that has data (i.e. one or two)
    AV_CHECK_ERR(ret = pthread_mutex_lock(p_bufWithDataMutex), VCD_read_err_mutex_in_mutex);
    // 4. Copy the data
    GA_LOGTRACE("%s: Copying data from buffer %s --xx--> thread(%ld)", __FUNCTION__, p_obj->m_dataFlag == DATA_IN_BUF_ONE ? "one" : "two", pthread_self());
    p_frame_buf = (*env)->GetPrimitiveArrayCritical(env, *p_javaBufWithDataObj, NULL);
    AV_CHECK_NULL(p_frame_buf, VCD_read_err_get_primitive_array);
    if (p_obj->m_fixedWidth != p_obj->m_fixedTrueWidth || p_obj->m_fixedHeight != p_obj->m_fixedTrueHeight) {
        VCD_crop(pp_data, (uint8_t**) &p_frame_buf, p_obj->m_fixedTrueWidth, p_obj->m_fixedTrueHeight, p_obj->m_fixedWidth, p_obj->m_fixedHeight);
    } else {
        (void) memcpy(*pp_data, p_frame_buf, bufsize);
    }
    (*env)->ReleasePrimitiveArrayCritical(env, *p_javaBufWithDataObj, p_frame_buf, 0);
    // 5. Unlock the buf that has data (i.e. the buf locked before copy)
    AV_CHECK_ERR(ret = pthread_mutex_unlock(p_bufWithDataMutex), VCD_read_err_mutex_in_mutex);
    // 6. Set enum flag to NO_BUF_HAS_DATA
    p_obj->m_dataFlag = NO_BUF_HAS_DATA;
    // 7. Unlock data flag mutex
    AV_CHECK_ERR(ret = pthread_mutex_unlock(&(p_obj->m_dataFlagMutex)), VCD_read_err_mutex);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return VCD_NO_ERROR;

    /*
     * propagate unhandled errors
     */
VCD_read_err_get_primitive_array:
    {
        GA_LOGERROR("%s: ERROR: GetPrimitiveArrayCritical() failed", __FUNCTION__);
        pthread_mutex_unlock(p_bufWithDataMutex);
        pthread_mutex_unlock(&(p_obj->m_dataFlagMutex));
        return VCD_ERR_GENERAL;
    }
VCD_read_err_mutex:
    {
        GA_LOGERROR("%s: ERROR: mutex returned: %d", __FUNCTION__, ret);
        return VCD_ERR_GENERAL;
    }

VCD_read_err_cond_wait:
    {
        GA_LOGERROR("%s: ERROR: pthread_cond_wait returned: %d", __FUNCTION__, ret);
        pthread_mutex_unlock(&(p_obj->m_dataFlagMutex));
        return VCD_ERR_GENERAL;
    }

VCD_read_err_mutex_in_mutex:
    {
        GA_LOGERROR("%s: ERROR: mutex in mutex returned: %d", __FUNCTION__, ret);
        pthread_mutex_unlock(&(p_obj->m_dataFlagMutex));
        return VCD_ERR_GENERAL;
    }
}


//
// onJavaDetach
//
static void onJavaDetach(void *arg)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    assert(s_VCD_javaVM);

    (*s_VCD_javaVM)->DetachCurrentThread(s_VCD_javaVM);
    GA_LOGINFO("%s detached thread(%ld) from Java VM", __FUNCTION__, pthread_self());

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
}


//
// cacheClasses
//
static int cacheClasses()
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    JNIEnv *env = NULL;

    env = VCD_getEnv();
    AV_CHECK_NULL(env, cacheClasses_err_get_env);

    if (s_VCD_are_classes_cached) {
        GA_LOGINFO("%s: Classes have already been cached. Will do nothing", __FUNCTION__);
        return VCD_NO_ERROR;
    }

    // keep the Camera class cached and loaded for the process life time
    AV_CHECK_ERR(VCD_cacheCameraClass(env), cacheClasses_err_cache_camera_class);

    // cache camera method ids
    AV_CHECK_ERR(VCD_cacheCameraMethodIds(env), cacheClasses_err_cache_camera_method_ids);

    // cache preview callback handler method ids
    AV_CHECK_ERR(VCD_cachePreviewCbHandlerMethodIds(env), cacheClasses_err_cache_preview_cb_handler_method_ids);

    // keep the List class cached and loaded for the process life time
    AV_CHECK_ERR(VCD_cacheListClass(env), cacheClasses_err_cache_list_class);

    // cache List method ids
    AV_CHECK_ERR(VCD_cacheListMethodIds(env), cacheClasses_err_cache_list_method_ids);

    // keep the Integer class cached and loaded for the process life time
    AV_CHECK_ERR(VCD_cacheIntegerClass(env), cacheClasses_err_cache_integer_class);

    // cache Integer method ids
    AV_CHECK_ERR(VCD_cacheIntegerMethodIds(env), cacheClasses_err_cache_integer_method_ids);

    if (s_VCD_AndroidSdkVersion >= VCD_ANDROID_SDK_VERSION_WITH_SURFACE_TEXTURE) {
        // keep the SurfaceTexture class cached and loaded for the process life time
        AV_CHECK_ERR(VCD_cacheSurfaceTextureClass(env), cacheClasses_err_cache_surface_texture_class);

        // cache surface texture method ids
        AV_CHECK_ERR(VCD_cacheSurfaceTextureMethodIds(env), cacheClasses_err_cache_surface_texture_method_ids);
    }

    s_VCD_are_classes_cached = TRUE;
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return VCD_NO_ERROR;

    /*
     * propagate unhandled errors
     */
cacheClasses_err_cache_camera_class:
    {
        GA_LOGERROR("%s ERROR: cache Camera class failed", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
cacheClasses_err_cache_list_class:
    {
        GA_LOGERROR("%s ERROR: cache List class failed", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
cacheClasses_err_cache_integer_class:
    {
        GA_LOGERROR("%s ERROR: cache Integer class failed", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
cacheClasses_err_cache_surface_texture_class:
    {
        GA_LOGERROR("%s ERROR: cache surface texture class failed", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
cacheClasses_err_get_env:
    {
        GA_LOGERROR("%s %s", __FUNCTION__, "ERROR: GetEnv failed");
        return VCD_ERR_GENERAL;
    }
cacheClasses_err_cache_camera_method_ids:
    {
        GA_LOGERROR("%s cache camera method ids failed", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
cacheClasses_err_cache_preview_cb_handler_method_ids:
    {
        GA_LOGERROR("%s cache preview callback handler method ids failed", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
cacheClasses_err_cache_list_method_ids:
    {
        GA_LOGERROR("%s cache List class method ids failed", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
cacheClasses_err_cache_integer_method_ids:
    {
        GA_LOGERROR("%s cache Integer class method ids failed", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
cacheClasses_err_cache_surface_texture_method_ids:
    {
        GA_LOGERROR("%s cache surface texture handler method ids failed", __FUNCTION__);
        return VCD_ERR_GENERAL;
    }
}


// The preview callback JNI stuff

void Java_com_ericsson_gstreamer_VideoCaptureDevicePreviewCallbackHandler_native_onPreviewFrame(
    JNIEnv *arg_env, jobject thiz, jbyteArray data)
{
    int ret;
    jobject *p_bufReturnedObj;
    jobject *p_bufNextObj;
    VCD_PreviewBufferID nextBuffer;
    pthread_mutex_t *p_bufCurrentMutex;
    pthread_mutex_t *p_bufNextMutex;
    VCD_DataFlag dataFlag;
    static struct timeval time_of_day;
    static struct timeval start_time;
    int timeDiffUsec;
    static int frameCount = 0;
    static int frameCountWindow = 0;
    int maxStrangeBufWarnings = 20;

    GA_LOGTRACE("ENTER %s: data=%p --xx--> thread(%ld)", __FUNCTION__, data, pthread_self());

    VCD_VideoCaptureDevice *p_obj = (VCD_VideoCaptureDevice*) s_VCD_handle;
    if (!s_VCD_handle) {
        GA_LOGERROR("%s: FATAL ERROR: global VCD_handle (s_VCD_handle) is NULL", __FUNCTION__);
        assert(s_VCD_handle);
    }

    if (!frameCount) { // Only first time
        gettimeofday(&start_time, NULL);
        gettimeofday(&(p_obj->m_onPreviewFrameTimer), NULL);
    }

    frameCount++;
    frameCountWindow++;

    gettimeofday(&time_of_day, NULL);
    timeDiffUsec = time_diff_usec(&time_of_day, &(p_obj->m_onPreviewFrameTimer));
    if (timeDiffUsec > p_obj->m_logInterval || !p_obj->m_logInterval) {
        int framerate;
        int framerateWindow;
        int timeDiffSec;
        timeDiffSec = time_diff_sec(&time_of_day, &start_time);
        framerate = frameCount / (timeDiffSec > 0 ? timeDiffSec : 1);
        framerateWindow = frameCountWindow * 1000000 / timeDiffUsec;
        GA_LOGVERB("'native_onPreviewFrame' has now been called %d times -----------------------------CAM---> framerate since start: %d fps, framerate last %d usec: %d fps", frameCount, framerate, timeDiffUsec, framerateWindow);
        gettimeofday(&(p_obj->m_onPreviewFrameTimer), NULL);
        frameCountWindow = 0;
    }

    JNIEnv *env = VCD_getEnv();
    if (!env) {
        GA_LOGERROR("%s: VCD_getEnv() returned NULL", __FUNCTION__);
    } else if (env != arg_env) {
        GA_LOGERROR("%s: strangness --> VCD_getEnv() returned something different from arg_env (JNI environment)", __FUNCTION__);
    } else {
        // GA_LOGTRACE("%s: JNI Environment check past!", __FUNCTION__);
    }

    //
    // 1. Check which buffer is currently used (i.e. one or two)
    // 2. Unlock the mutex for buf one or two (the returned buf)
    // --> 2.5 Check if we are still running, otherwise return...
    // 3. Lock data flag mutex
    // 4. Set enum data flag to DATA_IN_BUF_ONE or DATA_IN_BUF_TWO depending on which buf was returned in this method
    // 5. Unlock data flag mutex
    // 6. Signal condition data exists
    // 7. Lock buf mutex for "the other" buf and do addCallbackBuffer for that buf
    //

    // 1. Check which buffer we are currently using (i.e. one or two)
    if (p_obj->m_currPreviewBuf == BUF_ID_ONE) {
        dataFlag = DATA_IN_BUF_ONE;
        p_bufNextObj = &(p_obj->m_javaBufTwoObj);
        p_bufCurrentMutex = &(p_obj->m_bufOneMutex);
        p_bufNextMutex = &(p_obj->m_bufTwoMutex);
        nextBuffer = BUF_ID_TWO;
    } else if (p_obj->m_currPreviewBuf == BUF_ID_TWO) {
        dataFlag = DATA_IN_BUF_TWO;
        p_bufNextObj = &(p_obj->m_javaBufOneObj);
        p_bufCurrentMutex = &(p_obj->m_bufTwoMutex);
        p_bufNextMutex = &(p_obj->m_bufOneMutex);
        nextBuffer = BUF_ID_ONE;
    } else {
        // should never get here
        goto native_onPreviewFrame_err_wrong_state;
    }

    p_bufReturnedObj = (jobject*) &data;
    if (*p_bufReturnedObj != p_obj->m_javaBufOneObj && *p_bufReturnedObj != p_obj->m_javaBufTwoObj) {
        // This is a bit strange. On Xperia ARC (2.3.4) the buffer returned in the data
        // parameter is one of the buffers I use (i.e. buf one or two). But on Google
        // Nexus (4.0.4) (when setPreviewTexture() is used) some other (unknown) buffer
        // is returned, but the preview images are still put properly in one of the
        // buffers added by addCallbackBuffer (i.e. buf one or two). In any case, I used
        // to identify which buffer is the current buffer used by switching on
        // p_bufReturnedObj. Since this doesn't work for Google Nexus (4.0.4) I instead
        // added the m_currPreviewBuf flag to indicate which buffer we are currently
        // working with... /KD
        if (s_VCD_AndroidSdkVersion < VCD_ANDROID_SDK_VERSION_WITH_SURFACE_TEXTURE) {
            GA_LOGWARN("%s: strange buf object: data=%p but m_javaBufOneObj=%p and m_javaBufTwoObj=%p", __FUNCTION__, data, p_obj->m_javaBufOneObj, p_obj->m_javaBufTwoObj);
        } else {
            if (frameCount < maxStrangeBufWarnings) {
                GA_LOGWARN("%s: strange buf object: data=%p but m_javaBufOneObj=%p and m_javaBufTwoObj=%p", __FUNCTION__, data, p_obj->m_javaBufOneObj, p_obj->m_javaBufTwoObj);
            }
            if (frameCount == maxStrangeBufWarnings) {
                GA_LOGWARN("%s: ...got tired of warning about strange buf object! Will stop now...", __FUNCTION__);
            }
        }
    }

    // 2. Unlock the mutex for buf one or two (the returned buf)
    AV_CHECK_ERR(ret = pthread_mutex_unlock(p_bufCurrentMutex), native_onPreviewFrame_err_mutex);
    // 2.5 Check if we are still running, otherwise signal stopped and return...
    if (p_obj->m_state != VCD_STATE_RUNNING) {
        GA_LOGINFO("%s: Signalling condition that stop running is confirmed and completed...", __FUNCTION__);
        AV_CHECK_ERR(ret = pthread_cond_signal(&(p_obj->m_hasStoppedCondition)), native_onPreviewFrame_err_cond_signal);
        return; // Return here without locking and adding another buffer
    }
    // 3. Lock data flag mutex
    AV_CHECK_ERR(ret = pthread_mutex_lock(&(p_obj->m_dataFlagMutex)), native_onPreviewFrame_err_mutex);
    // 4. Set enum data flag to DATA_IN_BUF_ONE or DATA_IN_BUF_TWO depending on which buf was returned in this method
    p_obj->m_dataFlag = dataFlag;
    // 5. Unlock data flag mutex
    AV_CHECK_ERR(ret = pthread_mutex_unlock(&(p_obj->m_dataFlagMutex)), native_onPreviewFrame_err_mutex);
    // 6. Signal condition data exists
    GA_LOGTRACE("%s: Signalling condition for data in buf %s --xx--> thread(%ld)", __FUNCTION__, dataFlag == DATA_IN_BUF_ONE ? "one" : "two", pthread_self());
    AV_CHECK_ERR(ret = pthread_cond_signal(&(p_obj->m_dataFlagCondition)), native_onPreviewFrame_err_cond_signal);
    // 7. Lock buf mutex for "the other" buf and do addCallbackBuffer for that buf
    AV_CHECK_ERR(ret = pthread_mutex_lock(p_bufNextMutex), native_onPreviewFrame_err_mutex);
    VCD_addCallbackBuffer(s_VCD_handle, *p_bufNextObj);
    p_obj->m_currPreviewBuf = nextBuffer;

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return;

    /*
     * propagate unhandled errors
     */
native_onPreviewFrame_err_wrong_state:
    {
        GA_LOGERROR("%s: FATAL ERROR. Wrong state. m_currPreviewBuf == BUF_ID_NOT_SET", __FUNCTION__);
        assert(0);
        return;
    }
native_onPreviewFrame_err_mutex:
    {
        GA_LOGERROR("%s: ERROR: pthread_mutex_xxx returned %d", __FUNCTION__, ret);
        return;
    }
native_onPreviewFrame_err_cond_signal:
    {
        GA_LOGERROR("%s: ERROR: pthread_cond_signal returned %d", __FUNCTION__, ret);
        return;
    }
}


static JNINativeMethod gMethods[] =
{
    {"native_onPreviewFrame", "([B)V", (void *) Java_com_ericsson_gstreamer_VideoCaptureDevicePreviewCallbackHandler_native_onPreviewFrame}
};


// this function only registers the native methods
static int register_jni(JNIEnv *a_env)
{
    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    assert(a_env);
    assert(s_VCD_classPreviewCbHandler);

    int result = (*a_env)->RegisterNatives(a_env, s_VCD_classPreviewCbHandler, gMethods, VCD_NELEM(gMethods));

    GA_LOGTRACE("EXIT %s", __FUNCTION__);

    return result;
}


static jint android_video_source_jni_init(JavaVM *vm, void *reserved)
{
    GA_LOGTRACE("ENTER %s - file: %s", __FUNCTION__, __FILE__);

    if (s_VCD_is_initialized) {
        GA_LOGINFO("%s Already initialized (JNI). Will do nothing", __FUNCTION__);
        return VCD_NO_ERROR;
    }

    JNIEnv *env = NULL;

    assert(vm);
    s_VCD_javaVM = vm;

    if (!s_VCD_jniEnvKey) {
        AV_CHECK_ERR(pthread_key_create(&s_VCD_jniEnvKey, onJavaDetach), err_key_create);
    }
    env = VCD_getEnv();
    if (!env)
        goto err_get_env;

    // Check what version we are on...
    s_VCD_AndroidSdkVersion = getAndroidSdkVersion(env);
    if (s_VCD_AndroidSdkVersion < VCD_MIN_ANDROID_SDK_VERSION) {
        goto err_sdk_version_too_low;
    }

    // Find VCD VideoCaptureDevicePreviewCallbackHandler java class
    if (!s_VCD_classPreviewCbHandler)
    {
        int nRefs = 0;
        jclass *class_global_ref_array = NULL;
        const char *class_name_str_array[] = {VCD_CLASS_PREVIEW_CB_HANDLER_STR};
        size_t class_name_str_array_len = VCD_NELEM(class_name_str_array);

        nRefs = jniutils_find_app_classes(env, VCD_MODULE_NAME_STR, class_name_str_array, class_name_str_array_len,
                &class_global_ref_array);
        if (class_name_str_array_len != nRefs || !class_global_ref_array[0]) {
            free(class_global_ref_array);
            class_global_ref_array = NULL;
            goto err_find_app_class;
        }

        s_VCD_classPreviewCbHandler = class_global_ref_array[0];
        free(class_global_ref_array);
        class_global_ref_array = NULL;
    }

    // Native method registration
    if (!s_VCD_is_native_methods_registered) {
        if (register_jni(env) < 0) {
            goto err_register;
        }

        s_VCD_is_native_methods_registered = TRUE;
        GA_LOGINFO("%s registration of native methods done!", __FILE__);
    }

    // Find SDK java classes, method ids, ...
    GA_LOGINFO("%s Calling cacheClasses...", __FUNCTION__);
    if (cacheClasses() == VCD_ERR_GENERAL) {
        GA_LOGERROR("%s ERROR: cacheClasses failed", __FILE__);
        return VCD_ERR_GENERAL;
    }

    s_VCD_is_initialized = TRUE;
    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return VCD_NO_ERROR;

    /*
     * propagate unhandled errors
     */
err_key_create:
    {
        GA_LOGERROR("%s ERROR: thread-specific data key creation failed", __FILE__);
        return VCD_ERR_GENERAL;
    }

err_get_env:
    {
        GA_LOGERROR("%s ERROR: GetEnv failed", __FILE__);
        return VCD_ERR_GENERAL;
    }

err_sdk_version_too_low:
    {
        GA_LOGERROR("%s Android SDK version is %d. Android SDK versions < %u are not supported!", __FILE__, s_VCD_AndroidSdkVersion, VCD_MIN_ANDROID_SDK_VERSION);
        return VCD_ERR_GENERAL;
    }

err_find_app_class:
    {
        GA_LOGERROR("%s ERROR: find VCD java classes failed", __FILE__);
        return VCD_ERR_GENERAL;
    }

err_register:
    {
        GA_LOGERROR("%s ERROR: registration failed", __FILE__);
        return VCD_ERR_GENERAL;
    }
}


//
// VCD_init
//
int VCD_init()
{
    myLogLevel = DEFAULT_LOG_LEVEL_ANDROID;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    int ok = FALSE;
    if (s_VCD_is_initialized)
        ok = TRUE;
    else {
        JavaVM *p_vm = jniutils_get_created_vm();
        if (p_vm)
        {
            ok = (VCD_NO_ERROR == android_video_source_jni_init(p_vm, NULL)) ?
                  TRUE : FALSE;
        }
    }

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return ok;
}


/*
 * Utils
 */

/*
 * create_wait_time
 */
void create_wait_time(struct timespec * p_time, int seconds_to_wait)
{
    struct timeval time_of_day;
    gettimeofday(&time_of_day, NULL);
    p_time->tv_sec = time_of_day.tv_sec + seconds_to_wait;
    p_time->tv_nsec = time_of_day.tv_usec * 1000;
}

/*
 * time_diff_usec
 */
int time_diff_usec(struct timeval *p_timeA, struct timeval *p_timeB)
{
    static int diff_usec;

    diff_usec = (p_timeA->tv_sec - p_timeB->tv_sec) * 1000000;
    if (p_timeA->tv_usec >= p_timeB->tv_usec) {
        diff_usec += p_timeA->tv_usec - p_timeB->tv_usec;
    } else {
        diff_usec -= p_timeB->tv_usec - p_timeA->tv_usec;
    }

    return diff_usec;
}


/*
 * time_diff_sec
 */
int time_diff_sec(struct timeval *p_timeA, struct timeval *p_timeB)
{
    return p_timeA->tv_sec - p_timeB->tv_sec;
}

/*
 * getAndroidSdkVersion
 */
int getAndroidSdkVersion(JNIEnv *p_env)
{
    jfieldID sdkVersionFieldID;
    jint sdkVersion;

    GA_LOGTRACE("ENTER %s --xx--> thread(%ld)", __FUNCTION__, pthread_self());

    sdkVersionFieldID = NULL;
    sdkVersion = VCD_MIN_ANDROID_SDK_VERSION;

    assert(p_env);

    jclass versionClass = (*p_env)->FindClass(p_env, VCD_CLASS_OS_BUILD_VERSION_STR);
    AV_CHECK_NULL(versionClass, getAndroidApiVersion_err_find_class);

    sdkVersionFieldID = (*p_env)->GetStaticFieldID(p_env, versionClass, "SDK_INT", "I");
    AV_CHECK_NULL(sdkVersionFieldID, getAndroidApiVersion_err_get_field_id);

    sdkVersion = (*p_env)->GetStaticIntField(p_env, versionClass, sdkVersionFieldID);
    GA_LOGINFO("%s: Android SDK version identified as: %d", __FUNCTION__, sdkVersion);

    (*p_env)->DeleteLocalRef(p_env, versionClass);

    GA_LOGTRACE("EXIT %s", __FUNCTION__);
    return sdkVersion;

    /*
     * propagate unhandled errors
     */
getAndroidApiVersion_err_find_class:
    {
        GA_LOGERROR("%s: ERROR: FindClass() failed for %s", __FUNCTION__, VCD_CLASS_OS_BUILD_VERSION_STR);
        GA_LOGWARN("%s: Using default Android SDK version: %d", __FUNCTION__, sdkVersion);
        return sdkVersion;
    }
getAndroidApiVersion_err_get_field_id:
    {
        GA_LOGERROR("%s: ERROR: GetStaticFieldID() failed", __FUNCTION__);
        GA_LOGWARN("%s: Using default Android SDK version: %d", __FUNCTION__, sdkVersion);
        return sdkVersion;
    }
}
