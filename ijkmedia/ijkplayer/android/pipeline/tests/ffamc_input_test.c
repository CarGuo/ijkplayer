/* Both Android feed modes call this exact production state machine. */
#define FFAMC_QUEUE_TEST_LIBRARY 1
#include "ffamc_packet_queue_test.c"
#include "libavutil/intreadwrite.h"
#include "../ffamc_ffmpeg.h"

#define ALOGW(...) ((void)0)
typedef struct IJKFF_Pipenode_Opaque {
    FFPlayer *ffp;
    Decoder *decoder;
    AVCodecParameters *codecpar;
    AVBSFContext *bsfc;
    AVPacket filtered_packet;
    bool bsf_pending, acodec_flush_request, aformat_need_recreate;
    volatile bool abort;
    SDL_mutex *acodec_mutex;
    SDL_cond *acodec_cond;
    int input_packet_count;
    void *acodec, *weak_vout, *pipeline;
} IJKFF_Pipenode_Opaque;
typedef struct IJKFF_Pipenode { IJKFF_Pipenode_Opaque *opaque; } IJKFF_Pipenode;
static uint8_t flush_data;
static int codec_flushes;
static bool ffp_is_flush_packet(const AVPacket *pkt) { return pkt->data == &flush_data; }
static bool SDL_AMediaCodec_isStarted(void *codec) { (void)codec; return true; }
static void SDL_VoutAndroid_invalidateAllBuffers(void *vout) { (void)vout; }
static void SDL_AMediaCodec_flush(void *codec) { (void)codec; ++codec_flushes; }
static void ffpipeline_set_surface_need_reconfigure_l(void *pipeline, bool value)
{ (void)pipeline; assert(value); }
#include "../ffamc_input.h"

static void queue_packet(Test *t, const AVPacket *pkt, int64_t pts)
{
    int i = t->queue.nb_packets;
    assert(i < 8);
    assert(av_packet_ref(&t->queue.packets[i], pkt) == 0);
    t->queue.packets[i].pts = pts;
    t->queue.serials[i] = 1;
    ++t->queue.nb_packets;
    t->queue.serial = 1;
}

static void test_recovery(const char *path, bool threaded)
{
    Test t;
    AVFormatContext *ic = NULL;
    AVPacket key = {0}, damaged = {0};
    IJKFF_Pipenode_Opaque opaque = {0};
    IJKFF_Pipenode node = {&opaque};
    VideoState is;
    int stream;
    init(&t);
    assert(avformat_open_input(&ic, path, NULL, NULL) == 0);
    assert(avformat_find_stream_info(ic, NULL) == 0);
    stream = av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    assert(stream >= 0);
    do {
        av_packet_unref(&key);
        assert(av_read_frame(ic, &key) == 0);
    } while (key.stream_index != stream || !(key.flags & AV_PKT_FLAG_KEY));
    assert(av_packet_ref(&damaged, &key) == 0);
    assert(av_packet_make_writable(&damaged) == 0);
    AV_WB32(damaged.data, INT_MAX);

    is.video_st = ic->streams[stream];
    t.player.is = &is;
    opaque.ffp = &t.player;
    opaque.decoder = &t.decoder;
    opaque.codecpar = ic->streams[stream]->codecpar;
    opaque.acodec_mutex = &t.mutex;
    opaque.acodec_cond = &t.cond;
    assert(ijk_amc_create_bsf(&opaque.bsfc, opaque.codecpar, is.video_st->time_base) == 0);
    queue_packet(&t, &key, 100);
    queue_packet(&t, &damaged, 200);
    queue_packet(&t, &key, 300);
    assert(prepare_input_packet(&node, threaded) == 0);
    assert(t.decoder.packet_pending && t.decoder.pkt_temp.pts == 100);
    // Emulate MediaCodec consuming the first packet before a midstream error.
    t.decoder.packet_pending = 0;
    assert(prepare_input_packet(&node, threaded) == 0);
    assert(t.decoder.packet_pending && t.decoder.pkt_temp.pts == 300);
    assert(t.queue.nb_packets == 0);
    assert(t.decoder.pkt.size == key.size && !memcmp(t.decoder.pkt.data, key.data, key.size));
    assert(opaque.filtered_packet.size > 4 && AV_RB32(opaque.filtered_packet.data) == 1);
    av_packet_unref(&opaque.filtered_packet);
    av_bsf_free(&opaque.bsfc);
    av_packet_unref(&damaged);
    av_packet_unref(&key);
    avformat_close_input(&ic);
    destroy(&t);
    printf("PASS shared %s feeder: first packet -> malformed packet -> valid IDR (%s)\n",
           threaded ? "threaded" : "synchronous", path);
}

static void queue_eof(Test *t, int serial)
{
    int i = t->queue.nb_packets;
    assert(i < 8);
    // The demux queue transfers its null packet without av_packet_ref(), which
    // would allocate data even for a zero-length, non-refcounted packet.
    t->queue.packets[i] = (AVPacket){ .pts = AV_NOPTS_VALUE, .dts = AV_NOPTS_VALUE };
    t->queue.serials[i] = serial;
    ++t->queue.nb_packets;
}

static void test_eof(const char *path, bool threaded, bool filtered)
{
    Test t;
    AVFormatContext *ic = NULL;
    AVPacket key = {0};
    IJKFF_Pipenode_Opaque opaque = {0};
    IJKFF_Pipenode node = {&opaque};
    VideoState is;
    int stream;
    init(&t);
    assert(avformat_open_input(&ic, path, NULL, NULL) == 0);
    assert(avformat_find_stream_info(ic, NULL) == 0);
    stream = av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    assert(stream >= 0);
    do {
        av_packet_unref(&key);
        assert(av_read_frame(ic, &key) == 0);
    } while (key.stream_index != stream || !(key.flags & AV_PKT_FLAG_KEY));
    is.video_st = ic->streams[stream];
    t.player.is = &is;
    opaque.ffp = &t.player;
    opaque.decoder = &t.decoder;
    opaque.codecpar = ic->streams[stream]->codecpar;
    opaque.acodec_mutex = &t.mutex;
    opaque.acodec_cond = &t.cond;
    if (filtered)
        assert(ijk_amc_create_bsf(&opaque.bsfc, opaque.codecpar, is.video_st->time_base) == 0);

    queue_packet(&t, &key, 100);
    queue_eof(&t, 1);
    // A sentinel after EOF makes a dropped EOF fail immediately, not hang.
    queue_packet(&t, &key, 200);
    assert(prepare_input_packet(&node, threaded) == 0);
    assert(t.decoder.pkt_temp.data && t.decoder.pkt_temp.pts == 100);
    t.decoder.packet_pending = 0;
    assert(prepare_input_packet(&node, threaded) == 0);
    assert(t.decoder.packet_pending && !t.decoder.pkt_temp.data && !t.decoder.pkt_temp.size);
    assert(t.queue.nb_packets == 1);
    // Emulate the feeder's finished transition, then seek to a new serial.
    t.decoder.packet_pending = 0;
    t.decoder.finished = t.decoder.pkt_serial;
    av_packet_move_ref(&t.queue.packets[1], &t.queue.packets[0]);
    t.queue.packets[0] = (AVPacket){ .data = &flush_data };
    t.queue.serial = t.queue.serials[0] = t.queue.serials[1] = 2;
    ++t.queue.nb_packets;
    opaque.input_packet_count = 1;
    codec_flushes = 0;
    queue_eof(&t, 2);
    assert(prepare_input_packet(&node, threaded) == 0);
    assert(t.decoder.pkt_serial == 2 && t.decoder.pkt_temp.pts == 200);
    assert(t.decoder.finished == 0 && codec_flushes == 1);
    t.decoder.packet_pending = 0;
    assert(prepare_input_packet(&node, threaded) == 0);
    assert(t.decoder.packet_pending && !t.decoder.pkt_temp.data && !t.decoder.pkt_temp.size);
    assert(!t.queue.nb_packets && t.decoder.pkt_serial == 2);

    av_packet_unref(&opaque.filtered_packet);
    av_bsf_free(&opaque.bsfc);
    av_packet_unref(&key);
    avformat_close_input(&ic);
    destroy(&t);
    printf("PASS shared %s feeder: %s EOF marker and seek-after-EOF (%s)\n",
           threaded ? "threaded" : "synchronous", filtered ? "filtered" : "unfiltered", path);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    // Also exercise shutdown helpers imported with the shared mock support.
    test_abort(0);
    test_abort(1);
    for (int i = 1; i < argc; ++i) {
        test_recovery(argv[i], false);
        test_recovery(argv[i], true);
        test_eof(argv[i], false, true);
        test_eof(argv[i], true, true);
        test_eof(argv[i], false, false);
        test_eof(argv[i], true, false);
    }
    return 0;
}
