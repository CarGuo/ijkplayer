/* EOF media-clock snapshots. All fields are protected by eof_clock.mutex.
 * Readers acquire sampq -> audioq -> videoq -> snapshot. Publishers never hold
 * the snapshot while acquiring another lock. No container-duration bounds. */
#include "ff_rate_clock.h"

typedef struct EOFClockView {
    int audio_drained;
    int demux_eof;
    int seek_pending;
    int audio_terminal;
    double audio_end;
    double audio_clock;
    double video_clock;
    double audio_pause_pts;
    int audio_pause_serial;
} EOFClockView;

static EOFClockView eof_clock_view(VideoState *is)
{
    EOFClockView view = { .audio_end = NAN, .audio_clock = NAN, .video_clock = NAN };
    EOFClockState *c = &is->eof_clock;
    SDL_AudioPlaybackSnapshot position = { 0 };
    int head_mode = is->audclk.source_aout != NULL;
    if (head_mode)
        SDL_AoutGetPlaybackPosition(is->audclk.source_aout, &position);
    SDL_LockMutex(is->sampq.mutex);
    SDL_LockMutex(is->audioq.mutex);
    SDL_LockMutex(is->videoq.mutex);
    SDL_LockMutex(c->mutex);
    {
        double now = c->paused ? c->pause_at : av_gettime_relative() / 1000000.0;
        view.audio_pause_pts = c->audio_pause_pts;
        view.audio_pause_serial = c->audio_pause_serial;
        view.demux_eof = c->demux_eof;
        view.seek_pending = c->seek_pending;
        view.audio_terminal = c->demux_eof && c->audio_finished_serial == is->audioq.serial;
        view.audio_drained = view.audio_terminal &&
            !c->audio_callback_busy && !c->audio_submit_pending &&
            !(c->audio_output_error && c->audio_output_error_serial == is->audioq.serial) &&
            frame_queue_nb_remaining(&is->sampq) == 0 &&
            (c->audio_serial != is->audioq.serial ||
             (!c->audio_pending && now >= c->audio_deadline &&
              (!head_mode || (position.valid &&
               position.generation == is->audioq.serial &&
               position.epoch == c->audio_output_epoch &&
               position.played_frames >= c->audio_output_end_frame))));
        if (c->demux_eof && c->audio_finished_serial == is->audioq.serial &&
            c->audio_serial == is->audioq.serial) {
            if (view.audio_drained)
                view.audio_end = c->audio_end;
            /* Real PCM already submitted is the clock's available interval,
             * even if a later decoded tail frame is still being delivered. */
            if (head_mode) {
                if (position.valid && position.generation == is->audioq.serial &&
                    position.epoch == c->audio_output_epoch) {
                    view.audio_clock = c->paused && c->audio_pause_serial == is->audioq.serial ?
                        c->audio_pause_pts : position.pts;
                }
            } else if (isfinite(c->audio_end)) {
                view.audio_clock = c->audio_end - FFMAX(0.0, c->audio_deadline - now);
            }
        }
        if (c->video_serial == is->videoq.serial && isfinite(c->video_pts)) {
            double elapsed = FFMAX(0.0, now - c->video_updated);
            view.video_clock = FFMIN(c->video_pts + elapsed * c->video_applied_rate, c->video_end);
        }
    }
    SDL_UnlockMutex(c->mutex);
    SDL_UnlockMutex(is->videoq.mutex);
    SDL_UnlockMutex(is->audioq.mutex);
    SDL_UnlockMutex(is->sampq.mutex);
    return view;
}

static int eof_clock_rate_applied(VideoState *is, const SDL_AudioRateApplied *applied)
{
    SDL_AudioPlaybackSnapshot position = { 0 };
    if (!is->platform_rate_mode || !is->audclk.source_aout || !applied->sequence ||
        !isfinite(applied->speed) || applied->speed <= 0 ||
        !isfinite(applied->apply_before) || !isfinite(applied->apply_after) ||
        !isfinite(applied->observed_at) || applied->apply_before > applied->apply_after ||
        applied->apply_after > applied->observed_at) return 0;
    SDL_AoutGetPlaybackPosition(is->audclk.source_aout, &position);
    SDL_LockMutex(is->play_mutex);
    SDL_LockMutex(is->audioq.mutex);
    SDL_LockMutex(is->videoq.mutex);
    SDL_LockMutex(is->eof_clock.mutex);
    EOFClockState *c = &is->eof_clock;
    int accepted = 0;
    if (!c->seek_pending && position.valid &&
        position.generation == is->audioq.serial &&
        position.generation == applied->generation && position.epoch == applied->output_epoch) {
        accepted = 1;
        if (ffp_rate_fresh(c->applied_rate_sequence, c->applied_rate_generation,
                          c->applied_rate_epoch, applied->sequence,
                          applied->generation, applied->output_epoch)) {
            /* The default two-buffer latency is a nominal PCM duration.
             * Acknowledge rate changes at the current serialized clock boundary,
             * never at a historical JNI timestamp before a newer COMMIT.
             * Paused commits and acknowledgements share the fixed pause anchor. */
            if (c->audio_serial == is->audioq.serial &&
                c->audio_output_epoch == applied->output_epoch &&
                isfinite(c->audio_deadline) && isfinite(c->video_applied_rate) &&
                c->video_applied_rate > 0 && c->video_applied_rate != applied->speed) {
                double deadline_now = c->paused ? c->pause_at : av_gettime_relative() / 1000000.0;
                if (c->audio_deadline > deadline_now)
                    c->audio_deadline = deadline_now +
                        (c->audio_deadline - deadline_now) * c->video_applied_rate / applied->speed;
            }
            /* A newer real video presentation must not be rewound by a delayed
             * control callback. A paused command never advances media. */
            double effective_apply_at = applied->apply_after;
            /* An acknowledgement can arrive just after resume even though
             * JNI applied the rate while no media was being consumed. */
            if (!c->paused && effective_apply_at >= c->pause_at &&
                effective_apply_at <= c->last_resume_at)
                effective_apply_at = c->last_resume_at;
            double boundary = c->paused ? c->pause_at :
                FFMAX(effective_apply_at, c->video_updated);
            if (isfinite(c->video_pts) && c->video_serial == is->videoq.serial) {
                c->video_pts = ffp_rate_anchor(c->video_pts, c->video_end,
                    c->video_updated, c->video_applied_rate, boundary);
                c->video_updated = boundary;
            }
            c->video_applied_rate = applied->speed;
            c->video_applied_at = effective_apply_at;
            c->applied_rate_sequence = applied->sequence;
            c->applied_rate_generation = applied->generation;
            c->applied_rate_epoch = applied->output_epoch;
        }
    }
    SDL_UnlockMutex(c->mutex);
    SDL_UnlockMutex(is->videoq.mutex);
    SDL_UnlockMutex(is->audioq.mutex);
    SDL_UnlockMutex(is->play_mutex);
    return accepted;
}

static int audio_tail_drained(VideoState *is)
{
    EOFClockView view = eof_clock_view(is);
    return !view.seek_pending && view.audio_drained;
}

static void eof_clock_audio_finished(VideoState *is, int serial)
{
    SDL_LockMutex(is->eof_clock.mutex);
    is->eof_clock.audio_finished_serial = serial;
    SDL_UnlockMutex(is->eof_clock.mutex);
    /* Pair publication with the readable wait's queue mutex: no lost wakeup. */
    SDL_LockMutex(is->sampq.mutex);
    SDL_CondSignal(is->sampq.cond);
    SDL_UnlockMutex(is->sampq.mutex);
}

static void eof_clock_audio_delivered(VideoState *is, double end, int serial,
                                      int pending, double latency, double now)
{
    EOFClockState *c = &is->eof_clock;
    SDL_LockMutex(c->mutex);
    c->audio_serial = serial;
    c->audio_end = end;
    c->audio_pending = pending;
    /* Paused callbacks become audible only after resume. */
    c->audio_deadline = (c->paused ? c->pause_at : now) +
        (isfinite(latency) && latency > 0 ? latency : 0);
    SDL_UnlockMutex(c->mutex);
}

static void eof_clock_video_presented(VideoState *is, double pts,
                                      double duration, int serial)
{
    EOFClockState *c = &is->eof_clock;
    SDL_LockMutex(c->mutex);
    c->video_serial = serial;
    c->video_pts = pts;
    c->video_end = pts + (isfinite(duration) && duration > 0 ? duration : 0);
    if (!isfinite(c->video_end))
        c->video_end = pts;
    c->video_updated = c->paused ? c->pause_at : av_gettime_relative() / 1000000.0;
    SDL_UnlockMutex(c->mutex);
}

static void eof_clock_pause(VideoState *is, int paused)
{
    EOFClockState *c = &is->eof_clock;
    double now = av_gettime_relative() / 1000000.0;
    SDL_AudioPlaybackSnapshot position = { 0 };
    if (paused && is->audclk.source_aout)
        SDL_AoutGetPlaybackPosition(is->audclk.source_aout, &position);
    SDL_LockMutex(is->audioq.mutex);
    SDL_LockMutex(c->mutex);
    if (paused && !c->paused) {
        c->pause_at = now;
        c->audio_pause_pts = position.valid && position.generation == is->audioq.serial ? position.pts : NAN;
        c->audio_pause_serial = position.valid && position.generation == is->audioq.serial ? position.generation : -1;
    }
    else if (!paused && c->paused) {
        double delay = FFMAX(0.0, now - c->pause_at);
        c->last_resume_at = now;
        c->audio_deadline += delay;
        c->video_updated += delay;
    }
    c->paused = paused;
    SDL_UnlockMutex(c->mutex);
    SDL_UnlockMutex(is->audioq.mutex);
}

static void eof_clock_seek(VideoState *is, int pending)
{
    EOFClockState *c = &is->eof_clock;
    SDL_LockMutex(c->mutex);
    /* Hide completion/handoff while pending, but retain the OLD source clock:
     * read_thread can wait at EOF before committing an automatic loop seek.
     * Failure keeps packet serials and its terminal interval; success changes
     * queue serials, invalidating old snapshots without reviving a raw clock. */
    c->seek_pending = pending;
    SDL_UnlockMutex(c->mutex);
}

static void eof_clock_demux(VideoState *is, int eof)
{
    SDL_LockMutex(is->eof_clock.mutex);
    is->eof_clock.demux_eof = eof;
    SDL_UnlockMutex(is->eof_clock.mutex);
}

static Frame *audio_frame_queue_peek_readable(VideoState *is)
{
    FrameQueue *f = &is->sampq;
    SDL_LockMutex(f->mutex);
    while (f->size - f->rindex_shown <= 0 && !f->pktq->abort_request) {
        int finished;
        SDL_LockMutex(f->pktq->mutex);
        SDL_LockMutex(is->eof_clock.mutex);
        finished = is->eof_clock.audio_finished_serial == f->pktq->serial;
        SDL_UnlockMutex(is->eof_clock.mutex);
        SDL_UnlockMutex(f->pktq->mutex);
        if (finished)
            break;
        SDL_CondWait(f->cond, f->mutex);
    }
    Frame *frame = NULL;
    if (!f->pktq->abort_request && f->size - f->rindex_shown > 0) {
        /* Publish ownership before the consumer removes this frame from sampq. */
        SDL_LockMutex(is->eof_clock.mutex);
        is->eof_clock.audio_callback_busy = 1;
        SDL_UnlockMutex(is->eof_clock.mutex);
        frame = &f->queue[(f->rindex + f->rindex_shown) % f->max_size];
    }
    SDL_UnlockMutex(f->mutex);
    return frame;
}

static void eof_clock_audio_busy(VideoState *is, int busy)
{
    SDL_LockMutex(is->eof_clock.mutex);
    is->eof_clock.audio_callback_busy = busy;
    SDL_UnlockMutex(is->eof_clock.mutex);
}

/* Stage before callback ownership is released. The endpoint is tentative until
 * every byte in this output buffer has been accepted; no partial-byte PTS math. */
static void eof_clock_audio_stage(VideoState *is, double end, int serial,
                                 int pending, SDL_AudioSubmission *submission)
{
    EOFClockState *c = &is->eof_clock;
    SDL_LockMutex(c->mutex);
    if (++c->audio_submit_next_token == 0)
        ++c->audio_submit_next_token;
    c->audio_submit_token = c->audio_submit_next_token;
    c->audio_submit_pending = 1;
    c->audio_submit_serial = serial;
    c->audio_submit_end = end;
    c->audio_submit_buffer_pending = pending;
    submission->token = c->audio_submit_token;
    submission->generation = serial;
    SDL_UnlockMutex(c->mutex);
}

/* sampq -> audioq -> snapshot, matching EOF readers. Never called with a backend or
 * playback mutex held, and no lock is retained across an AudioTrack write. */
static int eof_clock_audio_submission(VideoState *is,
                                     const SDL_AudioSubmission *submission,
                                     int action, int error, double latency,
                                     double now, double *accepted_end,
                                     const SDL_AudioPlaybackSnapshot *position)
{
    EOFClockState *c = &is->eof_clock;
    int result = SDL_AUDIO_SUBMIT_READY;
    SDL_LockMutex(is->sampq.mutex);
    SDL_LockMutex(is->audioq.mutex);
    SDL_LockMutex(c->mutex);
    int owns_stage = submission->token && c->audio_submit_pending &&
        submission->token == c->audio_submit_token &&
        submission->generation == c->audio_submit_serial;
    if (action == SDL_AUDIO_SUBMIT_CANCEL) {
        if (owns_stage)
            c->audio_submit_pending = 0;
    } else if (is->abort_request) {
        result = SDL_AUDIO_SUBMIT_ABORT;
    } else if (action == SDL_AUDIO_SUBMIT_DEVICE_FAIL) {
        /* A failed pause/flush/accounting operation invalidates this device,
         * even when the triggering submission belongs to a retired seek. */
        c->audio_output_error = error ? error : AVERROR(EIO);
        c->audio_output_error_serial = is->audioq.serial;
        c->audio_submit_pending = 0;
        result = SDL_AUDIO_SUBMIT_FAILED;
    } else if (submission->generation != is->audioq.serial ||
               (submission->token && !owns_stage)) {
        result = SDL_AUDIO_SUBMIT_STALE;
    } else if (c->seek_pending) {
        /* A failed seek must retain the old buffer and its accepted offset. */
        result = SDL_AUDIO_SUBMIT_WAIT;
    } else if (c->audio_output_error &&
               c->audio_output_error_serial == is->audioq.serial) {
        result = SDL_AUDIO_SUBMIT_FAILED;
    } else if (!submission->token &&
               (action == SDL_AUDIO_SUBMIT_CHECK || action == SDL_AUDIO_SUBMIT_FAIL) &&
               c->demux_eof && c->audio_finished_serial == is->audioq.serial &&
               !c->audio_callback_busy && !c->audio_submit_pending &&
               frame_queue_nb_remaining(&is->sampq) == 0 &&
               (c->audio_serial != is->audioq.serial ||
                (!c->audio_pending &&
                 (c->paused ? c->pause_at : now) >= c->audio_deadline &&
                 (!position || (position->valid &&
                  position->generation == is->audioq.serial &&
                  position->epoch == c->audio_output_epoch &&
                  position->played_frames >= c->audio_output_end_frame))))) {
        /* Recheck on every idle wake: some sources can enqueue more data after
         * EOF without changing serial, so quiet idle is never a sticky latch. */
        result = SDL_AUDIO_SUBMIT_DRAINED;
    } else if (action == SDL_AUDIO_SUBMIT_FAIL) {
        c->audio_output_error = error;
        c->audio_output_error_serial = submission->generation;
        if (owns_stage)
            c->audio_submit_pending = 0;
        result = SDL_AUDIO_SUBMIT_FAILED;
    } else if (action == SDL_AUDIO_SUBMIT_COMMIT && owns_stage) {
        c->audio_serial = c->audio_submit_serial;
        c->audio_end = c->audio_submit_end;
        c->audio_output_epoch = submission->output_epoch;
        c->audio_output_end_frame = submission->end_frame;
        if (position && position->valid && position->generation == submission->generation &&
            position->epoch == submission->output_epoch && c->paused &&
            (c->audio_pause_serial != submission->generation || !isfinite(c->audio_pause_pts))) {
            c->audio_pause_pts = position->pts;
            c->audio_pause_serial = submission->generation;
        }
        c->audio_pending = c->audio_submit_buffer_pending;
        double wall_latency = isfinite(latency) && latency > 0 ? latency : 0;
        if (is->platform_rate_mode && is->audclk.source_aout &&
            isfinite(c->video_applied_rate) && c->video_applied_rate > 0 &&
            c->video_applied_rate != 1.0)
            wall_latency /= c->video_applied_rate;
        c->audio_deadline = (c->paused ? c->pause_at : now) + wall_latency;
        c->audio_submit_pending = 0;
        *accepted_end = c->audio_end;
    }
    SDL_UnlockMutex(c->mutex);
    SDL_UnlockMutex(is->audioq.mutex);
    SDL_UnlockMutex(is->sampq.mutex);
    return result;
}

static int eof_clock_audio_output_error(VideoState *is)
{
    int error;
    SDL_LockMutex(is->audioq.mutex);
    SDL_LockMutex(is->eof_clock.mutex);
    error = is->eof_clock.audio_output_error_serial == is->audioq.serial ?
        is->eof_clock.audio_output_error : 0;
    SDL_UnlockMutex(is->eof_clock.mutex);
    SDL_UnlockMutex(is->audioq.mutex);
    return error;
}
