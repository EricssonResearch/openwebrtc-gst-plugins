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

#include "include/jni_utils.h"
#include "classes_dex.h"

#include <android/log.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef FALSE
#define FALSE    (0)
#endif

#ifndef TRUE
#define TRUE    (!FALSE)
#endif

#define JNIU_NELEM(x)    ((int) (sizeof(x) / sizeof((x)[0])))

#define JNIU_ANDROID_APPLICATIONS_DATA_DIR_ROOT_STR    "/data/data"
#define JNIU_ANDROID_RUNTIME_DALVIK_LIB_STR    "libdvm.so"
#define JNIU_ANDROID_RUNTIME_ART_LIB_STR    "libart.so"

#define JNIU_OPENWEBRTC_RELATIVE_DIR_STR    "/owr"
#define JNIU_DEX_INPUT_RELATIVE_DIR_STR    "/dex_input"
#define JNIU_DEX_INPUT_FILE_STR    "/classes_dex.jar"
#define JNIU_DEX_OUTPUT_RELATIVE_DIR_STR    "/dex_output"

#undef LOGPREFIX
#define LOGPREFIX "jniutils-N: "
#ifndef LOG_TAG
#define LOG_TAG "OWR"
#endif

/* switch release, includes only logging-code for priorities Warning and higher */
#define JNIU_LOG_RELEASE

#define JNIU_LOG(prio, fmt, args...)    __android_log_print(prio, LOG_TAG, fmt, args)

#ifdef JNIU_LOG_RELEASE
#define JNIU_LOGVERB(fmt, args...)
#define JNIU_LOGINFO(fmt, args...)
#define JNIU_LOGWARN(fmt, args...)     JNIU_LOG(ANDROID_LOG_WARN, fmt, ##args)
#define JNIU_LOGERROR(fmt, args...)    JNIU_LOG(ANDROID_LOG_ERROR, fmt, ##args)
#else
#define JNIU_LOGVERB(fmt, args...)     JNIU_LOG(ANDROID_LOG_VERBOSE, fmt, ##args)
#define JNIU_LOGINFO(fmt, args...)     JNIU_LOG(ANDROID_LOG_INFO, fmt, ##args)
#define JNIU_LOGWARN(fmt, args...)     JNIU_LOG(ANDROID_LOG_WARN, fmt, ##args)
#define JNIU_LOGERROR(fmt, args...)    JNIU_LOG(ANDROID_LOG_ERROR, fmt, ##args)
#endif

JavaVM*
jniutils_get_created_vm()
{
    JNIU_LOGVERB(LOGPREFIX "ENTER %s", __FUNCTION__);

    const char* runtimeLibStr[] = {JNIU_ANDROID_RUNTIME_DALVIK_LIB_STR, JNIU_ANDROID_RUNTIME_ART_LIB_STR};
    const int nLibs = JNIU_NELEM(runtimeLibStr);

    JavaVM* pVm = NULL;
    int i = 0;
    while (!pVm && nLibs > i)
    {
        void* handle = dlopen(runtimeLibStr[i], RTLD_LOCAL);
        ++i;
        if (handle)
        {
            (void) dlerror();
            typedef jint GetCreatedJavaVms(JavaVM** vmBuf, jsize bufLen, jsize* nVMs);
            GetCreatedJavaVms* jniGetCreatedJavaVms = (GetCreatedJavaVms*) dlsym(handle, "JNI_GetCreatedJavaVMs");
            if (!(dlerror() || !jniGetCreatedJavaVms))
            {
                JNIU_LOGINFO(LOGPREFIX "%s symbol JNI_GetCreatedJavaVMs= %p", runtimeLibStr[i-1], jniGetCreatedJavaVms);

                jsize nVms = 0;
                if (!jniGetCreatedJavaVms(&pVm, 1, &nVms))
                {
                    if (1 != nVms)
                    {
                        pVm = NULL;
                    }
                    else
                    {
                        JNIU_LOGINFO(LOGPREFIX "%s got created Java VM= %p  nVms= %d", runtimeLibStr[i-1], pVm, nVms);
                    }
                }
                else
                    pVm = NULL;
            }

            /* only keep the reference on the runtime lib if the created Java VM was retrieved */
            if (!pVm)
            {
                int err = dlclose(handle);
                if (err)
                {
                    JNIU_LOGWARN(LOGPREFIX "%s %s WARNING: %s dlclose() failed (%d)",
                                 __FILE__, __FUNCTION__, runtimeLibStr[i-1], err);
                }
            }
        }
    }

    if (!pVm)
    {
        JNIU_LOGERROR(LOGPREFIX "%s %s ERROR: get created Java VM failed", __FILE__, __FUNCTION__);
    }

    JNIU_LOGVERB(LOGPREFIX "EXIT  %s", __FUNCTION__);
    return pVm;
}

static char*
get_app_data_root_dir_str()
{
    JNIU_LOGVERB(LOGPREFIX "ENTER %s", __FUNCTION__);

    int err = 0;
    char* str = NULL;
    char buf[MAXPATHLEN];
    pid_t pid = getpid();

    buf[0] = 0;
    (void) sprintf(buf, "/proc/%lu/cmdline", (unsigned long) pid);

    int move_offset = (int) strlen(JNIU_ANDROID_APPLICATIONS_DATA_DIR_ROOT_STR "/");
    FILE* procFile = fopen(buf, "r");
    if (procFile)
    {
        int num = MAXPATHLEN - move_offset - 1;
        if (!fgets(buf, num, procFile))
        {
            err = 1;
            JNIU_LOGERROR(LOGPREFIX "%s %s ERROR: read /proc file failed", __FILE__, __FUNCTION__);
        }

        if (fclose(procFile))
        {
            JNIU_LOGWARN(LOGPREFIX "%s %s WARNING: close /proc file failed", __FILE__, __FUNCTION__);
        }
    }
    else
    {
        err = 1;
        JNIU_LOGERROR(LOGPREFIX "%s %s ERROR: open /proc file failed", __FILE__, __FUNCTION__);
    }

    (void) memmove(buf + move_offset, buf, strlen(buf) + 1);
    (void) memcpy(buf, JNIU_ANDROID_APPLICATIONS_DATA_DIR_ROOT_STR "/", move_offset);

    if (!err)
    {
        size_t len = strlen(buf);
        if (len)
        {
            str = malloc(len + 1);
            if (str)
            {
                (void) strcpy(str, buf);
            }
            else
            {
                err = 1;
                JNIU_LOGERROR(LOGPREFIX "%s %s ERROR: malloc(%u) failed", __FILE__, __FUNCTION__, len + 1);
            }
        }
        else
        {
            err = 1;
            JNIU_LOGERROR(LOGPREFIX "%s %s ERROR: empty cmdline", __FILE__, __FUNCTION__);
        }
    }

    JNIU_LOGVERB(LOGPREFIX "EXIT %s", __FUNCTION__);
    return str;
}

static int
get_app_data_dir(const char* abs_path_str)
{
    JNIU_LOGVERB(LOGPREFIX "ENTER %s", __FUNCTION__);

    int err = 0;
    struct stat buf;

    if (!abs_path_str)
    {
        JNIU_LOGWARN(LOGPREFIX "%s %s WARNING: abs_path_str= NULL", __FILE__, __FUNCTION__);
        return 1;
    }

    if (stat(abs_path_str, &buf))
    {
        int buf = mkdir(abs_path_str, S_IRWXU);
        if (buf)
        {
            err = 1;
            JNIU_LOGERROR(LOGPREFIX "%s %s ERROR: create app dir failed %s", __FILE__, __FUNCTION__, abs_path_str);
        }
    }

    JNIU_LOGVERB(LOGPREFIX "EXIT %s", __FUNCTION__);
    return err;
}

static char*
cat_strings(const char* first_str, const char* second_str)
{
    JNIU_LOGVERB(LOGPREFIX "ENTER %s", __FUNCTION__);

    assert(first_str);
    assert(second_str);

    size_t str_length = strlen(first_str) + strlen(second_str);
    char* str = calloc(str_length + 1, sizeof(char));
    if (!str)
        goto err_calloc;

    (void) strcat(str, first_str);
    (void) strcat(str, second_str);

    JNIU_LOGVERB(LOGPREFIX "EXIT %s", __FUNCTION__);
    return str;

err_calloc:
    {
        JNIU_LOGERROR(LOGPREFIX "%s %s ERROR: calloc(%u) failed", __FILE__, __FUNCTION__, str_length + 1);
        return NULL;
    }
}

static int
write_dex_to_file(const char* dex_file_name_str, unsigned char* dex_data, unsigned int dex_size)
{
    JNIU_LOGVERB(LOGPREFIX "ENTER %s", __FUNCTION__);

    int err = 0;

    if (!dex_file_name_str || 0 == strlen(dex_file_name_str))
        goto err_args;

    if (!dex_data || 0 == dex_size)
        goto err_args;

    FILE* p_file = fopen(dex_file_name_str, "wb");
    if (!p_file)
        goto err_fopen;

    unsigned char* p_byte = dex_data;
    long n_bytes_written = 0;
    while (!err && n_bytes_written < dex_size)
    {
        fwrite(p_byte, 1, sizeof(*p_byte), p_file);
        if (ferror(p_file))
        {
            err = -1;
            break;
        }

        ++p_byte;
        ++n_bytes_written;
    }

    if (err)
        goto err_fwrite;

    JNIU_LOGINFO(LOGPREFIX "wrote dex data to file '%s'", dex_file_name_str);
    if (fclose(p_file))
    {
        JNIU_LOGWARN(LOGPREFIX "%s %s WARNING: close file '%s' failed", __FILE__, __FUNCTION__, dex_file_name_str);
    }

    JNIU_LOGVERB(LOGPREFIX "EXIT %s", __FUNCTION__);
    return err;

err_args:
    {
        JNIU_LOGERROR(LOGPREFIX "%s %s ERROR: bad arg", __FILE__, __FUNCTION__);
        return -1;
    }

err_fopen:
    {
        JNIU_LOGERROR(LOGPREFIX "%s %s ERROR: fopen(%s, \"wb\") failed", __FILE__, __FUNCTION__, dex_file_name_str);
        return -1;
    }

err_fwrite:
    {
        fclose(p_file);
        JNIU_LOGERROR(LOGPREFIX "%s %s ERROR: fwrite failed", __FILE__, __FUNCTION__);
        return -1;
    }
}

int
jniutils_find_app_classes(JNIEnv* p_env, const char* module_tag_str, const char** class_name_str_array, int class_name_str_array_len,
                          jclass** class_global_ref_array)
{
    JNIU_LOGVERB(LOGPREFIX "ENTER %s", __FUNCTION__);

    if (!p_env || !module_tag_str || !class_name_str_array || !class_global_ref_array)
    {
        goto err_arg_null;
    }

    *class_global_ref_array = NULL;

    if (0 >= class_name_str_array_len)
    {
        JNIU_LOGWARN(LOGPREFIX "%s %s WARNING: arg 'class_name_str_array_len' <= 0", __FILE__, __FUNCTION__);
        return 0;
    }

    char* app_data_root_str = NULL;
    char* app_dex_file_str = NULL;
    char* module_dex_input_dir_str = NULL;
    char* module_dex_output_dir_str = NULL;
    char module_dir_str[MAXPATHLEN];
    jclass* cls_app_x_globalref_array = NULL;
    jclass cls_class_loader = 0;
    jclass cls_dex_class_loader = 0;
    jclass cls_app_x = 0;
    jstring dex_file_jstr = 0;
    jstring dex_optim_dir_jstr = 0;
    jstring app_class_jstr = 0;
    jobject obj_class_loader = 0;
    jobject obj_dex_class_loader = 0;

    int n_global_refs = 0;
    int err_id = 0;
    int err = 0;

    /* get directories and write the dex data to an app data file */

    app_data_root_str = get_app_data_root_dir_str();
    if (!app_data_root_str)
    {
        err_id = -1;
        goto err_general;
    }

    JNIU_LOGINFO(LOGPREFIX "app_data_root_str= %s", app_data_root_str);

    (void) strcpy(module_dir_str, app_data_root_str);
    (void) strcat(module_dir_str, JNIU_OPENWEBRTC_RELATIVE_DIR_STR);
    if (get_app_data_dir(module_dir_str))
    {
        err_id = -10;
        goto err_general;
    }

    (void) strcat(module_dir_str, "/");
    (void) strcat(module_dir_str, module_tag_str);
    if (get_app_data_dir(module_dir_str))
    {
        err_id = -20;
        goto err_general;
    }

    module_dex_input_dir_str = cat_strings(module_dir_str, JNIU_DEX_INPUT_RELATIVE_DIR_STR);
    if (get_app_data_dir(module_dex_input_dir_str))
    {
        err_id = -30;
        goto err_general;
    }

    app_dex_file_str = cat_strings(module_dex_input_dir_str, JNIU_DEX_INPUT_FILE_STR);
    if (!app_dex_file_str)
    {
        err_id = -40;
        goto err_general;
    }

    JNIU_LOGINFO(LOGPREFIX "module_dex_input_dir_str= %s", module_dex_input_dir_str);

    err = write_dex_to_file(app_dex_file_str, classes_dex_jar, classes_dex_jar_len);
    if (err)
    {
        err_id = -50;
        goto err_general;
    }

    module_dex_output_dir_str = cat_strings(module_dir_str, JNIU_DEX_OUTPUT_RELATIVE_DIR_STR);
    if (!module_dex_output_dir_str)
    {
        err_id = -60;
        goto err_general;
    }

    if (get_app_data_dir(module_dex_output_dir_str))
    {
        err_id = -70;
        goto err_general;
    }

    JNIU_LOGINFO(LOGPREFIX "module_dex_output_dir_str= %s", module_dex_output_dir_str);

    /* get classes (Android SDK) */

    cls_class_loader = (*p_env)->FindClass(p_env, "java/lang/ClassLoader");
    if ((*p_env)->ExceptionCheck(p_env))
    {
        err_id = -80;
        goto err_general;
    }

    cls_dex_class_loader = (*p_env)->FindClass(p_env, "dalvik/system/DexClassLoader");
    if ((*p_env)->ExceptionCheck(p_env))
    {
        err_id = -90;
        goto err_general;
    }

    /* get method IDs (Android SDK) */

    jmethodID id_get_system_class_loader =
        (*p_env)->GetStaticMethodID(p_env, cls_class_loader, "getSystemClassLoader",
                                    "()Ljava/lang/ClassLoader;");
    if (!id_get_system_class_loader)
    {
        err_id = -100;
        goto err_general;
    }

    jmethodID id_dex_class_loader =
        (*p_env)->GetMethodID(p_env, cls_dex_class_loader, "<init>",
                              "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    if (!id_dex_class_loader)
    {
        err_id = -110;
        goto err_general;
    }

    jmethodID id_load_class = (*p_env)->GetMethodID(p_env, cls_dex_class_loader, "loadClass",
                                                    "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!id_load_class)
    {
        err_id = -120;
        goto err_general;
    }

    /* get objects (Android SDK) */

    dex_file_jstr = (*p_env)->NewStringUTF(p_env, app_dex_file_str);
    if ((*p_env)->ExceptionCheck(p_env))
    {
        err_id = -130;
        goto err_general;
    }

    dex_optim_dir_jstr = (*p_env)->NewStringUTF(p_env, module_dex_output_dir_str);
    if ((*p_env)->ExceptionCheck(p_env))
    {
        err_id = -140;
        goto err_general;
    }

    obj_class_loader = (*p_env)->CallStaticObjectMethod(p_env, cls_class_loader, id_get_system_class_loader);
    if (!obj_class_loader)
    {
        err_id = -150;
        goto err_general;
    }

    obj_dex_class_loader = (*p_env)->NewObject(p_env, cls_dex_class_loader, id_dex_class_loader,
                                                       dex_file_jstr, dex_optim_dir_jstr, 0, obj_class_loader);
    if ((*p_env)->ExceptionCheck(p_env))
    {
        err_id = -160;
        goto err_general;
    }

    JNIU_LOGINFO(LOGPREFIX "%s", "called NewObject(), created DexClassLoader obj");

    /* load the application classes */

    cls_app_x_globalref_array = calloc(class_name_str_array_len, sizeof(jclass));
    if (!cls_app_x_globalref_array)
    {
        err_id = -170;
        goto err_general;
    }
    *class_global_ref_array = cls_app_x_globalref_array;

    int i;
    for (i = 0; i < class_name_str_array_len; ++i)
    {
        app_class_jstr = (*p_env)->NewStringUTF(p_env, class_name_str_array[i]);
        if ((*p_env)->ExceptionCheck(p_env))
        {
            err_id = -180;
            goto err_general;
        }

        cls_app_x = (jclass) (*p_env)->CallNonvirtualObjectMethod(p_env, obj_dex_class_loader,
                                           cls_dex_class_loader, id_load_class, app_class_jstr);
        (*p_env)->DeleteLocalRef(p_env, app_class_jstr);
        app_class_jstr = 0;
        if ((*p_env)->ExceptionCheck(p_env))
        {
            (*p_env)->ExceptionClear(p_env);
            JNIU_LOGWARN(LOGPREFIX "%s %s WARNING: load class '%s' failed, will try loading any remaining classes",
                         __FILE__, __FUNCTION__, class_name_str_array[i]);
        }
        else
        {
            cls_app_x_globalref_array[i] = (*p_env)->NewGlobalRef(p_env, cls_app_x);
            if (!cls_app_x_globalref_array[i])
            {
                err_id = -190;
                goto err_general;
            }
            else
            {
                ++n_global_refs;
                JNIU_LOGINFO(LOGPREFIX "loaded '%s' class", class_name_str_array[i]);
            }

            (*p_env)->DeleteLocalRef(p_env, cls_app_x);
            cls_app_x = 0;
        }
    }

    free(app_data_root_str);
    free(app_dex_file_str);
    free(module_dex_input_dir_str);
    free(module_dex_output_dir_str);

    (*p_env)->DeleteLocalRef(p_env, cls_class_loader);
    (*p_env)->DeleteLocalRef(p_env, cls_dex_class_loader);
    (*p_env)->DeleteLocalRef(p_env, dex_file_jstr);
    (*p_env)->DeleteLocalRef(p_env, dex_optim_dir_jstr);
    (*p_env)->DeleteLocalRef(p_env, obj_class_loader);
    (*p_env)->DeleteLocalRef(p_env, obj_dex_class_loader);

    JNIU_LOGVERB(LOGPREFIX "EXIT %s", __FUNCTION__);
    return n_global_refs;

err_general:
    {
        free(app_data_root_str);
        free(app_dex_file_str);
        free(module_dex_input_dir_str);
        free(module_dex_output_dir_str);
        free(cls_app_x_globalref_array);

        (*p_env)->ExceptionClear(p_env);

        if (cls_class_loader)
            (*p_env)->DeleteLocalRef(p_env, cls_class_loader);
        if (cls_dex_class_loader)
            (*p_env)->DeleteLocalRef(p_env, cls_dex_class_loader);
        if (cls_app_x)
            (*p_env)->DeleteLocalRef(p_env, cls_app_x);
        if (dex_file_jstr)
            (*p_env)->DeleteLocalRef(p_env, dex_file_jstr);
        if (dex_optim_dir_jstr)
            (*p_env)->DeleteLocalRef(p_env, dex_optim_dir_jstr);
        if (app_class_jstr)
            (*p_env)->DeleteLocalRef(p_env, app_class_jstr);
        if (obj_class_loader)
            (*p_env)->DeleteLocalRef(p_env, obj_class_loader);
        if (obj_dex_class_loader)
            (*p_env)->DeleteLocalRef(p_env, obj_dex_class_loader);

        JNIU_LOGERROR(LOGPREFIX "%s %s ERROR: err_id= %d", __FILE__, __FUNCTION__, err_id);
        return n_global_refs;
    }

err_arg_null:
    {
        JNIU_LOGERROR(LOGPREFIX "%s %s ERROR: an arg is NULL", __FILE__, __FUNCTION__);
        return 0;
    }
}
