/*
 * FFmpeg packet helpers for the Android MediaCodec pipeline.
 * Copyright (c) 2026
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef IJKPLAYER_ANDROID_FFAMC_FFMPEG_H
#define IJKPLAYER_ANDROID_FFAMC_FFMPEG_H

#include <limits.h>
#include <string.h>
#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"

/* Keep the demuxer's codec parameters in their original (avcC/hvcC) form.
 * MediaCodec's CSD comes from par_out; software fallback still needs par_in. */
static int ijk_amc_create_bsf(AVBSFContext **pbsf,
                              const AVCodecParameters *par, AVRational time_base)
{
    const char *name;
    const AVBitStreamFilter *filter;
    AVBSFContext *bsf = NULL;
    int ret;

    if (par->codec_id == AV_CODEC_ID_H264)
        name = "h264_mp4toannexb";
    else if (par->codec_id == AV_CODEC_ID_HEVC)
        name = "hevc_mp4toannexb";
    else
        return 0;

    filter = av_bsf_get_by_name(name);
    if (!filter)
        return AVERROR_BSF_NOT_FOUND;
    ret = av_bsf_alloc(filter, &bsf);
    if (ret < 0)
        return ret;
    ret = avcodec_parameters_copy(bsf->par_in, par);
    if (ret < 0)
        goto fail;
    bsf->time_base_in = time_base;
    ret = av_bsf_init(bsf);
    if (ret < 0)
        goto fail;

    av_bsf_free(pbsf);
    *pbsf = bsf;
    return 0;
fail:
    av_bsf_free(&bsf);
    return ret;
}

/* Probe the access unit without consuming or modifying the demuxer's packet.
 * A delayed decoder may legitimately return EAGAIN without producing a frame;
 * the SPS parsed by send/receive still updates its codec parameters. */
static int ijk_amc_update_h264_extradata(AVCodecParameters *par, const AVPacket *pkt)
{
    size_t extradata_size = 0;
    uint8_t *extradata = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                                &extradata_size);
    const AVCodec *codec;
    AVCodecContext *avctx = NULL;
    AVFrame *frame = NULL;
    int ret;

    if (!extradata || extradata_size < 7)
        return 0;
    if (extradata_size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR_INVALIDDATA;
    if (par->extradata_size == extradata_size &&
        !memcmp(par->extradata, extradata, extradata_size))
        return 0;

    codec = avcodec_find_decoder(par->codec_id);
    if (!codec)
        return AVERROR_DECODER_NOT_FOUND;
    avctx = avcodec_alloc_context3(codec);
    frame = av_frame_alloc();
    if (!avctx || !frame) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    ret = avcodec_parameters_to_context(avctx, par);
    if (ret < 0)
        goto end;
    av_freep(&avctx->extradata);
    avctx->extradata_size = 0;
    avctx->extradata = av_mallocz(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    memcpy(avctx->extradata, extradata, extradata_size);
    avctx->extradata_size = extradata_size;
    avctx->thread_count = 1;
    ret = avcodec_open2(avctx, codec, NULL);
    if (ret < 0)
        goto end;
    ret = avcodec_send_packet(avctx, pkt);
    if (ret < 0)
        goto end;
    do {
        ret = avcodec_receive_frame(avctx, frame);
        av_frame_unref(frame);
    } while (ret >= 0);
    if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        goto end;
    if (avctx->width <= 0 || avctx->height <= 0) {
        ret = AVERROR_INVALIDDATA;
        goto end;
    }
    ret = avcodec_parameters_from_context(par, avctx);
    if (ret >= 0)
        ret = 1;
end:
    av_frame_free(&frame);
    avcodec_free_context(&avctx);
    return ret;
}

/* Use the same non-consuming probe with HEVC's decoder/parser. This is only
 * metadata discovery for a new hvcC generation, never a software playback path. */
static int ijk_amc_update_hevc_extradata(AVCodecParameters *par, const AVPacket *pkt)
{
    size_t size = 0;
    const uint8_t *extra = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, &size);
    if (!extra)
        return 0;
    if (par->codec_id != AV_CODEC_ID_HEVC || size < 23 || extra[0] != 1)
        return AVERROR_INVALIDDATA;
    return ijk_amc_update_h264_extradata(par, pkt);
}

/* Locate Annex B NALs without copying or modifying the playback packet. */
static int ijk_amc_next_annexb_nal(const uint8_t *data, int size, int *cursor,
                                  const uint8_t **nal, int *nal_size)
{
    int i = *cursor, start = -1, end;
    for (; i + 2 < size; ++i)
        if (!data[i] && !data[i + 1] && data[i + 2] == 1) { start = i + 3; break; }
    if (start < 0)
        return 0;
    for (i = start; i + 2 < size; ++i)
        if (!data[i] && !data[i + 1] && data[i + 2] == 1)
            break;
    end = i + 2 < size ? i : size;
    *cursor = end;
    while (end > start && !data[end - 1])
        --end;
    if (end - start < 2 || (data[start] & 0x80) || !(data[start + 1] & 7))
        return AVERROR_INVALIDDATA;
    *nal = data + start;
    *nal_size = end - start;
    return 1;
}

/* Canonical CSD comes from a TEMPORARY copy of playback's validated HEVC BSF
 * parameter-set cache. Apply hvcC plus all actual filtered-AU parameter sets,
 * then ask that cache to inject its effective sets before an IRAP marker.
 * The marker is removed here: it never reaches a decoder, player or fixture. */
static int ijk_amc_hevc_effective_csd(const AVCodecParameters *par,
                                    const AVPacket *original,
                                    const AVPacket *filtered,
                                    AVCodecParameters **out)
{
    static const uint8_t marker[7] = {0, 0, 0, 1, 38, 1, 128};
    AVBSFContext *bsf = NULL;
    AVPacket input = {0}, converted = {0};
    AVCodecParameters *csd = NULL;
    const uint8_t *nal, *extra = par->extradata;
    int nal_size, cursor = 0, total = 7, pos = 0, ret;
    size_t extra_size = par->extradata_size;
    if (original) {
        size_t size = 0;
        const uint8_t *side = av_packet_get_side_data(original, AV_PKT_DATA_NEW_EXTRADATA, &size);
        if (side) { extra = side; extra_size = size; }
    }
    if (!extra || extra_size < 23 || extra_size > INT_MAX || extra[0] != 1)
        return AVERROR_INVALIDDATA;
    if (filtered && filtered->data) {
        while ((ret = ijk_amc_next_annexb_nal(filtered->data, filtered->size,
                                             &cursor, &nal, &nal_size)) > 0) {
            int type = (nal[0] >> 1) & 63;
            if (type >= 32 && type <= 34) {
                if (nal_size > INT_MAX - total - 4)
                    return AVERROR_INVALIDDATA;
                total += nal_size + 4;
            }
        }
        if (ret < 0)
            return ret;
    }
    ret = ijk_amc_create_bsf(&bsf, par, (AVRational){1, 1000000});
    if (ret < 0 || (ret = av_new_packet(&input, total)) < 0)
        goto done;
    uint8_t *side = av_packet_new_side_data(&input, AV_PKT_DATA_NEW_EXTRADATA, extra_size);
    if (!side) { ret = AVERROR(ENOMEM); goto done; }
    memcpy(side, extra, extra_size);
    side[21] = (side[21] & ~3) | 3; // temporary metadata packet uses four-byte lengths
    cursor = 0;
    if (filtered && filtered->data) {
        while ((ret = ijk_amc_next_annexb_nal(filtered->data, filtered->size,
                                             &cursor, &nal, &nal_size)) > 0) {
            int type = (nal[0] >> 1) & 63;
            if (type >= 32 && type <= 34) {
                input.data[pos++] = nal_size >> 24;
                input.data[pos++] = nal_size >> 16;
                input.data[pos++] = nal_size >> 8;
                input.data[pos++] = nal_size;
                memcpy(input.data + pos, nal, nal_size); pos += nal_size;
            }
        }
        if (ret < 0)
            goto done;
    }
    memcpy(input.data + pos, marker, 7);
    input.data[pos + 3] = 3; // length-prefixed marker input, Annex B output
    if ((ret = av_bsf_send_packet(bsf, &input)) < 0 ||
        (ret = av_bsf_receive_packet(bsf, &converted)) < 0)
        goto done;
    // Output also contains the supplied PS NALs before the injected canonical
    // cache. Request a second marker without updates to obtain ONLY the cache.
    av_packet_unref(&converted);
    if ((ret = av_new_packet(&input, 7)) < 0)
        goto done;
    memcpy(input.data, marker, 7); input.data[3] = 3;
    if ((ret = av_bsf_send_packet(bsf, &input)) < 0 ||
        (ret = av_bsf_receive_packet(bsf, &converted)) < 0)
        goto done;
    if (converted.size <= 7 || memcmp(converted.data + converted.size - 7, marker, 7)) {
        ret = AVERROR_INVALIDDATA; goto done;
    }
    // CSD is the canonical VPS/SPS/PPS set only. SEI remains byte-exact in
    // the real held playback AU; it must not trigger a reference-destroying
    // restart merely because static/encoder metadata changed.
    int ps_size = 0;
    cursor = 0;
    while ((ret = ijk_amc_next_annexb_nal(converted.data, converted.size - 7,
                                         &cursor, &nal, &nal_size)) > 0) {
        int type = (nal[0] >> 1) & 63;
        if (type >= 32 && type <= 34)
            ps_size += nal_size + 4; // bounded by converted.size
        else if (type != 39 && type != 40) {
            ret = AVERROR_INVALIDDATA; goto done;
        }
    }
    if (ret < 0 || ps_size <= 0) { ret = AVERROR_INVALIDDATA; goto done; }
    csd = avcodec_parameters_alloc();
    if (!csd) { ret = AVERROR(ENOMEM); goto done; }
    if ((ret = avcodec_parameters_copy(csd, par)) < 0)
        goto done;
    av_freep(&csd->extradata);
    csd->extradata_size = ps_size;
    csd->extradata = av_mallocz(ps_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!csd->extradata) { ret = AVERROR(ENOMEM); goto done; }
    cursor = pos = 0;
    while ((ret = ijk_amc_next_annexb_nal(converted.data, converted.size - 7,
                                         &cursor, &nal, &nal_size)) > 0) {
        int type = (nal[0] >> 1) & 63;
        if (type >= 32 && type <= 34) {
            memcpy(csd->extradata + pos, marker, 4); pos += 4;
            memcpy(csd->extradata + pos, nal, nal_size); pos += nal_size;
        }
    }
    if (ret < 0 || pos != ps_size) { ret = AVERROR_INVALIDDATA; goto done; }
    *out = csd; csd = NULL; ret = 0;
done:
    avcodec_parameters_free(&csd); av_packet_unref(&input);
    av_packet_unref(&converted); av_bsf_free(&bsf);
    return ret;
}

/* Probe the actual filtered AU against its effective Annex B CSD, then retain
 * hvcC container form in codecpar. No marker or probe picture is played. */
static int ijk_amc_hevc_generation_parameters(const AVCodecParameters *current,
                                              const AVPacket *original,
                                              const AVPacket *filtered,
                                              AVCodecParameters **next,
                                              AVCodecParameters **csd)
{
    AVCodecContext *ctx = NULL;
    AVFrame *frame = NULL;
    AVPacket packet = {0};
    AVCodecParameters *probe = NULL;
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    int ret = codec ? ijk_amc_hevc_effective_csd(current, original, filtered, csd)
                    : AVERROR_DECODER_NOT_FOUND;
    if (ret < 0)
        return ret;
    ctx = avcodec_alloc_context3(codec); frame = av_frame_alloc();
    probe = avcodec_parameters_alloc();
    if (!ctx || !frame || !probe) { ret = AVERROR(ENOMEM); goto done; }
    if ((ret = avcodec_parameters_to_context(ctx, *csd)) < 0)
        goto done;
    ctx->thread_count = 1;
    if ((ret = avcodec_open2(ctx, codec, NULL)) < 0 ||
        (ret = av_packet_ref(&packet, filtered)) < 0)
        goto done;
    av_packet_free_side_data(&packet); // effective CSD above supersedes stale hvcC side metadata
    if ((ret = avcodec_send_packet(ctx, filtered->size > 0 ? &packet : NULL)) < 0)
        goto done;
    do { ret = avcodec_receive_frame(ctx, frame); av_frame_unref(frame); } while (ret >= 0);
    if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        goto done;
    if (ctx->width <= 0 || ctx->height <= 0) { ret = AVERROR_INVALIDDATA; goto done; }
    if ((ret = avcodec_parameters_from_context(probe, ctx)) < 0)
        goto done;
    size_t size = 0;
    const uint8_t *extra = av_packet_get_side_data(original, AV_PKT_DATA_NEW_EXTRADATA, &size);
    if (!extra) { extra = current->extradata; size = current->extradata_size; }
    if (size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE) { ret = AVERROR_INVALIDDATA; goto done; }
    av_freep(&probe->extradata); probe->extradata_size = size;
    probe->extradata = av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!probe->extradata) { ret = AVERROR(ENOMEM); goto done; }
    memcpy(probe->extradata, extra, size);
    *next = probe; probe = NULL; ret = 0;
done:
    if (ret < 0) avcodec_parameters_free(csd);
    avcodec_parameters_free(&probe); av_frame_free(&frame);
    avcodec_free_context(&ctx); av_packet_unref(&packet);
    return ret;
}

#endif
