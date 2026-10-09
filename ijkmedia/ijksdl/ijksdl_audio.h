/*****************************************************************************
 * ijksdl_audio.h
 *****************************************************************************
 *
 * Copyright (c) 2013 Bilibili
 * copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
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

#ifndef IJKSDL__IJKSDL_AUDIO_H
#define IJKSDL__IJKSDL_AUDIO_H

#include "ijksdl_stdinc.h"
#include "ijksdl_endian.h"

typedef uint16_t SDL_AudioFormat;

#define SDL_AUDIO_MASK_BITSIZE       (0xFF)
#define SDL_AUDIO_MASK_DATATYPE      (1<<8)
#define SDL_AUDIO_MASK_ENDIAN        (1<<12)
#define SDL_AUDIO_MASK_SIGNED        (1<<15)
#define SDL_AUDIO_BITSIZE(x)         (x & SDL_AUDIO_MASK_BITSIZE)
#define SDL_AUDIO_ISFLOAT(x)         (x & SDL_AUDIO_MASK_DATATYPE)
#define SDL_AUDIO_ISBIGENDIAN(x)     (x & SDL_AUDIO_MASK_ENDIAN)
#define SDL_AUDIO_ISSIGNED(x)        (x & SDL_AUDIO_MASK_SIGNED)
#define SDL_AUDIO_ISINT(x)           (!SDL_AUDIO_ISFLOAT(x))
#define SDL_AUDIO_ISLITTLEENDIAN(x)  (!SDL_AUDIO_ISBIGENDIAN(x))
#define SDL_AUDIO_ISUNSIGNED(x)      (!SDL_AUDIO_ISSIGNED(x))

#define AUDIO_INVALID   0x0000
#define AUDIO_U8        0x0008  /**< Unsigned 8-bit samples */
#define AUDIO_S8        0x8008  /**< Signed 8-bit samples */
#define AUDIO_U16LSB    0x0010  /**< Unsigned 16-bit samples */
#define AUDIO_S16LSB    0x8010  /**< Signed 16-bit samples */
#define AUDIO_U16MSB    0x1010  /**< As above, but big-endian byte order */
#define AUDIO_S16MSB    0x9010  /**< As above, but big-endian byte order */
#define AUDIO_U16       AUDIO_U16LSB
#define AUDIO_S16       AUDIO_S16LSB

#define AUDIO_S32LSB    0x8020  /**< 32-bit integer samples */
#define AUDIO_S32MSB    0x9020  /**< As above, but big-endian byte order */
#define AUDIO_S32       AUDIO_S32LSB

#define AUDIO_F32LSB    0x8120  /**< 32-bit floating point samples */
#define AUDIO_F32MSB    0x9120  /**< As above, but big-endian byte order */
#define AUDIO_F32       AUDIO_F32LSB

#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define AUDIO_U16SYS    AUDIO_U16LSB
#define AUDIO_S16SYS    AUDIO_S16LSB
#define AUDIO_S32SYS    AUDIO_S32LSB
#define AUDIO_F32SYS    AUDIO_F32LSB
#else
#define AUDIO_U16SYS    AUDIO_U16MSB
#define AUDIO_S16SYS    AUDIO_S16MSB
#define AUDIO_S32SYS    AUDIO_S32MSB
#define AUDIO_F32SYS    AUDIO_F32MSB
#endif

#define SDL_MIX_MAXVOLUME (128)

typedef void (*SDL_AudioCallback) (void *userdata, Uint8 * stream,
                                   int len);

/* Optional transactional submission, used only by backends that acknowledge
 * a complete output buffer. The backend treats token/generation as opaque. */
typedef struct SDL_AudioSubmission {
    uint64_t token;
    int generation;
    /* Optional homogeneous segment for a device-head clock. Backend fills the
     * output coordinates only after positive, whole-frame write acceptance. */
    int output_bytes, real_pcm, clock_unsupported;
    int processing_error;
    int engine_switch; /* 0 PCM, 1 platform, 2 SoundTouch; drain before commit */
    uint64_t engine_sequence;
    float engine_rate;
    double pts_start, pts_end;
    double estimate_start, estimate_end; /* approximate compatibility clock */
    double end_media; /* acknowledged cumulative output-media progress */
    double media_rate; /* software media seconds per output second, 1 if plain */
    uint64_t output_epoch, first_frame, end_frame;
} SDL_AudioSubmission;

enum {
    SDL_AUDIO_SUBMIT_CHECK,
    SDL_AUDIO_SUBMIT_COMMIT,
    SDL_AUDIO_SUBMIT_CANCEL,
    SDL_AUDIO_SUBMIT_FAIL,
    SDL_AUDIO_SUBMIT_DEVICE_FAIL,
    SDL_AUDIO_SUBMIT_ENGINE_COMMIT,
    SDL_AUDIO_SUBMIT_ENGINE_REJECT
};
enum {
    SDL_AUDIO_SUBMIT_READY,
    SDL_AUDIO_SUBMIT_WAIT,
    SDL_AUDIO_SUBMIT_STALE,
    SDL_AUDIO_SUBMIT_ABORT,
    SDL_AUDIO_SUBMIT_FAILED,
    SDL_AUDIO_SUBMIT_DRAINED,
    SDL_AUDIO_SUBMIT_SUPERSEDED /* engine control cancelled before commit */
};
typedef void (*SDL_AudioSubmitCallback)(void *userdata, Uint8 *stream, int len,
                                      SDL_AudioSubmission *submission);
typedef int (*SDL_AudioSubmissionCallback)(void *userdata,
                                         const SDL_AudioSubmission *submission,
                                         int action, int error);

/* Successful platform control result, separate from requested parameters.
 * Monotonic seconds bracket JNI application/read-back, not a hardware instant. */
typedef struct SDL_AudioRateApplied {
    uint64_t sequence, output_epoch;
    int generation, preserve_pitch;
    int rejected; /* request rejected, previous tuple positively restored */
    float speed, pitch;
    double apply_before, apply_after, observed_at;
    uint32_t head_before, head_after;
} SDL_AudioRateApplied;
typedef int (*SDL_AudioRateAppliedCallback)(void *userdata,
                                            const SDL_AudioRateApplied *applied);

typedef struct SDL_AudioSpec
{
    int freq;                   /**< DSP frequency -- samples per second */
    SDL_AudioFormat format;     /**< Audio data format */
    Uint8 channels;             /**< Number of channels: 1 mono, 2 stereo */
    Uint8 silence;              /**< Audio buffer silence value (calculated) */
    Uint16 samples;             /**< Audio buffer size in samples (power of 2) */
    Uint16 padding;             /**< NOT USED. Necessary for some compile environments */
    Uint32 size;                /**< Audio buffer size in bytes (calculated) */
    SDL_AudioCallback callback;
    void *userdata;
    SDL_AudioSubmitCallback callback_submit;
    SDL_AudioSubmissionCallback submission;
    SDL_AudioRateAppliedCallback rate_applied;
    int software_tempo; /* backend-owned active engine policy after open */
} SDL_AudioSpec;

void SDL_CalculateAudioSpec(SDL_AudioSpec * spec);

void SDL_MixAudio(Uint8*       dst,
                  const Uint8* src,
                  Uint32       len,
                  int          volume);

#endif
