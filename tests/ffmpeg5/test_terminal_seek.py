#!/usr/bin/env python3
"""Local-only production seek/terminal-loop regression with real pause/buffering.

Only avformat_seek_file is replaced by a deterministic return value. No socket,
URL open, network read or full player thread is started. --repo permits running
identical assertions against a previous source tree as a negative control.
"""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument("source", help="FFmpeg source directory")
p.add_argument("build", help="FFmpeg build directory")
p.add_argument("output", help="generated test and executable directory")
p.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
a = p.parse_args()
root, out = a.repo.resolve(), Path(a.output).resolve()
out.mkdir(parents=True, exist_ok=True)
s = (root / "ijkmedia/ijkplayer/ff_ffplay.c").read_text()
start = s.index("        if (is->seek_req) {", s.index("static int read_thread"))
end = s.index("        if (is->queue_attachments_req)", start)
seek = s[start:end]
start = s.index("        /* Opt-in live mode must keep draining", end)
if "        /* A terminal read latch cannot refill buffering." in s[end:start]:
    start = s.index("        /* A terminal read latch cannot refill buffering.", end)
end = s.index("        pkt->flags = 0;", start)
terminal = s[start:end]
code = r'''
#define main upstream_rtsp_test_main
#include "@ROOT@/tests/ffmpeg5/test_rtsp_live.c"
#undef main
static int seek_result;
static int fixture_seek(AVFormatContext *ic, int stream, int64_t lo, int64_t target, int64_t hi, int flags)
{ (void)ic; (void)stream; (void)lo; (void)target; (void)hi; (void)flags; return seek_result; }
#define avformat_seek_file fixture_seek
static void run_seek(FFPlayer *ffp, int *latch, int *done, int result)
{
    VideoState *is = ffp->is;
    int rtsp_io_error = *latch, completed = *done, ret = 0;
    seek_result = result;
@SEEK@
    *latch = rtsp_io_error; *done = completed;
}
#undef avformat_seek_file
static int terminal_iteration(FFPlayer *ffp, int rtsp_io_error, int *done)
{
    VideoState *is = ffp->is;
    int completed = *done, ret = 0, last_error = 0;
    SDL_mutex *wait_mutex = SDL_CreateMutex();
    for (int once = 0; once < 1; once++) {
@TERMINAL@
        ret = 1; /* would reach the next av_read_frame; never perform I/O */
    }
fail:
    (void)last_error;
    *done = completed; SDL_DestroyMutex(wait_mutex); return ret;
}
static void notifications(FFPlayer *ffp, int *errors, int *completions)
{
    AVMessage msg;
    while (msg_queue_get(&ffp->msg_queue, &msg, 0) > 0) {
        if (msg.what == FFP_MSG_ERROR) { (*errors)++; assert(msg.arg1 == ffp->error); }
        if (msg.what == FFP_MSG_COMPLETED) (*completions)++;
        msg_free_res(&msg);
    }
}
static void setup(FFPlayer *ffp, VideoState *is, AVFormatContext *ic)
{
    initialize(ffp, is, ic);
    ffp->packet_buffering = 1; ffp->loop = 1;
    msg_queue_init(&ffp->msg_queue); ffp->msg_queue.abort_request = 0;
    is->continue_read_thread = SDL_CreateCond();
}
static void cleanup(FFPlayer *ffp, VideoState *is)
{
    SDL_DestroyCond(is->continue_read_thread);
    msg_queue_destroy(&ffp->msg_queue); destroy(is);
}
int main(void)
{
    flush_pkt.data = (uint8_t *)&flush_pkt; av_log_set_level(AV_LOG_ERROR);
    AVFormatContext *ic = avformat_alloc_context();
    ic->iformat = av_find_input_format("rtsp"); ic->duration = AV_NOPTS_VALUE;
    AVStream *a = avformat_new_stream(ic, NULL), *v = avformat_new_stream(ic, NULL);
    a->time_base = v->time_base = (AVRational){1,1000};
    FFPlayer ffp; VideoState is;
    int latch, done, errors = 0, completions = 0, as, vs;

    setup(&ffp, &is, ic);
    latch = ffp.error = AVERROR(ENOSYS); done = 0; is.eof = 1; is.seek_req = 1;
    as = is.audioq.serial; vs = is.videoq.serial;
    run_seek(&ffp, &latch, &done, AVERROR(EIO));
    assert(latch == AVERROR(ENOSYS) && is.eof && ffp.error == latch && !done);
    assert(!is.buffering_on && !is.paused && !is.pause_req);
    assert(is.audioq.serial == as && is.videoq.serial == vs); /* failed seek never flushes */
    assert(terminal_iteration(&ffp, latch, &done) == 0 && done);
    notifications(&ffp, &errors, &completions);
    assert(errors == 1 && completions == 0);
    is.abort_request = 1; /* avoid the completed branch's intentional blocking wait */
    assert(terminal_iteration(&ffp, latch, &done) == 0);
    notifications(&ffp, &errors, &completions); assert(errors == 1);
    cleanup(&ffp, &is);

    /* Failed seek after notification keeps the same terminal generation.
     * A second pending seek wakes the completed branch without aborting it. */
    for (int error_case = 0; error_case < 2; ++error_case) {
        setup(&ffp, &is, ic);
        latch = ffp.error = error_case ? AVERROR_EXIT : AVERROR(ENOSYS);
        done = errors = completions = 0; is.eof = 1;
        assert(terminal_iteration(&ffp, latch, &done) == 0 && done);
        notifications(&ffp, &errors, &completions);
        assert(errors == 1 && !completions && is.pause_req);
        as = is.audioq.serial; vs = is.videoq.serial;
        for (int attempt = 0; attempt < 3; ++attempt) {
            is.seek_req = 1;
            run_seek(&ffp, &latch, &done, AVERROR(EIO));
            assert(done && latch == ffp.error && is.eof);
            assert(!is.buffering_on && is.pause_req && is.step && !is.paused);
            assert(is.audioq.serial == as && is.videoq.serial == vs);
            is.seek_req = 1; /* deterministic wake, no wait/thread/network */
            assert(terminal_iteration(&ffp, latch, &done) == 0 && done);
            notifications(&ffp, &errors, &completions);
            assert(errors == 1 && !completions);
        }
        cleanup(&ffp, &is);
    }

    /* A remaining tail must resume draining, without rereads or early errors. */
    setup(&ffp, &is, ic);
    latch = ffp.error = AVERROR(ENOSYS); done = 0; is.eof = 1;
    is.video_stream = 1; is.video_st = v;
    ffp_toggle_buffering(&ffp, 1);
    assert(is.paused && is.buffering_on && !is.pause_req);
    assert(terminal_iteration(&ffp, latch, &done) == 0 && !done);
    assert(!is.buffering_on && !is.paused && !is.pause_req);
    errors = completions = 0; notifications(&ffp, &errors, &completions); assert(!errors);
    is.viddec.finished = is.videoq.serial;
    assert(terminal_iteration(&ffp, latch, &done) == 0 && done);
    notifications(&ffp, &errors, &completions); assert(errors == 1 && !completions);
    cleanup(&ffp, &is);

    /* Clearing buffering cannot override a user pause or a requested step. */
    setup(&ffp, &is, ic);
    latch = ffp.error = AVERROR(ENOSYS); done = 0; is.eof = 1;
    is.video_stream = 1; is.video_st = v;
    is.pause_req = 1; ffp_toggle_buffering(&ffp, 1);
    assert(terminal_iteration(&ffp, latch, &done) == 0 && !done);
    assert(!is.buffering_on && is.paused && is.pause_req && !is.step);
    is.step = 1; ffp_toggle_buffering(&ffp, 1);
    assert(terminal_iteration(&ffp, latch, &done) == 0 && !done);
    assert(!is.buffering_on && !is.paused && is.pause_req && is.step);
    cleanup(&ffp, &is);

    /* A seek while user-paused keeps the existing one-frame preview request. */
    setup(&ffp, &is, ic);
    latch = ffp.error = AVERROR(ENOSYS); done = 0; is.eof = 1;
    is.pause_req = 1; is.seek_req = 1;
    run_seek(&ffp, &latch, &done, AVERROR(EIO));
    assert(!is.buffering_on && is.pause_req && is.step && !is.paused);
    cleanup(&ffp, &is);

    /* Successful seek clears the latch and advances selected generations. */
    setup(&ffp, &is, ic);
    is.audio_stream = 0; is.audio_st = a; is.video_stream = 1; is.video_st = v;
    latch = ffp.error = AVERROR(ENOSYS); done = 1; is.eof = 1; is.seek_req = 1;
    is.rtsp_live_wait_keyframe = is.rtsp_live_drop_audio_before = 1;
    as = is.audioq.serial; vs = is.videoq.serial;
    run_seek(&ffp, &latch, &done, 0);
    assert(!latch && !ffp.error && !is.eof && !done);
    assert(is.audioq.serial == as + 1 && is.videoq.serial == vs + 1);
    assert(!is.rtsp_live_wait_keyframe && !is.rtsp_live_drop_audio_before);
    assert(is.buffering_on && is.paused && !is.pause_req);
    assert(terminal_iteration(&ffp, latch, &done) == 1); /* reads may resume */
    assert(is.buffering_on); /* do not disable normal successful-seek buffering */
    cleanup(&ffp, &is);

    /* A failed seek with no terminal error keeps ordinary rebuffering. */
    setup(&ffp, &is, ic);
    is.video_stream = 1; is.video_st = v; is.seek_req = 1; latch = done = 0;
    run_seek(&ffp, &latch, &done, AVERROR(EIO));
    assert(!latch && is.buffering_on && is.paused && !is.pause_req);
    assert(terminal_iteration(&ffp, latch, &done) == 1 && is.buffering_on);
    cleanup(&ffp, &is);

    /* Normal EOF still completes once, and ordinary auto-loop still seeks. */
    setup(&ffp, &is, ic);
    latch = done = errors = completions = 0; is.eof = 1;
    assert(terminal_iteration(&ffp, latch, &done) == 1 && done);
    notifications(&ffp, &errors, &completions);
    assert(!errors && completions == 1 && is.pause_req && !is.buffering_on);
    is.seek_req = 1;
    run_seek(&ffp, &latch, &done, AVERROR(EIO));
    assert(!done && !latch && is.buffering_on); /* ordinary seek policy unchanged */
    cleanup(&ffp, &is);
    setup(&ffp, &is, ic);
    latch = done = errors = completions = 0; is.eof = 1; ffp.loop = 2;
    assert(terminal_iteration(&ffp, latch, &done) == 1 && !done);
    notifications(&ffp, &errors, &completions);
    assert(!errors && !completions && is.seek_req && ffp.loop == 1);
    cleanup(&ffp, &is);

    /* Existing autoexit receives the terminal error after buffering releases. */
    setup(&ffp, &is, ic);
    latch = ffp.error = AVERROR(ENOSYS); done = 0; is.eof = 1; ffp.autoexit = 1;
    ffp_toggle_buffering(&ffp, 1);
    assert(terminal_iteration(&ffp, latch, &done) == AVERROR(ENOSYS));
    assert(!is.buffering_on && !is.paused && !is.pause_req);
    cleanup(&ffp, &is);
    avformat_free_context(ic);
    puts("PASS exact production seek/terminal blocks: repeated post-notification failed seek cannot rearm the same ENOSYS/EXIT error; normal EOF/auto-loop; failed-terminal-seek buffering release, drain before one error, no rereads, user pause/step, successful seek generations and ordinary buffering, autoexit error");
    return 0;
}
'''.replace("@ROOT@", str(root)).replace("@SEEK@", seek).replace("@TERMINAL@", terminal)
(out / "test_terminal_seek.c").write_text(code)
runner = (root / "tests/ffmpeg5/run-rtsp-live.sh").read_text()
runner = runner.replace('ROOT=$(cd "$(dirname "$0")/../.." && pwd)', f'ROOT="{root}"')
runner = runner.replace('"$ROOT/tests/ffmpeg5/test_rtsp_live.c"', f'"{out}/test_terminal_seek.c"')
runner = runner.replace('"$OUTPUT/test-rtsp-live"', '"$OUTPUT/test-terminal-seek"')
(out / "run.sh").write_text(runner)
subprocess.run(["bash", str(out / "run.sh"), str(Path(a.source).resolve()),
                str(Path(a.build).resolve()), str(out)], check=True, timeout=60)
