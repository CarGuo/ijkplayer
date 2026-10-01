/* SPDX-License-Identifier: LGPL-2.1-or-later
 * HEVC in-band parameter updates must survive subsequent IRAPs and must not
 * leak across a discarded packet or a seek. Real decode and structural tests.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "libavformat/avformat.h"
#include "libavutil/intreadwrite.h"
#include "../ffamc_ffmpeg.h"
#ifdef NDEBUG
#error "Regression tests require assertions enabled"
#endif

typedef struct Fixture {
    AVFormatContext *ic;
    AVPacket frame, ps, pps;
} Fixture;

static void append_nal(AVPacket *pkt, const uint8_t *nal, int size, int length)
{
    int pos = pkt->size;
    assert(size > 0 && (uint64_t)size < (UINT64_C(1) << (length * 8)));
    assert(av_grow_packet(pkt, size + length) == 0);
    for (int i = 0; i < length; i++)
        pkt->data[pos + i] = size >> (8 * (length - i - 1));
    memcpy(pkt->data + pos + length, nal, size);
}

static void concatenate(AVPacket *dst, const AVPacket *a, const AVPacket *b)
{
    assert(av_new_packet(dst, a->size + b->size) == 0);
    if (a->size) memcpy(dst->data, a->data, a->size);
    if (b->size) memcpy(dst->data + a->size, b->data, b->size);
    dst->pts = 123;
    dst->dts = 117;
    dst->duration = 6;
}

static void load(Fixture *f, const char *path)
{
    assert(avformat_open_input(&f->ic, path, NULL, NULL) == 0);
    assert(avformat_find_stream_info(f->ic, NULL) >= 0);
    assert(f->ic->streams[0]->codecpar->codec_id == AV_CODEC_ID_HEVC);
    assert(av_read_frame(f->ic, &f->frame) == 0);
    const AVCodecParameters *par = f->ic->streams[0]->codecpar;
    const uint8_t *p = par->extradata + 23;
    assert(par->extradata_size > 23 && (par->extradata[21] & 3) == 3);
    for (int a = 0; a < par->extradata[22]; a++) {
        int type = *p++ & 63;
        int count = AV_RB16(p); p += 2;
        for (int n = 0; n < count; n++) {
            int size = AV_RB16(p); p += 2;
            assert(size > 2 && size <= par->extradata + par->extradata_size - p);
            if (type >= 32 && type <= 34)
                append_nal(&f->ps, p, size, 4);
            if (type == 34)
                append_nal(&f->pps, p, size, 4);
            p += size;
        }
    }
}

static AVBSFContext *create(const AVCodecParameters *par)
{
    AVBSFContext *bsf = NULL;
    assert(ijk_amc_create_bsf(&bsf, par, (AVRational){1, 5}) == 0);
    return bsf;
}

static void submit(AVBSFContext *bsf, const AVPacket *pkt)
{
    AVPacket ref = {0};
    assert(av_packet_ref(&ref, pkt) == 0);
    assert(av_bsf_send_packet(bsf, &ref) == 0);
    assert(!ref.data && !ref.buf && !ref.side_data_elems);
}

static void convert(AVBSFContext *bsf, const AVPacket *pkt, AVPacket *out)
{
    uint8_t *bytes = av_memdup(pkt->data, pkt->size);
    assert(bytes);
    submit(bsf, pkt);
    assert(av_bsf_receive_packet(bsf, out) == 0);
    assert(out->pts == pkt->pts && out->dts == pkt->dts && out->duration == pkt->duration);
    assert(!memcmp(bytes, pkt->data, pkt->size));
    av_free(bytes);
    AVPacket empty = {0};
    assert(av_bsf_receive_packet(bsf, &empty) == AVERROR(EAGAIN));
}

static unsigned decode(const AVCodecParameters *par, const AVPacket *pkt)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    AVCodecContext *c = avcodec_alloc_context3(codec);
    AVFrame *f = av_frame_alloc();
    unsigned hash = 0;
    assert(c && f && avcodec_parameters_to_context(c, par) == 0);
    c->thread_count = 1;
    assert(avcodec_open2(c, codec, NULL) == 0);
    assert(avcodec_send_packet(c, pkt) == 0);
    assert(avcodec_send_packet(c, NULL) == 0);
    assert(avcodec_receive_frame(c, f) == 0);
    for (int y = 0; y < f->height; y++)
        for (int x = 0; x < f->width; x++)
            hash = hash * 31 + f->data[0][y * f->linesize[0] + x];
    av_frame_unref(f);
    assert(avcodec_receive_frame(c, f) == AVERROR_EOF);
    av_frame_free(&f);
    avcodec_free_context(&c);
    return hash;
}

static void check_hash(const char *name, AVBSFContext *bsf, const AVPacket *pkt, unsigned expected)
{
    AVPacket out = {0};
    convert(bsf, pkt, &out);
    unsigned actual = decode(bsf->par_out, &out);
    printf("%s expected=%08x actual=%08x\n", name, expected, actual);
    fflush(stdout);
    assert(actual == expected);
    av_packet_unref(&out);
}

static void discard_output(AVBSFContext *bsf, const AVPacket *pkt)
{
    AVPacket out = {0};
    convert(bsf, pkt, &out);
    av_packet_unref(&out);
}

static void side_data(AVPacket *pkt, const AVCodecParameters *par, int length)
{
    uint8_t *p = av_packet_new_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, par->extradata_size);
    assert(p);
    memcpy(p, par->extradata, par->extradata_size);
    p[21] = (p[21] & ~3) | (length - 1);
}

static void test_side_extradata(Fixture *old, Fixture *next)
{
    const AVCodecParameters *par = old->ic->streams[0]->codecpar;
    const AVCodecParameters *next_par = next->ic->streams[0]->codecpar;
    unsigned expected = decode(next_par, &next->frame);
    unsigned initial = decode(par, &old->frame);
    AVBSFContext *bsf = create(par);
    AVPacket update = {0}, output = {0}, two = {0}, bad = {0};
    assert(av_packet_ref(&update, &next->frame) == 0);
    side_data(&update, next_par, 4);
    convert(bsf, &update, &output);
    assert(decode(bsf->par_out, &output) == expected);
    size_t size, original_size;
    const uint8_t *annexb = av_packet_get_side_data(&output, AV_PKT_DATA_NEW_EXTRADATA, &size);
    const uint8_t *hvcc = av_packet_get_side_data(&update, AV_PKT_DATA_NEW_EXTRADATA, &original_size);
    assert(annexb && size >= 4 && AV_RB32(annexb) == 1);
    assert(original_size == next_par->extradata_size && !memcmp(hvcc, next_par->extradata, original_size));
    av_packet_unref(&output);
    check_hash("NEW_EXTRADATA persists without in-band PS", bsf, &next->frame, expected);
    av_bsf_flush(bsf);
    check_hash("flush after NEW_EXTRADATA restores original CSD", bsf, &old->frame, initial);

    int pos = 0;
    while (pos < next->frame.size) {
        unsigned nal_size = AV_RB32(next->frame.data + pos); pos += 4;
        assert(nal_size <= (unsigned)(next->frame.size - pos));
        append_nal(&two, next->frame.data + pos, nal_size, 2);
        pos += nal_size;
    }
    av_packet_unref(&update);
    assert(av_packet_ref(&update, &two) == 0);
    side_data(&update, next_par, 2);
    check_hash("NEW_EXTRADATA switches NAL length", bsf, &update, expected);
    check_hash("changed NAL length persists", bsf, &two, expected);
    av_bsf_flush(bsf);
    check_hash("flush restores original NAL length", bsf, &old->frame, initial);

    assert(av_packet_ref(&bad, &update) == 0);
    assert(av_packet_make_writable(&bad) == 0);
    AV_WB16(bad.data, UINT16_MAX);
    submit(bsf, &bad);
    assert(av_bsf_receive_packet(bsf, &output) == AVERROR_INVALIDDATA);
    check_hash("invalid packet rolls back side CSD and length", bsf, &old->frame, initial);
    av_packet_unref(&bad);
    assert(av_packet_ref(&bad, &next->frame) == 0);
    side_data(&bad, next_par, 4);
    assert(av_packet_shrink_side_data(&bad, AV_PKT_DATA_NEW_EXTRADATA, 24) == 0);
    submit(bsf, &bad);
    assert(av_bsf_receive_packet(bsf, &output) == AVERROR_INVALIDDATA);
    check_hash("truncated side hvcC rolls back", bsf, &old->frame, initial);
    av_packet_unref(&bad);

    /* A side-data-only packet is configuration, not EOF. */
    side_data(&bad, next_par, 4);
    submit(bsf, &bad);
    assert(av_bsf_receive_packet(bsf, &output) == 0);
    assert(!output.size && output.side_data_elems);
    av_packet_unref(&output);
    check_hash("side-data-only packet seeds current CSD", bsf, &next->frame, expected);
    av_bsf_free(&bsf);

    /* Initial absence of hvcC can be resolved by later packet side data. */
    AVCodecParameters *empty = avcodec_parameters_alloc();
    assert(empty && avcodec_parameters_copy(empty, par) == 0);
    av_freep(&empty->extradata); empty->extradata_size = 0;
    bsf = create(empty);
    av_packet_unref(&update);
    assert(av_packet_ref(&update, &next->frame) == 0);
    side_data(&update, next_par, 4);
    check_hash("NEW_EXTRADATA initializes missing hvcC", bsf, &update, expected);
    av_bsf_flush(bsf);
    convert(bsf, &next->frame, &output);
    assert(output.size == next->frame.size && !memcmp(output.data, next->frame.data, output.size));
    avcodec_parameters_free(&empty); av_bsf_free(&bsf);
    av_packet_unref(&update); av_packet_unref(&output); av_packet_unref(&two); av_packet_unref(&bad);
    puts("PASS HEVC NEW_EXTRADATA, persistence, length change, rollback, flush and missing initial CSD");
}

static int selected(const char *selection, const char *name)
{
    return !selection || !strcmp(selection, name);
}

static void test_decode(Fixture *old, Fixture *next, const char *selection)
{
    const AVCodecParameters *par = old->ic->streams[0]->codecpar;
    unsigned old_hash = decode(par, &old->frame);
    unsigned next_hash = decode(next->ic->streams[0]->codecpar, &next->frame);
    AVPacket update = {0}, partial = {0}, suffix = {0}, damaged = {0};
    concatenate(&update, &next->ps, &next->frame);
    concatenate(&partial, &next->pps, &next->frame);
    concatenate(&suffix, &old->frame, &next->pps);
    concatenate(&damaged, &next->pps, &(AVPacket){0});
    int size = damaged.size;
    assert(av_grow_packet(&damaged, 4) == 0);
    AV_WB32(damaged.data + size, INT_MAX);

    if (selected(selection, "same-packet")) {
        AVBSFContext *bsf = create(par);
        check_hash("same-packet VPS/SPS/PPS update", bsf, &update, next_hash);
        assert(par->extradata_size == bsf->par_in->extradata_size);
        assert(!memcmp(par->extradata, bsf->par_in->extradata, par->extradata_size));
        av_bsf_free(&bsf);
    }
    if (selected(selection, "later-irap")) {
        AVBSFContext *bsf = create(par);
        discard_output(bsf, &update);
        for (int i = 0; i < 3; i++)
            check_hash("later IRAP retains updated PPS", bsf, &next->frame, next_hash);
        av_bsf_free(&bsf);
    }
    if (selected(selection, "pps-only")) {
        AVBSFContext *bsf = create(par);
        check_hash("PPS-only update retains VPS/SPS", bsf, &partial, next_hash);
        check_hash("later IRAP after PPS-only update", bsf, &next->frame, next_hash);
        av_bsf_free(&bsf);
    }
    if (selected(selection, "parameter-only")) {
        AVBSFContext *bsf = create(par);
        discard_output(bsf, &next->pps);
        check_hash("parameter-only packet then IRAP", bsf, &next->frame, next_hash);
        av_bsf_free(&bsf);
    }
    if (selected(selection, "post-irap")) {
        AVBSFContext *bsf = create(par);
        discard_output(bsf, &suffix);
        check_hash("update after previous IRAP", bsf, &next->frame, next_hash);
        av_bsf_free(&bsf);
    }
    if (selected(selection, "rollback")) {
        AVBSFContext *bsf = create(par);
        AVPacket out = {0};
        submit(bsf, &damaged);
        assert(av_bsf_receive_packet(bsf, &out) == AVERROR_INVALIDDATA);
        assert(!out.data && !out.size);
        check_hash("discarded packet does not update PPS", bsf, &old->frame, old_hash);
        check_hash("valid update after malformed packet", bsf, &update, next_hash);
        av_bsf_free(&bsf);
    }
    if (selected(selection, "flush")) {
        AVBSFContext *bsf = create(par);
        uint8_t *csd = av_memdup(bsf->par_out->extradata, bsf->par_out->extradata_size);
        int csd_size = bsf->par_out->extradata_size;
        assert(csd);
        for (int i = 0; i < 8; i++) {
            discard_output(bsf, &update);
            AVPacket blocked = {0}, out = {0};
            submit(bsf, &next->frame);
            assert(av_packet_ref(&blocked, &old->frame) == 0);
            assert(av_bsf_send_packet(bsf, &blocked) == AVERROR(EAGAIN));
            assert(blocked.data && blocked.size == old->frame.size);
            av_packet_unref(&blocked);
            av_bsf_flush(bsf); // also discards a queued old-epoch packet
            assert(av_bsf_receive_packet(bsf, &out) == AVERROR(EAGAIN));
            check_hash("seek/flush restores initial parameters", bsf, &old->frame, old_hash);
            assert(av_bsf_send_packet(bsf, NULL) == 0);
            assert(av_bsf_receive_packet(bsf, &out) == AVERROR_EOF);
            av_bsf_flush(bsf);
        }
        assert(csd_size == bsf->par_out->extradata_size);
        assert(!memcmp(csd, bsf->par_out->extradata, csd_size));
        av_free(csd);
        av_bsf_free(&bsf);
    }
    av_packet_unref(&update);
    av_packet_unref(&partial);
    av_packet_unref(&suffix);
    av_packet_unref(&damaged);
}

/* Structural fixtures are header/id probes, not decodable pictures. They test
 * preservation of all parameter-set IDs/layers and long SPS PTL prefixes. */
static void put_bits(uint8_t *data, int *pos, unsigned value, int bits)
{
    for (int i = bits - 1; i >= 0; i--, (*pos)++)
        data[*pos / 8] |= ((value >> i) & 1) << (7 - *pos % 8);
}

static void put_ue(uint8_t *data, int *pos, unsigned value)
{
    unsigned n = value + 1;
    int bits = 0;
    while (n >>= 1) bits++;
    put_bits(data, pos, 0, bits);
    put_bits(data, pos, value + 1, bits + 1);
}

static void synthetic_ps(AVPacket *pkt, int type, int id, int layer, int tag, int length, int sublayers)
{
    uint8_t rbsp[256] = {0}, nal[384] = {0};
    int bits = 0, size = 2, zeros = 0;
    if (type == 32) {
        put_bits(rbsp, &bits, id, 4);
    } else if (type == 33) {
        put_bits(rbsp, &bits, 0, 4);
        put_bits(rbsp, &bits, sublayers, 3);
        if (!layer || sublayers != 7) {
            put_bits(rbsp, &bits, 1, 1);
            for (int i = 0; i < 12; i++) put_bits(rbsp, &bits, 0, 8);
            if (sublayers) {
                for (int i = 0; i < sublayers; i++) put_bits(rbsp, &bits, 3, 2);
                put_bits(rbsp, &bits, 0, 2 * (8 - sublayers));
                for (int i = 0; i < sublayers * 12; i++) put_bits(rbsp, &bits, 0, 8);
            }
        }
        put_ue(rbsp, &bits, id);
    } else {
        put_ue(rbsp, &bits, id);
    }
    put_bits(rbsp, &bits, tag, 8);
    put_bits(rbsp, &bits, 1, 1);
    nal[0] = type << 1 | (layer >> 5);
    nal[1] = (layer & 31) << 3 | 1;
    for (int i = 0; i < (bits + 7) / 8; i++) {
        if (zeros >= 2 && rbsp[i] <= 3) {
            nal[size++] = 3;
            zeros = 0;
        }
        nal[size++] = rbsp[i];
        zeros = rbsp[i] ? 0 : zeros + 1;
    }
    append_nal(pkt, nal, size, length);
}

static AVCodecParameters *empty_hvcc(int length)
{
    AVCodecParameters *par = avcodec_parameters_alloc();
    assert(par);
    par->codec_type = AVMEDIA_TYPE_VIDEO;
    par->codec_id = AV_CODEC_ID_HEVC;
    par->extradata = av_mallocz(23 + AV_INPUT_BUFFER_PADDING_SIZE);
    assert(par->extradata);
    par->extradata_size = 23;
    par->extradata[0] = 1;
    par->extradata[21] = 0xfc | (length - 1);
    return par;
}

static void length_to_annexb(const AVPacket *pkt, AVPacket *out, int length)
{
    int pos = 0;
    while (pos < pkt->size) {
        unsigned size = 0;
        for (int i = 0; i < length; i++) size = size << 8 | pkt->data[pos++];
        int dst = out->size;
        assert(size <= (unsigned)(pkt->size - pos));
        assert(av_grow_packet(out, size + 4) == 0);
        AV_WB32(out->data + dst, 1);
        memcpy(out->data + dst + 4, pkt->data + pos, size);
        pos += size;
    }
}

static void test_structure(void)
{
    const uint8_t irap[] = {19 << 1, 1, 0x80};
    for (int length = 1; length <= 4; length *= 2) {
        AVCodecParameters *par = empty_hvcc(length);
        AVBSFContext *bsf = create(par);
        AVPacket first = {0}, update = {0}, expected = {0}, expected_annexb = {0};
        AVPacket frame = {0}, output = {0};
        synthetic_ps(&first, 32, 0, 0, 1, length, 0);
        synthetic_ps(&first, 32, 15, 0, 2, length, 0);
        synthetic_ps(&first, 33, 0, 0, 1, length, 0);
        synthetic_ps(&first, 33, 15, 0, 2, length, 6);
        synthetic_ps(&first, 33, 2, 1, 3, length, 7);
        synthetic_ps(&first, 34, 0, 0, 1, length, 0);
        synthetic_ps(&first, 34, 63, 0, 2, length, 0);
        synthetic_ps(&first, 34, 63, 1, 3, length, 0);
        discard_output(bsf, &first);
        synthetic_ps(&update, 33, 15, 0, 7, length, 6);
        synthetic_ps(&update, 34, 63, 0, 7, length, 0);
        synthetic_ps(&update, 34, 63, 0, 8, length, 0); // last wins, no growth
        discard_output(bsf, &update);
        synthetic_ps(&expected, 32, 0, 0, 1, length, 0);
        synthetic_ps(&expected, 32, 15, 0, 2, length, 0);
        synthetic_ps(&expected, 33, 0, 0, 1, length, 0);
        synthetic_ps(&expected, 33, 15, 0, 7, length, 6);
        synthetic_ps(&expected, 33, 2, 1, 3, length, 7);
        synthetic_ps(&expected, 34, 0, 0, 1, length, 0);
        synthetic_ps(&expected, 34, 63, 0, 8, length, 0);
        synthetic_ps(&expected, 34, 63, 1, 3, length, 0);
        append_nal(&expected, irap, sizeof(irap), length);
        append_nal(&frame, irap, sizeof(irap), length);
        length_to_annexb(&expected, &expected_annexb, length);
        convert(bsf, &frame, &output);
        assert(output.size == expected_annexb.size);
        assert(!memcmp(output.data, expected_annexb.data, output.size));
        av_packet_unref(&output);
        /* Invalid IDs, truncated SPS PTL and empty PS payload are rejected,
         * retaining the previous current cache. */
        for (int kind = 0; kind < 4; kind++) {
            AVPacket invalid = {0};
            if (kind == 0) synthetic_ps(&invalid, 34, 64, 0, 1, length, 0);
            else if (kind == 1) {
                uint8_t bad_sps[] = {33 << 1, 1, 1};
                append_nal(&invalid, bad_sps, sizeof(bad_sps), length);
            } else if (kind == 2) {
                uint8_t no_sps_id[15] = {33 << 1, 1, 1};
                append_nal(&invalid, no_sps_id, sizeof(no_sps_id), length);
            } else {
                uint8_t bad_pps[] = {34 << 1, 1};
                append_nal(&invalid, bad_pps, sizeof(bad_pps), length);
            }
            submit(bsf, &invalid);
            assert(av_bsf_receive_packet(bsf, &output) == AVERROR_INVALIDDATA);
            convert(bsf, &frame, &output);
            assert(output.size == expected_annexb.size);
            assert(!memcmp(output.data, expected_annexb.data, output.size));
            av_packet_unref(&invalid);
            av_packet_unref(&output);
        }
        av_bsf_flush(bsf);
        convert(bsf, &frame, &output);
        assert(output.size == 4 + sizeof(irap)); // initially empty hvcC
        av_packet_unref(&first); av_packet_unref(&update); av_packet_unref(&expected);
        av_packet_unref(&expected_annexb); av_packet_unref(&frame); av_packet_unref(&output);
        av_bsf_free(&bsf); avcodec_parameters_free(&par);
        printf("PASS %d-byte lengths, multiple PS IDs/layers, emulation prevention, six sublayers, replacements, malformed IDs and flush\n", length);
    }
}

static void test_many_sei(void)
{
    const int arrays = 8, per_array = 512, nal_size = 4;
    const int extra_size = 23 + arrays * (3 + per_array * (2 + nal_size));
    const uint8_t irap[] = {19 << 1, 1, 0x80};
    AVCodecParameters *par = empty_hvcc(4);
    AVPacket expected = {0}, expected_annexb = {0}, frame = {0}, out = {0};
    par->extradata = av_realloc(par->extradata, extra_size + AV_INPUT_BUFFER_PADDING_SIZE);
    assert(par->extradata);
    par->extradata_size = extra_size;
    par->extradata[22] = arrays;
    memset(par->extradata + extra_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    uint8_t *p = par->extradata + 23;
    for (int a = 0; a < arrays; a++) {
        int type = a & 1 ? 39 : 40; // deliberately preserve suffix/prefix order
        *p++ = type;
        AV_WB16(p, per_array); p += 2;
        for (int i = 0; i < per_array; i++) {
            int layer = i % 64;
            uint8_t nal[] = {type << 1 | (layer >> 5), (layer & 31) << 3 | 1,
                             a, i};
            AV_WB16(p, sizeof(nal)); p += 2;
            memcpy(p, nal, sizeof(nal)); p += sizeof(nal);
            append_nal(&expected, nal, sizeof(nal), 4);
        }
    }
    assert(p == par->extradata + extra_size);
    append_nal(&expected, irap, sizeof(irap), 4);
    append_nal(&frame, irap, sizeof(irap), 4);
    length_to_annexb(&expected, &expected_annexb, 4);
    AVBSFContext *bsf = create(par);
    for (int i = 0; i < 16; i++) {
        AVPacket update = {0}, ps_annexb = {0};
        convert(bsf, &frame, &out);
        assert(out.size == expected_annexb.size);
        assert(!memcmp(out.data, expected_annexb.data, out.size));
        av_packet_unref(&out);
        synthetic_ps(&update, 34, 0, 0, i, 4, 0);
        discard_output(bsf, &update);
        length_to_annexb(&update, &ps_annexb, 4);
        convert(bsf, &frame, &out);
        assert(out.size == ps_annexb.size + expected_annexb.size);
        assert(!memcmp(out.data, ps_annexb.data, ps_annexb.size));
        assert(!memcmp(out.data + ps_annexb.size, expected_annexb.data, expected_annexb.size));
        av_packet_unref(&update); av_packet_unref(&ps_annexb); av_packet_unref(&out);
        av_bsf_flush(bsf);
    }
    av_bsf_free(&bsf); avcodec_parameters_free(&par);
    av_packet_unref(&expected); av_packet_unref(&expected_annexb); av_packet_unref(&frame);
    puts("PASS 4096 hvcC SEI NALs preserve order in one cache block across update/flush");
}

static void test_passthrough_and_hvcc(Fixture *old)
{
    AVCodecParameters *par = avcodec_parameters_alloc();
    AVBSFContext *first = create(old->ic->streams[0]->codecpar), *bsf = NULL;
    AVPacket packet = {0}, output = {0};
    assert(avcodec_parameters_copy(par, first->par_out) == 0);
    convert(first, &old->frame, &packet);
    for (int absent = 0; absent < 2; absent++) {
        if (absent) { av_freep(&par->extradata); par->extradata_size = 0; }
        bsf = create(par);
        convert(bsf, &packet, &output);
        assert(output.size == packet.size && !memcmp(output.data, packet.data, packet.size));
        av_packet_unref(&output); av_bsf_free(&bsf);
    }
    assert(avcodec_parameters_copy(par, old->ic->streams[0]->codecpar) == 0);
    for (int size = 23; size < par->extradata_size; size++) {
        int saved = par->extradata_size;
        par->extradata_size = size;
        assert(ijk_amc_create_bsf(&bsf, par, (AVRational){1,5}) < 0);
        assert(!bsf);
        par->extradata_size = saved;
    }
    av_packet_unref(&packet); av_bsf_free(&first); avcodec_parameters_free(&par);
    puts("PASS Annex B and no-extradata passthrough; truncated hvcC rejects safely");
}

static void test_muxed(const char *path, unsigned old_hash, unsigned new_hash)
{
    AVFormatContext *ic = NULL;
    assert(avformat_open_input(&ic, path, NULL, NULL) == 0);
    assert(avformat_find_stream_info(ic, NULL) >= 0);
    AVBSFContext *bsf = create(ic->streams[0]->codecpar);
    AVPacket packet = {0};
    int count = 0, ret;
    while ((ret = av_read_frame(ic, &packet)) >= 0) {
        check_hash("actual hev1 MP4 frame", bsf, &packet, count ? new_hash : old_hash);
        count++;
        av_packet_unref(&packet);
    }
    assert(ret == AVERROR_EOF && count == 3);
    av_bsf_free(&bsf); avformat_close_input(&ic);
    puts("PASS actual 3-frame hev1 MP4: old hvcC, in-band update, later IRAP");
}

int main(int argc, char **argv)
{
    Fixture old = {0}, next = {0};
    assert(argc == 3 || argc == 4 || (argc == 5 && !strcmp(argv[3], "--muxed")));
    (void)ijk_amc_update_h264_extradata;
    av_log_set_level(AV_LOG_ERROR);
    load(&old, argv[1]); load(&next, argv[2]);
    test_decode(&old, &next, argc == 4 ? argv[3] : NULL);
    if (argc != 4 || selected(argv[3], "side-extradata")) test_side_extradata(&old, &next);
    if (argc != 4 || selected(argv[3], "structure")) { test_structure(); test_many_sei(); }
    if (argc != 4 || selected(argv[3], "passthrough")) test_passthrough_and_hvcc(&old);
    if (argc == 5)
        test_muxed(argv[4], decode(old.ic->streams[0]->codecpar, &old.frame),
                   decode(next.ic->streams[0]->codecpar, &next.frame));
    av_packet_unref(&old.frame); av_packet_unref(&old.ps); av_packet_unref(&old.pps);
    av_packet_unref(&next.frame); av_packet_unref(&next.ps); av_packet_unref(&next.pps);
    avformat_close_input(&old.ic); avformat_close_input(&next.ic);
    puts("PASS HEVC parameter update regressions");
    return 0;
}
