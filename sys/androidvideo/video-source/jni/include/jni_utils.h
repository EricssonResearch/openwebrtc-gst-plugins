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

#ifndef JNI_UTILS_H
#define JNI_UTILS_H

#include <jni.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


JavaVM*
jniutils_get_created_vm();

/* Returns global references to the class objects from fully-qualified names.
 * class_global_ref_array:    contains the global references or NULL if a class cannot be found,
 *                            ownership is transferred =>
 *                            dispose the global references (use DeleteGlobalRef()) and
 *                            deallocate the array (use free()) when they are no longer needed
 * return value:              number of global references found
 * */
int
jniutils_find_app_classes(JNIEnv* p_env, const char* module_tag_str, const char** class_name_str_array, int class_name_str_array_len,
                          jclass** class_global_ref_array);


#ifdef __cplusplus
}
#endif

#endif /* JNI_UTILS_H */
