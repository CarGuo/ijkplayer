/* Host regression coverage for the actual core implementation, not a copy. */
#include "libavcodec/avcodec.h"
static int test_send_packet(AVCodecContext *ctx, const AVPacket *pkt);
#define avcodec_send_packet test_send_packet
#include "../../ijkmedia/ijkplayer/ff_ffplay.c"
#undef avcodec_send_packet
#include <sys/stat.h>

/* Native Android/iOS timer and video-output plumbing is outside this test. */
Uint64 SDL_GetTickHR(void) { return av_gettime_relative() / 1000; }
void SDL_ProfilerReset(SDL_Profiler *p, int max_sample) { memset(p, 0, sizeof(*p)); }
float SDL_SpeedSamplerAdd(SDL_SpeedSampler *s, int log, const char *tag) { return 0; }

static int inject_eagain;
static int send_calls;
static int test_send_packet(AVCodecContext *ctx, const AVPacket *pkt)
{
    send_calls++;
    if (inject_eagain && pkt && pkt->size) {
        inject_eagain = 0;
        return AVERROR(EAGAIN);
    }
    return avcodec_send_packet(ctx, pkt);
}

static int freed_packets;
static void packet_free(void *opaque, uint8_t *data)
{
    freed_packets++;
    av_free(data);
}

static void queue_pcm(PacketQueue *q, int64_t pts, int track_free)
{
    AVPacket pkt = { 0 };
    assert(av_new_packet(&pkt, 16) == 0);
    for (int i = 0; i < 16; i++) pkt.data[i] = i;
    pkt.pts = pts;
    pkt.duration = 8;
    if (track_free) {
        av_buffer_unref(&pkt.buf);
        pkt.data = av_mallocz(16 + AV_INPUT_BUFFER_PADDING_SIZE);
        pkt.buf = av_buffer_create(pkt.data, 16, packet_free, NULL, 0);
        assert(pkt.buf);
    }
    assert(packet_queue_put(q, &pkt) == 0);
}

static void test_audio_decoder(void)
{
    FFPlayer ffp = { 0 };
    PacketQueue q;
    Decoder d;
    AVFrame *frame = av_frame_alloc();
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_PCM_S16LE);
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    assert(ctx && frame);
    ctx->sample_rate = 48000;
    ctx->channels = 1;
    ctx->channel_layout = AV_CH_LAYOUT_MONO;
    ctx->pkt_timebase = (AVRational){ 1, 1000 };
    assert(avcodec_open2(ctx, codec, NULL) == 0);
    assert(packet_queue_init(&q) == 0);
    packet_queue_start(&q);
    decoder_init(&d, ctx, &q, q.cond);
    d.pkt_serial = -1;
    queue_pcm(&q, 0, 1);
    assert(packet_queue_put(&q, &flush_pkt) == 0);
    queue_pcm(&q, 125, 0);
    queue_pcm(&q, AV_NOPTS_VALUE, 0);
    assert(packet_queue_put_nullpacket(&q, 0) == 0);
    inject_eagain = 1;
    send_calls = 0;
    assert(decoder_decode_frame(&ffp, &d, frame, NULL) == 1);
    assert(freed_packets == 1); /* old seek serial was discarded and released */
    assert(send_calls == 2 && !d.packet_pending);
    assert(frame->pts == 6000 && frame->nb_samples == 8);
    assert(frame->data[0][0] == 0 && frame->data[0][15] == 15);
    av_frame_unref(frame);
    assert(decoder_decode_frame(&ffp, &d, frame, NULL) == 1);
    assert(frame->pts == 6008); /* missing PTS uses sample-accurate continuation */
    av_frame_unref(frame);
    assert(decoder_decode_frame(&ffp, &d, frame, NULL) == 0);
    assert(d.finished == q.serial);
    packet_queue_flush(&q);
    assert(packet_queue_put(&q, &flush_pkt) == 0);
    queue_pcm(&q, 250, 0);
    assert(decoder_decode_frame(&ffp, &d, frame, NULL) == 1);
    assert(frame->pts == 12000 && d.finished == 0);
    packet_queue_abort(&q);
    av_frame_unref(frame);
    assert(decoder_decode_frame(&ffp, &d, frame, NULL) == -1);
    decoder_destroy(&d);
    packet_queue_destroy(&q);
    av_frame_free(&frame);
    puts("PASS audio: EAGAIN pending packet, PTS, stale serial release, EOF drain, seek, abort");
}

static void check_png(const char *path, int red)
{
    struct stat st;
    assert(stat(path, &st) == 0);
    AVPacket *pkt = av_packet_alloc();
    assert(av_new_packet(pkt, st.st_size) == 0);
    FILE *file = fopen(path, "rb");
    assert(file && fread(pkt->data, 1, pkt->size, file) == (size_t)pkt->size);
    fclose(file);
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_PNG);
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    AVFrame *frame = av_frame_alloc();
    assert(avcodec_open2(ctx, codec, NULL) == 0);
    assert(avcodec_send_packet(ctx, pkt) == 0);
    assert(avcodec_receive_frame(ctx, frame) == 0);
    assert(frame->width == 8 && frame->height == 4);
    assert(frame->format == AV_PIX_FMT_RGB24 && frame->data[0][0] == red);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    av_packet_free(&pkt);
}

static void test_snapshots(const char *directory)
{
    FFPlayer ffp = { 0 };
    VideoState is = { 0 };
    GetImgInfo info = { 0 };
    AVStream stream = { 0 };
    ffp.is = &is;
    ffp.get_img_info = &info;
    is.viddec.avctx = avcodec_alloc_context3(NULL);
    is.viddec.avctx->width = 16;
    is.viddec.avctx->height = 8;
    is.viddec.avctx->sample_aspect_ratio = (AVRational){ 1, 1 };
    stream.time_base = (AVRational){ 1, 25 };
    is.video_st = &stream;
    info.width = info.height = 8;
    info.count = 3;
    info.img_path = (char *)directory;
    AVFrame *frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_RGB24;
    frame->width = 16;
    frame->height = 8;
    assert(av_frame_get_buffer(frame, 32) == 0);
    msg_queue_init(&ffp.msg_queue);
    ffp.msg_queue.abort_request = 0;
    for (int n = 0; n < 3; n++) {
        memset(frame->data[0], 20 + n * 50, frame->linesize[0] * frame->height);
        frame->pts = n;
        int64_t time = n == 2 ? INT64_MAX : n * 100;
        assert(convert_image(&ffp, frame, time, 16, 8) == 0);
        assert(info.count == 2 - n);
        char path[1024];
        snprintf(path, sizeof(path), "%s/%"PRId64".png", directory, time);
        check_png(path, 20 + n * 50);
        AVMessage msg;
        assert(msg_queue_get(&ffp.msg_queue, &msg, 0) == 1);
        assert(msg.what == FFP_MSG_GET_IMG_STATE && msg.arg2 == (n == 2));
        msg_free_res(&msg);
    }
    info.img_path = "/missing-snapshot-directory/no-such-folder";
    info.count = 1;
    assert(convert_image(&ffp, frame, 999, 16, 8) < 0);
    assert(info.count == 1 && ffp.msg_queue.nb_messages == 0);
    av_frame_free(&frame);
    avcodec_free_context(&is.viddec.avctx);
    avcodec_free_context(&info.frame_img_codec_ctx);
    sws_freeContext(info.frame_img_convert_ctx);
    msg_queue_destroy(&ffp.msg_queue);
    puts("PASS PNG: repeated encoder use, aspect ratio, pixels, int64 names, notifications, failed output");
}

static void test_video_decoder(const char *path)
{
    AVFormatContext *ic = NULL;
    assert(avformat_open_input(&ic, path, NULL, NULL) == 0);
    assert(avformat_find_stream_info(ic, NULL) == 0);
    int stream = av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    assert(stream >= 0);
    const AVCodec *codec = avcodec_find_decoder(ic->streams[stream]->codecpar->codec_id);
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    assert(avcodec_parameters_to_context(ctx, ic->streams[stream]->codecpar) == 0);
    ctx->pkt_timebase = ic->streams[stream]->time_base;
    assert(avcodec_open2(ctx, codec, NULL) == 0);
    FFPlayer ffp = { 0 };
    ffp.decoder_reorder_pts = -1;
    PacketQueue q;
    Decoder d;
    assert(packet_queue_init(&q) == 0);
    packet_queue_start(&q);
    decoder_init(&d, ctx, &q, q.cond);
    d.pkt_serial = -1;
    AVPacket pkt = { 0 };
    int packets = 0;
    while (av_read_frame(ic, &pkt) >= 0) {
        if (pkt.stream_index == stream) {
            assert(packet_queue_put(&q, &pkt) == 0);
            memset(&pkt, 0, sizeof(pkt));
            packets++;
        } else {
            av_packet_unref(&pkt);
        }
    }
    assert(packet_queue_put_nullpacket(&q, stream) == 0);
    AVFrame *frame = av_frame_alloc();
    int frames = 0;
    int64_t last_pts = AV_NOPTS_VALUE;
    int ret;
    while ((ret = decoder_decode_frame(&ffp, &d, frame, NULL)) > 0) {
        assert(last_pts == AV_NOPTS_VALUE || frame->pts > last_pts);
        last_pts = frame->pts;
        frames++;
        av_frame_unref(frame);
    }
    assert(ret == 0 && frames == packets && frames == 12);
    assert(ctx->has_b_frames > 0 && d.finished == q.serial);
    decoder_destroy(&d);
    packet_queue_destroy(&q);
    av_frame_free(&frame);
    avformat_close_input(&ic);
    puts("PASS H.264: B-frame presentation ordering and delayed-frame EOF draining");
}

static void test_playback_clock(void)
{
    /* These predicates are a required build contract throughout the player.
     * Keep the value runtime-visible so unsafe optimization cannot turn this
     * into a compile-time exercise with a known NaN constant. */
    volatile double unavailable_clock = NAN;
    assert(isnan(unavailable_clock));
    assert(!isfinite(unavailable_clock));
    FFPlayer ffp = { 0 };
    VideoState is = { 0 };
    AVFormatContext ic = { 0 };
    AVStream audio = { 0 };
    ffp.is = &is;
    is.ic = &ic;
    ic.start_time = AV_NOPTS_VALUE;
    is.audio_st = &audio;
    is.av_sync_type = AV_SYNC_AUDIO_MASTER;
    is.audioq.serial = 3;
    init_clock(&is.audclk, &is.audioq.serial);
    is.audclk.paused = 1;

    /* Uninitialized and stale-serial clocks must use the seek target, not
     * convert NaN to an integer. Android formerly built this with fast-math. */
    is.seek_pos = 3500000;
    assert(ffp_get_current_position_l(&ffp) == 3500);
    set_clock_at(&is.audclk, NAN, is.audioq.serial, 0);
    assert(ffp_get_current_position_l(&ffp) == 3500);
    set_clock_at(&is.audclk, 5.0, is.audioq.serial - 1, 0);
    assert(ffp_get_current_position_l(&ffp) == 3500);

    /* Non-finite and finite but non-representable clocks are equally invalid.
     * 2^63 cannot be converted to int64_t even though (double)INT64_MAX rounds
     * to that same value. Exercise the boundary and multiplication overflow. */
    const double invalid[] = { INFINITY, -INFINITY, 0x1p63 / 1000.0,
                               -0x1p64 / 1000.0, 0x1p1023 };
    for (size_t n = 0; n < sizeof(invalid) / sizeof(invalid[0]); n++) {
        set_clock_at(&is.audclk, invalid[n], is.audioq.serial, 0);
        assert(ffp_get_current_position_l(&ffp) == 3500);
    }
    is.seek_pos = AV_NOPTS_VALUE;
    set_clock_at(&is.audclk, NAN, is.audioq.serial, 0);
    ffp.no_time_adjust = 1;
    assert(ffp_get_current_position_l(&ffp) == 0);
    ffp.no_time_adjust = 0;

    /* Keep established offset, truncation and unadjusted negative-PTS rules. */
    ic.start_time = 2000000;
    set_clock_at(&is.audclk, 3.125, is.audioq.serial, 0);
    assert(ffp_get_current_position_l(&ffp) == 1125);
    ffp.no_time_adjust = 1;
    assert(ffp_get_current_position_l(&ffp) == 3125);
    set_clock_at(&is.audclk, -0.125, is.audioq.serial, 0);
    assert(ffp_get_current_position_l(&ffp) == -125);
    ffp.no_time_adjust = 0;
    assert(ffp_get_current_position_l(&ffp) == 0);
    ic.start_time = -2000000;
    set_clock_at(&is.audclk, 1.125, is.audioq.serial, 0);
    assert(ffp_get_current_position_l(&ffp) == 1125);
    set_clock_at(&is.audclk, 1.250, is.audioq.serial, 0);
    assert(ffp_get_current_position_l(&ffp) == 1250);
    set_clock_at(&is.audclk, 0.250, is.audioq.serial, 0);
    assert(ffp_get_current_position_l(&ffp) == 250); /* backward seek is legal */

    /* Clock synchronization itself also relies on NaN checks surviving O3. */
    init_clock(&is.extclk, &is.extclk.serial);
    is.extclk.paused = 1;
    sync_clock_to_slave(&is.extclk, &is.audclk);
    assert(get_clock(&is.extclk) == 0.250);
    set_clock_at(&is.audclk, NAN, is.audioq.serial, 0);
    sync_clock_to_slave(&is.extclk, &is.audclk);
    assert(get_clock(&is.extclk) == 0.250);
    puts("PASS playback clock: NaN/stale serial, finite/range safety, offsets, raw negative PTS, seek and sync");
}

static void test_network_read_pause(void)
{
    VideoState is = { 0 };
    for (int buffering = 0; buffering <= 1; buffering++) {
        for (int user_pause = 0; user_pause <= 1; user_pause++) {
            for (int step = 0; step <= 1; step++) {
                is.buffering_on = buffering;
                is.pause_req = user_pause;
                is.step = step;
                /* Same rendering-state rule as stream_update_pause_l. */
                is.paused = !step && (user_pause || buffering);
                assert(stream_should_pause_read(&is) == (user_pause && !step));
            }
        }
    }
    is.pause_req = 0;
    is.step = 0;
    is.buffering_on = is.paused = 1;
    assert(!stream_should_pause_read(&is));
    puts("PASS RTSP/MMSH read pause: buffering keeps reading, user pause stops, single-step resumes");
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    flush_pkt.data = (uint8_t *)&flush_pkt;
    test_network_read_pause();
    test_playback_clock();
    test_audio_decoder();
    test_snapshots(argv[1]);
    test_video_decoder(argv[2]);
    return 0;
}
