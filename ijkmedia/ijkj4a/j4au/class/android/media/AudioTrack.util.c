/*
 * copyright (c) 2015 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "AudioTrack.util.h"
#include <math.h>
#include "j4a/class/android/media/PlaybackParams.h"

#ifndef STREAM_MUSIC
#define STREAM_MUSIC 3
#endif
void J4AC_android_media_AudioTrack__setSpeed(JNIEnv *env, jobject thiz, jfloat speed)
{
    if (J4A_GetSystemAndroidApiLevel(env) < 23) {
        jint sample_rate = J4AC_android_media_AudioTrack__getSampleRate(env,thiz);
        if (J4A_ExceptionCheck__throwAny(env)) {
            return;
        }
        J4AC_android_media_AudioTrack__setPlaybackRate__catchAll(env, thiz,
                                                                 (jint) (sample_rate * speed));
        return;
    }

    jobject temp = NULL;
    jobject params = J4AC_android_media_AudioTrack__getPlaybackParams(env, thiz);
    if (J4A_ExceptionCheck__throwAny(env) || !params)
        goto fail;

    temp = J4AC_android_media_PlaybackParams__setSpeed(env, params, speed);
    J4A_DeleteLocalRef__p(env, &temp);
    if (J4A_ExceptionCheck__throwAny(env))
        goto fail;

    J4A_ALOGE("%s %f", __func__, (double)speed);
    J4AC_android_media_AudioTrack__setPlaybackParams(env, thiz, params);
    if (J4A_ExceptionCheck__throwAny(env))
        goto fail;

fail:
    J4A_DeleteLocalRef__p(env, &params);
}

void J4AC_android_media_AudioTrack__setSpeed__catchAll(JNIEnv *env, jobject thiz, jfloat speed)
{
    J4A_ALOGE("%s", __func__);
    J4AC_android_media_AudioTrack__setSpeed(env, thiz, speed);
    if (J4A_ExceptionCheck__catchAll(env))
        return;

    return;
}

/* Keep generated JNI bindings and the pre-23 entry point unchanged. */
int J4AC_android_media_AudioTrack__setParamsChecked(JNIEnv *env, jobject thiz,
    jfloat speed, int preserve_pitch, jfloat *actual_speed, jfloat *actual_pitch)
{
    jobject params = NULL, returned = NULL, observed = NULL;
    jclass clazz = NULL;
    jmethodID set_pitch, set_fallback, get_speed, get_pitch, get_fallback;
    int result = -1;
    jfloat expected_pitch = 1.0f;
    if (J4A_GetSystemAndroidApiLevel(env) < 23)
        return -1;
    params = J4AC_android_media_AudioTrack__getPlaybackParams(env, thiz);
    if (J4A_ExceptionCheck__catchAll(env) || !params)
        goto done;
    clazz = (*env)->GetObjectClass(env, params);
    if (J4A_ExceptionCheck__catchAll(env) || !clazz)
        goto done;
    set_pitch = (*env)->GetMethodID(env, clazz, "setPitch", "(F)Landroid/media/PlaybackParams;");
    if (J4A_ExceptionCheck__catchAll(env) || !set_pitch) goto done;
    set_fallback = (*env)->GetMethodID(env, clazz, "setAudioFallbackMode", "(I)Landroid/media/PlaybackParams;");
    if (J4A_ExceptionCheck__catchAll(env) || !set_fallback) goto done;
    get_speed = (*env)->GetMethodID(env, clazz, "getSpeed", "()F");
    if (J4A_ExceptionCheck__catchAll(env) || !get_speed) goto done;
    get_pitch = (*env)->GetMethodID(env, clazz, "getPitch", "()F");
    if (J4A_ExceptionCheck__catchAll(env) || !get_pitch) goto done;
    get_fallback = (*env)->GetMethodID(env, clazz, "getAudioFallbackMode", "()I");
    if (J4A_ExceptionCheck__catchAll(env) || !get_fallback) goto done;
    if (!preserve_pitch) {
        expected_pitch = (*env)->CallFloatMethod(env, params, get_pitch);
        if (J4A_ExceptionCheck__catchAll(env)) goto done;
    }
    returned = J4AC_android_media_PlaybackParams__setSpeed(env, params, speed);
    if (J4A_ExceptionCheck__catchAll(env) || !returned) goto done;
    J4A_DeleteLocalRef__p(env, &returned);
    if (preserve_pitch) {
        returned = (*env)->CallObjectMethod(env, params, set_pitch, 1.0f);
        if (J4A_ExceptionCheck__catchAll(env) || !returned) goto done;
        J4A_DeleteLocalRef__p(env, &returned);
    }
    /* AUDIO_FALLBACK_MODE_FAIL: an unsupported request must not become silence. */
    returned = (*env)->CallObjectMethod(env, params, set_fallback, 2);
    if (J4A_ExceptionCheck__catchAll(env) || !returned) goto done;
    J4A_DeleteLocalRef__p(env, &returned);
    J4AC_android_media_AudioTrack__setPlaybackParams(env, thiz, params);
    if (J4A_ExceptionCheck__catchAll(env)) goto done;
    observed = J4AC_android_media_AudioTrack__getPlaybackParams(env, thiz);
    if (J4A_ExceptionCheck__catchAll(env) || !observed) goto done;
    *actual_speed = (*env)->CallFloatMethod(env, observed, get_speed);
    if (J4A_ExceptionCheck__catchAll(env)) goto done;
    *actual_pitch = (*env)->CallFloatMethod(env, observed, get_pitch);
    if (J4A_ExceptionCheck__catchAll(env)) goto done;
    jint fallback = (*env)->CallIntMethod(env, observed, get_fallback);
    if (J4A_ExceptionCheck__catchAll(env) || fallback != 2) goto done;
    if (!isfinite(*actual_speed) || !isfinite(*actual_pitch) || *actual_speed <= 0 || *actual_pitch <= 0 ||
        fabsf(*actual_speed - speed) > 0.0001f || fabsf(*actual_pitch - expected_pitch) > 0.0001f)
        goto done;
    result = 0;
done:
    J4A_DeleteLocalRef__p(env, &returned);
    J4A_DeleteLocalRef__p(env, &observed);
    J4A_DeleteLocalRef__p(env, &params);
    if (clazz) (*env)->DeleteLocalRef(env, clazz);
    return result;
}
