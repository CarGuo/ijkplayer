#!/usr/bin/env python3
"""Extract actual current production bodies into an external build directory."""
import argparse
from pathlib import Path


def section(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    pipeline = root / "ijkmedia/ijkplayer/android/pipeline"
    vdec = (pipeline / "ffpipenode_android_mediacodec_vdec.c").read_text()
    java = (root / "ijkmedia/ijksdl/android/ijksdl_codec_android_mediacodec_java.c").read_text()
    fmt = (root / "ijkmedia/ijksdl/android/ijksdl_codec_android_mediaformat_java.c").read_text()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    feeder = section(vdec, "static int feed_input_buffer(JNIEnv", "static int enqueue_thread_func")
    feeder = feeder[feeder.index("    if (d->pkt_temp.data) {"):]
    (output / "feeder_body.h").write_text(feeder)

    lifecycle = section(java, "sdl_amedia_status_t SDL_AMediaCodecJava_releaseChecked", "static sdl_amedia_status_t SDL_AMediaCodecJava_configure_surface")
    lifecycle += section(fmt, "int SDL_AMediaFormatJava_setBufferChecked", "static void setup_aformat")
    lifecycle += section(vdec, "static int reconfigure_codec_l_checked", "static int reconfigure_codec_l(")
    (output / "lifecycle_functions.h").write_text(lifecycle)

    begin = vdec.index("static int feed_input_buffer(JNIEnv")
    begin = vdec.index("    // EOF has no AU needing a baseline configuration.", begin)
    end = vdec.index("    // prepare_input_packet has already processed", begin)
    policy = (pipeline / "ffamc_eos_policy.h").read_text()
    predicate_begin = policy.index("static bool codec_input_is_eof(")
    predicate_end = policy.index("\n}", predicate_begin) + 2
    wrapper = """
static int extracted_feed_eof_prefix(IJKFF_Pipenode *node) {
    IJKFF_Pipenode_Opaque *opaque=node->opaque;Decoder *d=opaque->decoder;
    int64_t timeUs=1;int *enqueue_count=NULL;
"""
    (output / "baseline_eof_prefix.h").write_text(policy[predicate_begin:predicate_end] + wrapper + vdec[begin:end] + "\n    return 0;\n}\n")

    begin = vdec.index("static bool h264_crop_enabled(")
    end = vdec.index("\n}", begin) + 2
    prefix = """#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define AV_CODEC_ID_H264 27
#define AV_CODEC_ID_HEVC 173
typedef struct {int codec_id;} Context;
typedef struct {Context *avctx;} Decoder;
typedef struct {int mediacodec_h264_crop_workaround,mediacodec_handle_resolution_change,mediacodec_sync;} FFPlayer;
typedef struct {FFPlayer *ffp;Decoder *decoder;} IJKFF_Pipenode_Opaque;
"""
    test = """
int main(void){int checked=0;Context c;Decoder d={&c};FFPlayer f;IJKFF_Pipenode_Opaque o={&f,&d};
for(int on=0;on<2;++on)for(int res=0;res<2;++res)for(int sync=0;sync<2;++sync)for(int avc=0;avc<2;++avc){
f=(FFPlayer){on,res,sync};c.codec_id=avc?AV_CODEC_ID_H264:AV_CODEC_ID_HEVC;
assert(h264_crop_enabled(&o)==(on&&!sync&&avc));++checked;}
printf("PASS byte-extracted actual eligibility matrix %d combinations; resolution-change remains enabled\\n",checked);return 0;}
"""
    (output / "eligibility_test.c").write_text(prefix + vdec[begin:end] + test)


if __name__ == "__main__":
    main()
