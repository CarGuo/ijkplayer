/* Compile the production output-size adapter against minimal pipeline types. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
/* Dependency constants/types only; size validation and policy are production code. */
enum { AV_CODEC_ID_H264, AV_CODEC_ID_HEVC, AV_CODEC_ID_MPEG2VIDEO };
typedef struct { int codec_id; } AVCodecParameters;
typedef struct {
    AVCodecParameters *codecpar;
    char acodec_name[128];
    int frame_width, frame_height, frame_rotate_degrees;
} IJKFF_Pipenode_Opaque;
#include "../ffamc_output_size.h"

/* Allocation remains 1920x1080 across ad -> 1280x536 content -> ad. */
static void test_crop_only_transition(void)
{
    AVCodecParameters par = { AV_CODEC_ID_H264 };
    IJKFF_Pipenode_Opaque o = { .codecpar = &par, .frame_width = 1920, .frame_height = 1080 };
    strcpy(o.acodec_name, "OMX.amlogic.avc.decoder.awesome");
    assert(update_output_frame_size(&o, 1920, 1080, 15, 0, 0, 1919, 1079) == 0);
    assert(o.frame_width == 1920 && o.frame_height == 1080);
    assert(update_output_frame_size(&o, 1920, 1080, 15, 0, 0, 1279, 535) == 0);
    assert(o.frame_width == 1280 && o.frame_height == 536);
    assert(update_output_frame_size(&o, 1920, 1080, 15, 0, 0, 1279, 535) == 0);
    assert(o.frame_width == 1280 && o.frame_height == 536);
    assert(update_output_frame_size(&o, 1920, 1080, 15, 0, 0, 1919, 1079) == 0);
    assert(o.frame_width == 1920 && o.frame_height == 1080);
    /* A single changed axis and nonzero crop origins are also valid. */
    assert(update_output_frame_size(&o, 1920, 1080, 15, 100, 0, 1379, 1079) == 0);
    assert(o.frame_width == 1280 && o.frame_height == 1080);
    assert(update_output_frame_size(&o, 1920, 1080, 15, 0, 100, 1919, 635) == 0);
    assert(o.frame_width == 1920 && o.frame_height == 536);
}

/* Missing crop falls back to format dimensions; honor MediaCodec rotation. */
static void test_missing_crop_rotation_and_boundaries(void)
{
    AVCodecParameters par = { AV_CODEC_ID_H264 };
    IJKFF_Pipenode_Opaque o = { .codecpar = &par };
    int rotations[] = { 0, 90, 180, 270 };
    for (unsigned i = 0; i < sizeof(rotations) / sizeof(rotations[0]); ++i) {
        o.frame_rotate_degrees = rotations[i];
        assert(update_output_frame_size(&o, 1280, 536, 0, -1, -1, -1, -1) == 0);
        int rotated = rotations[i] == 90 || rotations[i] == 270;
        assert(o.frame_width == (rotated ? 536 : 1280));
        assert(o.frame_height == (rotated ? 1280 : 536));
        assert(update_output_frame_size(&o, 1920, 1080, 15, 0, 0, 1279, 535) == 0);
        assert(o.frame_width == (rotated ? 536 : 1280));
        assert(o.frame_height == (rotated ? 1280 : 536));
    }
    o.frame_rotate_degrees = 0;
    assert(update_output_frame_size(&o, 1, 1, 15, 0, 0, 0, 0) == 0);
    assert(o.frame_width == 1 && o.frame_height == 1);
    assert(update_output_frame_size(&o, INT_MAX, INT_MAX, 15, 0, 0, INT_MAX-1, INT_MAX-1) == 0);
    assert(o.frame_width == INT_MAX && o.frame_height == INT_MAX);
}

/* Partial/invalid H.264 vendor formats preserve state; HEVC stays strict. */
static void test_invalid_geometry(void)
{
    int invalid[][7] = {
        {0,1080,0,0,0,0,0}, {-1,1080,0,0,0,0,0},
        {1920,0,0,0,0,0,0}, {1920,-1,0,0,0,0,0},
        {1920,1080,15,-1,0,1279,535}, {1920,1080,15,0,-1,1279,535},
        {1920,1080,15,1280,0,1279,535}, {1920,1080,15,0,536,1279,535},
        {1920,1080,15,0,0,1920,535}, {1920,1080,15,0,0,1279,1080},
        {1920,1080,15,INT_MIN,0,INT_MAX,535},
        {1920,1080,16,0,0,1279,535}, {1920,1080,-1,0,0,1279,535}
    };
    AVCodecParameters par = { AV_CODEC_ID_H264 };
    IJKFF_Pipenode_Opaque o = { .codecpar = &par, .frame_width = 1280, .frame_height = 536 };
    for (int codec = AV_CODEC_ID_H264; codec <= AV_CODEC_ID_HEVC; ++codec) {
        par.codec_id = codec;
        for (int mask = 1; mask < 15; ++mask) {
            int ret = update_output_frame_size(&o, 1920, 1080, mask, 0, 0, 1919, 1079);
            assert(ret == (codec == AV_CODEC_ID_HEVC ? -1 : 0));
            assert(o.frame_width == 1280 && o.frame_height == 536);
        }
        for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            int *v = invalid[i];
            int ret = update_output_frame_size(&o, v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
            assert(ret == (codec == AV_CODEC_ID_HEVC ? -1 : 0));
            assert(o.frame_width == 1280 && o.frame_height == 536);
        }
    }
}

/* Unqualified codecs and the legacy TI H.264 path retain their old sizing. */
static void test_codec_policy(void)
{
    AVCodecParameters par = { AV_CODEC_ID_MPEG2VIDEO };
    IJKFF_Pipenode_Opaque o = { .codecpar = &par, .frame_width = 1920, .frame_height = 1080 };
    assert(update_output_frame_size(&o, 1920, 1080, 15, 0, 0, 1279, 535) == 0);
    assert(o.frame_width == 1920 && o.frame_height == 1080);
    par.codec_id = AV_CODEC_ID_H264;
    strcpy(o.acodec_name, "OMX.TI.DUCATI1.VIDEO.DECODER");
    assert(update_output_frame_size(&o, 1920, 1080, 15, 0, 0, 1279, 535) == 0);
    assert(o.frame_width == 1920 && o.frame_height == 1080);
    strcpy(o.acodec_name, "omx.ti.ducati1.video.decoder");
    assert(update_output_frame_size(&o, 1920, 1080, 15, 0, 0, 1279, 535) == 0);
    assert(o.frame_width == 1920 && o.frame_height == 1080);
    /* A shared vendor name must not change the existing HEVC behavior. */
    par.codec_id = AV_CODEC_ID_HEVC;
    assert(update_output_frame_size(&o, 1920, 1080, 15, 0, 0, 1279, 535) == 0);
    assert(o.frame_width == 1280 && o.frame_height == 536);
    par.codec_id = AV_CODEC_ID_H264;
    strcpy(o.acodec_name, "OMX.TI.other.decoder");
    assert(update_output_frame_size(&o, 1920, 1080, 15, 0, 0, 1919, 1079) == 0);
    assert(o.frame_width == 1920 && o.frame_height == 1080);
}

/* Exercise the actual adapter/resolver without FFmpeg, JNI or a decoder. */
int main(void)
{
    test_crop_only_transition();
    test_missing_crop_rotation_and_boundaries();
    test_invalid_geometry();
    test_codec_policy();
    puts("PASS MediaCodec output sizing: H.264 crop-only transitions, repeat/one-axis changes, rotation, invalid geometry, HEVC and legacy TI policy");
    return 0;
}
