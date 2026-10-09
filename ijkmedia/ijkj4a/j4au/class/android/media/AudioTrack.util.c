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
#include <limits.h>
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

/* A rejected tuple is recoverable only after a checked rollback of the entire
 * old tuple. Exceptions/read-back failures never imply that nothing changed.
 * Return 0 = applied, 1 = rejected and restored, -1 = unverified device state. */
int J4AC_android_media_AudioTrack__setParamsChecked(JNIEnv *env, jobject thiz,
    jfloat speed, int preserve_pitch, jfloat *actual_speed, jfloat *actual_pitch)
{
    jobject params = NULL, returned = NULL, observed = NULL;
    jclass clazz = NULL;
    jmethodID set_pitch, set_fallback, get_speed, get_pitch, get_fallback;
    int result = -1, rollback = 0;
    jfloat old_speed = 0, old_pitch = 0, target_speed, target_pitch;
    jint old_fallback = 0, target_fallback;
    if (J4A_GetSystemAndroidApiLevel(env) < 23 || !isfinite(speed) || speed <= 0)
        return -1;
    params = J4AC_android_media_AudioTrack__getPlaybackParams(env, thiz);
    if (J4A_ExceptionCheck__catchAll(env) || !params) goto done;
    clazz = (*env)->GetObjectClass(env, params);
    if (J4A_ExceptionCheck__catchAll(env) || !clazz) goto done;
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
    old_speed = (*env)->CallFloatMethod(env, params, get_speed);
    if (J4A_ExceptionCheck__catchAll(env)) goto done;
    old_pitch = (*env)->CallFloatMethod(env, params, get_pitch);
    if (J4A_ExceptionCheck__catchAll(env)) goto done;
    old_fallback = (*env)->CallIntMethod(env, params, get_fallback);
    if (J4A_ExceptionCheck__catchAll(env) || !isfinite(old_speed) || old_speed <= 0 ||
        !isfinite(old_pitch) || old_pitch <= 0) goto done;
    target_speed = speed;
    target_pitch = preserve_pitch ? 1.0f : old_pitch;
    target_fallback = 2; /* AUDIO_FALLBACK_MODE_FAIL, never silent mute. */
apply:
    returned = J4AC_android_media_PlaybackParams__setSpeed(env, params, target_speed);
    if (J4A_ExceptionCheck__catchAll(env) || !returned) goto failed;
    J4A_DeleteLocalRef__p(env, &returned);
    returned = (*env)->CallObjectMethod(env, params, set_pitch, target_pitch);
    if (J4A_ExceptionCheck__catchAll(env) || !returned) goto failed;
    J4A_DeleteLocalRef__p(env, &returned);
    returned = (*env)->CallObjectMethod(env, params, set_fallback, target_fallback);
    if (J4A_ExceptionCheck__catchAll(env) || !returned) goto failed;
    J4A_DeleteLocalRef__p(env, &returned);
    J4AC_android_media_AudioTrack__setPlaybackParams(env, thiz, params);
    if (J4A_ExceptionCheck__catchAll(env)) goto failed;
    observed = J4AC_android_media_AudioTrack__getPlaybackParams(env, thiz);
    if (J4A_ExceptionCheck__catchAll(env) || !observed) goto failed;
    *actual_speed = (*env)->CallFloatMethod(env, observed, get_speed);
    if (J4A_ExceptionCheck__catchAll(env)) goto failed;
    *actual_pitch = (*env)->CallFloatMethod(env, observed, get_pitch);
    if (J4A_ExceptionCheck__catchAll(env)) goto failed;
    jint fallback = (*env)->CallIntMethod(env, observed, get_fallback);
    if (J4A_ExceptionCheck__catchAll(env) || fallback != target_fallback ||
        !isfinite(*actual_speed) || !isfinite(*actual_pitch) ||
        fabsf(*actual_speed - target_speed) > 0.0001f ||
        fabsf(*actual_pitch - target_pitch) > 0.0001f) goto failed;
    result = rollback;
    goto done;
failed:
    if (rollback) goto done;
    rollback = 1;
    J4A_DeleteLocalRef__p(env, &returned);
    J4A_DeleteLocalRef__p(env, &observed);
    target_speed = old_speed;
    target_pitch = old_pitch;
    target_fallback = old_fallback;
    goto apply;
done:
    J4A_DeleteLocalRef__p(env, &returned);
    J4A_DeleteLocalRef__p(env, &observed);
    J4A_DeleteLocalRef__p(env, &params);
    if (clazz) (*env)->DeleteLocalRef(env, clazz);
    return result;
}

/* API16-22 native sample-rate control. The denominator is the negotiated PCM
 * sample rate, never getSampleRate after a previous rate mutation. */
int J4AC_android_media_AudioTrack__setLegacyRateChecked(JNIEnv *env, jobject thiz,
    int sample_rate, jfloat speed, int preserve_pitch, jfloat *actual_speed, jfloat *actual_pitch)
{
    jclass clazz = NULL;
    jmethodID set_rate, get_rate;
    int result = -1, rollback = 0;
    jint old_rate, wanted, observed;
    if (sample_rate <= 0 || !isfinite(speed) || speed <= 0) return -1;
    clazz = (*env)->GetObjectClass(env, thiz);
    if (J4A_ExceptionCheck__catchAll(env) || !clazz) goto done;
    set_rate = (*env)->GetMethodID(env, clazz, "setPlaybackRate", "(I)I");
    if (J4A_ExceptionCheck__catchAll(env) || !set_rate) goto done;
    get_rate = (*env)->GetMethodID(env, clazz, "getPlaybackRate", "()I");
    if (J4A_ExceptionCheck__catchAll(env) || !get_rate) goto done;
    old_rate = (*env)->CallIntMethod(env, thiz, get_rate);
    if (J4A_ExceptionCheck__catchAll(env) || old_rate <= 0) goto done;
    double requested = (double)sample_rate * speed;
    if (requested < 1 || requested > INT_MAX || (preserve_pitch && speed != 1.0f)) {
        *actual_speed = *actual_pitch = (float)old_rate / sample_rate;
        result = 1;
        goto done;
    }
    wanted = (jint)llround(requested);
apply:
    observed = (*env)->CallIntMethod(env, thiz, set_rate, wanted);
    if (J4A_ExceptionCheck__catchAll(env) || observed != 0) goto failed;
    observed = (*env)->CallIntMethod(env, thiz, get_rate);
    if (J4A_ExceptionCheck__catchAll(env) || observed != wanted) goto failed;
    *actual_speed = *actual_pitch = (float)observed / sample_rate;
    result = rollback;
    goto done;
failed:
    if (rollback) goto done;
    rollback = 1;
    wanted = old_rate;
    goto apply;
done:
    if (clazz) (*env)->DeleteLocalRef(env, clazz);
    return result;
}
