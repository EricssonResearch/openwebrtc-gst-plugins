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

#ifndef gst_android_video_source_log_h
#define gst_android_video_source_log_h

#include <gst/gst.h>

/* main logging switch */
#define GA_LOG_ENABLED

/* trace switch (independent of main logging switch) */
#define GA_TRACES_ENABLED

/* Enable Android logging on Android (results in both GST and Android logging) */
#define ANDROID_LOGGING_ENABLED
#ifdef ANDROID_LOGGING_ENABLED
#include <android/log.h>
#endif

/* Repetetive logs (such as for each frame) are only shown with this interval */
#define DEFAULT_USEC_BETWEEN_LOGS 2000000

/* main log prefix */
#define LOG_TAG "gstandroidvideosrc"

/* Levels */
#define DEFAULT_LOG_LEVEL_ANDROID LLA_ERROR // LLA_TRACE
#define LOG_LEVEL_ANDROID_MAX 6
typedef enum {LLA_NONE, LLA_ERROR, LLA_WARNING, LLA_INFO, LLA_DEBUG, LLA_VERBOSE, LLA_TRACE} LogLevel;
LogLevel myLogLevel;

/* int            prio */
/* const char*    tag */
/* const char*    fmt */
#ifdef GA_LOG_ENABLED
#ifdef ANDROID_LOGGING_ENABLED /* GStreamer logging and Android logging */
#define GA_LOGVERB(...)     GST_LOG(__VA_ARGS__); if (myLogLevel > LLA_DEBUG) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define GA_LOGDEBUG(...)    GST_DEBUG(__VA_ARGS__); if (myLogLevel > LLA_INFO) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define GA_LOGINFO(...)     GST_INFO(__VA_ARGS__); if (myLogLevel > LLA_WARNING) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define GA_LOGWARN(...)     GST_WARNING(__VA_ARGS__); if (myLogLevel > LLA_ERROR) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define GA_LOGERROR(...)    GST_ERROR(__VA_ARGS__); if (myLogLevel > LLA_NONE) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else /* Only GStreamer logging */
#define GA_LOGERROR(...)    GST_ERROR(__VA_ARGS__)
#define GA_LOGWARN(...)     GST_WARNING(__VA_ARGS__)
#define GA_LOGINFO(...)     GST_INFO(__VA_ARGS__)
#define GA_LOGDEBUG(...)    GST_DEBUG(__VA_ARGS__)
#define GA_LOGVERB(...)     GST_LOG(__VA_ARGS__)
#endif
#else /* No logging at all */
#define GA_LOGERROR(...)
#define GA_LOGWARN(...)
#define GA_LOGINFO(...)
#define GA_LOGDEBUG(...)
#define GA_LOGVERB(...)
#endif /* GA_LOG_ENABLED */


#ifdef GA_TRACES_ENABLED
#ifdef ANDROID_LOGGING_ENABLED /* GStreamer traces and Android traces */
#define GA_LOGTRACE(...)    GST_LOG(__VA_ARGS__); if (myLogLevel > LLA_VERBOSE) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#else /* Only GStreamer traces */
#define GA_LOGTRACE(...)    GST_LOG(__VA_ARGS__);
#endif
#else /* No traces at all */
#define GA_LOGTRACE(...)
#endif /* GA_TRACES_ENABLED */

#endif /* gst_android_video_source_log_h */
