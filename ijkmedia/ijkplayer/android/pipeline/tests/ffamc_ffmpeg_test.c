/* Host regression tests; see run_ffamc_ffmpeg_tests.sh. */
#include <assert.h>
#include <stdio.h>
#include "libavformat/avformat.h"
#include "libavutil/intreadwrite.h"
#include "../ffamc_ffmpeg.h"

static AVFormatContext *open_video(const char *path, int *stream)
{
    AVFormatContext *ic = NULL;
    assert(avformat_open_input(&ic, path, NULL, NULL) >= 0);
    assert(avformat_find_stream_info(ic, NULL) >= 0);
    *stream = av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    assert(*stream >= 0);
    return ic;
}

static void read_video(AVFormatContext *ic, int stream, AVPacket *pkt)
{
    for (;;) {
        assert(av_read_frame(ic, pkt) >= 0);
        if (pkt->stream_index == stream)
            return;
        av_packet_unref(pkt);
    }
}

static int annexb(const uint8_t *p, int size)
{
    return size >= 4 && p[0] == 0 && p[1] == 0 &&
           (p[2] == 1 || (p[2] == 0 && p[3] == 1));
}

static void submit(AVBSFContext *bsf, const AVPacket *original)
{
    AVPacket input = {0};
    assert(av_packet_ref(&input, original) >= 0);
    assert(av_bsf_send_packet(bsf, &input) >= 0);
    assert(!input.data && !input.buf && !input.side_data_elems);
    av_packet_unref(&input);
}

static void test_filter(const char *path)
{
    int stream, frames = 0, packets = 0;
    AVFormatContext *ic = open_video(path, &stream);
    const AVCodecParameters *par = ic->streams[stream]->codecpar;
    AVBSFContext *bsf = NULL;
    AVPacket original = {0}, first = {0}, output = {0};
    AVFrame *frame = av_frame_alloc();
    AVCodecContext *decoder = avcodec_alloc_context3(avcodec_find_decoder(par->codec_id));
    assert(frame && decoder);
    assert(ijk_amc_create_bsf(&bsf, par, ic->streams[stream]->time_base) == 0);
    assert(bsf && annexb(bsf->par_out->extradata, bsf->par_out->extradata_size));
    assert(bsf->par_in->extradata_size == par->extradata_size);
    assert(!memcmp(bsf->par_in->extradata, par->extradata, par->extradata_size));
    assert(avcodec_parameters_to_context(decoder, bsf->par_out) == 0);
    decoder->thread_count = 1;
    assert(avcodec_open2(decoder, decoder->codec, NULL) == 0);

    while (av_read_frame(ic, &original) >= 0) {
        if (original.stream_index == stream) {
            uint8_t *copy = av_memdup(original.data, original.size);
            int size = original.size;
            assert(copy);
            if (!first.data)
                assert(av_packet_ref(&first, &original) == 0);
            submit(bsf, &original);
            while (av_bsf_receive_packet(bsf, &output) == 0) {
                assert(annexb(output.data, output.size));
                assert(output.pts == original.pts && output.dts == original.dts);
                assert(output.duration == original.duration);
                assert(avcodec_send_packet(decoder, &output) == 0);
                while (avcodec_receive_frame(decoder, frame) == 0) {
                    ++frames;
                    av_frame_unref(frame);
                }
                av_packet_unref(&output);
            }
            assert(original.size == size && !memcmp(copy, original.data, size));
            av_free(copy);
            ++packets;
        }
        av_packet_unref(&original);
    }
    assert(av_bsf_send_packet(bsf, NULL) == 0);
    assert(av_bsf_receive_packet(bsf, &output) == AVERROR_EOF);
    assert(avcodec_send_packet(decoder, NULL) == 0);
    while (avcodec_receive_frame(decoder, frame) == 0) {
        ++frames;
        av_frame_unref(frame);
    }
    assert(frames == packets);

    /* Seek/flush clears EOF, discards pending data, and restarts IDR CSD. */
    for (int i = 0; i < 32; ++i) {
        AVPacket blocked = {0};
        av_bsf_flush(bsf);
        submit(bsf, &first);
        assert(av_packet_ref(&blocked, &first) == 0);
        assert(av_bsf_send_packet(bsf, &blocked) == AVERROR(EAGAIN));
        assert(blocked.data && blocked.size == first.size);
        assert(av_bsf_receive_packet(bsf, &output) == 0);
        assert(annexb(output.data, output.size));
        av_packet_unref(&output);
        assert(av_bsf_receive_packet(bsf, &output) == AVERROR(EAGAIN));
        assert(av_bsf_send_packet(bsf, &blocked) == 0);
        assert(!blocked.data);
        av_bsf_flush(bsf);
        assert(av_bsf_receive_packet(bsf, &output) == AVERROR(EAGAIN));
        av_packet_unref(&blocked);
    }
    // Already-Annex-B streams (e.g. transport streams) must pass unchanged.
    av_bsf_flush(bsf);
    submit(bsf, &first);
    assert(av_bsf_receive_packet(bsf, &output) == 0);
    {
        AVBSFContext *passthrough = NULL;
        AVPacket copied = {0};
        assert(ijk_amc_create_bsf(&passthrough, bsf->par_out, bsf->time_base_out) == 0);
        submit(passthrough, &output);
        assert(av_bsf_receive_packet(passthrough, &copied) == 0);
        assert(copied.size == output.size && !memcmp(copied.data, output.data, output.size));
        av_packet_unref(&copied);
        av_bsf_free(&passthrough);
    }
    av_packet_unref(&output);
    // A malformed length is reported without losing the original input owner.
    av_bsf_flush(bsf);
    {
        AVPacket damaged = {0};
        assert(av_packet_ref(&damaged, &first) == 0);
        assert(av_packet_make_writable(&damaged) == 0);
        AV_WB32(damaged.data, INT_MAX);
        submit(bsf, &damaged);
        assert(av_bsf_receive_packet(bsf, &output) == AVERROR_INVALIDDATA);
        assert(damaged.data && AV_RB32(damaged.data) == INT_MAX);
        av_packet_unref(&damaged);
    }
    // Corruption is packet-local: no flush/recreation is needed for recovery.
    submit(bsf, &first);
    assert(av_bsf_receive_packet(bsf, &output) == 0);
    assert(annexb(output.data, output.size));
    av_packet_unref(&output);
    printf("  FFmpeg decoder=%s, bsf=%s\n", decoder->codec->name, bsf->filter->name);
    av_packet_unref(&first);
    av_frame_free(&frame);
    avcodec_free_context(&decoder);
    av_bsf_free(&bsf);
    assert(!bsf);
    avformat_close_input(&ic);
    printf("PASS conversion, ownership, delayed decode, EAGAIN, EOF, seek and corrupt-packet recovery: %s (%d frames)\n", path, frames);
}

static void test_nal_lengths(const char *path)
{
    int stream;
    AVFormatContext *ic = open_video(path, &stream);
    AVPacket original = {0}, expected = {0};
    AVCodecParameters *par = avcodec_parameters_alloc();
    assert(avcodec_parameters_copy(par, ic->streams[stream]->codecpar) == 0);
    read_video(ic, stream, &original);
    assert((par->extradata[4] & 3) == 3);
    for (int length = 1; length <= 4; length *= 2) {
        AVBSFContext *bsf = NULL;
        AVPacket packed = {0}, output = {0};
        const uint8_t *src = original.data;
        const uint8_t *end = original.data + original.size;
        uint8_t *dst;
        assert(av_new_packet(&packed, original.size) == 0);
        dst = packed.data;
        while (src < end) {
            unsigned size = AV_RB32(src);
            src += 4;
            assert(size && size <= end - src);
            // x264's long encoder-identification SEI cannot fit a 1-byte length.
            if ((src[0] & 31) != 6) {
                assert(size < (UINT64_C(1) << (length * 8)));
                for (int i = length - 1; i >= 0; --i)
                    *dst++ = size >> (i * 8);
                memcpy(dst, src, size);
                dst += size;
            }
            src += size;
        }
        av_shrink_packet(&packed, dst - packed.data);
        par->extradata[4] = (par->extradata[4] & ~3) | (length - 1);
        assert(ijk_amc_create_bsf(&bsf, par, ic->streams[stream]->time_base) == 0);
        submit(bsf, &packed);
        assert(av_bsf_receive_packet(bsf, &output) == 0);
        if (!expected.data)
            assert(av_packet_ref(&expected, &output) == 0);
        else
            assert(expected.size == output.size && !memcmp(expected.data, output.data, output.size));
        av_packet_unref(&packed);
        av_packet_unref(&output);
        av_bsf_free(&bsf);
    }
    av_packet_unref(&expected);
    av_packet_unref(&original);
    avcodec_parameters_free(&par);
    avformat_close_input(&ic);
    puts("PASS H264 1-, 2- and 4-byte NAL lengths produce identical Annex B packets");
}

static void test_resolution(const char *old_path, const char *new_path, int width, int height)
{
    int old_stream, new_stream;
    AVFormatContext *old = open_video(old_path, &old_stream);
    AVFormatContext *next = open_video(new_path, &new_stream);
    AVCodecParameters *par = avcodec_parameters_alloc();
    AVBSFContext *bsf = NULL;
    AVPacket pkt = {0}, output = {0};
    uint8_t *side;
    const AVCodecParameters *next_par = next->streams[new_stream]->codecpar;
    assert(avcodec_parameters_copy(par, old->streams[old_stream]->codecpar) == 0);
    read_video(next, new_stream, &pkt);
    assert(ijk_amc_update_h264_extradata(par, &pkt) == 0);
    side = av_packet_new_side_data(&pkt, AV_PKT_DATA_NEW_EXTRADATA, next_par->extradata_size);
    assert(side);
    memcpy(side, next_par->extradata, next_par->extradata_size);
    assert(ijk_amc_update_h264_extradata(par, &pkt) == 1);
    assert(par->width == width && par->height == height);
    assert(ijk_amc_update_h264_extradata(par, &pkt) == 0);
    assert(ijk_amc_create_bsf(&bsf, par, next->streams[new_stream]->time_base) == 0);
    submit(bsf, &pkt);
    assert(av_bsf_receive_packet(bsf, &output) == 0);
    assert(annexb(output.data, output.size));
    av_packet_unref(&output);
    av_packet_unref(&pkt);
    av_bsf_free(&bsf);
    avcodec_parameters_free(&par);
    avformat_close_input(&old);
    avformat_close_input(&next);
    printf("PASS single-dimension NEW_EXTRADATA with delayed H264: %dx%d\n", width, height);
}

int main(int argc, char **argv)
{
    assert(argc == 5);
    printf("FFmpeg runtime: %s (libavcodec %u)\n", av_version_info(), avcodec_version());
    test_nal_lengths(argv[1]);
    test_filter(argv[1]);
    test_filter(argv[2]);
    test_resolution(argv[1], argv[3], 96, 48);
    test_resolution(argv[1], argv[4], 64, 96);
    return 0;
}
