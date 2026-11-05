#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define AVERROR(x) (-(x))
#define AVERROR_EOF (-1000)
#define AVERROR_INVALIDDATA (-1001)
#define AVERROR_EXTERNAL (-1002)
#define AV_NOPTS_VALUE INT64_MIN
#define AV_CODEC_ID_H264 27
#define SDL_AMEDIA_OK 0
#define ALOGW(...) ((void)0)
typedef int SDL_mutex;
typedef int SDL_cond;
typedef struct { int num, den; } AVRational;
typedef struct AVPacket { uint8_t *data; int size, side_data_elems; int64_t pts,dts,duration; } AVPacket;
typedef struct { int codec_id,width,height; } AVCodecParameters;
typedef struct { int unused; } AVBSFContext;
typedef struct { int serial,nb_packets; } PacketQueue;
typedef struct Decoder {
    PacketQueue *queue; int packet_pending,pkt_serial,finished;
    AVPacket pkt,pkt_temp; SDL_cond *empty_queue_cond;
    int64_t start_pts,next_pts; AVRational start_pts_tb,next_pts_tb;
} Decoder;
typedef struct { AVRational time_base; } AVStream;
typedef struct { AVStream *video_st; } VideoState;
typedef struct { VideoState *is; int mediacodec_handle_resolution_change; } FFPlayer;
typedef struct { int packet_checked; } Crop;
typedef struct IJKFF_Pipenode_Opaque {
    FFPlayer *ffp; Decoder *decoder; AVCodecParameters *codecpar,*hevc_active_csd,*h264_baseline_codecpar; AVBSFContext *bsfc,*h264_next_bsfc;
    AVPacket filtered_packet; bool bsf_pending,acodec_flush_request,aformat_need_recreate,h264_input_abandoned,h264_crop_disabled;
    bool hevc_packet_checked,acodec_reconfigure_request,input_eos_queued,output_eos_seen;
    bool h264_baseline_restore_pending;
    bool h264_crop_committed;
    volatile bool abort; SDL_mutex *acodec_mutex; SDL_cond *acodec_cond;
    int input_packet_count,off_buf_out; int64_t last_queued_pts;
    void *acodec,*weak_vout,*pipeline; Crop *crop;
} IJKFF_Pipenode_Opaque;
typedef struct { IJKFF_Pipenode_Opaque *opaque; } IJKFF_Pipenode;
static IJKFF_Pipenode_Opaque *subject;
static int flushes,syncs,sync_serial,reads;
static uint8_t ordinary_byte=0x65,flush_byte;
static void SDL_LockMutex(SDL_mutex *p) {(void)p;}
static void SDL_UnlockMutex(SDL_mutex *p) {(void)p;}
static void SDL_CondSignal(SDL_cond *p) {(void)p;}
static void av_packet_unref(AVPacket *p) {memset(p,0,sizeof(*p));}
static int av_packet_ref(AVPacket *d,const AVPacket *s) {*d=*s; return 0;}
static void av_packet_move_ref(AVPacket *d,AVPacket *s) {*d=*s; memset(s,0,sizeof(*s));}
static void av_bsf_flush(AVBSFContext *b) {(void)b;}
static int av_bsf_receive_packet(AVBSFContext *b,AVPacket *p) {(void)b;(void)p;assert(0);return -1;}
static int av_bsf_send_packet(AVBSFContext *b,AVPacket *p) {(void)b;(void)p;assert(0);return -1;}
static void cancel_hevc_generation_l(IJKFF_Pipenode_Opaque *p) {(void)p;}
static int stage_h264_baseline_restore_l(IJKFF_Pipenode_Opaque *p) {(void)p;assert(0);return 0;}
static bool ffp_is_flush_packet(const AVPacket *p) {return p->data==&flush_byte;}
static bool SDL_AMediaCodec_isStarted(void *p) {(void)p;return true;}
static int SDL_AMediaCodec_getSerial(void *p) {(void)p;return 91;}
static void SDL_VoutAndroid_invalidateAllBuffers(void *p) {(void)p;}
static void SDL_VoutAndroid_resetBufferReleaseError(void *p,int s) {(void)p;(void)s;}
static int SDL_AMediaCodec_flush(void *p) {(void)p;++flushes;return 0;}
static int hc_android_flush_done_l(IJKFF_Pipenode_Opaque *p) {(void)p;return 0;}
static int hc_android_after_input_flush_l(IJKFF_Pipenode_Opaque *p) {++syncs;sync_serial=p->decoder->pkt_serial;return 0;}
static int ijk_amc_update_h264_extradata(AVCodecParameters *p,AVPacket *a) {(void)p;(void)a;assert(0);return -1;}
static int ijk_amc_create_bsf(AVBSFContext **b,AVCodecParameters *p,AVRational q) {(void)b;(void)p;(void)q;assert(0);return -1;}
static void ffpipeline_set_surface_need_reconfigure_l(void *p,bool b) {(void)p;(void)b;assert(0);}
static int ijk_amc_get_packet(FFPlayer *f,Decoder *d,volatile bool *abort,AVPacket *out) {
    (void)f;(void)abort; assert(++reads==1); assert(d->pkt_serial==7&&d->queue->serial==7);
    /* First pump already allowed prepare. Request is now published, but marker
     * has NOT been inserted. Return the already obtained ordinary old packet. */
    subject->acodec_flush_request=true;
    *out=(AVPacket){.data=&ordinary_byte,.size=1,.pts=100,.dts=90};
    d->queue->nb_packets=0;
    return 1;
}
static int h264_crop_enabled(const IJKFF_Pipenode_Opaque *o) { (void)o; return 1; }
#define AV_PKT_DATA_NEW_EXTRADATA 1
static void avcodec_parameters_free(AVCodecParameters **p) {assert(!*p);}
static uint8_t *av_packet_get_side_data(const AVPacket *p,int type,size_t *n) {(void)p;(void)type;*n=0;return NULL;}
#include "../../../ijkmedia/ijkplayer/android/pipeline/ffamc_input.h"
int main(void) {
    PacketQueue q={.serial=7,.nb_packets=1}; Decoder d={.queue=&q,.pkt_serial=7};
    AVCodecParameters par={.codec_id=AV_CODEC_ID_H264}; FFPlayer f={0}; Crop crop={0};
    IJKFF_Pipenode_Opaque o={.ffp=&f,.decoder=&d,.codecpar=&par,.crop=&crop,.input_packet_count=1};
    IJKFF_Pipenode n={&o}; subject=&o;
    assert(!o.acodec_flush_request); /* pre-prepare pump observation */
    int ret=prepare_input_packet(&n,true);
    printf("ret=%d flushes=%d flag=%d syncs=%d sync_serial=%d queue_serial=%d decoder_serial=%d pending=%d\n",
           ret,flushes,o.acodec_flush_request,syncs,sync_serial,q.serial,d.pkt_serial,d.packet_pending);
    if(flushes||!o.acodec_flush_request||syncs) {
        puts("FAIL: request consumed before actual marker; old serial can appear ready to post-prepare pump.");
        return 1;
    }
    puts("PASS: flag-only cancellation remains pending; post-prepare pump must not queue old AU.");
    return 0;
}
