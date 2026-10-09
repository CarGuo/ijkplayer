#!/usr/bin/env python3
"""Exercise the exact production public state/queue path without I/O threads.

Only one queue-dequeue hook is used to deterministically order a concurrent stop.
Otherwise player operations, queues, seek dispatch and buffering are production.
"""
import argparse
from pathlib import Path
import subprocess
p = argparse.ArgumentParser()
p.add_argument('source')
p.add_argument('build')
p.add_argument('output')
p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
a = p.parse_args()
root, out = a.repo.resolve(), Path(a.output).resolve()
out.mkdir(parents=True, exist_ok=True)
code = r'''
#define main upstream_rtsp_test_main
#include "@ROOT@/tests/ffmpeg5/test_rtsp_live.c"
#undef main
#include "@ROOT@/ijkmedia/ijkplayer/ijkplayer_internal.h"
static IjkMediaPlayer *stop_after_dequeue;
static int dequeue_with_stop(MessageQueue *q, AVMessage *msg, int block)
{
    int ret = msg_queue_get(q, msg, block);
    if (ret > 0 && msg->what == FFP_MSG_ERROR && stop_after_dequeue) {
        IjkMediaPlayer *mp = stop_after_dequeue;
        stop_after_dequeue = NULL;
        assert(ijkmp_stop(mp) == 0);
    }
    return ret;
}
#define msg_queue_get dequeue_with_stop
#include "@ROOT@/ijkmedia/ijkplayer/ijkplayer.c"
#undef msg_queue_get
static int state_events;
static void setup_player(IjkMediaPlayer *mp, FFPlayer *ffp, VideoState *is,
                         AVFormatContext *ic, int state)
{
    initialize(ffp, is, ic);
    memset(mp, 0, sizeof(*mp));
    assert(pthread_mutex_init(&mp->mutex, NULL) == 0);
    mp->ffplayer = ffp; mp->mp_state = state;
    state_events = 0;
    ffp->loop = 1; ffp->start_on_prepared = 1;
    msg_queue_init(&ffp->msg_queue); ffp->msg_queue.abort_request = 0;
    is->continue_read_thread = SDL_CreateCond();
}
static void cleanup_player(IjkMediaPlayer *mp)
{
    SDL_DestroyCond(mp->ffplayer->is->continue_read_thread);
    msg_queue_destroy(&mp->ffplayer->msg_queue);
    destroy(mp->ffplayer->is);
    pthread_mutex_destroy(&mp->mutex);
}
static int drain(IjkMediaPlayer *mp, int *errors, int *completions)
{
    AVMessage msg; int ret;
    while ((ret = ijkmp_get_msg(mp, &msg, 0)) > 0) {
        if (msg.what == FFP_MSG_ERROR) {
            ++*errors;
            assert(msg.arg1 == AVERROR_EXIT || msg.arg1 == AVERROR(EIO));
            /* The listener receives a terminal state, not STARTED. */
            assert(ijkmp_get_state(mp) == MP_STATE_ERROR);
            assert(!ijkmp_is_playing(mp));
        }
        if (msg.what == FFP_MSG_COMPLETED) ++*completions;
        if (msg.what == FFP_MSG_PLAYBACK_STATE_CHANGED) ++state_events;
        msg_free_res(&msg);
    }
    return ret;
}
int main(void)
{
    av_log_set_level(AV_LOG_ERROR);
    AVFormatContext *ic = avformat_alloc_context();
    ic->iformat = av_find_input_format("rtsp");
    ic->duration = AV_NOPTS_VALUE;
    IjkMediaPlayer mp; FFPlayer ffp; VideoState is;
    /* ERROR is already the state-bearing event used by both bridges. iOS
     * emits one state notification and then error-finish from that event.
     * Preserve native cardinality and ordering without a second state event. */
    setup_player(&mp, &ffp, &is, ic, MP_STATE_STARTED);
    ffp_notify_msg2(&ffp, FFP_MSG_ERROR, AVERROR_EXIT);
    ffp_notify_msg3(&ffp, FFP_MSG_BUFFERING_UPDATE, 37, 0);
    int native_errors = 0, native_states = 0, native_total = 0, ordered = 1;
    const int expected[] = {FFP_MSG_ERROR, FFP_MSG_BUFFERING_UPDATE};
    AVMessage msg;
    while (ijkmp_get_msg(&mp, &msg, 0) > 0) {
        if (native_total >= 2 || msg.what != expected[native_total]) ordered = 0;
        ++native_total;
        if (msg.what == FFP_MSG_ERROR) {
            ++native_errors;
            assert(mp.mp_state == MP_STATE_ERROR && !ijkmp_is_playing(&mp));
            assert(msg.arg1 == AVERROR_EXIT);
            assert(ijkmp_seek_to(&mp, 1000) == EIJK_INVALID_STATE);
        }
        if (msg.what == FFP_MSG_PLAYBACK_STATE_CHANGED) ++native_states;
        msg_free_res(&msg);
    }
    printf("NATIVE_CARDINALITY errors=%d state_changed=%d total=%d ordered=%d\n",
           native_errors, native_states, native_total, ordered);
    fflush(stdout);
    assert(native_errors == 1 && native_states == 0 && native_total == 2 && ordered);
    cleanup_player(&mp);
    puts("PASS one native ERROR, no extra STATE_CHANGED, unchanged event order, and ERROR state visible before listener controls");

    const int active[] = {MP_STATE_ASYNC_PREPARING, MP_STATE_PREPARED,
                          MP_STATE_STARTED, MP_STATE_PAUSED, MP_STATE_COMPLETED};
    for (unsigned i = 0; i < sizeof(active)/sizeof(active[0]); ++i) {
        int errors = 0, completions = 0;
        setup_player(&mp, &ffp, &is, ic, active[i]);
        mp.seek_req = 1; mp.seek_msec = 1000;
        ffp_notify_msg2(&ffp, FFP_MSG_ERROR, AVERROR_EXIT);
        assert(drain(&mp, &errors, &completions) == 0);
        assert(errors == 1 && completions == 0 && state_events == 0);
        assert(!mp.seek_req && !mp.seek_msec && !is.seek_req);
        assert(ijkmp_seek_to(&mp, 1000) == EIJK_INVALID_STATE);
        assert(ijkmp_start(&mp) == EIJK_INVALID_STATE);
        assert(ijkmp_pause(&mp) == EIJK_INVALID_STATE);
        assert(!is.seek_req);
        ffp_notify_msg2(&ffp, FFP_REQ_SEEK, 2000); /* queued before ERROR dispatch */
        ffp_notify_msg1(&ffp, FFP_REQ_START);
        ffp_notify_msg1(&ffp, FFP_REQ_PAUSE);
        ffp_notify_msg2(&ffp, FFP_MSG_ERROR, AVERROR_EXIT);
        ffp_notify_msg1(&ffp, FFP_MSG_COMPLETED); /* late seek-to-end result */
        ffp.start_on_prepared = 0;
        ffp_notify_msg1(&ffp, FFP_MSG_PREPARED); /* late callback cannot pause ERROR */
        assert(drain(&mp, &errors, &completions) == 0);
        assert(errors == 1 && !completions && !is.seek_req && state_events == 0);
        assert(ijkmp_get_state(&mp) == MP_STATE_ERROR && !ijkmp_is_playing(&mp));
        cleanup_player(&mp);
    }
    puts("PASS errors during preparing/prepared/started/paused/completed set ERROR before listener; pending seek clears; duplicate errors and queued controls cannot revive it");

    const int inactive[] = {MP_STATE_IDLE, MP_STATE_INITIALIZED, MP_STATE_STOPPED, MP_STATE_END};
    for (unsigned i = 0; i < sizeof(inactive)/sizeof(inactive[0]); ++i) {
        int errors = 0, completions = 0;
        setup_player(&mp, &ffp, &is, ic, inactive[i]);
        ffp_notify_msg2(&ffp, FFP_MSG_ERROR, AVERROR(EIO));
        assert(drain(&mp, &errors, &completions) == 0);
        assert(!errors && !completions && ijkmp_get_state(&mp) == inactive[i]);
        cleanup_player(&mp);
    }
    /* Exact stop in the dequeue/dispatch race preserves STOPPED and aborts delivery. */
    int errors = 0, completions = 0;
    setup_player(&mp, &ffp, &is, ic, MP_STATE_STARTED);
    ffp_notify_msg2(&ffp, FFP_MSG_ERROR, AVERROR(EIO));
    stop_after_dequeue = &mp;
    assert(drain(&mp, &errors, &completions) < 0);
    assert(!errors && !completions && mp.mp_state == MP_STATE_STOPPED);
    assert(!ijkmp_is_playing(&mp) && is.abort_request);
    cleanup_player(&mp);
    puts("PASS inactive states and error dequeued immediately before stop retain their state and suppress obsolete errors");

    /* Android reset replaces the native player; release/stop abort its queue.
     * Check real abort semantics and independent old/new queues, without JNI. */
    IjkMediaPlayer fresh; FFPlayer fresh_ffp; VideoState fresh_is;
    setup_player(&mp, &ffp, &is, ic, MP_STATE_STARTED);
    ffp_notify_msg2(&ffp, FFP_MSG_ERROR, AVERROR(EIO));
    assert(ffp_stop_l(&ffp) == 0);
    setup_player(&fresh, &fresh_ffp, &fresh_is, ic, MP_STATE_ASYNC_PREPARING);
    ffp_notify_msg1(&fresh_ffp, FFP_MSG_PREPARED);
    assert(drain(&mp, &errors, &completions) < 0 && !errors);
    assert(drain(&fresh, &errors, &completions) == 0);
    assert(fresh.mp_state == MP_STATE_PREPARED && ijkmp_is_playing(&fresh));
    assert(ijkmp_seek_to(&fresh, 3500) == 0);
    assert(drain(&fresh, &errors, &completions) == 0 && fresh_is.seek_req);
    assert(fresh_is.seek_pos == 3500000);
    cleanup_player(&mp); cleanup_player(&fresh);
    puts("PASS abort/replacement-queue isolation: queued error cannot reach reset's new player; new prepare/seek works");

    /* Ordinary EOF stays COMPLETED, is not playing, and remains seek/start capable. */
    errors = completions = 0;
    setup_player(&mp, &ffp, &is, ic, MP_STATE_STARTED);
    ffp_notify_msg1(&ffp, FFP_MSG_COMPLETED);
    assert(drain(&mp, &errors, &completions) == 0);
    assert(!errors && completions == 1 && mp.mp_state == MP_STATE_COMPLETED);
    assert(state_events == 1); /* ordinary transitions still notify */
    assert(!ijkmp_is_playing(&mp) && mp.restart && mp.restart_from_beginning);
    assert(ijkmp_seek_to(&mp, 4000) == 0);
    assert(drain(&mp, &errors, &completions) == 0 && is.seek_req);
    assert(is.seek_pos == 4000000 && !mp.restart_from_beginning);
    ffp_notify_msg3(&ffp, FFP_MSG_SEEK_COMPLETE, 4000, 0);
    assert(drain(&mp, &errors, &completions) == 0 && !mp.seek_req);
    assert(ijkmp_start(&mp) == 0);
    assert(drain(&mp, &errors, &completions) == 0);
    assert(ijkmp_is_playing(&mp) && mp.mp_state == MP_STATE_STARTED);
    assert(state_events == 2);
    cleanup_player(&mp);
    avformat_free_context(ic);
    puts("PASS normal COMPLETED-to-seek/start behavior and seek-complete bookkeeping preserved");
    return 0;
}
'''.replace('@ROOT@',str(root))
(out/'test_player_error_state.c').write_text(code)
runner = (root/'tests/ffmpeg5/run-rtsp-live.sh').read_text()
runner = runner.replace('ROOT=$(cd "$(dirname "$0")/../.." && pwd)', f'ROOT="{root}"')
runner = runner.replace('"$ROOT/tests/ffmpeg5/test_rtsp_live.c"', f'"{out}/test_player_error_state.c"')
runner = runner.replace('"$OUTPUT/test-rtsp-live"', '"$OUTPUT/test-player-error-state"')
(out/'run.sh').write_text(runner)
subprocess.run(['bash',str(out/'run.sh'),str(Path(a.source).resolve()),str(Path(a.build).resolve()),str(out)],check=True,timeout=60)
