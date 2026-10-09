/* Real FFmpeg outer option/open dispatch + production ijklivehook callbacks.
 * Inner demux I/O is deterministic, including failures, URL rewrites and epochs.
 * This is not network/device playback; run the loopback fixture separately. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "libavformat/avformat.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/application.h"

static int fixture_open(AVFormatContext **, const char *, const AVInputFormat *, AVDictionary **);
static int fixture_info(AVFormatContext *, AVDictionary **);
static int fixture_read(AVFormatContext *, AVPacket *);
static void fixture_close(AVFormatContext **);
#define avformat_open_input fixture_open
#define avformat_find_stream_info fixture_info
#define av_read_frame fixture_read
#define avformat_close_input fixture_close
#include "../../ijkmedia/ijkplayer/ijkavformat/ijklivehook.c"
#undef avformat_open_input
#undef avformat_find_stream_info
#undef av_read_frame
#undef avformat_close_input

int ijkav_register_ijklivehook_demuxer(AVInputFormat *, int);

typedef struct Spec {
    const char *url;
    const char *format;
    int64_t duration;
    AVRational time_base;
    int streams;
    enum AVCodecID audio_codec;
    int open_error;
    int coded_bits;
    int64_t bit_rate;
    unsigned codec_tag;
} Spec;
static Spec specs[16];
static int spec_count, opens, closes, hooks, checks, budget, read_error, read_eagain, reads;
static char observed_url[4096];
static AVDictionary *observed_options;
static AVInputFormat rtsp_format = { .name = "rtsp" };
static AVInputFormat other_format = { .name = "not-rtsp" };
static AVApplicationContext *app;

static int interrupted(void *opaque) { (void)opaque; return ++checks >= budget; }
static int on_event(AVApplicationContext *ctx, int type, void *obj, size_t size)
{
    (void)ctx; (void)size;
    if (type != AVAPP_CTRL_WILL_LIVE_OPEN)
        return 0;
    hooks++;
    if (opens >= spec_count)
        return AVERROR_EXIT; /* An exhausted scenario must never retry forever. */
    AVAppIOControl *ctl = obj;
    av_strlcpy(ctl->url, specs[opens].url, sizeof(ctl->url));
    return 0;
}
static int fixture_open(AVFormatContext **out, const char *url,
                        const AVInputFormat *format, AVDictionary **options)
{
    (void)format;
    assert(opens < spec_count);
    const Spec *s = &specs[opens++];
    av_strlcpy(observed_url, url, sizeof(observed_url));
    assert(!strcmp(url, s->url));
    av_dict_free(&observed_options);
    assert(av_dict_copy(&observed_options, *options, 0) >= 0);
    if (s->open_error)
        return s->open_error;
    AVFormatContext *ic = *out;
    ic->iformat = !strcmp(s->format, "rtsp") ? &rtsp_format : &other_format;
    ic->duration = s->duration;
    ic->start_time = 0;
    for (int i = 0; i < s->streams; i++) {
        AVStream *st = avformat_new_stream(ic, NULL);
        assert(st);
        st->time_base = s->time_base;
        st->start_time = 0;
        st->duration = s->duration > 0 ? av_rescale_q(s->duration, AV_TIME_BASE_Q, st->time_base) : AV_NOPTS_VALUE;
        st->codecpar->codec_type = i == 0 ? AVMEDIA_TYPE_AUDIO : AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id = i == 0 ? s->audio_codec : AV_CODEC_ID_H264;
        if (i == 0) {
            st->codecpar->bits_per_coded_sample = s->coded_bits;
            st->codecpar->bit_rate = s->bit_rate;
            st->codecpar->codec_tag = s->codec_tag;
            st->codecpar->sample_rate = 8000;
            st->codecpar->channels = 1;
            st->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
        } else {
            st->codecpar->width = st->codecpar->height = 16;
        }
    }
    return 0;
}
static int fixture_info(AVFormatContext *ic, AVDictionary **options)
{ (void)ic; (void)options; return 0; }
static int fixture_read(AVFormatContext *ic, AVPacket *pkt)
{
    (void)ic;
    reads++;
    if (read_error) { int result = read_error; read_error = 0; return result; }
    if (read_eagain) return AVERROR(EAGAIN);
    assert(av_new_packet(pkt, 16) >= 0);
    pkt->stream_index = 0;
    pkt->pts = pkt->dts = 80; pkt->duration = 20;
    pkt->flags = AV_PKT_FLAG_KEY;
    return 0;
}
static void fixture_close(AVFormatContext **ic)
{
    if (*ic) { closes++; avformat_free_context(*ic); *ic = NULL; }
}
static void reset(void)
{
    av_dict_free(&observed_options);
    memset(specs, 0, sizeof(specs));
    spec_count = opens = closes = hooks = checks = read_error = read_eagain = reads = 0;
    budget = 10000;
}
static void add_spec(const char *url, const char *format, int64_t duration,
                     int denominator, int streams)
{
    specs[spec_count++] = (Spec){ url, format, duration, { 1, denominator }, streams,
                                 AV_CODEC_ID_PCM_MULAW, 0 };
}
static int open_wrapper(AVFormatContext **ic, int stable, const char *transport, const char *timeout)
{
    *ic = avformat_alloc_context();
    (*ic)->interrupt_callback = (AVIOInterruptCB){ interrupted, NULL };
    AVDictionary *opts = NULL;
    char ptr[64]; snprintf(ptr, sizeof(ptr), "%p", (void *)app);
    av_dict_set(&opts, "ijkapplication", ptr, 0);
    if (stable >= 0) av_dict_set_int(&opts, "ijklivehook-stable-streams", stable, 0);
    if (transport) av_dict_set(&opts, "rtsp_transport", transport, 0);
    if (timeout) av_dict_set(&opts, "timeout", timeout, 0);
    int ret = avformat_open_input(ic, "ijklivehook:rtsp://original/live", NULL, &opts);
    /* The actual FFmpeg outer AVOption parser consumed every supplied option. */
    if (!ret) assert(!av_dict_count(opts));
    av_dict_free(&opts);
    return ret;
}
static void assert_option(const char *key, const char *value)
{
    AVDictionaryEntry *e = av_dict_get(observed_options, key, NULL, 0);
    assert(value ? e && !strcmp(e->value, value) : !e);
}
static void reconnect(AVFormatContext *ic)
{
    read_error = AVERROR(ETIMEDOUT);
    AVPacket pkt = { 0 };
    assert(ic->iformat->read_packet(ic, &pkt) == 0);
    assert(pkt.flags & AV_PKT_FLAG_DISCONTINUITY);
    av_packet_unref(&pkt);
}
/* Decoder-specific metadata exemptions must preserve ordinary reconnects;
 * actual initialization changes remain incompatible. */
static void codec_initialization_tests(void)
{
    AVFormatContext *a = avformat_alloc_context(), *b = avformat_alloc_context();
    AVStream *as = avformat_new_stream(a, NULL), *bs = avformat_new_stream(b, NULL);
    assert(a && b && as && bs);
    as->time_base = bs->time_base = (AVRational){1,8000};
    AVCodecParameters *ap = as->codecpar, *bp = bs->codecpar;
    ap->codec_type = AVMEDIA_TYPE_AUDIO; ap->codec_id = AV_CODEC_ID_WMAV2;
    ap->bits_per_coded_sample = 4;
    assert(!avcodec_parameters_copy(bp, ap));
    assert(streams_compatible(a, b));
#define CHANGED(field, value) do { \
    __typeof__(bp->field) saved = bp->field; bp->field = value; \
    assert(!streams_compatible(a, b)); bp->field = saved; \
    assert(streams_compatible(a, b)); \
} while (0)
    CHANGED(bits_per_coded_sample, 3);
    CHANGED(bits_per_raw_sample, 16);
    CHANGED(codec_tag, 1);
    CHANGED(bit_rate, 24000);
    CHANGED(block_align, 20);
    CHANGED(frame_size, 160);
    CHANGED(initial_padding, 1);
    CHANGED(trailing_padding, 1);
    CHANGED(seek_preroll, 1);
    CHANGED(video_delay, 1);
    CHANGED(field_order, AV_FIELD_TT);
    CHANGED(color_range, AVCOL_RANGE_JPEG);
    CHANGED(color_primaries, AVCOL_PRI_BT709);
    CHANGED(color_trc, AVCOL_TRC_BT709);
    CHANGED(color_space, AVCOL_SPC_BT709);
    CHANGED(chroma_location, AVCHROMA_LOC_LEFT);
    CHANGED(sample_aspect_ratio.num, 1);
    CHANGED(sample_aspect_ratio.den, 2);
#undef CHANGED
    av_channel_layout_default(&bp->ch_layout, 2);
    assert(!streams_compatible(a, b));
    av_channel_layout_uninit(&bp->ch_layout);
    assert(streams_compatible(a, b));
    const enum AVCodecID advisory_codecs[] = {
        AV_CODEC_ID_H264, AV_CODEC_ID_HEVC, AV_CODEC_ID_AAC, AV_CODEC_ID_AAC_LATM,
        AV_CODEC_ID_PCM_ALAW, AV_CODEC_ID_PCM_MULAW, AV_CODEC_ID_PCM_S16LE,
        AV_CODEC_ID_ADPCM_G726, AV_CODEC_ID_ADPCM_G726LE
    };
    for (unsigned i = 0; i < sizeof(advisory_codecs)/sizeof(advisory_codecs[0]); i++) {
        ap->codec_id = advisory_codecs[i];
        assert(!avcodec_parameters_copy(bp, ap));
        bp->bit_rate = 64000; bp->codec_tag = MKTAG('a','v','c','1');
        bp->format = 0; bp->profile = 1; bp->level = 1; /* unknown -> known */
        bp->sample_aspect_ratio = (AVRational){2,1};
        bp->color_range = AVCOL_RANGE_JPEG; bp->video_delay = 2;
        bp->frame_size = 160; bp->block_align = 160;
        assert(streams_compatible(a, b));
        bp->bit_rate = 72000; /* changed estimate, not only unknown -> known */
        assert(streams_compatible(a, b));
        if (ap->codec_id == AV_CODEC_ID_H264) {
            bp->codec_tag = MKTAG('S','M','V','2');
            assert(!streams_compatible(a, b));
        }
        if (ap->codec_id == AV_CODEC_ID_ADPCM_G726 || ap->codec_id == AV_CODEC_ID_ADPCM_G726LE) {
            bp->bits_per_coded_sample = 3;
            assert(!streams_compatible(a, b));
        }
    }
    assert(!avcodec_parameters_copy(bp, ap));
    ap->channels = 1; ap->channel_layout = AV_CH_LAYOUT_MONO;
    av_channel_layout_default(&bp->ch_layout, 1);
    assert(streams_compatible(a, b)); /* equivalent old/new layout representations */
    bs->time_base = (AVRational){1,16000};
    assert(streams_compatible(a, b)); /* rescalable time-base change stays valid */
    avformat_free_context(a); avformat_free_context(b);
}

int main(void)
{
    assert(!ijkav_register_ijklivehook_demuxer(&ijkff_ijklivehook_demuxer, sizeof(AVInputFormat)));
    assert(!av_application_open(&app, NULL));
    app->func_on_app_event = on_event;
    AVFormatContext *ic = NULL;
    AVPacket pkt = { 0 };
    av_log_set_level(AV_LOG_ERROR);

    codec_initialization_tests();

    /* A same-named unrelated demuxer is never interpreted as our Context. */
    AVInputFormat impostor = { .name = "ijklivehook" };
    AVFormatContext fake = { .iformat = &impostor, .priv_data = (void *)1 };
    assert(!ijklivehook_is_live_rtsp(&fake));
    assert(!ijklivehook_is_terminal_error(&fake, AVERROR(ENOSYS)));

    reset(); add_spec("rtsp://actual/live", "rtsp", AV_NOPTS_VALUE, 1000, 2);
    assert(!open_wrapper(&ic, 1, "tcp", "1234567"));
    assert(ijklivehook_is_live_rtsp(ic));
    assert_option("rtsp_transport", "tcp"); assert_option("timeout", "1234567");
    assert(ic->nb_streams == 2 && ic->duration == AV_NOPTS_VALUE);
    AVStream *original = ic->streams[0];
    for (int n = 0; n < 5; n++) {
        add_spec("rtsp://actual/reconnected", "rtsp", AV_NOPTS_VALUE, 2000, 2);
        reconnect(ic);
        assert(ic->nb_streams == 2 && ic->streams[0] == original);
        assert(ic->streams[0]->time_base.den == 1000 && ijklivehook_is_live_rtsp(ic));
    }
    assert(!ic->iformat->read_packet(ic, &pkt));
    assert(pkt.pts == 40 && pkt.dts == 40 && pkt.duration == 10);
    assert(!(pkt.flags & AV_PKT_FLAG_DISCONTINUITY)); av_packet_unref(&pkt);
    assert_option("timeout", "1234567"); assert_option("rtsp_transport", "tcp");
    add_spec("rtsp://actual/recording", "rtsp", 60 * AV_TIME_BASE, 1000, 2);
    reconnect(ic); assert(!ijklivehook_is_live_rtsp(ic));
    add_spec("https://actual/media", "not-rtsp", AV_NOPTS_VALUE, 1000, 2);
    reconnect(ic); assert(!ijklivehook_is_live_rtsp(ic));
    assert_option("timeout", NULL); assert_option("rtsp_transport", NULL);
    add_spec("rtsp://actual/live-again", "rtsp", AV_NOPTS_VALUE, 1000, 2);
    reconnect(ic); assert(ijklivehook_is_live_rtsp(ic));
    assert_option("timeout", "1234567"); assert_option("rtsp_transport", "tcp");
    avformat_close_input(&ic); assert(closes == opens);

    reset(); add_spec("rtsp://actual/recording", "rtsp", 60 * AV_TIME_BASE, 1000, 2);
    assert(!open_wrapper(&ic, 1, "udp", "0"));
    assert(!ijklivehook_is_live_rtsp(ic) && ic->duration == 60 * AV_TIME_BASE);
    assert_option("rtsp_transport", "udp"); assert_option("timeout", "0");
    avformat_close_input(&ic);

    reset(); add_spec("rtsp://rewritten/non-rtsp", "not-rtsp", AV_NOPTS_VALUE, 1000, 2);
    assert(!open_wrapper(&ic, 1, NULL, NULL));
    assert(!ijklivehook_is_live_rtsp(ic)); /* actual format wins over URL */
    assert_option("rtsp_transport", NULL); assert_option("timeout", NULL);
    avformat_close_input(&ic);

    /* No cap: keep legacy stream/reconnect behavior; no capability is granted. */
    reset(); add_spec("rtsp://actual/live", "rtsp", AV_NOPTS_VALUE, 1000, 2);
    assert(!open_wrapper(&ic, -1, NULL, NULL));
    assert(!ijklivehook_is_live_rtsp(ic));
    add_spec("rtsp://actual/retry", "rtsp", AV_NOPTS_VALUE, 1000, 2);
    reconnect(ic); assert(ic->nb_streams == 4);
    avformat_close_input(&ic);

    for (int codec_change = 0; codec_change < 2; codec_change++) {
        reset(); add_spec("rtsp://actual/live", "rtsp", AV_NOPTS_VALUE, 1000, 2);
        assert(!open_wrapper(&ic, 1, NULL, NULL));
        add_spec("rtsp://actual/changed", "rtsp", AV_NOPTS_VALUE, 1000, codec_change ? 2 : 1);
        if (codec_change) specs[1].audio_codec = AV_CODEC_ID_PCM_S16LE;
        read_error = AVERROR(ETIMEDOUT);
        assert(ic->iformat->read_packet(ic, &pkt) == AVERROR(ENOSYS));
        assert(ijklivehook_is_terminal_error(ic, AVERROR(ENOSYS)));
        assert(!ijklivehook_is_live_rtsp(ic) && ic->nb_streams == 2 && opens == 2);
        assert(ic->iformat->read_packet(ic, &pkt) == AVERROR(ENOSYS) && opens == 2);
        avformat_close_input(&ic);
    }

    /* A normal PCM reconnect can refine estimates/tags without configuring a
     * different decoder. This tests the actual wrapper open/reconnect path. */
    reset(); add_spec("rtsp://actual/metadata", "rtsp", AV_NOPTS_VALUE, 8000, 1);
    assert(!open_wrapper(&ic, 1, NULL, NULL));
    add_spec("rtsp://actual/refined", "rtsp", AV_NOPTS_VALUE, 8000, 1);
    specs[1].bit_rate = 64000; specs[1].codec_tag = 7; specs[1].coded_bits = 8;
    reconnect(ic);
    assert(ijklivehook_is_live_rtsp(ic) && ic->nb_streams == 1 && opens == 2);
    avformat_close_input(&ic);

    /* G726 SDP codeword width changes while codec/rate/layout are identical. */
    reset(); add_spec("rtsp://actual/g726-32", "rtsp", AV_NOPTS_VALUE, 8000, 1);
    specs[0].audio_codec = AV_CODEC_ID_ADPCM_G726LE; specs[0].coded_bits = 4;
    assert(!open_wrapper(&ic, 1, NULL, NULL));
    add_spec("rtsp://actual/g726-24", "rtsp", AV_NOPTS_VALUE, 8000, 1);
    specs[1].audio_codec = AV_CODEC_ID_ADPCM_G726LE; specs[1].coded_bits = 3;
    read_error = AVERROR(ETIMEDOUT);
    assert(ic->iformat->read_packet(ic, &pkt) == AVERROR(ENOSYS));
    assert(ijklivehook_is_terminal_error(ic, AVERROR(ENOSYS)) && !pkt.size);
    assert(ic->streams[0]->codecpar->bits_per_coded_sample == 4 && opens == 2);
    avformat_close_input(&ic);

    /* Exhausting the scripted callback is an application veto. It must remain
     * cancelled even if the old inner stream would return a packet next time. */
    reset(); add_spec("rtsp://actual/cancel", "rtsp", AV_NOPTS_VALUE, 1000, 1);
    assert(!open_wrapper(&ic, 1, NULL, NULL));
    read_error = AVERROR(ETIMEDOUT);
    assert(av_read_frame(ic, &pkt) == AVERROR_EXIT);
    assert(ijklivehook_is_terminal_error(ic, AVERROR_EXIT) && !ijklivehook_is_live_rtsp(ic));
    int cancelled_reads = reads, cancelled_hooks = hooks;
    for (int n = 0; n < 3; n++) {
        assert(av_read_frame(ic, &pkt) == AVERROR_EXIT && !pkt.size);
        assert(reads == cancelled_reads && hooks == cancelled_hooks && opens == 1);
        assert(!ijklivehook_is_live_rtsp(ic));
    }
    avformat_close_input(&ic);

    reset(); add_spec("rtsp://actual/interrupt", "rtsp", AV_NOPTS_VALUE, 1000, 1);
    assert(!open_wrapper(&ic, 1, NULL, NULL));
    budget = checks + 1;
    assert(ic->iformat->read_packet(ic, &pkt) == AVERROR_EXIT);
    budget += 100; /* Clearing a transient interrupt cannot revive the session. */
    cancelled_reads = reads;
    assert(ic->iformat->read_packet(ic, &pkt) == AVERROR_EXIT && reads == cancelled_reads);
    assert(ijklivehook_is_terminal_error(ic, AVERROR_EXIT));
    avformat_close_input(&ic);

    /* Continuous EAGAIN and failed-open loops must honor interruption. */
    reset(); add_spec("rtsp://actual/live", "rtsp", AV_NOPTS_VALUE, 1000, 2);
    assert(!open_wrapper(&ic, 1, NULL, NULL));
    budget = checks + 20; read_eagain = 1;
    assert(ic->iformat->read_packet(ic, &pkt) == AVERROR_EXIT);
    assert(!ijklivehook_is_live_rtsp(ic) && opens == 1);
    avformat_close_input(&ic);
    reset(); add_spec("rtsp://actual/fail", "rtsp", AV_NOPTS_VALUE, 1000, 2);
    specs[0].open_error = AVERROR(ECONNREFUSED);
    assert(open_wrapper(&ic, 1, NULL, NULL) == AVERROR_EXIT && !ic && opens == 1);
    av_application_closep(&app); av_dict_free(&observed_options);
    puts("PASS production ijklivehook: real outer option parser; explicit/default TCP/UDP/timeout forwarding; actual-format/duration identity; URL rewrites; repeated reconnect; stable streams and rescaled time base; bounded topology/codec-init/G726 errors; latched callback cancellation and interrupts; retry budgets; legacy cap-off control");
    return 0;
}
