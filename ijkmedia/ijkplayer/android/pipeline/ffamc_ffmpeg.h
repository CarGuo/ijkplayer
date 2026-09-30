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

#endif
