/* Copyright (c) 2026 ijkPlayer contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * SoundTouch is a separately built, pinned LGPL dependency. */
#include "ff_soundtouch.h"
#include "SoundTouch.h"
#include <cmath>
#include <climits>
#include <new>

struct FFPSoundTouch {
    soundtouch::SoundTouch engine;
    double tempo, expected;
    bool flushed;
};

void *ffp_soundtouch_create(int sample_rate, int channels, double tempo)
{
    if (sample_rate <= 0 || channels <= 0 || channels > 8 ||
        !std::isfinite(tempo) || tempo < FFP_SOUNDTOUCH_MIN_RATE || tempo > FFP_SOUNDTOUCH_MAX_RATE) return NULL;
    FFPSoundTouch *s = NULL;
#if defined(__EXCEPTIONS)
    try {
#endif
    s = new (std::nothrow) FFPSoundTouch;
    if (!s) return NULL;
    s->tempo = tempo;
    s->expected = 0;
    s->flushed = false;
    s->engine.setSampleRate(sample_rate);
    s->engine.setChannels(channels);
    s->engine.setRate(1.0);
    s->engine.setPitch(1.0);
    s->engine.setTempo(tempo);
    return s;
#if defined(__EXCEPTIONS)
    } catch (...) { delete s; return NULL; }
#endif
}

void ffp_soundtouch_destroy(void *handle)
{
    /* The pinned engine's clear() does not reset samplesOutput. Construct a
     * fresh instance at each completed segment/seek so flush's accounting is
     * never reused with an old output counter. */
    delete static_cast<FFPSoundTouch *>(handle);
}

int ffp_soundtouch_needs_reset(void *handle, int next_frames)
{
    FFPSoundTouch *s = static_cast<FFPSoundTouch *>(handle);
    return s && (next_frames < 0 || s->expected + next_frames / s->tempo > INT_MAX - 131072);
}

int ffp_soundtouch_put(void *handle, const short *pcm, int frames)
{
    FFPSoundTouch *s = static_cast<FFPSoundTouch *>(handle);
    if (!s || !pcm || frames < 0 || s->flushed) return -1;
    double expected = s->expected + frames / s->tempo;
    /* flush() in the pinned version uses signed int/long frame counters. */
    if (!std::isfinite(expected) || expected > INT_MAX - 131072) return -1;
#if defined(__EXCEPTIONS)
    try {
#endif
    s->engine.putSamples(pcm, frames);
    s->expected = expected;
    return 0;
#if defined(__EXCEPTIONS)
    } catch (...) { return -1; }
#endif
}

int ffp_soundtouch_receive(void *handle, short *pcm, int capacity_frames)
{
    FFPSoundTouch *s = static_cast<FFPSoundTouch *>(handle);
    if (!s || !pcm || capacity_frames <= 0) return -1;
    return s->engine.receiveSamples(pcm, capacity_frames);
}

int ffp_soundtouch_flush(void *handle)
{
    FFPSoundTouch *s = static_cast<FFPSoundTouch *>(handle);
    if (!s) return -1;
#if defined(__EXCEPTIONS)
    try {
#endif
    if (!s->flushed) {
        s->engine.flush();
        s->flushed = true;
    }
    return 0;
#if defined(__EXCEPTIONS)
    } catch (...) { return -1; }
#endif
}
