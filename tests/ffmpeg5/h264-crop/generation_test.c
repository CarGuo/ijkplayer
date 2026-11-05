/* Production generation and EOS helpers, real FFmpeg BSF, deterministic codec/SDL. */
#define main crop_parameters_main
#include "crop_parameters_test.c"
#undef main
#include <stdbool.h>
#include "libavformat/avformat.h"
#include <sys/types.h>
#include "libavutil/time.h"
#define SDL_AMEDIA_OK 0
#define AMEDIACODEC__INFO_TRY_AGAIN_LATER -1
#define AMEDIACODEC__BUFFER_FLAG_END_OF_STREAM 4
#define AMEDIACODEC__BUFFER_FLAG_CODEC_CONFIG 2
#define AMEDIACODEC__BUFFER_FLAG_FAKE_FRAME 0x1000
#define ALOGI(...) ((void)0)
typedef void JNIEnv;
typedef struct {int serial,abort_request,nb_packets;void *mutex,*cond;} PacketQueue;
typedef struct {PacketQueue *queue;int pkt_serial,finished,packet_pending;AVPacket pkt,pkt_temp;void *empty_queue_cond;int64_t start_pts,next_pts;AVRational start_pts_tb,next_pts_tb;} Decoder;
typedef struct {int flags,size;} SDL_AMediaCodecBufferInfo;
typedef struct {int port,acodec_serial;SDL_AMediaCodecBufferInfo info;} AMC_Buf_Out;
typedef struct {int abort_request,paused;AVStream *video_st;} VideoState;
typedef struct {VideoState *is;int mediacodec_handle_resolution_change;} FFPlayer;
typedef struct IJKFF_Pipenode_Opaque {
    FFPlayer *ffp;Decoder *decoder;AVCodecParameters *codecpar,*hevc_active_csd,*hevc_next_csd,*hevc_next_codecpar,*h264_baseline_codecpar;
    AVBSFContext *bsfc,*h264_next_bsfc;AVPacket filtered_packet;
    bool bsf_pending,abort,acodec_flush_request,acodec_reconfigure_request,input_eos_queued,output_eos_seen;
    bool hevc_boundary_pending,hevc_boundary_drained,hevc_packet_checked,h264_crop_disabled,h264_input_abandoned,h264_crop_replay_pending;
    bool h264_crop_committed,h264_baseline_restore_pending;
    bool aformat_need_recreate;
    int hevc_boundary_serial,input_packet_count,off_buf_out,output_eos_serial;
    int64_t hevc_drain_idle_since_us;
    int64_t last_queued_pts;
    void *pipeline,*jsurface,*weak_vout,*acodec,*acodec_mutex,*acodec_cond;
    AMC_Buf_Out *amc_buf_out;
} IJKFF_Pipenode_Opaque;
typedef struct {IJKFF_Pipenode_Opaque *opaque;} IJKFF_Pipenode;
static int dequeue_result,queued_bytes,queued_flags;
static int owned,codec_serial=91,surface_change,queued,configured,format_failed,configure_failed,serial_during_config,serial_during_borrow;
static int locks;
static void SDL_LockMutex(void *p){(void)p;++locks;}
static void SDL_UnlockMutex(void *p){(void)p;--locks;assert(locks>=0);}
static void SDL_CondSignal(void *p){(void)p;}
static void SDL_CondWaitTimeout(void *a,void *b,int t){(void)a;(void)b;(void)t;}
static bool crop_opt_enabled=true;
static bool h264_crop_enabled(const IJKFF_Pipenode_Opaque *o){(void)o;return crop_opt_enabled;}
static void ffpipeline_lock_surface(void *p){(void)p;++locks;}
static void ffpipeline_unlock_surface(void *p){(void)p;--locks;}
static bool ffpipeline_is_surface_need_reconfigure_l(void *p){(void)p;return surface_change;}
static int SDL_AMediaCodec_getSerial(void *p){(void)p;return codec_serial;}
static int SDL_VoutAndroid_countOwnedBuffers(void *p,int s){(void)p;assert(s==codec_serial);return owned;}
static bool h264_input_valid_l(const IJKFF_Pipenode_Opaque *o){return !o->abort&&!o->ffp->is->abort_request&&!o->decoder->queue->abort_request&&!o->acodec_flush_request&&!o->h264_input_abandoned&&o->decoder->pkt_serial==o->decoder->queue->serial&&!surface_change;}
static IJKFF_Pipenode_Opaque *subject;
static ssize_t SDL_AMediaCodec_dequeueInputBuffer(void *c,int64_t t){(void)c;(void)t;if(serial_during_borrow)++subject->decoder->queue->serial;return dequeue_result;}
static int SDL_AMediaCodec_queueInputBuffer(void *c,int i,int off,int size,int64_t pts,int flags){(void)c;(void)i;(void)off;(void)size;(void)pts;queued_bytes+=size;queued_flags=flags;++queued;return 0;}
static int SDL_AMediaCodec_releaseOutputBuffer(void *c,int i,bool render){(void)c;(void)i;(void)render;return 0;}
static int amc_fill_frame(IJKFF_Pipenode *n,AVFrame *f,int *got,int port,int serial,SDL_AMediaCodecBufferInfo *info){(void)n;(void)f;(void)port;(void)serial;(void)info;*got=1;return 0;}
static int recreate_format_l(JNIEnv *env,IJKFF_Pipenode *n){(void)env;assert(n->opaque->hevc_next_csd);return format_failed?AVERROR_EXTERNAL:0;}
static int reconfigure_codec_l_checked(JNIEnv *env,IJKFF_Pipenode *n,void *surface,bool checked){(void)env;assert(checked&&(surface||n->opaque->h264_baseline_restore_pending));++configured;if(configure_failed)return AVERROR_EXTERNAL;++codec_serial;n->opaque->hevc_boundary_pending=false;n->opaque->hevc_boundary_drained=false;n->opaque->input_eos_queued=false;n->opaque->output_eos_seen=false;n->opaque->input_packet_count=0;if(serial_during_config)++n->opaque->decoder->queue->serial;return 0;}
static int reconfigure_codec_l(JNIEnv *env,IJKFF_Pipenode *n,void *surface){return reconfigure_codec_l_checked(env,n,surface,true);}
#include "../../../ijkmedia/ijkplayer/android/pipeline/ffamc_eos.h"
#include "../../../ijkmedia/ijkplayer/android/pipeline/ffamc_generation.h"

static void scenario(const AVPacket *a,const AVPacket *b,int mode) {
    HCPair pa=pair(a),pb=pair(b);PacketQueue q={.serial=7};Decoder d={.queue=&q,.pkt_serial=7};
    AVStream st={.time_base={1,90000}};VideoState is={.video_st=&st};FFPlayer f={.is=&is};
    IJKFF_Pipenode_Opaque o={.ffp=&f,.decoder=&d,.codecpar=parameters(&pa,4),.jsurface=(void*)1,.input_packet_count=4};
    IJKFF_Pipenode n={&o};subject=&o;assert(!ijk_amc_create_bsf(&o.bsfc,o.codecpar,st.time_base));
    d.pkt=framing(b,4,0);o.filtered_packet=filter(o.bsfc,&d.pkt);d.pkt_temp=o.filtered_packet;o.bsf_pending=true;
    AVBSFContext *old=o.bsfc;AVCodecParameters *oldpar=o.codecpar;uint8_t *held=d.pkt_temp.data;
    if(mode==5)o.jsurface=NULL;
    owned=1;queued=configured=format_failed=configure_failed=serial_during_config=serial_during_borrow=0;
    assert(advance_hevc_generation(NULL,&n,true,1,NULL)==1);
    assert(queued==(mode==5?0:1)&&!configured&&o.hevc_boundary_pending&&o.h264_next_bsfc&&o.bsfc==old&&o.codecpar==oldpar);
    assert(d.pkt_temp.data==held && d.pkt_temp.pts==b->pts && d.pkt_temp.dts==b->dts);
    // Surface removal cancels codec indices but must not consume this admitted AU.
    if(mode==1){o.jsurface=NULL;o.hevc_boundary_pending=false;o.input_packet_count=0;o.input_eos_queued=false;assert(advance_hevc_generation(NULL,&n,true,1,NULL)==1);assert(d.pkt_temp.data==held&&o.h264_next_bsfc);o.jsurface=(void*)1;}
    if(mode==5){assert(o.h264_next_bsfc);o.jsurface=(void*)1;o.input_packet_count=0;}
    owned=0;o.hevc_boundary_drained=true;
    if(mode==2)format_failed=1;
    if(mode==3)configure_failed=1;
    if(mode==4)serial_during_config=1;
    int ret=advance_hevc_generation(NULL,&n,true,1,NULL);
    if(mode==2||mode==3){assert(ret==AVERROR_EXTERNAL&&o.codecpar==oldpar&&o.bsfc==old&&!o.hevc_active_csd);}
    else {assert(ret==(mode==4?1:0));assert(o.codecpar!=oldpar&&o.bsfc!=old&&!o.h264_next_bsfc&&o.hevc_active_csd);assert(o.codecpar->width==pb.sps.visible_w&&o.h264_crop_replay_pending);assert(d.pkt_temp.data==held);}
    cancel_hevc_generation_l(&o);assert(!o.h264_crop_replay_pending);avcodec_parameters_free(&o.codecpar);avcodec_parameters_free(&o.hevc_active_csd);avcodec_parameters_free(&o.h264_baseline_codecpar);av_bsf_free(&o.bsfc);av_packet_unref(&d.pkt);av_packet_unref(&o.filtered_packet);
    assert(!locks);printf("PASS production generation scenario=%d\n",mode);
}
static void eos_cancel(void) {
    PacketQueue q={.serial=7};Decoder d={.queue=&q,.pkt_serial=7};VideoState is={0};FFPlayer f={.is=&is};IJKFF_Pipenode_Opaque o={.ffp=&f,.decoder=&d};IJKFF_Pipenode n={&o};subject=&o;
    queued=0;serial_during_borrow=1;assert(!feed_codec_eos(&n,1,true,NULL));assert(!queued&&!o.input_eos_queued&&o.h264_input_abandoned);
    serial_during_borrow=0;q.serial=7;assert(!feed_codec_eos(&n,1,true,NULL));assert(!queued);assert(!locks);puts("PASS borrowed EOS cancellation cannot reborrow");
}
int generation_test_main(int argc,char **argv){assert(argc==3);AVPacket a=load(argv[1]),b=load(argv[2]);for(int i=0;i<6;++i)scenario(&a,&b,i);eos_cancel();av_packet_unref(&a);av_packet_unref(&b);return 0;}
#ifndef GENERATION_TEST_LIBRARY
int main(int argc,char **argv){return generation_test_main(argc,argv);}
#endif
