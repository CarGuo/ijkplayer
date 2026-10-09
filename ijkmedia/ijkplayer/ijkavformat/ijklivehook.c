/*
 * Copyright (c) 2015 Bilibili
 * Copyright (c) 2015 Zhang Rui <bbcallen@gmail.com>
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

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavformat/url.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include <stdatomic.h>

#include "ijkplayer/ijkavutil/opt.h"

#include "ijkavformat.h"
#include "libavutil/application.h"

typedef struct {
    AVClass         *class;
    AVFormatContext *inner;

    AVAppIOControl   io_control;
    int              discontinuity;
    atomic_int       error;
    atomic_int       live_rtsp; /* published scalar; inner is read-thread-only */

    /* options */
    AVDictionary   *open_opts;
    char *         app_ctx_intptr;
    AVApplicationContext *app_ctx;
    char *         rtsp_timeout;
    char *         rtsp_transport;
    int            stable_streams; /* opt-in queue recovery needs immutable streams */
} Context;

static const AVClass ijklivehook_class;

int ijklivehook_is_live_rtsp(const AVFormatContext *avf)
{
    /* FFmpeg registers a copy of the AVInputFormat; its class identity survives
     * that copy. A matching name alone is not enough to access private storage. */
    if (!avf || !avf->iformat || avf->iformat->priv_class != &ijklivehook_class ||
        !avf->priv_data)
        return 0;
    const Context *c = avf->priv_data;
    return atomic_load_explicit(&c->live_rtsp, memory_order_acquire);
}

int ijklivehook_is_terminal_error(const AVFormatContext *avf, int error)
{
    if (!avf || !avf->iformat || avf->iformat->priv_class != &ijklivehook_class ||
        !avf->priv_data)
        return 0;
    const Context *c = avf->priv_data;
    return c->stable_streams && error < 0 &&
        error == atomic_load_explicit(&c->error, memory_order_acquire);
}

static int ijkurlhook_call_inject(AVFormatContext *h)
{
    Context *c = h->priv_data;
    int ret = 0;

    if (ff_check_interrupt(&h->interrupt_callback)) {
        ret = AVERROR_EXIT;
        goto fail;
    }

    if (c->app_ctx) {
        av_log(h, AV_LOG_INFO, "livehook %s\n", c->io_control.url);
        c->io_control.is_handled = 0;
        ret = av_application_on_io_control(c->app_ctx, AVAPP_CTRL_WILL_LIVE_OPEN, &c->io_control);
        if (ret || !c->io_control.url[0]) {
            ret = AVERROR_EXIT;
            goto fail;
        }
    }

    if (ff_check_interrupt(&h->interrupt_callback)) {
        ret = AVERROR_EXIT;
        goto fail;
    }

fail:
    return ret;
}

static int ijklivehook_probe(const AVProbeData *probe)
{
    if (av_strstart(probe->filename, "ijklivehook:", NULL))
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int ijklivehook_read_close(AVFormatContext *avf)
{
    Context *c = avf->priv_data;

    atomic_store_explicit(&c->live_rtsp, 0, memory_order_release);
    avformat_close_input(&c->inner);
    av_dict_free(&c->open_opts);
    return 0;
}

// FIXME: install libavformat/internal.h
int ff_alloc_extradata(AVCodecParameters *par, int size);

static int copy_stream_props(AVStream *st, AVStream *source_st)
{
    int ret;

    if (st->codecpar->codec_id || !source_st->codecpar->codec_id) {
        if (st->codecpar->extradata_size < source_st->codecpar->extradata_size) {
            if (st->codecpar->extradata) {
                av_freep(&st->codecpar->extradata);
                st->codecpar->extradata_size = 0;
            }
            ret = ff_alloc_extradata(st->codecpar,
                                     source_st->codecpar->extradata_size);
            if (ret < 0)
                return ret;
        }
        memcpy(st->codecpar->extradata, source_st->codecpar->extradata,
               source_st->codecpar->extradata_size);
        return 0;
    }
    if ((ret = avcodec_parameters_copy(st->codecpar, source_st->codecpar)) < 0)
        return ret;
    st->r_frame_rate        = source_st->r_frame_rate;
    st->avg_frame_rate      = source_st->avg_frame_rate;
    st->time_base           = source_st->time_base;
    st->sample_aspect_ratio = source_st->sample_aspect_ratio;

    av_dict_copy(&st->metadata, source_st->metadata, 0);
    return 0;
}

/* These decoder contracts were checked against the pinned FFmpeg source:
 * H264/HEVC/AAC derive sample/frame details from the bitstream; the listed PCM
 * formats derive them from codec ID; G726 derives codeword width from codecpar.
 * Other codecs may use bitrate/tag/block sizing as actual initialization data
 * (e.g. WMA/iLBC/rawvideo), so do not extend these exemptions by analogy. */
static int codec_has_advisory_metadata(enum AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_H264:
    case AV_CODEC_ID_HEVC:
    case AV_CODEC_ID_AAC:
    case AV_CODEC_ID_AAC_LATM:
    case AV_CODEC_ID_PCM_ALAW:
    case AV_CODEC_ID_PCM_MULAW:
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_ADPCM_G726:
    case AV_CODEC_ID_ADPCM_G726LE:
        return 1;
    default:
        return 0;
    }
}

/* Match avcodec_parameters_to_context's old/new layout precedence instead of
 * rejecting an equivalent layout merely represented with newer API fields. */
static const AVChannelLayout *decoder_channel_layout(const AVCodecParameters *p,
                                                     AVChannelLayout *legacy)
{
    if ((p->channels && p->channels != p->ch_layout.nb_channels) ||
        (p->channel_layout && (p->ch_layout.order != AV_CHANNEL_ORDER_NATIVE ||
                              p->ch_layout.u.mask != p->channel_layout))) {
        if (p->channel_layout)
            av_channel_layout_from_mask(legacy, p->channel_layout);
        else {
            legacy->order = AV_CHANNEL_ORDER_UNSPEC;
            legacy->nb_channels = p->channels;
        }
        return legacy;
    }
    return &p->ch_layout;
}

static int codec_parameters_compatible(const AVCodecParameters *a,
                                      const AVCodecParameters *b)
{
    int advisory = codec_has_advisory_metadata(a->codec_id);
    AVChannelLayout old_layout = {0}, new_layout = {0};
    if (a->codec_type != b->codec_type || a->codec_id != b->codec_id ||
        a->width != b->width || a->height != b->height ||
        a->sample_rate != b->sample_rate ||
        av_channel_layout_compare(decoder_channel_layout(a, &old_layout),
                                  decoder_channel_layout(b, &new_layout)) != 0 ||
        a->initial_padding != b->initial_padding ||
        a->trailing_padding != b->trailing_padding || a->seek_preroll != b->seek_preroll ||
        a->extradata_size != b->extradata_size ||
        (a->extradata_size && memcmp(a->extradata, b->extradata, a->extradata_size)))
        return 0;
    if ((a->format != b->format && (!advisory || (a->format >= 0 && b->format >= 0))) ||
        (a->profile != b->profile && (!advisory ||
          (a->profile != FF_PROFILE_UNKNOWN && b->profile != FF_PROFILE_UNKNOWN))) ||
        (a->level != b->level && (!advisory ||
          (a->level != FF_LEVEL_UNKNOWN && b->level != FF_LEVEL_UNKNOWN))))
        return 0;
    if (advisory) {
        /* h264_ps.c has an SMV2-specific SPS interpretation. Ordinary container
         * tags and bitrate estimates do not configure these other decoders. */
        if (a->codec_id == AV_CODEC_ID_H264 && a->codec_tag != b->codec_tag &&
            (a->codec_tag == MKTAG('S','M','V','2') || b->codec_tag == MKTAG('S','M','V','2')))
            return 0;
        if ((a->codec_id == AV_CODEC_ID_ADPCM_G726 || a->codec_id == AV_CODEC_ID_ADPCM_G726LE) &&
            a->bits_per_coded_sample != b->bits_per_coded_sample)
            return 0;
        return 1;
    }
    /* No advisory exemption was established for this codec. Preserve all
     * remaining initialization data rather than assume it is only metadata. */
    return a->codec_tag == b->codec_tag && a->bit_rate == b->bit_rate &&
        a->bits_per_coded_sample == b->bits_per_coded_sample &&
        a->bits_per_raw_sample == b->bits_per_raw_sample &&
        a->block_align == b->block_align && a->frame_size == b->frame_size &&
        a->sample_aspect_ratio.num == b->sample_aspect_ratio.num &&
        a->sample_aspect_ratio.den == b->sample_aspect_ratio.den &&
        a->field_order == b->field_order && a->color_range == b->color_range &&
        a->color_primaries == b->color_primaries && a->color_trc == b->color_trc &&
        a->color_space == b->color_space && a->chroma_location == b->chroma_location &&
        a->video_delay == b->video_delay;
}

/* The player owns decoders and stream pointers after the first open. Never
 * mutate those objects under decoder threads. A new time base is harmless:
 * packets are rescaled below; incompatible codec state requires player reopen. */
static int streams_compatible(const AVFormatContext *outer, const AVFormatContext *inner)
{
    if (outer->nb_streams != inner->nb_streams)
        return 0;
    for (unsigned i = 0; i < inner->nb_streams; i++) {
        AVRational tb = inner->streams[i]->time_base;
        if (tb.num <= 0 || tb.den <= 0 ||
            !codec_parameters_compatible(outer->streams[i]->codecpar,
                                         inner->streams[i]->codecpar))
            return 0;
    }
    return 1;
}

static int open_inner(AVFormatContext *avf)
{
    Context         *c          = avf->priv_data;
    AVDictionary    *tmp_opts   = NULL;
    AVFormatContext *new_avf    = NULL;
    int ret = -1;
    int i   = 0;
    AVDictionaryEntry *t = NULL;
    int fps_flag = 0;

    atomic_store_explicit(&c->live_rtsp, 0, memory_order_release);
    new_avf = avformat_alloc_context();
    if (!new_avf) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (c->open_opts)
        av_dict_copy(&tmp_opts, c->open_opts, 0);

    /* These names have different meanings in other protocols (notably RTMP).
     * Evaluate the post-callback URL again on every reconnect. NULL preserves
     * FFmpeg's own defaults instead of installing a wrapper-specific default. */
    if (av_stristart(c->io_control.url, "rtsp:", NULL) ||
        av_stristart(c->io_control.url, "rtsps:", NULL)) {
        if (c->rtsp_timeout &&
            (ret = av_dict_set(&tmp_opts, "timeout", c->rtsp_timeout, 0)) < 0)
            goto fail;
        if (c->rtsp_transport &&
            (ret = av_dict_set(&tmp_opts, "rtsp_transport", c->rtsp_transport, 0)) < 0)
            goto fail;
    }

    av_dict_set_int(&tmp_opts, "probesize",         avf->probesize, 0);
    av_dict_set_int(&tmp_opts, "formatprobesize",   avf->format_probesize, 0);
    av_dict_set_int(&tmp_opts, "analyzeduration",   avf->max_analyze_duration, 0);
    av_dict_set_int(&tmp_opts, "fpsprobesize",      avf->fps_probe_size, 0);
    av_dict_set_int(&tmp_opts, "max_ts_probe",      avf->max_ts_probe, 0);

    t = av_dict_get(tmp_opts, "skip-calc-frame-rate", NULL, AV_DICT_MATCH_CASE);
    if (t) {
        fps_flag = (int) strtol(t->value, NULL, 10);
        if (fps_flag > 0) {
            av_dict_set_int(&new_avf->metadata, "skip-calc-frame-rate", fps_flag, 0);
        }
    }

    new_avf->interrupt_callback = avf->interrupt_callback;
    ret = avformat_open_input(&new_avf, c->io_control.url, NULL, &tmp_opts);
    if (ret < 0)
        goto fail;

    ret = avformat_find_stream_info(new_avf, NULL);
    if (ret < 0)
        goto fail;

    if (c->stable_streams && avf->nb_streams) {
        if (!streams_compatible(avf, new_avf)) {
            av_log(avf, AV_LOG_ERROR, "livehook stream layout/codec changed; reopen the player\n");
            c->error = ret = AVERROR(ENOSYS);
            goto fail;
        }
    } else for (i = 0; i < new_avf->nb_streams; i++) {
        AVStream *st = avformat_new_stream(avf, NULL);
        if (!st) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = copy_stream_props(st, new_avf->streams[i]);
        if (ret < 0)
            goto fail;
        if (c->stable_streams) {
            st->start_time = new_avf->streams[i]->start_time;
            st->duration = new_avf->streams[i]->duration;
        }
    }

    int initial_open = !c->inner;
    avformat_close_input(&c->inner);
    c->inner = new_avf;
    if (c->stable_streams) {
        /* Only publish mutable duration through the atomic capability. The
         * outer duration/start_time are copied once, before player publication. */
        if (initial_open) {
            avf->duration = new_avf->duration;
            avf->start_time = new_avf->start_time;
        }
        atomic_store_explicit(&c->live_rtsp,
            new_avf->iformat && !strcmp(new_avf->iformat->name, "rtsp") &&
            new_avf->duration <= 0, memory_order_release);
    }
    new_avf = NULL;
    ret = 0;
fail:
    av_dict_free(&tmp_opts);
    avformat_close_input(&new_avf);
    return ret;
}

static int ijklivehook_read_header_impl(AVFormatContext *avf, AVDictionary **options)
{
    Context    *c           = avf->priv_data;
    const char *inner_url   = NULL;
    int         ret         = -1;

    atomic_init(&c->live_rtsp, 0);
    atomic_init(&c->error, 0);
    c->app_ctx = (AVApplicationContext *)av_dict_strtoptr(c->app_ctx_intptr);
    if (!avf->url || !av_strstart(avf->url, "ijklivehook:", &inner_url) || !*inner_url)
        return AVERROR(EINVAL);

    c->io_control.size = sizeof(c->io_control);
    strlcpy(c->io_control.url, inner_url, sizeof(c->io_control.url));

    if (av_stristart(c->io_control.url, "rtmp", NULL)) {
        // There is total different meaning for 'timeout' option in rtmp
        av_log(avf, AV_LOG_WARNING, "remove 'timeout' option for rtmp.\n");
        if (options)
            av_dict_set(options, "timeout", NULL, 0);
    }

    if (options)
        av_dict_copy(&c->open_opts, *options, 0);

    c->io_control.retry_counter = 0;
    ret = ijkurlhook_call_inject(avf);
    if (ret) {
        ret = AVERROR_EXIT;
        goto fail;
    }
    ret = open_inner(avf);
    while (ret < 0) {
        if (c->error)
            goto fail;
        // no EOF in live mode
        switch (ret) {
            case AVERROR_EXIT:
                goto fail;
        }

        c->io_control.retry_counter++;
        ret = ijkurlhook_call_inject(avf);
        if (ret) {
            ret = AVERROR_EXIT;
            goto fail;
        }

        c->discontinuity = 1;
        ret = open_inner(avf);
    }

    return 0;
fail:
    return ret;
}

static int ijklivehook_read_packet(AVFormatContext *avf, AVPacket *pkt)
{
    Context *c   = avf->priv_data;
    int      ret = -1;

    if (c->error)
        return c->error;
    if (ff_check_interrupt(&avf->interrupt_callback)) {
        atomic_store_explicit(&c->live_rtsp, 0, memory_order_release);
        c->error = AVERROR_EXIT;
        return AVERROR_EXIT;
    }

    if (c->inner)
        ret = av_read_frame(c->inner, pkt);

    c->io_control.retry_counter = 0;
    while (ret < 0) {
        atomic_store_explicit(&c->live_rtsp, 0, memory_order_release);
        if (c->error) {
            ret = c->error;
            goto fail;
        }
        if (ff_check_interrupt(&avf->interrupt_callback)) {
            ret = AVERROR_EXIT;
            goto fail;
        }
        if (c->inner && c->inner->pb && c->inner->pb->error && avf->pb)
            avf->pb->error = c->inner->pb->error;

        // no EOF in live mode
        switch (ret) {
            case AVERROR_EXIT:
                c->error = AVERROR_EXIT;
                goto fail;
            case AVERROR(EAGAIN):
                goto continue_read;
        }

        c->io_control.retry_counter++;
        ret = ijkurlhook_call_inject(avf);
        if (ret) {
            ret = AVERROR_EXIT;
            goto fail;
        }

        c->discontinuity = 1;
        ret = open_inner(avf);
        if (ret)
            continue;

continue_read:
        ret = av_read_frame(c->inner, pkt);
    }

    if (c->stable_streams) {
        if (pkt->stream_index < 0 || pkt->stream_index >= avf->nb_streams ||
            pkt->stream_index >= c->inner->nb_streams) {
            av_packet_unref(pkt);
            return AVERROR_INVALIDDATA;
        }
        av_packet_rescale_ts(pkt, c->inner->streams[pkt->stream_index]->time_base,
                            avf->streams[pkt->stream_index]->time_base);
        atomic_store_explicit(&c->live_rtsp,
            c->inner->iformat && !strcmp(c->inner->iformat->name, "rtsp") &&
            c->inner->duration <= 0, memory_order_release);
    }
    if (c->discontinuity) {
        pkt->flags |= AV_PKT_FLAG_DISCONTINUITY;
        c->discontinuity = 0;
    }

    return 0;
fail:
    /* A callback veto or an interrupt ends this wrapper session just like an
     * inner demuxer AVERROR_EXIT. Never revive the previous inner on a later
     * read after the application has cancelled a reconnect. */
    if (ret == AVERROR_EXIT)
        c->error = ret;
    atomic_store_explicit(&c->live_rtsp, 0, memory_order_release);
    return ret;
}

static int ijklivehook_read_header(AVFormatContext *avf)
{
    return ijklivehook_read_header_impl(avf, NULL);
}

#define OFFSET(x) offsetof(Context, x)
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "ijkapplication", "AVApplicationContext", OFFSET(app_ctx_intptr), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, .flags = D },
    { "timeout", "forward explicit RTSP socket timeout (microseconds)", OFFSET(rtsp_timeout), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, .flags = D },
    { "rtsp_transport", "forward explicit RTSP transport", OFFSET(rtsp_transport), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, .flags = D },
    { "ijklivehook-stable-streams", "internal opt-in live-buffer stream safety", OFFSET(stable_streams), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, .flags = D },
    { NULL }
};

#undef D
#undef OFFSET

static const AVClass ijklivehook_class = {
    .class_name = "LiveHook demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ijkff_ijklivehook_demuxer = {
    .name           = "ijklivehook",
    .long_name      = "Live Hook Controller",
    .flags          = AVFMT_NOFILE | AVFMT_TS_DISCONT,
    .priv_data_size = sizeof(Context),
    .read_probe     = ijklivehook_probe,
    .read_header    = ijklivehook_read_header,
    .read_packet    = ijklivehook_read_packet,
    .read_close     = ijklivehook_read_close,
    .priv_class     = &ijklivehook_class,
};
