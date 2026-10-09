/*
 * Private AudioTrack head-to-PCM mapping and acknowledged frame drain.
 * Exact PTS and transform compatibility estimates are separate fields.
 * Media-progress/rate spans preserve queued tempo changes and discontinuities.
 *
 * No allocation, locks, wall clock, platform calls, or public PTS clamping.
 * The caller serializes every operation with its position/lifetime mutex and
 * owns the fixed span storage until the tracker is no longer used.
 *
 * Frames are absolute within one successfully flushed output epoch. Initialize
 * at raw head zero; call Reset only after a checked, successful platform flush,
 * supplying the new generation and a strictly greater epoch. A generation of
 * -1 means unbound. Accept only the actual whole-frame positive write prefix.
 * Never call Accept with the requested count before the write acknowledges it.
 *
 * Observe interprets the uint32_t playback head modulo 2^32. Queries must be
 * frequent enough that fewer than 2^31 frames can play between observations.
 * A greater delta is ambiguous (including an unexplained backwards reset),
 * and a delta beyond the acknowledged prefix is held until a later query after
 * ACK. Rejected observations preserve raw/head/PTS/valid and clear freshness.
 * A reset extremely close to wrap is inherently indistinguishable from wrap;
 * all intentional platform resets MUST therefore use Reset with a new epoch.
 *
 * valid describes the head sample, not the PTS: valid head + NAN PTS is useful
 * for draining unknown-PTS audio. fresh describes the most recent Observe and
 * is not consumed by GetSnapshot. A failed platform read should return the
 * cached snapshot with fresh=0 without feeding an invented raw zero here.
 */
#ifndef IJKSDL_AUDIO_PLAYBACK_H
#define IJKSDL_AUDIO_PLAYBACK_H

#include <stddef.h>
#include <stdint.h>
#include <math.h>

typedef struct SDL_AudioPlaybackSpan {
    uint64_t first_frame;
    uint64_t frame_count;
    double pts_start;
    double pts_end;
    int real_pcm;
    double media_start, media_step, media_rate;
    double estimate_start, estimate_end;
} SDL_AudioPlaybackSpan;

typedef struct SDL_AudioPlaybackSnapshot {
    int generation;
    uint64_t epoch;
    uint64_t played_frames;
    uint64_t accepted_frames;
    double pts;
    double played_media, accepted_media, media_rate;
    double estimated_pts; /* compatibility estimate, never an exact PTS claim */
    int valid;
    int fresh;
} SDL_AudioPlaybackSnapshot;

typedef enum SDL_AudioPlaybackResult {
    SDL_AUDIO_PLAYBACK_OK = 0,
    SDL_AUDIO_PLAYBACK_INVALID = -1,
    SDL_AUDIO_PLAYBACK_CAPACITY = -2,
    SDL_AUDIO_PLAYBACK_OVERFLOW = -3,
    SDL_AUDIO_PLAYBACK_HEAD_AHEAD = -4,
    SDL_AUDIO_PLAYBACK_EPOCH = -5,
    SDL_AUDIO_PLAYBACK_HEAD_AMBIGUOUS = -6
} SDL_AudioPlaybackResult;

typedef struct SDL_AudioPlaybackTracker {
    SDL_AudioPlaybackSpan *spans;
    size_t capacity;
    size_t first;
    size_t count;
    int generation;
    uint64_t epoch;
    uint64_t played_frames;
    uint64_t accepted_frames;
    uint32_t raw_head;
    double pts;
    double last_real_end, estimated_pts, last_estimate_end;
    double played_media, accepted_media, media_rate;
    int sample_mapped;
    int valid;
    int fresh;
} SDL_AudioPlaybackTracker;

static inline SDL_AudioPlaybackResult SDL_AudioPlaybackTracker_Init(
    SDL_AudioPlaybackTracker *t, SDL_AudioPlaybackSpan *spans, size_t capacity)
{
    if (!t || !spans || !capacity || capacity > SIZE_MAX / sizeof(*spans))
        return SDL_AUDIO_PLAYBACK_INVALID;
    t->spans = spans;
    t->capacity = capacity;
    t->first = t->count = 0;
    t->generation = -1;
    t->epoch = t->played_frames = t->accepted_frames = 0;
    t->raw_head = 0;
    t->pts = t->last_real_end = t->estimated_pts = t->last_estimate_end = NAN;
    t->played_media = t->accepted_media = 0;
    t->media_rate = 1.0;
    t->sample_mapped = t->valid = t->fresh = 0;
    return SDL_AUDIO_PLAYBACK_OK;
}

static inline SDL_AudioPlaybackResult SDL_AudioPlaybackTracker_Reset(
    SDL_AudioPlaybackTracker *t, int generation, uint64_t epoch)
{
    if (!t || !t->spans || !t->capacity)
        return SDL_AUDIO_PLAYBACK_INVALID;
    if (epoch <= t->epoch)
        return SDL_AUDIO_PLAYBACK_EPOCH;
    t->first = t->count = 0;
    t->generation = generation;
    t->epoch = epoch;
    t->played_frames = t->accepted_frames = 0;
    t->raw_head = 0;
    t->pts = t->last_real_end = t->estimated_pts = t->last_estimate_end = NAN;
    t->played_media = t->accepted_media = 0;
    t->media_rate = 1.0;
    t->sample_mapped = t->valid = t->fresh = 0;
    return SDL_AUDIO_PLAYBACK_OK;
}

/* Avoid overflowing size_t even for an unusually large caller-owned array. */
static inline size_t SDL_AudioPlaybackTracker_SpanIndex(
    const SDL_AudioPlaybackTracker *t, size_t offset)
{
    size_t until_wrap = t->capacity - t->first;
    return offset >= until_wrap ? offset - until_wrap : t->first + offset;
}

static inline SDL_AudioPlaybackResult SDL_AudioPlaybackTracker_AcceptEstimated(
    SDL_AudioPlaybackTracker *t, uint64_t frame_count, int real_pcm,
    double pts_start, double pts_end, double media_step, double media_rate,
    double estimate_start, double estimate_end)
{
    SDL_AudioPlaybackSpan *s;
    if (!t || !t->spans || !t->capacity)
        return SDL_AUDIO_PLAYBACK_INVALID;
    if (!frame_count)
        return SDL_AUDIO_PLAYBACK_OK;
    if (frame_count > UINT64_MAX - t->accepted_frames)
        return SDL_AUDIO_PLAYBACK_OVERFLOW;
    if (!isfinite(media_step) || media_step < 0 || !isfinite(media_rate) || media_rate <= 0)
        return SDL_AUDIO_PLAYBACK_INVALID;
    real_pcm = !!real_pcm;
    if (!real_pcm) media_step = 0;
    double next_media = t->accepted_media + frame_count * media_step;
    if (!isfinite(next_media)) return SDL_AUDIO_PLAYBACK_OVERFLOW;
    if (!real_pcm || !isfinite(pts_start) || !isfinite(pts_end))
        pts_start = pts_end = NAN;
    if (!real_pcm || !isfinite(estimate_start) || !isfinite(estimate_end))
        estimate_start = estimate_end = NAN;

    /* Conservative exact coalescing only: synthetic silence or identical,
     * finite constant-PTS real spans. General floating slopes are not merged
     * merely because rounded division happens to produce equal numbers. */
    if (t->count) {
        s = &t->spans[SDL_AudioPlaybackTracker_SpanIndex(t, t->count - 1)];
        if (s->real_pcm == real_pcm && s->media_step == media_step && s->media_rate == media_rate &&
            (!real_pcm ||
             (!isfinite(s->estimate_start) && !isfinite(s->estimate_end) &&
              !isfinite(estimate_start) && !isfinite(estimate_end)) ||
             (s->estimate_start == s->estimate_end &&
              s->estimate_end == estimate_start && estimate_start == estimate_end)) &&
            (!real_pcm || (isfinite(pts_start) &&
              s->pts_start == s->pts_end && s->pts_end == pts_start &&
              pts_start == pts_end))) {
            s->frame_count += frame_count; /* bounded by accepted overflow check */
            t->accepted_frames += frame_count;
            t->accepted_media = next_media;
            return SDL_AUDIO_PLAYBACK_OK;
        }
    }
    if (t->count == t->capacity)
        return SDL_AUDIO_PLAYBACK_CAPACITY;
    s = &t->spans[SDL_AudioPlaybackTracker_SpanIndex(t, t->count)];
    s->first_frame = t->accepted_frames;
    s->frame_count = frame_count;
    s->real_pcm = real_pcm;
    s->media_start = t->accepted_media;
    s->media_step = media_step;
    s->media_rate = media_rate;
    s->pts_start = pts_start;
    s->pts_end = pts_end;
    s->estimate_start = estimate_start;
    s->estimate_end = estimate_end;
    ++t->count;
    t->accepted_frames += frame_count;
    t->accepted_media = next_media;
    /* Queueing never changes the clock at an unchanged head. */
    return SDL_AUDIO_PLAYBACK_OK;
}

static inline SDL_AudioPlaybackResult SDL_AudioPlaybackTracker_AcceptRate(
    SDL_AudioPlaybackTracker *t, uint64_t count, int real_pcm, double start, double end,
    double step, double rate)
{
    return SDL_AudioPlaybackTracker_AcceptEstimated(t, count, real_pcm, start, end,
        step, rate, NAN, NAN);
}

/* Compatibility for callers that need only exact PTS/frame accounting. */
static inline SDL_AudioPlaybackResult SDL_AudioPlaybackTracker_Accept(
    SDL_AudioPlaybackTracker *t, uint64_t count, int real_pcm, double start, double end)
{
    return SDL_AudioPlaybackTracker_AcceptRate(t, count, real_pcm, start, end, 0, 1);
}

static inline double SDL_AudioPlaybackTracker_Interpolate(
    const SDL_AudioPlaybackSpan *s, uint64_t consumed)
{
    double fraction;
    if (!isfinite(s->pts_start) || !isfinite(s->pts_end))
        return NAN;
    if (!consumed)
        return s->pts_start;
    if (consumed == s->frame_count)
        return s->pts_end;
    fraction = (double)consumed / (double)s->frame_count;
    /* Avoid overflow for finite opposite-sign endpoints such as +/-DBL_MAX. */
    if ((s->pts_start < 0) != (s->pts_end < 0))
        return s->pts_start * (1.0 - fraction) + s->pts_end * fraction;
    return s->pts_start + (s->pts_end - s->pts_start) * fraction;
}

static inline SDL_AudioPlaybackResult SDL_AudioPlaybackTracker_Observe(
    SDL_AudioPlaybackTracker *t, uint32_t raw_head)
{
    uint32_t delta;
    size_t i;
    if (!t || !t->spans || !t->capacity)
        return SDL_AUDIO_PLAYBACK_INVALID;
    delta = (uint32_t)(raw_head - t->raw_head);
    if (delta > (uint32_t)INT32_MAX) {
        t->fresh = 0;
        return SDL_AUDIO_PLAYBACK_HEAD_AMBIGUOUS;
    }
    if ((uint64_t)delta > t->accepted_frames - t->played_frames) {
        t->fresh = 0;
        return SDL_AUDIO_PLAYBACK_HEAD_AHEAD;
    }
    t->raw_head = raw_head;
    t->played_frames += delta;
    if (delta || !t->sample_mapped) {
        double value = t->last_real_end, estimate = t->last_estimate_end;
        for (i = 0; i < t->count; ++i) {
            const SDL_AudioPlaybackSpan *s =
                &t->spans[SDL_AudioPlaybackTracker_SpanIndex(t, i)];
            uint64_t end = s->first_frame + s->frame_count;
            uint64_t consumed;
            if (t->played_frames < s->first_frame)
                break;
            consumed = t->played_frames - s->first_frame;
            if (consumed > s->frame_count)
                consumed = s->frame_count;
            t->played_media = s->media_start + consumed * s->media_step;
            if (s->real_pcm) t->media_rate = s->media_rate;
            if (s->real_pcm) {
                value = SDL_AudioPlaybackTracker_Interpolate(s, consumed);
                SDL_AudioPlaybackSpan approximate = *s;
                approximate.pts_start = s->estimate_start;
                approximate.pts_end = s->estimate_end;
                estimate = SDL_AudioPlaybackTracker_Interpolate(&approximate, consumed);
            }
            t->sample_mapped = 1;
            /* Consumed-prefix semantics: exactly at a discontinuity, retain
             * the preceding endpoint; enter the new span only after progress. */
            if (t->played_frames <= end)
                break;
        }
        t->pts = value;
        t->estimated_pts = estimate;
    }
    /* Keep the real endpoint independently while reclaiming played spans. */
    while (t->count) {
        SDL_AudioPlaybackSpan *s = &t->spans[t->first];
        if (s->first_frame + s->frame_count > t->played_frames)
            break;
        if (s->real_pcm) {
            t->last_real_end = s->pts_end;
            t->last_estimate_end = s->estimate_end;
        }
        ++t->first;
        if (t->first == t->capacity)
            t->first = 0;
        --t->count;
    }
    t->valid = t->fresh = 1;
    return SDL_AUDIO_PLAYBACK_OK;
}

static inline SDL_AudioPlaybackResult SDL_AudioPlaybackTracker_GetSnapshot(
    const SDL_AudioPlaybackTracker *t, SDL_AudioPlaybackSnapshot *out)
{
    if (!t || !out || !t->spans || !t->capacity)
        return SDL_AUDIO_PLAYBACK_INVALID;
    out->generation = t->generation;
    out->epoch = t->epoch;
    out->played_frames = t->played_frames;
    out->accepted_frames = t->accepted_frames;
    out->pts = t->pts;
    out->estimated_pts = t->estimated_pts;
    out->played_media = t->played_media;
    out->accepted_media = t->accepted_media;
    out->media_rate = t->media_rate;
    out->valid = t->valid;
    out->fresh = t->fresh;
    return SDL_AUDIO_PLAYBACK_OK;
}

#endif /* IJKSDL_AUDIO_PLAYBACK_H */
