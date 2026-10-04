/* Compile the production EOS helpers against deterministic codec/SDL fakes. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#define AMEDIACODEC__INFO_TRY_AGAIN_LATER -1
#define AMEDIACODEC__BUFFER_FLAG_FAKE_FRAME 0x1000
#define AMEDIACODEC__BUFFER_FLAG_CODEC_CONFIG 2
#define AMEDIACODEC__BUFFER_FLAG_END_OF_STREAM 4
#define SDL_AMEDIA_OK 0
#define AVERROR_EXTERNAL -100

typedef struct { int flags, size; } SDL_AMediaCodecBufferInfo;
typedef struct { int serial, abort_request; void *mutex, *cond; } PacketQueue;
typedef struct { void *data; int side_data_elems; } Packet;
typedef struct { Packet pkt; int packet_pending, pkt_serial, finished; PacketQueue *queue; } Decoder;
typedef struct { Decoder viddec; } VideoState;
typedef struct { VideoState *is; bool mediacodec_auto_fallback, mediacodec_auto_fallback_triggered; } FFPlayer;
typedef struct { int port, acodec_serial; SDL_AMediaCodecBufferInfo info; } AMC_Buf_Out;
typedef struct { int value; } AVFrame;
typedef struct {
    FFPlayer *ffp; Decoder *decoder; void *acodec, *acodec_mutex;
    bool real_output_seen, input_eos_queued, output_eos_seen, abort, acodec_flush_request;
    int output_eos_serial, input_packet_count, off_buf_out;
    AMC_Buf_Out *amc_buf_out;
} IJKFF_Pipenode_Opaque;
typedef struct { IJKFF_Pipenode_Opaque *opaque; } IJKFF_Pipenode;
static int queued, released, waits, locks, deq_result, queue_result, release_result, fill_result;
static void SDL_LockMutex(void *p) { (void)p; ++locks; }
static void SDL_UnlockMutex(void *p) { (void)p; --locks; }
static void SDL_CondWaitTimeout(void *c, void *m, int ms)
{ (void)c; (void)m; assert(ms > 0 && ms <= 100); ++waits; }
static ssize_t SDL_AMediaCodec_dequeueInputBuffer(void *c, int64_t us)
{ (void)c; (void)us; return deq_result; }
static int SDL_AMediaCodec_queueInputBuffer(void *c, int i, int off, int size, int64_t pts, int flags)
{ (void)c; assert(i >= 0 && off == 0 && size == 0 && pts == 0); assert(flags == 4); ++queued; return queue_result; }
static int SDL_AMediaCodec_releaseOutputBuffer(void *c, int i, bool render)
{ (void)c; assert(i >= 0 && !render); ++released; return release_result; }
static int amc_fill_frame(IJKFF_Pipenode *n, AVFrame *f, int *got, int port, int serial, SDL_AMediaCodecBufferInfo *info)
{ (void)n; (void)serial; (void)info; if (fill_result) return fill_result; f->value = port; *got = 1; return 0; }
#include "../ffamc_eos_policy.h"
#include "../ffamc_eos.h"

int main(void)
{
    PacketQueue q = {.serial = 1};
    VideoState is = {.viddec = {.packet_pending=1, .pkt_serial=1, .queue=&q}};
    FFPlayer f = {.is=&is, .mediacodec_auto_fallback=true};
    IJKFF_Pipenode_Opaque o = {.ffp=&f, .decoder=&is.viddec};
    IJKFF_Pipenode n = {&o};
    SDL_AMediaCodecBufferInfo info = {0};
    AMC_Buf_Out reorder[2] = {{.port=12}, {.port=11}};
    AVFrame frame = {0};
    int got = 0, count = 0;
    assert(codec_input_is_eof(&is.viddec));
    assert(!should_fallback_to_ffplay(&n));
    is.viddec.pkt.side_data_elems = 1;
    assert(!codec_input_is_eof(&is.viddec));
    assert(should_fallback_to_ffplay(&n));
    is.viddec.pkt.side_data_elems = 0;
    deq_result = -1;
    assert(feed_codec_eos(&n, 1, true, &count) == 0 && !o.input_eos_queued);
    deq_result = -99;
    assert(feed_codec_eos(&n, 1, true, &count) == AVERROR_EXTERNAL);
    deq_result = 0; queue_result = -1;
    assert(feed_codec_eos(&n, 1, true, &count) == AVERROR_EXTERNAL && !o.input_eos_queued);
    queued = 0; queue_result = 0;
    assert(feed_codec_eos(&n, 1, true, &count) == 0);
    assert(queued == 1 && count == 1 && o.input_eos_queued && !is.viddec.finished);
    assert(feed_codec_eos(&n, 1, true, &count) == 0 && queued == 1 && waits == 1);
    info.flags=4; info.size=8;
    assert(receive_codec_eos(&n, 7, &info) == 0 && o.output_eos_seen && !is.viddec.finished);
    record_codec_output(&n, &info); assert(o.real_output_seen);
    o.off_buf_out=2; o.amc_buf_out=reorder;
    assert(drain_codec_eos(&n,&frame,&got)==0 && got && frame.value==11 && !is.viddec.finished);
    got=0;
    assert(drain_codec_eos(&n,&frame,&got)==0 && got && frame.value==12 && !is.viddec.finished);
    got=0;
    assert(drain_codec_eos(&n,&frame,&got)==0 && !got && is.viddec.finished==1);
    q.serial=2; is.viddec.finished=0;
    assert(drain_codec_eos(&n,&frame,&got)==0 && !is.viddec.finished);
    // Emulate production flush reset. Real-output evidence is deliberately kept.
    o.input_eos_queued=false; o.output_eos_seen=false; is.viddec.pkt_serial=2;
    assert(feed_codec_eos(&n,1,false,&count)==0 && queued==2);
    assert(o.real_output_seen);
    info.size=0; info.flags=4;
    assert(receive_codec_eos(&n,8,&info)==1 && released==1);
    o.real_output_seen=false; record_codec_output(&n,&info); assert(!o.real_output_seen);
    info.flags=2; assert(receive_codec_eos(&n,9,&info)==1 && released==2);
    record_codec_output(&n,&info); assert(!o.real_output_seen);
    info.flags=0x1000; record_codec_output(&n,&info); assert(!o.real_output_seen);
    info.flags=0; record_codec_output(&n,&info); assert(o.real_output_seen);
    info.flags=4; release_result=-1;
    assert(receive_codec_eos(&n,10,&info)==AVERROR_EXTERNAL);
    release_result=0; info.flags=4|0x1000;
    assert(receive_codec_eos(&n,11,&info)==1 && released==3);
    o.off_buf_out=1; fill_result=-1;
    assert(drain_codec_eos(&n,&frame,&got)==AVERROR_EXTERNAL);
    o.input_eos_queued=false; o.abort=true;
    assert(feed_codec_eos(&n,1,true,&count)==0 && queued==2);
    o.abort=false; o.acodec_flush_request=true;
    assert(feed_codec_eos(&n,1,true,&count)==0 && queued==2);
    assert(locks==0);
    puts("PASS production EOS helpers: single EOF, retry/errors, final payload, reorder drain, stale serial, seek reset, fake/config/empty EOS, zero-size Surface, fallback guard, abort/flush");
    return 0;
}
