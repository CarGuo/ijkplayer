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

#ifndef J4AU__android_media_AudioTrack__UTIL__H
#define J4AU__android_media_AudioTrack__UTIL__H

#include "j4a/j4a_base.h"
#include "j4a/class/android/media/AudioTrack.h"

/* API23+; 0 applied, 1 rejected/verified rollback, -1 unverified device state.
 * Output values are actual read-back values, never requested echoes. */
int J4AC_android_media_AudioTrack__setParamsChecked(JNIEnv *env, jobject thiz,
    jfloat speed, int preserve_pitch, jfloat *actual_speed, jfloat *actual_pitch);
int J4AC_android_media_AudioTrack__setLegacyRateChecked(JNIEnv *env, jobject thiz,
    int sample_rate, jfloat speed, int preserve_pitch, jfloat *actual_speed, jfloat *actual_pitch);
void J4AC_android_media_AudioTrack__setSpeed(JNIEnv *env, jobject thiz, jfloat speed);
void J4AC_android_media_AudioTrack__setSpeed__catchAll(JNIEnv *env, jobject thiz, jfloat speed);

#ifdef J4A_HAVE_SIMPLE__J4AC_android_media_AudioTrack
#define J4AC_AudioTrack__setSpeed           J4AC_android_media_AudioTrack__setSpeed
#define J4AC_AudioTrack__setSpeed__catchAll J4AC_android_media_AudioTrack__setSpeed__catchAll
#endif

#endif
