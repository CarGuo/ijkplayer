/* The production wrapper and actual pinned RTSP demuxer over loopback sockets.
 * Private Context is inspected only in this test, never by the player API. */
#include <assert.h>
#include <stdio.h>
#include "libavutil/time.h"
#include "libavformat/rtsp.h"
#include "../../ijkmedia/ijkplayer/ijkavformat/ijklivehook.c"
int ijkav_register_ijklivehook_demuxer(AVInputFormat *, int);
static int64_t deadline;
static int callbacks, retry_callbacks;
static int stop(void *unused) { (void)unused; return av_gettime_relative() >= deadline; }
static int control(AVApplicationContext *app, int type, void *obj, size_t size)
{
    (void)app; (void)size;
    if (type == AVAPP_CTRL_WILL_LIVE_OPEN) {
        callbacks++;
        if (((AVAppIOControl *)obj)->retry_counter) retry_callbacks++;
        if (callbacks > 2) return AVERROR_EXIT;
    }
    return 0;
}
int main(int argc, char **argv)
{
    assert(argc == 4); /* URL transport expected-positive-duration */
    int vod = atoi(argv[3]);
    assert(!strncmp(argv[1], "rtsp://127.", 11));
    deadline = av_gettime_relative() + 8000000;
    assert(!ijkav_register_ijklivehook_demuxer(&ijkff_ijklivehook_demuxer, sizeof(AVInputFormat)));
    AVApplicationContext *app = NULL;
    assert(!av_application_open(&app, NULL)); app->func_on_app_event = control;
    AVFormatContext *ic = avformat_alloc_context();
    ic->interrupt_callback = (AVIOInterruptCB){stop, NULL};
    AVDictionary *opts = NULL;
    char ptr[64], url[4096]; snprintf(ptr, sizeof(ptr), "%p", (void *)app);
    snprintf(url, sizeof(url), "ijklivehook:%s", argv[1]);
    av_dict_set(&opts, "ijkapplication", ptr, 0);
    av_dict_set(&opts, "rtsp_transport", argv[2], 0);
    av_dict_set(&opts, "timeout", "300000", 0);
    av_dict_set_int(&opts, "ijklivehook-stable-streams", 1, 0);
    av_dict_set_int(&opts, "probesize", 2048, 0);
    av_dict_set_int(&opts, "analyzeduration", 100000, 0);
    assert(!avformat_open_input(&ic, url, NULL, &opts));
    Context *c = ic->priv_data;
    int64_t effective_timeout;
    assert(!av_opt_get_int(c->inner->priv_data, "timeout", 0, &effective_timeout));
    /* FFmpeg consumes/clears its allowed-transport mask during SETUP, so use
     * the negotiated transport and separately assert the wire SETUP in Python. */
    enum RTSPLowerTransport effective_transport = ((RTSPState *)c->inner->priv_data)->lower_transport;
    assert(effective_timeout == 300000);
    assert(effective_transport == (!strcmp(argv[2], "tcp") ? RTSP_LOWER_TRANSPORT_TCP : RTSP_LOWER_TRANSPORT_UDP));
    assert(!!ijklivehook_is_live_rtsp(ic) == !vod);
    assert((ic->duration > 0) == vod);
    AVPacket pkt = {0}; int packets = 0, discontinuities = 0, ret;
    unsigned original_streams = ic->nb_streams;
    while ((ret = av_read_frame(ic, &pkt)) >= 0) {
        packets++;
        if (pkt.flags & AV_PKT_FLAG_DISCONTINUITY) discontinuities++;
        assert(ic->nb_streams == original_streams);
        av_packet_unref(&pkt);
    }
    assert(ret == AVERROR_EXIT && packets > 0 && retry_callbacks >= 1);
    assert(discontinuities >= 1); /* A successful timeout/reopen has delivered data. */
    printf("PASS network wrapper transport=%s timeout=%"PRId64" packets=%d reconnects=%d discontinuities=%d vod=%d streams=%u\n",
           argv[2], effective_timeout, packets, retry_callbacks, discontinuities, vod, original_streams);
    av_dict_free(&opts); avformat_close_input(&ic); av_application_closep(&app);
    return 0;
}
