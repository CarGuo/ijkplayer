#define GENERATION_TEST_LIBRARY 1
#include "generation_test.c"
#define ALOGE(...) ((void)0)
#define AMEDIACODEC__UNKNOWN_ERROR -100
static int input_valid=1,write_size,flush_during_write;
static int SDL_AMediaCodec_isInputBuffersValid(void *c){(void)c;return input_valid;}
static void SDL_AMediaCodecFake_flushFakeFrames(void *c){(void)c;}
static ssize_t SDL_AMediaCodec_writeInputData(void *c,int i,const uint8_t *p,int n){(void)c;(void)i;assert(p&&n);if(flush_during_write)subject->acodec_flush_request=true;return write_size;}
static int feed(IJKFF_Pipenode *node) {
    IJKFF_Pipenode_Opaque *opaque=node->opaque;FFPlayer *ffp=opaque->ffp;VideoState *is=ffp->is;Decoder *d=opaque->decoder;PacketQueue *q=d->queue;void *pipeline=opaque->pipeline;
    JNIEnv *env=NULL;int64_t timeUs=1;int *enqueue_count=NULL;
    int amc_ret=0,ret=0;ssize_t input_buffer_index=0,copy_size=0;int64_t time_stamp=0;uint32_t queue_flags=0;
    bool crop_guard=h264_crop_enabled(opaque),surface_locked=false,queue_locked=false;int input_codec_serial=0;
#include "feeder_body.h"

static void scenario_feed(int mode) {
    AVStream st={.time_base={1,90000}};PacketQueue q={.serial=7};Decoder d={.queue=&q,.pkt_serial=7,.packet_pending=1};VideoState is={.video_st=&st};FFPlayer f={.is=&is};
    IJKFF_Pipenode_Opaque o={.ffp=&f,.decoder=&d,.jsurface=(void*)1,.hevc_packet_checked=true,.h264_crop_replay_pending=true};IJKFF_Pipenode n={&o};subject=&o;
    uint8_t bytes[16]={1};d.pkt_temp=(AVPacket){.data=bytes,.size=16,.pts=1009,.dts=900};
    dequeue_result=0;write_size=16;input_valid=1;queued=queued_bytes=queued_flags=serial_during_borrow=flush_during_write=0;
    if(mode==1)dequeue_result=-1;
    if(mode==2){dequeue_result=-2;input_valid=0;}
    if(mode==3)write_size=15;
    if(mode==4)serial_during_borrow=1;
    if(mode==5)flush_during_write=1;
    int ret=feed(&n);
    if(mode==0){assert(!ret&&queued==1&&queued_bytes==16&&!queued_flags&&!o.h264_crop_replay_pending&&!d.packet_pending);}
    else {assert(!queued&&o.h264_crop_replay_pending&&d.pkt_temp.data==bytes&&d.pkt_temp.size==16&&d.pkt_temp.pts==1009&&d.pkt_temp.dts==900);if(mode==2||mode==3)assert(ret<0);else assert(!ret);}
    if(mode==4||mode==5){assert(o.h264_input_abandoned);serial_during_borrow=flush_during_write=0;q.serial=7;o.acodec_flush_request=false;assert(!feed(&n)&&!queued);}
    assert(!locks);printf("PASS extracted production feeder scenario=%d\n",mode);
}
int main(void){for(int i=0;i<6;++i)scenario_feed(i);return 0;}
