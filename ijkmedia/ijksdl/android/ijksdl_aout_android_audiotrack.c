/*****************************************************************************
 * ijksdl_aout_android_audiotrack.c
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

#include "ijksdl_aout_android_audiotrack.h"

#include <stdbool.h>
#include <assert.h>
#include <jni.h>
#include <math.h>
#include <inttypes.h>
#include "libavutil/time.h"
#include "../ijksdl_inc_internal.h"
#include "../ijksdl_thread.h"
#include "../ijksdl_aout_internal.h"
#include "ijksdl_android_jni.h"
#include "android_audiotrack.h"

#ifdef SDLTRACE
#undef SDLTRACE
#define SDLTRACE(...)
//#define SDLTRACE ALOGE
#endif

static SDL_Class g_audiotrack_class = {
    .name = "AudioTrack",
};

typedef struct SDL_Aout_Opaque {
    SDL_cond *wakeup_cond;
    SDL_mutex *wakeup_mutex;
    /* Never held across a blocking write or a player callback. */
    SDL_mutex *position_mutex;
    SDL_AudioPlaybackTracker playback;
    SDL_AudioPlaybackSpan *playback_spans;
    uint64_t output_epoch;
    int frame_size, output_sample_rate;

    SDL_AudioSpec spec;
    SDL_Android_AudioTrack* atrack;
    uint8_t *buffer;
    int buffer_size;

    volatile bool need_flush;
    volatile bool pause_on;
    volatile bool abort_request;

    volatile bool need_set_volume;
    volatile float left_volume;
    volatile float right_volume;

    SDL_Thread *audio_tid;
    SDL_Thread _audio_tid;

    int audio_session_id;

    volatile float speed;
    volatile bool speed_changed;
    /* Logical user intent is distinct from temporary handoff device controls.
     * Both requested tuples are wakeup-mutex protected; applied values come
     * only from checked JNI. */
    float logical_platform_rate;
    int logical_preserve_pitch;
    uint64_t requested_rate_sequence;
    float requested_platform_rate;
    int requested_preserve_pitch;
    SDL_AudioRateApplied applied_rate;
    uint64_t notified_rate_sequence, notified_rate_epoch;
    int notified_rate_generation;
    int engine_wakeup;

} SDL_Aout_Opaque;

/* This mutex also serializes head reads with track reset/release. */
static int aout_get_playback_position(SDL_Aout *aout, SDL_AudioPlaybackSnapshot *snapshot)
{
    SDL_Aout_Opaque *opaque = aout->opaque;
    JNIEnv *env = NULL;
    int ok = SDL_JNI_SetupThreadEnv(&env) == JNI_OK;
    SDL_LockMutex(opaque->position_mutex);
    if (ok && opaque->atrack && opaque->playback_spans) {
        uint32_t raw;
        if (SDL_Android_AudioTrack_get_playback_head(env, opaque->atrack, &raw) == 0)
            SDL_AudioPlaybackTracker_Observe(&opaque->playback, raw);
        else
            opaque->playback.fresh = 0;
    } else {
        opaque->playback.fresh = 0;
    }
    SDL_AudioPlaybackTracker_GetSnapshot(&opaque->playback, snapshot);
    if (!opaque->atrack)
        snapshot->valid = 0;
    SDL_UnlockMutex(opaque->position_mutex);
    return 0;
}

static int aout_reset_playback(JNIEnv *env, SDL_Aout *aout, int generation,
                               int *track_paused)
{
    SDL_Aout_Opaque *opaque = aout->opaque;
    int result = -1;
    SDL_LockMutex(opaque->position_mutex);
    if (opaque->output_epoch != UINT64_MAX &&
        SDL_Android_AudioTrack_pause_checked(env, opaque->atrack) == 0) {
        *track_paused = 1;
        if (SDL_Android_AudioTrack_flush_checked(env, opaque->atrack) == 0) {
            ++opaque->output_epoch;
            result = SDL_AudioPlaybackTracker_Reset(&opaque->playback, generation,
                                                    opaque->output_epoch);
        }
    }
    SDL_UnlockMutex(opaque->position_mutex);
    return result == 0 ? 0 : -1;
}

static int aout_bind_generation(JNIEnv *env, SDL_Aout *aout, int generation,
                                int *track_paused)
{
    SDL_Aout_Opaque *opaque = aout->opaque;
    int reset = 0;
    SDL_LockMutex(opaque->position_mutex);
    if (opaque->playback.generation == -1 && opaque->playback.accepted_frames == 0)
        opaque->playback.generation = generation;
    else if (opaque->playback.generation != generation)
        reset = 1;
    SDL_UnlockMutex(opaque->position_mutex);
    return reset ? aout_reset_playback(env, aout, generation, track_paused) : 0;
}

static int aout_device_failed(SDL_Aout_Opaque *opaque,
                               const SDL_AudioSubmission *submission)
{
    if (opaque->spec.submission)
        opaque->spec.submission(opaque->spec.userdata, submission,
                                 SDL_AUDIO_SUBMIT_DEVICE_FAIL, -1);
    return -1;
}

static int aout_accept_prefix(SDL_Aout *aout, SDL_AudioSubmission *submission,
                              int offset, int written)
{
    SDL_Aout_Opaque *opaque = aout->opaque;
    if (written <= 0 || written % opaque->frame_size ||
        written > submission->output_bytes - offset)
        return -1;
    SDL_AudioPlaybackSpan whole = {
        .frame_count = submission->output_bytes / opaque->frame_size,
        .pts_start = submission->pts_start,
        .pts_end = submission->pts_end,
        .real_pcm = submission->real_pcm
    };
    double begin = SDL_AudioPlaybackTracker_Interpolate(&whole, offset / opaque->frame_size);
    double end = SDL_AudioPlaybackTracker_Interpolate(&whole, (offset + written) / opaque->frame_size);
    whole.pts_start = submission->estimate_start;
    whole.pts_end = submission->estimate_end;
    double estimated_begin = SDL_AudioPlaybackTracker_Interpolate(&whole, offset / opaque->frame_size);
    double estimated_end = SDL_AudioPlaybackTracker_Interpolate(&whole, (offset + written) / opaque->frame_size);
    SDL_LockMutex(opaque->position_mutex);
    if (offset == 0) {
        submission->output_epoch = opaque->playback.epoch;
        submission->first_frame = opaque->playback.accepted_frames;
    }
    double rate = submission->media_rate > 0 ? submission->media_rate : 1.0;
    int result = SDL_AudioPlaybackTracker_AcceptEstimated(&opaque->playback,
        written / opaque->frame_size, submission->real_pcm, begin, end,
        opaque->output_sample_rate > 0 ? rate / opaque->output_sample_rate : 0, rate,
        estimated_begin, estimated_end);
    submission->end_frame = opaque->playback.accepted_frames;
    submission->end_media = opaque->playback.accepted_media;
    SDL_UnlockMutex(opaque->position_mutex);
    return result == 0 ? 0 : -1;
}

/* Return 0 to continue with a fresh callback, -1 to terminate output. The
 * transaction covers real PCM only; silence tokens never keep EOF pending. */
static int aout_apply_platform_rate(JNIEnv *env, SDL_Aout *aout);
static int aout_apply_active_rate(JNIEnv *env, SDL_Aout *aout);
static int aout_request_platform_rate(SDL_Aout *aout, float speed, int preserve_pitch, int fields);
static void aout_notify_platform_rate(SDL_Aout *aout);

static int aout_write_submitted_buffer(JNIEnv *env, SDL_Aout *aout,
                                        uint8_t *buffer, int size,
                                        SDL_AudioSubmission *submission,
                                        int *track_paused)
{
    SDL_Aout_Opaque *opaque = aout->opaque;
    SDL_AudioSubmissionCallback notify = opaque->spec.submission;
    void *userdata = opaque->spec.userdata;
    int offset = 0, failure = 0, idle = 0;
    for (;;) {
        int abort_request, paused, flush;
        SDL_LockMutex(opaque->wakeup_mutex);
        abort_request = opaque->abort_request;
        paused = opaque->pause_on;
        flush = opaque->need_flush;
        opaque->need_flush = 0;
        SDL_UnlockMutex(opaque->wakeup_mutex);
        int status = notify(userdata, submission, SDL_AUDIO_SUBMIT_CHECK, 0);
        if (abort_request || status == SDL_AUDIO_SUBMIT_ABORT) {
            notify(userdata, submission, SDL_AUDIO_SUBMIT_CANCEL, 0);
            return -1;
        }
        if (status == SDL_AUDIO_SUBMIT_STALE) {
            if (aout_reset_playback(env, aout, -1, track_paused) < 0)
                return aout_device_failed(opaque, submission);
            notify(userdata, submission, SDL_AUDIO_SUBMIT_CANCEL, 0);
            return 0;
        }
        if (status == SDL_AUDIO_SUBMIT_FAILED) {
            notify(userdata, submission, SDL_AUDIO_SUBMIT_CANCEL, 0);
            return -1;
        }
        if (!submission->engine_switch && !submission->token && status == SDL_AUDIO_SUBMIT_DRAINED) {
            SDL_LockMutex(opaque->wakeup_mutex);
            int engine_wakeup = opaque->engine_wakeup;
            opaque->engine_wakeup = 0;
            SDL_UnlockMutex(opaque->wakeup_mutex);
            if (engine_wakeup) return 0; /* Let producer prepare a control-only boundary. */
        }
        if (submission->engine_switch) {
            if (status != SDL_AUDIO_SUBMIT_WAIT) {
                if (aout_bind_generation(env, aout, submission->generation, track_paused) < 0)
                    return aout_device_failed(opaque, submission);
                SDL_AudioPlaybackSnapshot position = { 0 };
                aout_get_playback_position(aout, &position);
                if (position.valid && position.fresh &&
                    position.generation == submission->generation &&
                    position.played_frames < position.accepted_frames) {
                    SDL_LockMutex(opaque->position_mutex);
                    int synthetic_only = !opaque->playback.has_real_pcm;
                    SDL_UnlockMutex(opaque->position_mutex);
                    /* The sole writer may retire an exclusively synthetic
                     * physical epoch. A short startup-silence prefix cannot
                     * reach AudioTrack's minimum fill while this control waits
                     * for drain. Never flush an epoch that accepted real PCM,
                     * even silent/unknown-PTS PCM or already-reclaimed spans.
                     * Keep pause/flush checked and re-read the new epoch before
                     * commit; ordinary generation/seek validation still applies. */
                    if (synthetic_only) {
                        int reset_result = aout_reset_playback(env, aout, submission->generation, track_paused);
                        ALOGI("IJK_AUDIO_SYNTHETIC_EPOCH_RESET generation=%d epoch=%" PRIu64
                            " accepted=%" PRIu64 " played=%" PRIu64 " real=0 result=%d new_epoch=%" PRIu64,
                            submission->generation, position.epoch, position.accepted_frames,
                            position.played_frames, reset_result, opaque->output_epoch);
                        if (reset_result < 0)
                            return aout_device_failed(opaque, submission);
                        aout_get_playback_position(aout, &position);
                    }
                }
                if (position.valid && position.generation == submission->generation &&
                    position.played_frames == position.accepted_frames) {
                    float speed = submission->engine_switch == 2 ? 1.0f : submission->engine_rate;
                    if (aout_request_platform_rate(aout, speed, submission->engine_switch == 2,
                            SDL_AUDIO_RATE_SPEED | SDL_AUDIO_RATE_PITCH_POLICY | SDL_AUDIO_RATE_TRANSIENT) < 0 ||
                        aout_apply_platform_rate(env, aout) < 0)
                        return aout_device_failed(opaque, submission);
                    SDL_LockMutex(opaque->wakeup_mutex);
                    SDL_AudioRateApplied applied = opaque->applied_rate;
                    SDL_UnlockMutex(opaque->wakeup_mutex);
                    int verified_unit_rate = fabsf(applied.speed - 1.0f) < .0001f &&
                        fabsf(applied.pitch - 1.0f) < .0001f;
                    submission->output_epoch = position.epoch;
                    submission->end_frame = position.accepted_frames;
                    int action = submission->engine_switch == 2 && !verified_unit_rate ?
                        SDL_AUDIO_SUBMIT_ENGINE_REJECT : SDL_AUDIO_SUBMIT_ENGINE_COMMIT;
                    status = notify(userdata, submission, action, 0);
                    if (status == SDL_AUDIO_SUBMIT_READY || status == SDL_AUDIO_SUBMIT_SUPERSEDED) {
                        if (status == SDL_AUDIO_SUBMIT_READY && action == SDL_AUDIO_SUBMIT_ENGINE_COMMIT)
                            opaque->spec.software_tempo = submission->engine_switch == 2;
                        if (status == SDL_AUDIO_SUBMIT_SUPERSEDED && aout_apply_active_rate(env, aout) < 0)
                            return aout_device_failed(opaque, submission);
                        aout_notify_platform_rate(aout);
                        return 0;
                    }
                    continue; /* A concurrent seek retains normal generation handling. */
                }
            }
            /* No synthetic PCM while waiting: otherwise this drain could never
             * finish. Service pause/abort/seek without holding any lock asleep. */
            if (paused && !*track_paused) {
                if (SDL_Android_AudioTrack_pause_checked(env, opaque->atrack) < 0)
                    return aout_device_failed(opaque, submission);
                *track_paused = 1;
            } else if (!paused && *track_paused && status != SDL_AUDIO_SUBMIT_WAIT) {
                SDL_Android_AudioTrack_play(env, opaque->atrack);
                *track_paused = 0;
            }
            SDL_LockMutex(opaque->wakeup_mutex);
            if (!opaque->abort_request)
                SDL_CondWaitTimeout(opaque->wakeup_cond, opaque->wakeup_mutex, 10);
            SDL_UnlockMutex(opaque->wakeup_mutex);
            continue;
        }
        /* This loop also owns the audio-drained video tail and a partial PCM
         * suffix. Service controls without leaving or discarding either. */
        if ((opaque->spec.rate_applied || opaque->spec.software_tempo) && aout_apply_active_rate(env, aout) < 0) {
            SDL_AudioSubmission rejected = *submission;
            return aout_device_failed(opaque, &rejected);
        }
        aout_notify_platform_rate(aout);
        if (flush) {
            SDL_LockMutex(opaque->position_mutex);
            int changed_generation = opaque->playback.generation != submission->generation;
            int empty_epoch = opaque->playback.accepted_frames == 0;
            SDL_UnlockMutex(opaque->position_mutex);
            /* The generation binder performs a real seek reset. Duplicate
             * requests before any write are harmless. Never discard accepted
             * same-generation PCM at a clean callback boundary. */
            if (!changed_generation && !empty_epoch)
                failure = -1;
        }
        if (idle && status == SDL_AUDIO_SUBMIT_READY && !failure)
            return 0; /* New same-generation PCM became available after EOF. */
        if (status == SDL_AUDIO_SUBMIT_DRAINED)
            idle = 1;
        if (status != SDL_AUDIO_SUBMIT_WAIT && failure) {
            status = notify(userdata, submission, SDL_AUDIO_SUBMIT_FAIL, failure);
            if (status == SDL_AUDIO_SUBMIT_FAILED)
                return -1;
            if (status == SDL_AUDIO_SUBMIT_DRAINED) {
                failure = 0;
                idle = 1;
            } else {
                continue; /* A concurrent seek/abort is handled above. */
            }
        }
        if (!idle && status != SDL_AUDIO_SUBMIT_WAIT &&
            (submission->clock_unsupported || submission->processing_error || size <= 0 || size > 256 || size % opaque->frame_size))
            return aout_device_failed(opaque, submission);
        if (!idle && status != SDL_AUDIO_SUBMIT_WAIT && offset == size) {
            status = notify(userdata, submission, SDL_AUDIO_SUBMIT_COMMIT, 0);
            if (status == SDL_AUDIO_SUBMIT_READY)
                return 0;
            continue;
        }
        if (!idle && status != SDL_AUDIO_SUBMIT_WAIT &&
            aout_bind_generation(env, aout, submission->generation, track_paused) < 0)
            return aout_device_failed(opaque, submission);
        if (!paused && !idle && status != SDL_AUDIO_SUBMIT_WAIT && *track_paused) {
            SDL_Android_AudioTrack_play(env, opaque->atrack);
            *track_paused = 0;
        }
        if (idle || paused || status == SDL_AUDIO_SUBMIT_WAIT) {
            if (paused && !*track_paused) {
                if (SDL_Android_AudioTrack_pause_checked(env, opaque->atrack) < 0)
                    return aout_device_failed(opaque, submission);
                *track_paused = 1;
            }
            SDL_LockMutex(opaque->wakeup_mutex);
            if (!opaque->abort_request)
                SDL_CondWaitTimeout(opaque->wakeup_cond, opaque->wakeup_mutex, 10);
            SDL_UnlockMutex(opaque->wakeup_mutex);
            continue;
        }
        int written = SDL_Android_AudioTrack_write(env, opaque->atrack,
                                                  buffer + offset, size - offset);
        if (written > 0 && written <= size - offset) {
            if (aout_accept_prefix(aout, submission, offset, written) < 0)
                return aout_device_failed(opaque, submission);
            offset += written;
            SDL_AudioPlaybackSnapshot snapshot;
            aout_get_playback_position(aout, &snapshot);
            continue;
        }
        if (written == 0) {
            SDL_LockMutex(opaque->wakeup_mutex);
            paused = opaque->pause_on;
            SDL_UnlockMutex(opaque->wakeup_mutex);
            status = notify(userdata, submission, SDL_AUDIO_SUBMIT_CHECK, 0);
            if (paused || status == SDL_AUDIO_SUBMIT_WAIT)
                continue; /* A control interruption retains this exact suffix. */
        }
        failure = written ? written : -1;
    }
}

static int aout_supports_platform_rate(SDL_Aout *aout)
{
    JNIEnv *env = NULL;
    return aout && SDL_JNI_SetupThreadEnv(&env) == JNI_OK &&
        J4A_GetSystemAndroidApiLevel(env) >= 16;
}

static int aout_request_platform_rate(SDL_Aout *aout, float speed, int preserve_pitch, int fields)
{
    SDL_Aout_Opaque *opaque = aout->opaque;
    if ((fields & SDL_AUDIO_RATE_SPEED) && (!isfinite(speed) || speed <= 0))
        return -1;
    SDL_LockMutex(opaque->wakeup_mutex);
    int changed = 0;
    if (fields & SDL_AUDIO_RATE_ENGINE_WAKE) opaque->engine_wakeup = 1;
    if (!(fields & SDL_AUDIO_RATE_TRANSIENT)) {
        if (fields & SDL_AUDIO_RATE_SPEED) opaque->logical_platform_rate = speed;
        if (fields & SDL_AUDIO_RATE_PITCH_POLICY) opaque->logical_preserve_pitch = !!preserve_pitch;
    }
    if (!(fields & SDL_AUDIO_RATE_LOGICAL_ONLY) &&
        (fields & SDL_AUDIO_RATE_SPEED) && opaque->requested_platform_rate != speed) {
        opaque->requested_platform_rate = speed;
        changed = 1;
    }
    if (!(fields & SDL_AUDIO_RATE_LOGICAL_ONLY) &&
        (fields & SDL_AUDIO_RATE_PITCH_POLICY) && opaque->requested_preserve_pitch != !!preserve_pitch) {
        opaque->requested_preserve_pitch = !!preserve_pitch;
        changed = 1;
    }
    if (changed) ++opaque->requested_rate_sequence;
    SDL_CondSignal(opaque->wakeup_cond);
    SDL_UnlockMutex(opaque->wakeup_mutex);
    return 0;
}

/* Called by the sole AudioTrack owner, with no backend mutex held on entry. */
static int aout_apply_platform_rate(JNIEnv *env, SDL_Aout *aout)
{
    SDL_Aout_Opaque *opaque = aout->opaque;
    SDL_AudioRateApplied applied = { 0 };
    float requested;
    SDL_LockMutex(opaque->wakeup_mutex);
    applied.sequence = opaque->requested_rate_sequence;
    applied.preserve_pitch = opaque->requested_preserve_pitch;
    requested = opaque->requested_platform_rate;
    int previous_preserve_pitch = opaque->applied_rate.preserve_pitch;
    int pending = applied.sequence != opaque->applied_rate.sequence;
    SDL_UnlockMutex(opaque->wakeup_mutex);
    if (!pending) return 0;

    SDL_LockMutex(opaque->position_mutex);
    applied.apply_before = av_gettime_relative() / 1000000.0;
    int result = !opaque->atrack ? -1 :
        SDL_Android_AudioTrack_get_playback_head(env, opaque->atrack, &applied.head_before);
    if (result == 0)
        result = SDL_Android_AudioTrack_set_rate_checked(env, opaque->atrack, requested,
            applied.preserve_pitch, &applied.speed, &applied.pitch);
    applied.rejected = result > 0;
    if (result >= 0)
        result = SDL_Android_AudioTrack_get_playback_head(env, opaque->atrack, &applied.head_after);
    applied.apply_after = av_gettime_relative() / 1000000.0;
    applied.generation = opaque->playback.generation;
    applied.output_epoch = opaque->playback.epoch;
    SDL_UnlockMutex(opaque->position_mutex);
    if (result < 0) {
        ALOGE("IJK_PLATFORM_RATE_REJECTED seq=%" PRIu64 " requested=%.6f preserve=%d",
            applied.sequence, requested, applied.preserve_pitch);
        return -1;
    }
    if (applied.rejected) {
        applied.preserve_pitch = previous_preserve_pitch;
        ALOGW("IJK_PLATFORM_RATE_REJECTED_RESTORED seq=%" PRIu64 " requested=%.6f actual=%.6f pitch=%.6f",
            applied.sequence, requested, applied.speed, applied.pitch);
    }
    SDL_LockMutex(opaque->wakeup_mutex);
    opaque->applied_rate = applied;
    /* Do not overwrite requested fields: a newer request remains pending. */
    SDL_UnlockMutex(opaque->wakeup_mutex);
    ALOGI("IJK_PLATFORM_RATE_APPLIED seq=%" PRIu64 " requested=%.6f actual=%.6f pitch=%.6f preserve=%d generation=%d epoch=%" PRIu64 " before=%.6f after=%.6f head_before=%u head_after=%u",
        applied.sequence, requested, applied.speed, applied.pitch, applied.preserve_pitch,
        applied.generation, applied.output_epoch, applied.apply_before, applied.apply_after,
        applied.head_before, applied.head_after);
    return 0;
}

/* Only the owner changes this engine policy, after a successful player commit.
 * A seek can invalidate an engine-control callback after JNI has changed the
 * device. Restore the last committed policy before any further PCM is produced
 * or written, including exact partial-write suffixes. */
static int aout_apply_active_rate(JNIEnv *env, SDL_Aout *aout)
{
    SDL_Aout_Opaque *opaque = aout->opaque;
    /* Read intent and publish its physical request in the same critical
     * section, so a concurrent newer setter cannot be overwritten. Temporary
     * handoff controls never replace logical intent, including when a seek or
     * option cancellation makes the handoff stale after JNI changed hardware. */
    SDL_LockMutex(opaque->wakeup_mutex);
    float speed = opaque->spec.software_tempo ? 1.0f : opaque->logical_platform_rate;
    int preserve = opaque->spec.software_tempo ? 1 : opaque->logical_preserve_pitch;
    if (opaque->requested_platform_rate != speed || opaque->requested_preserve_pitch != preserve) {
        opaque->requested_platform_rate = speed;
        opaque->requested_preserve_pitch = preserve;
        ++opaque->requested_rate_sequence;
    }
    SDL_UnlockMutex(opaque->wakeup_mutex);
    if (aout_apply_platform_rate(env, aout) < 0) return -1;
    SDL_LockMutex(opaque->wakeup_mutex);
    int valid = !opaque->spec.software_tempo ||
        (fabsf(opaque->applied_rate.speed - 1.0f) < .0001f &&
         fabsf(opaque->applied_rate.pitch - 1.0f) < .0001f);
    SDL_UnlockMutex(opaque->wakeup_mutex);
    return valid ? 0 : -1;
}

/* Rebind the actual applied rate after a seek's first acknowledged PCM too. */
static void aout_notify_platform_rate(SDL_Aout *aout)
{
    SDL_Aout_Opaque *opaque = aout->opaque;
    if (!opaque->spec.rate_applied) return;
    SDL_AudioRateApplied applied;
    SDL_LockMutex(opaque->wakeup_mutex);
    applied = opaque->applied_rate;
    SDL_UnlockMutex(opaque->wakeup_mutex);
    SDL_LockMutex(opaque->position_mutex);
    applied.generation = opaque->playback.generation;
    applied.output_epoch = opaque->playback.epoch;
    applied.observed_at = av_gettime_relative() / 1000000.0;
    SDL_UnlockMutex(opaque->position_mutex);
    if (!applied.sequence || applied.generation < 0 ||
        (opaque->notified_rate_sequence == applied.sequence &&
         opaque->notified_rate_generation == applied.generation &&
         opaque->notified_rate_epoch == applied.output_epoch)) return;
    /* Player callback happens only after all backend locks are released.
     * A seek may be committing; retry binding until the player accepts it. */
    if (opaque->spec.rate_applied(opaque->spec.userdata, &applied) > 0) {
        opaque->notified_rate_sequence = applied.sequence;
        opaque->notified_rate_generation = applied.generation;
        opaque->notified_rate_epoch = applied.output_epoch;
    }
}

static int aout_thread_n(JNIEnv *env, SDL_Aout *aout)
{
    SDL_Aout_Opaque *opaque = aout->opaque;
    SDL_Android_AudioTrack *atrack = opaque->atrack;
    SDL_AudioCallback audio_cblk = opaque->spec.callback;
    void *userdata = opaque->spec.userdata;
    uint8_t *buffer = opaque->buffer;
    int copy_size = 256 - 256 % opaque->frame_size;
    /* The output thread owns the last commanded track state across callbacks.
     * A stale submission can return while paused; a concurrent resume must not
     * lose that state merely because the outer pause branch is skipped. */
    int track_paused = 1;
    int transactional = opaque->spec.callback_submit && opaque->spec.submission;

    assert(atrack);
    assert(buffer);

    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);

    if (!transactional && !opaque->abort_request && !opaque->pause_on) {
        SDL_Android_AudioTrack_play(env, atrack);
        track_paused = 0;
    }

    while (!opaque->abort_request) {
        SDL_LockMutex(opaque->wakeup_mutex);
        if (!opaque->abort_request && opaque->pause_on) {
            if (SDL_Android_AudioTrack_pause_checked(env, atrack) < 0) {
                SDL_UnlockMutex(opaque->wakeup_mutex);
                SDL_AudioSubmission failure = { 0 };
                aout_device_failed(opaque, &failure);
                break;
            }
            track_paused = 1;
            while (!opaque->abort_request && opaque->pause_on) {
                SDL_CondWaitTimeout(opaque->wakeup_cond, opaque->wakeup_mutex, 1000);
            }
            if (!opaque->abort_request && !opaque->pause_on) {
                if (!transactional && opaque->need_flush) {
                    opaque->need_flush = 0;
                    if (aout_reset_playback(env, aout, -1, &track_paused) < 0) {
                        SDL_UnlockMutex(opaque->wakeup_mutex);
                        SDL_AudioSubmission failure = { 0 };
                        aout_device_failed(opaque, &failure);
                        break;
                    }
                }
                if (!transactional) {
                    SDL_Android_AudioTrack_play(env, atrack);
                    track_paused = 0;
                }
            }
        }
        if (!transactional && opaque->need_flush) {
            opaque->need_flush = 0;
            if (aout_reset_playback(env, aout, -1, &track_paused) < 0) {
                SDL_UnlockMutex(opaque->wakeup_mutex);
                SDL_AudioSubmission failure = { 0 };
                aout_device_failed(opaque, &failure);
                break;
            }
        }
        if (opaque->need_set_volume) {
            opaque->need_set_volume = 0;
            SDL_Android_AudioTrack_set_volume(env, atrack, opaque->left_volume, opaque->right_volume);
        }
        if (opaque->speed_changed) {
            opaque->speed_changed = 0;
            SDL_Android_AudioTrack_setSpeed(env, atrack, opaque->speed);
        }
        if (!transactional && !opaque->abort_request && !opaque->pause_on && track_paused) {
            SDL_Android_AudioTrack_play(env, atrack);
            track_paused = 0;
        }
        SDL_UnlockMutex(opaque->wakeup_mutex);

        if ((opaque->spec.rate_applied || opaque->spec.software_tempo) && aout_apply_active_rate(env, aout) < 0) {
            SDL_AudioSubmission failure = { 0 };
            aout_device_failed(opaque, &failure);
            break;
        }
        aout_notify_platform_rate(aout);
        if (opaque->spec.callback_submit && opaque->spec.submission) {
            SDL_AudioSubmission submission = { 0 };
            opaque->spec.callback_submit(userdata, buffer, copy_size, &submission);
            if (aout_write_submitted_buffer(env, aout, buffer, submission.output_bytes, &submission,
                                            &track_paused) < 0)
                break;
            aout_notify_platform_rate(aout);
            continue;
        }
        audio_cblk(userdata, buffer, copy_size);
        if (opaque->need_flush) {
            SDL_Android_AudioTrack_flush(env, atrack);
            opaque->need_flush = false;
        }

        if (opaque->need_flush) {
            opaque->need_flush = 0;
            SDL_Android_AudioTrack_flush(env, atrack);
        } else {
            int written = SDL_Android_AudioTrack_write(env, atrack, buffer, copy_size);
            if (written != copy_size) {
                ALOGW("AudioTrack: not all data copied %d/%d", (int)written, (int)copy_size);
            }
        }

        // TODO: 1 if callback return -1 or 0
    }

    SDL_LockMutex(opaque->position_mutex);
    opaque->atrack = NULL;
    SDL_Android_AudioTrack_free(env, atrack);
    SDL_UnlockMutex(opaque->position_mutex);
    return 0;
}

static int aout_thread(void *arg)
{
    SDL_Aout *aout = arg;
    // SDL_Aout_Opaque *opaque = aout->opaque;
    JNIEnv *env = NULL;

    if (JNI_OK != SDL_JNI_SetupThreadEnv(&env)) {
        ALOGE("aout_thread: SDL_AndroidJni_SetupEnv: failed");
        return -1;
    }

    return aout_thread_n(env, aout);
}

static int aout_open_audio_n(JNIEnv *env, SDL_Aout *aout, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    assert(desired);
    SDL_Aout_Opaque *opaque = aout->opaque;

    SDL_LockMutex(opaque->position_mutex);
    opaque->spec = *desired;
    opaque->atrack = SDL_Android_AudioTrack_new_from_sdl_spec(env, desired);
    if (!opaque->atrack) {
        ALOGE("aout_open_audio_n: failed to new AudioTrcak()");
        SDL_UnlockMutex(opaque->position_mutex);
        return -1;
    }

    /* A new AudioTrack needs fresh read-back; a seek flush does not come here. */
    memset(&opaque->applied_rate, 0, sizeof(opaque->applied_rate));
    opaque->notified_rate_sequence = opaque->notified_rate_epoch = 0;
    opaque->notified_rate_generation = -1;
    opaque->buffer_size = SDL_Android_AudioTrack_get_min_buffer_size(opaque->atrack);
    if (opaque->buffer_size <= 0) {
        ALOGE("aout_open_audio_n: failed to getMinBufferSize()");
        SDL_Android_AudioTrack_free(env, opaque->atrack);
        opaque->atrack = NULL;
        SDL_UnlockMutex(opaque->position_mutex);
        return -1;
    }

    free(opaque->buffer);
    opaque->buffer = malloc(opaque->buffer_size);
    if (!opaque->buffer) {
        ALOGE("aout_open_audio_n: failed to allocate buffer");
        SDL_Android_AudioTrack_free(env, opaque->atrack);
        opaque->atrack = NULL;
        SDL_UnlockMutex(opaque->position_mutex);
        return -1;
    }

    if (obtained) {
        SDL_Android_AudioTrack_get_target_spec(opaque->atrack, obtained);
        SDLTRACE("audio target format fmt:0x%x, channel:0x%x", (int)obtained->format, (int)obtained->channels);
    }

    SDL_AudioSpec target = { 0 };
    SDL_Android_AudioTrack_get_target_spec(opaque->atrack, &target);
    opaque->output_sample_rate = target.freq;
    opaque->frame_size = target.channels * (SDL_AUDIO_BITSIZE(target.format) / 8);
    if (opaque->frame_size <= 0 || opaque->frame_size > 256 || opaque->output_epoch == UINT64_MAX)
        goto fail_playback;
    size_t capacity = opaque->buffer_size / opaque->frame_size + 256 / opaque->frame_size + 2;
    /* Worst case is one span per output frame. Refuse excessive accounting
     * storage rather than silently discard queued media mappings. */
    if (capacity > 65536)
        goto fail_playback;
    free(opaque->playback_spans);
    opaque->playback_spans = calloc(capacity, sizeof(*opaque->playback_spans));
    if (!opaque->playback_spans ||
        SDL_AudioPlaybackTracker_Init(&opaque->playback, opaque->playback_spans, capacity) != 0 ||
        SDL_AudioPlaybackTracker_Reset(&opaque->playback, -1, ++opaque->output_epoch) != 0)
        goto fail_playback;

    opaque->audio_session_id = SDL_Android_AudioTrack_getAudioSessionId(env, opaque->atrack);
    ALOGI("audio_session_id = %d\n", opaque->audio_session_id);

    opaque->pause_on = 1;
    opaque->abort_request = 0;
    opaque->audio_tid = SDL_CreateThreadEx(&opaque->_audio_tid, aout_thread, aout, "ff_aout_android");
    if (!opaque->audio_tid) {
        ALOGE("aout_open_audio_n: failed to create audio thread");
        SDL_Android_AudioTrack_free(env, opaque->atrack);
        opaque->atrack = NULL;
        SDL_UnlockMutex(opaque->position_mutex);
        return -1;
    }

    SDL_UnlockMutex(opaque->position_mutex);

    return 0;

fail_playback:
    SDL_Android_AudioTrack_free(env, opaque->atrack);
    opaque->atrack = NULL;
    free(opaque->playback_spans);
    opaque->playback_spans = NULL;
    SDL_UnlockMutex(opaque->position_mutex);
    return -1;
}

static int aout_open_audio(SDL_Aout *aout, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    // SDL_Aout_Opaque *opaque = aout->opaque;
    JNIEnv *env = NULL;
    if (JNI_OK != SDL_JNI_SetupThreadEnv(&env)) {
        ALOGE("aout_open_audio: AttachCurrentThread: failed");
        return -1;
    }

    return aout_open_audio_n(env, aout, desired, obtained);
}

static void aout_pause_audio(SDL_Aout *aout, int pause_on)
{
    SDL_Aout_Opaque *opaque = aout->opaque;

    SDL_LockMutex(opaque->wakeup_mutex);
    SDLTRACE("aout_pause_audio(%d)", pause_on);
    opaque->pause_on = pause_on;
    if (!pause_on)
        SDL_CondSignal(opaque->wakeup_cond);
    SDL_UnlockMutex(opaque->wakeup_mutex);
}

static void aout_flush_audio(SDL_Aout *aout)
{
    SDL_Aout_Opaque *opaque = aout->opaque;
    SDL_LockMutex(opaque->wakeup_mutex);
    SDLTRACE("aout_flush_audio()");
    opaque->need_flush = 1;
    SDL_CondSignal(opaque->wakeup_cond);
    SDL_UnlockMutex(opaque->wakeup_mutex);
}

static void aout_set_volume(SDL_Aout *aout, float left_volume, float right_volume)
{
    SDL_Aout_Opaque *opaque = aout->opaque;
    SDL_LockMutex(opaque->wakeup_mutex);
    SDLTRACE("aout_flush_audio()");
    opaque->left_volume = left_volume;
    opaque->right_volume = right_volume;
    opaque->need_set_volume = 1;
    SDL_CondSignal(opaque->wakeup_cond);
    SDL_UnlockMutex(opaque->wakeup_mutex);
}

static void aout_close_audio(SDL_Aout *aout)
{
    SDL_Aout_Opaque *opaque = aout->opaque;

    SDL_LockMutex(opaque->wakeup_mutex);
    opaque->abort_request = true;
    SDL_CondSignal(opaque->wakeup_cond);
    SDL_UnlockMutex(opaque->wakeup_mutex);

    SDL_WaitThread(opaque->audio_tid, NULL);

    opaque->audio_tid = NULL;
}

static int aout_get_audio_session_id(SDL_Aout *aout)
{
    SDL_Aout_Opaque *opaque = aout->opaque;

    return opaque->audio_session_id;
}

static void aout_free_l(SDL_Aout *aout)
{
    if (!aout)
        return;

    aout_close_audio(aout);

    SDL_Aout_Opaque *opaque = aout->opaque;
    if (opaque) {
        free(opaque->buffer);
        opaque->buffer = NULL;
        opaque->buffer_size = 0;

        free(opaque->playback_spans);
        opaque->playback_spans = NULL;
        SDL_DestroyMutex(opaque->position_mutex);
        SDL_DestroyCond(opaque->wakeup_cond);
        SDL_DestroyMutex(opaque->wakeup_mutex);
    }

    SDL_Aout_FreeInternal(aout);
}

static void func_set_playback_rate(SDL_Aout *aout, float speed)
{
    if (!aout)
        return;

    SDL_Aout_Opaque *opaque = aout->opaque;
    SDL_LockMutex(opaque->wakeup_mutex);
    SDLTRACE("%s %f", __func__, (double)speed);
    opaque->speed = speed;
    opaque->speed_changed = 1;
    SDL_CondSignal(opaque->wakeup_cond);
    SDL_UnlockMutex(opaque->wakeup_mutex);
}

SDL_Aout *SDL_AoutAndroid_CreateForAudioTrack()
{
    SDL_Aout *aout = SDL_Aout_CreateInternal(sizeof(SDL_Aout_Opaque));
    if (!aout)
        return NULL;

    SDL_Aout_Opaque *opaque = aout->opaque;
    opaque->wakeup_cond  = SDL_CreateCond();
    opaque->wakeup_mutex = SDL_CreateMutex();
    opaque->position_mutex = SDL_CreateMutex();
    if (!opaque->wakeup_cond || !opaque->wakeup_mutex || !opaque->position_mutex) {
        SDL_DestroyCond(opaque->wakeup_cond);
        SDL_DestroyMutex(opaque->wakeup_mutex);
        SDL_DestroyMutex(opaque->position_mutex);
        SDL_Aout_FreeInternal(aout);
        return NULL;
    }
    opaque->speed        = 1.0f;

    aout->opaque_class = &g_audiotrack_class;
    aout->free_l       = aout_free_l;
    aout->open_audio   = aout_open_audio;
    aout->pause_audio  = aout_pause_audio;
    aout->flush_audio  = aout_flush_audio;
    aout->set_volume   = aout_set_volume;
    aout->close_audio  = aout_close_audio;
    aout->func_get_playback_position = aout_get_playback_position;
    aout->func_get_audio_session_id = aout_get_audio_session_id;
    aout->func_set_playback_rate    = func_set_playback_rate;
    aout->func_supports_platform_rate = aout_supports_platform_rate;
    aout->func_request_platform_rate = aout_request_platform_rate;
    opaque->logical_platform_rate = 1.0f;
    opaque->requested_platform_rate = 1.0f;
    opaque->requested_rate_sequence = 1;
    opaque->notified_rate_generation = -1;

    return aout;
}

bool SDL_AoutAndroid_IsObjectOfAudioTrack(SDL_Aout *aout)
{
    if (aout)
        return false;

    return aout->opaque_class == &g_audiotrack_class;
}

void SDL_Init_AoutAndroid(JNIEnv *env)
{

}
