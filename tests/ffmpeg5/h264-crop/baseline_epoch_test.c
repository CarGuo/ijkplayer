/* Actual production input + generation helpers, actual FFmpeg 5.1.10 BSF.
 * JNI/codec/SDL are deterministic fakes: this proves staged ownership and the
 * exact source/filtered contract, not real Android decoder behavior. */
#define GENERATION_TEST_LIBRARY 1
#include "generation_test.c"
#define ALOGW(...) ((void)0)
static uint8_t flush_byte;
static int flush_calls,packet_reads,send_marker,marker_serial;
static const AVPacket *queued_source;
static bool ffp_is_flush_packet(const AVPacket *p){return p->data==&flush_byte;}
static bool SDL_AMediaCodec_isStarted(void *c){(void)c;return true;}
static int SDL_AMediaCodec_flush(void *c){(void)c;++flush_calls;return 0;}
static void SDL_VoutAndroid_invalidateAllBuffers(void *v){(void)v;}
static void SDL_VoutAndroid_resetBufferReleaseError(void *v,int s){(void)v;(void)s;}
static void ffpipeline_set_surface_need_reconfigure_l(void *p,bool set){(void)p;surface_change=set;}
static int ijk_amc_get_packet(FFPlayer *f,Decoder *d,bool *abort,AVPacket *p){
    (void)f;(void)abort;++packet_reads;
    if(send_marker){send_marker=0;d->pkt_serial=marker_serial;*p=(AVPacket){.data=&flush_byte};return 1;}
    assert(queued_source);d->pkt_serial=marker_serial;if(!queued_source->data&&!queued_source->side_data_elems)*p=(AVPacket){0};else assert(!av_packet_ref(p,queued_source));queued_source=NULL;return 1;
}
#include "../../../ijkmedia/ijkplayer/android/pipeline/ffamc_input.h"
#include "baseline_eof_prefix.h"

static void exact(const AVPacket *a,const AVPacket *b){assert(a->size==b->size&&!memcmp(a->data,b->data,a->size)&&a->pts==b->pts&&a->dts==b->dts&&a->duration==b->duration&&a->side_data_elems==b->side_data_elems);}
static AVPacket baseline_output(const HCPair *pa,const AVPacket *raw){
    AVCodecParameters *par=parameters(pa,4);AVBSFContext *bsf=NULL;
    assert(!ijk_amc_create_bsf(&bsf,par,(AVRational){1,90000}));
    AVPacket out=filter(bsf,raw);avcodec_parameters_free(&par);av_bsf_free(&bsf);return out;
}
static void cleanup(IJKFF_Pipenode_Opaque *o){
    cancel_hevc_generation_l(o);avcodec_parameters_free(&o->codecpar);avcodec_parameters_free(&o->hevc_active_csd);avcodec_parameters_free(&o->h264_baseline_codecpar);av_bsf_free(&o->bsfc);av_packet_unref(&o->filtered_packet);av_packet_unref(&o->decoder->pkt);assert(!locks);
}
/* mode: 0 back-to-A; 1 headerless-B baseline downgrade; 2 full B reentry;
 * 3 disabled after unsupported; 4 NEW_EXTRADATA; 5 null Surface interruption;
 * 6 format failure; 7 configure failure; 8 serial cancellation while pending;
 * 9 EOF as first packet after the real marker; 10 EOF without a Surface;
 * 11 full B while Surface absent: baseline dummy restore, then B must wait. */
static void scenario_epoch(const AVPacket *a,const AVPacket *b,const AVPacket *a_raw,const AVPacket *b_raw,int mode){
    HCPair pa=pair(a),pb=pair(b);PacketQueue q={.serial=7};Decoder d={.queue=&q,.pkt_serial=7};
    AVStream st={.time_base={1,90000}};VideoState is={.video_st=&st};FFPlayer f={.is=&is};
    IJKFF_Pipenode_Opaque o={.ffp=&f,.decoder=&d,.codecpar=parameters(&pa,4),.jsurface=(void*)1};IJKFF_Pipenode node={&o};subject=&o;
    assert(!ijk_amc_create_bsf(&o.bsfc,o.codecpar,st.time_base));AVCodecParameters *initial=o.codecpar;
    crop_opt_enabled=true;owned=queued=configured=format_failed=configure_failed=serial_during_config=serial_during_borrow=surface_change=flush_calls=packet_reads=0;
    assert(!av_packet_ref(&d.pkt,b_raw));o.filtered_packet=filter(o.bsfc,&d.pkt);d.pkt_temp=o.filtered_packet;o.bsf_pending=true;
    assert(!advance_hevc_generation(NULL,&node,true,1,NULL));assert(o.codecpar->height==pb.sps.visible_h&&o.h264_baseline_codecpar==initial&&o.h264_crop_committed&&configured==1);
    o.input_packet_count=4;d.packet_pending=0;
    AVPacket target={0};
    bool eof_case=mode==9||mode==10;
    if(mode==1)target=framing(b,4,1);else if(!eof_case)assert(!av_packet_ref(&target,(mode==2||mode==11)?b_raw:a_raw));
    if(mode==3){o.h264_crop_disabled=true;avcodec_parameters_free(&o.hevc_active_csd);}
    if(mode==4){uint8_t *extra=av_packet_new_side_data(&target,AV_PKT_DATA_NEW_EXTRADATA,initial->extradata_size);assert(extra);memcpy(extra,initial->extradata,initial->extradata_size);}
    AVPacket expected={0};if(!eof_case)expected=baseline_output(&pa,&target);
    AVBSFContext *previous_bsf=o.bsfc;AVCodecParameters *previous=o.codecpar;
    q.serial=marker_serial=8;send_marker=1;queued_source=&target;o.acodec_flush_request=true;
    assert(!prepare_input_packet(&node,true));
    assert(flush_calls==1&&configured==1&&o.h264_baseline_restore_pending&&o.h264_next_bsfc&&o.codecpar==previous&&o.bsfc==previous_bsf);
    assert(o.hevc_next_codecpar->height==pa.sps.visible_h&&o.hevc_next_csd->height==pa.sps.visible_h);
    if(!eof_case)exact(&o.filtered_packet,&expected);else assert(codec_input_is_eof(&d)&&!o.bsf_pending&&!d.pkt_temp.data);
    assert(!o.acodec_flush_request&&o.input_packet_count==0&&!o.h264_input_abandoned);
    if(mode==4)assert(o.h264_crop_disabled);
    if(mode==5||mode==11){o.jsurface=NULL;o.hevc_boundary_pending=false;exact(&o.filtered_packet,&expected);}
    if(mode==8){q.serial=marker_serial=9;send_marker=1;queued_source=&target;d.packet_pending=1;assert(!prepare_input_packet(&node,true));assert(o.hevc_boundary_serial==9&&o.h264_baseline_restore_pending&&configured==1);exact(&o.filtered_packet,&expected);}
    if(mode==6)format_failed=1;
    if(mode==7)configure_failed=1;
    if(mode==10)o.jsurface=NULL;
    int ret=eof_case?extracted_feed_eof_prefix(&node):advance_hevc_generation(NULL,&node,true,1,NULL);
    if(eof_case){
        assert(!ret&&configured==1&&o.codecpar==previous&&o.bsfc==previous_bsf&&o.h264_crop_committed&&o.h264_baseline_codecpar==initial&&!o.h264_baseline_restore_pending&&!o.hevc_boundary_pending);
        assert(queued==1&&o.input_eos_queued);SDL_AMediaCodecBufferInfo info={.flags=AMEDIACODEC__BUFFER_FLAG_END_OF_STREAM};assert(receive_codec_eos(&node,0,&info)==1);
        int got=0;assert(!drain_codec_eos(&node,NULL,&got,true));assert(d.finished==q.serial&&!o.hevc_boundary_pending);
        q.serial=marker_serial=9;send_marker=1;queued_source=a_raw;d.packet_pending=1;
        assert(!prepare_input_packet(&node,true)&&o.h264_baseline_restore_pending&&o.h264_crop_committed);
        assert(advance_hevc_generation(NULL,&node,true,1,NULL)==1&&configured==2&&o.codecpar->height==pa.sps.visible_h);
        assert(!advance_hevc_generation(NULL,&node,true,1,NULL)&&!o.h264_crop_committed);
    }else if(mode==6||mode==7){assert(ret<0&&o.codecpar==previous&&o.bsfc==previous_bsf&&o.h264_crop_committed&&o.h264_baseline_codecpar==initial&&o.h264_baseline_restore_pending);}
    else {
        assert(ret==1&&configured==2&&o.codecpar->height==pa.sps.visible_h&&!o.h264_crop_committed&&!o.h264_baseline_restore_pending&&!o.h264_crop_replay_pending&&!o.hevc_packet_checked);
        assert(o.bsfc!=previous_bsf&&o.hevc_active_csd->height==pa.sps.visible_h&&o.h264_baseline_codecpar==initial);
        exact(&o.filtered_packet,&expected);
        if(mode==11){assert(advance_hevc_generation(NULL,&node,true,1,NULL)==1&&configured==2&&o.h264_next_bsfc&&o.hevc_boundary_pending&&!o.h264_crop_replay_pending);exact(&o.filtered_packet,&expected);o.jsurface=(void*)1;}
        assert(!advance_hevc_generation(NULL,&node,true,1,NULL));
        if(mode==2||mode==11)assert(configured==3&&o.codecpar->height==pb.sps.visible_h&&o.h264_crop_committed&&o.h264_crop_replay_pending);
        else assert(configured==2&&o.codecpar->height==pa.sps.visible_h&&!o.h264_crop_committed&&!o.h264_crop_replay_pending);
    }
    printf("PASS baseline epoch mode=%d marker_reads=%d configured=%d output=%dx%d%s\n",mode,packet_reads,configured,o.codecpar->width,o.codecpar->height,mode==1?" (headerless B explicitly downgraded to baseline A)":"");
    cleanup(&o);av_packet_unref(&target);av_packet_unref(&expected);
}
static void no_commit(const AVPacket *a,const AVPacket *a_raw,int enabled){
    HCPair pa=pair(a);PacketQueue q={.serial=1};Decoder d={.queue=&q,.pkt_serial=1};AVStream st={.time_base={1,90000}};VideoState is={.video_st=&st};FFPlayer f={.is=&is};
    IJKFF_Pipenode_Opaque o={.ffp=&f,.decoder=&d,.codecpar=parameters(&pa,4),.jsurface=(void*)1,.input_packet_count=3};IJKFF_Pipenode node={&o};subject=&o;
    assert(!ijk_amc_create_bsf(&o.bsfc,o.codecpar,st.time_base));AVPacket expected=baseline_output(&pa,a_raw);
    crop_opt_enabled=enabled;configured=flush_calls=packet_reads=0;q.serial=marker_serial=2;send_marker=1;queued_source=a_raw;
    assert(!prepare_input_packet(&node,true));assert(!o.h264_baseline_restore_pending&&!o.h264_baseline_codecpar&&!configured&&flush_calls==1);exact(&o.filtered_packet,&expected);
    printf("PASS ordinary no-commit seek enabled=%d has zero new reconfigures\n",enabled);cleanup(&o);av_packet_unref(&expected);crop_opt_enabled=true;
}
static int packet_from_file(FILE *fp,AVPacket *p){
    uint32_t size;if(fread(&size,sizeof(size),1,fp)!=1)return 0;
    int64_t times[3];assert(fread(times,sizeof(times),1,fp)==1&&size<HC_MAX_AU);assert(!av_new_packet(p,size));assert(fread(p->data,1,size,fp)==size);p->pts=times[0];p->dts=times[1];p->duration=times[2];return 1;
}
static void ordinary_sequence(const char *path,int enabled){
    FILE *fp=fopen(path,"rb");assert(fp);AVPacket raw={0};assert(packet_from_file(fp,&raw));HCPair pa=pair(&raw);
    PacketQueue q={.serial=1};Decoder d={.queue=&q,.pkt_serial=1};AVStream st={.time_base={1,90000}};VideoState is={.video_st=&st};FFPlayer f={.is=&is};
    IJKFF_Pipenode_Opaque o={.ffp=&f,.decoder=&d,.codecpar=parameters(&pa,0),.jsurface=(void*)1};IJKFF_Pipenode node={&o};subject=&o;
    AVCodecParameters *baseline=parameters(&pa,0);AVBSFContext *reference=NULL;assert(!ijk_amc_create_bsf(&o.bsfc,o.codecpar,st.time_base));assert(!ijk_amc_create_bsf(&reference,baseline,st.time_base));
    crop_opt_enabled=enabled;owned=queued=configured=format_failed=configure_failed=serial_during_config=serial_during_borrow=surface_change=flush_calls=packet_reads=0;int count=0;
    do{
        marker_serial=q.serial;send_marker=0;
        if(count==96||count==192){marker_serial=++q.serial;send_marker=1;av_bsf_flush(reference);}
        queued_source=&raw;d.packet_pending=0;assert(!prepare_input_packet(&node,true));AVPacket expected=filter(reference,&raw);exact(&o.filtered_packet,&expected);
        assert(!advance_hevc_generation(NULL,&node,true,1,NULL)&&!configured&&!o.h264_crop_committed&&!o.h264_baseline_restore_pending&&!o.h264_next_bsfc&&!o.h264_baseline_codecpar);
        ++o.input_packet_count;++count;av_packet_unref(&expected);av_packet_unref(&raw);
    }while(packet_from_file(fp,&raw));
    fclose(fp);assert(count==288&&flush_calls==2);printf("PASS real ordinary H264 B-frame stream enabled=%d packets=%d exact baseline bytes/PTS/DTS; two marker flushes; zero reconfigures\n",enabled,count);
    cleanup(&o);av_bsf_free(&reference);avcodec_parameters_free(&baseline);crop_opt_enabled=true;
}
#ifndef BASELINE_EPOCH_TEST_LIBRARY
int main(int argc,char **argv){
    assert(argc==6);AVPacket a=load(argv[1]),b=load(argv[2]),a_raw=load(argv[3]),b_raw=load(argv[4]);
    for(int mode=0;mode<=11;++mode)scenario_epoch(&a,&b,&a_raw,&b_raw,mode);
    no_commit(&a,&a_raw,0);no_commit(&a,&a_raw,1);
    ordinary_sequence(argv[5],0);ordinary_sequence(argv[5],1);
    av_packet_unref(&a);av_packet_unref(&b);av_packet_unref(&a_raw);av_packet_unref(&b_raw);return 0;
}
#endif
