/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef IJK_FFAMC_H264_CROP_H
#define IJK_FFAMC_H264_CROP_H
#include "ffamc_h264_crop_parse.h"

/* Deliberately narrower than a general H264 reconfiguration implementation:
 * one complete source AU, one SPS/PPS pair before one IDR, progressive 8-bit
 * 4:2:0, unchanged PPS and SPS bits outside frame_cropping. Unsupported input
 * follows the existing feeder. A BSF-injected parameter set is not evidence
 * that the original source supplied the new configuration. */
static int hc_source_pair(const AVCodecParameters *par, const AVPacket *pkt,
                          HCPair *pair)
{
    size_t side_size = 0, cur = 0, len = 0;
    const uint8_t *nal;
    int sps = 0, pps = 0, vcl = 0, bad = 0, ret = 0;
    if (av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, &side_size)) return -1;
    if (!pkt->data || pkt->size <= 0) return 0;
    if (pkt->size > HC_MAX_AU) return -1;
    int length = par->extradata_size >= 7 && par->extradata[0] == 1
               ? (par->extradata[4] & 3) + 1 : 0;
    if (length == 3) return -1;
    if (length) {
        const uint8_t *e = par->extradata;
        size_t n = par->extradata_size;
        if (n < 8) return -1;
        size_t end = 8 + ((size_t)e[6] << 8 | e[7]);
        if ((e[5] & 31) != 1 || end + 3 > n || e[end] != 1) return -1;
        end += 3 + ((size_t)e[end + 1] << 8 | e[end + 2]);
        if (end > n || (end != n && (n - end != 4 || e[1] != 100 ||
            memcmp(e + end, "\xfd\xf8\xf8\0", 4)))) return -1;
    }
    memset(pair, 0, sizeof(*pair));
    while (cur < (size_t)pkt->size) {
        if (length) {
            if ((size_t)pkt->size - cur < (unsigned)length) return -1;
            len = 0;
            for (int i = 0; i < length; ++i) len = (len << 8) | pkt->data[cur++];
            if (!len || len > (size_t)pkt->size - cur) return -1;
            nal = pkt->data + cur; cur += len;
        } else {
            ret = hc_nal(pkt->data, pkt->size, &cur, &nal, &len);
            if (ret <= 0) return -1;
        }
        if (nal[0] & 0x80) return -1;
        unsigned type = nal[0] & 31;
        if (type == 7) {
            if (vcl || ++sps != 1 || hc_parse_sps(nal, len, &pair->sps)) return -1;
        } else if (type == 8) {
            if (vcl || sps != 1 || ++pps != 1 || hc_parse_pps(nal, len, &pair->pps)) return -1;
        } else if (type >= 1 && type <= 5) {
            ++vcl;
            if (type != 5 || sps != 1 || pps != 1 || vcl != 1) bad = 1;
        } else if (type != 6 && type != 9 && type != 12) bad = 1;
    }
    if (!sps && !pps) return 0;
    return !bad && sps == 1 && pps == 1 && vcl == 1 &&
           pair->pps.sps_id == pair->sps.id ? 1 : -1;
}

/* Preserve avcC framing for future length-prefixed AUs, including 1/2-byte
 * NAL lengths. Annex B remains Annex B. Never replace container codecpar with
 * the canonical Annex B CSD merely because MediaCodec wants that form. */
static int hc_replace_extradata(AVCodecParameters *par, const HCPair *pair,
                                const HCPair *old, int annexb)
{
    uint8_t bytes[HC_SPS_BYTES + HC_PPS_BYTES + 32];
    size_t size = 0;
    if (!annexb && par->extradata_size >= 7 && par->extradata[0] == 1) {
        size_t old_end = 11 + old->sps.raw_size + old->pps.raw_size;
        if (old_end > (size_t)par->extradata_size) return AVERROR_INVALIDDATA;
        size_t extension = par->extradata_size - old_end;
        if (extension != 0 && extension != 4) return AVERROR_INVALIDDATA;
        memcpy(bytes, par->extradata, 6); bytes[5] = 0xe1; size = 6;
        bytes[size++] = pair->sps.raw_size >> 8; bytes[size++] = pair->sps.raw_size;
        memcpy(bytes + size, pair->sps.raw, pair->sps.raw_size); size += pair->sps.raw_size;
        bytes[size++] = 1;
        bytes[size++] = pair->pps.raw_size >> 8; bytes[size++] = pair->pps.raw_size;
        memcpy(bytes + size, pair->pps.raw, pair->pps.raw_size); size += pair->pps.raw_size;
        memcpy(bytes + size, par->extradata + old_end, extension); size += extension;
    } else size = hc_csd(pair, bytes);
    uint8_t *extra = av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!extra) return AVERROR(ENOMEM);
    memcpy(extra, bytes, size);
    av_freep(&par->extradata); par->extradata = extra; par->extradata_size = size;
    par->width = pair->sps.visible_w; par->height = pair->sps.visible_h;
    return 0;
}

/* 1: prepared boundary; 0: unchanged; 2: unsupported, stop future admission;
 * negative: allocation/filter failure. All outputs are staged, never active.
 * The caller owns the original and filtered packets throughout this helper. */
static int ijk_amc_h264_crop_parameters(const AVCodecParameters *current,
        const AVCodecParameters *active_csd, const AVPacket *original,
        const AVPacket *filtered, AVRational time_base,
        AVCodecParameters **next, AVCodecParameters **csd, AVBSFContext **bsf)
{
    HCPair source, active;
    HCAU au;
    HCMap *map = NULL;
    AVCodecParameters *np = NULL, *cp = NULL;
    AVBSFContext *nb = NULL;
    int ret = 0, sps = 0, pps = 0;
    ret = hc_source_pair(current, original, &source);
    if (ret <= 0) return ret < 0 ? 2 : 0;
    ret = 2;
    map = av_mallocz(sizeof(*map));
    if (!map) return AVERROR(ENOMEM);
    if (hc_extra(map, active_csd->extradata, active_csd->extradata_size)) goto done;
    for (int i = 0; i < 32; ++i) sps += map->sps[i].valid;
    for (int i = 0; i < 256; ++i) pps += map->pps[i].valid;
    if (sps != 1 || pps != 1 || hc_pair(map, source.pps.id, &active)) goto done;
    int change = hc_change(&active, &source);
    if (change == 0) { ret = 0; goto done; }
    if (change < 0) goto done;
    if (hc_access_unit(map, filtered->data, filtered->size, &au) ||
        !au.idr || au.no_output_prior || hc_change(&active, &au.pair) != 1 ||
        au.pair.sps.raw_size != source.sps.raw_size ||
        memcmp(au.pair.sps.raw, source.sps.raw, source.sps.raw_size) ||
        au.pair.pps.raw_size != source.pps.raw_size ||
        memcmp(au.pair.pps.raw, source.pps.raw, source.pps.raw_size)) goto done;
    np = avcodec_parameters_alloc(); cp = avcodec_parameters_alloc();
    if (!np || !cp) { ret = AVERROR(ENOMEM); goto done; }
    if ((ret = avcodec_parameters_copy(np, current)) < 0 ||
        (ret = avcodec_parameters_copy(cp, current)) < 0 ||
        (ret = hc_replace_extradata(np, &au.pair, &active, 0)) < 0 ||
        (ret = hc_replace_extradata(cp, &au.pair, &active, 1)) < 0 ||
        (ret = ijk_amc_create_bsf(&nb, np, time_base)) < 0) goto done;
    *next = np; np = NULL; *csd = cp; cp = NULL; *bsf = nb; nb = NULL;
    ret = 1;
done:
    av_free(map); avcodec_parameters_free(&np); avcodec_parameters_free(&cp);
    av_bsf_free(&nb); return ret;
}
#endif
