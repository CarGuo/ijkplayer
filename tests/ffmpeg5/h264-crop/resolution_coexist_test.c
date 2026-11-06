#define BASELINE_EPOCH_TEST_LIBRARY 1
#include "baseline_epoch_test.c"

/* mode 0/1: real changed C without/with a marker; 2/3: same-current B;
 * 4/5: short ignored side datum; 6/7: invalid >=7-byte side datum;
 * 8: valid initial A replacing B after a marker. */
static void coexist(const AVPacket *a,const AVPacket *b,const AVPacket *a_raw,
                    const AVPacket *b_raw,const AVPacket *c_extra,const AVPacket *c_raw,int mode){
    HCPair pa=pair(a),pb=pair(b);PacketQueue q={.serial=7};Decoder d={.queue=&q,.pkt_serial=7};
    AVStream st={.time_base={1,90000}};VideoState is={.video_st=&st};FFPlayer f={.is=&is,.mediacodec_handle_resolution_change=1};
    IJKFF_Pipenode_Opaque o={.ffp=&f,.decoder=&d,.codecpar=parameters(&pa,4),.jsurface=(void*)1};IJKFF_Pipenode node={&o};subject=&o;
    assert(!ijk_amc_create_bsf(&o.bsfc,o.codecpar,st.time_base));AVCodecParameters *initial=o.codecpar;
    crop_opt_enabled=true;owned=queued=configured=format_failed=configure_failed=serial_during_config=serial_during_borrow=surface_change=flush_calls=packet_reads=0;
    marker_serial=7;send_marker=0;queued_source=b_raw;
    assert(!prepare_input_packet(&node,true)&&!o.h264_crop_disabled);
    assert(!advance_hevc_generation(NULL,&node,true,1,NULL));assert(o.codecpar->height==pb.sps.visible_h&&o.h264_baseline_codecpar==initial&&o.h264_crop_committed&&configured==1);
    o.input_packet_count=4;d.packet_pending=0;
    AVPacket target={0};AVCodecParameters *description=NULL;
    if(mode<2){assert(!av_packet_ref(&target,c_raw));}
    else if(mode<4){target=framing(b,4,1);description=parameters(&pb,4);}
    else {assert(!av_packet_ref(&target,a_raw));description=parameters(&pa,4);}
    size_t extra_size=mode<2?c_extra->size:mode<4||mode==8?description->extradata_size:mode<6?1:8;
    uint8_t *extra=av_packet_new_side_data(&target,AV_PKT_DATA_NEW_EXTRADATA,extra_size);assert(extra);
    if(mode<2)memcpy(extra,c_extra->data,extra_size);
    else if(mode<4||mode==8)memcpy(extra,description->extradata,extra_size);
    else memset(extra,0,extra_size);
    AVBSFContext *before=o.bsfc;AVCodecParameters *before_par=o.codecpar;
    marker_serial=q.serial=(mode%2||mode==8)?8:7;send_marker=marker_serial==8;queued_source=&target;
    int ret=prepare_input_packet(&node,true);
    if(mode==6||mode==7){
        assert(ret<0&&o.codecpar==before_par&&o.bsfc==before&&o.h264_crop_committed&&o.h264_baseline_codecpar==initial&&o.h264_crop_disabled);
        assert(o.h264_baseline_restore_pending==(mode==7));
        printf("PASS coexist mode=%d invalid extradata stays terminal; old baseline ownership is not retired\n",mode);
    }else if(mode==4||mode==5){
        assert(!ret&&o.h264_crop_disabled&&o.h264_baseline_codecpar==initial&&o.h264_crop_committed);
        assert(o.h264_baseline_restore_pending==(mode==5));
        if(mode==4){marker_serial=q.serial=8;send_marker=1;queued_source=a_raw;d.packet_pending=0;assert(!prepare_input_packet(&node,true));}
        assert(o.h264_baseline_restore_pending);assert(advance_hevc_generation(NULL,&node,true,1,NULL)==1);
        assert(!o.h264_crop_committed&&!o.h264_baseline_restore_pending&&o.h264_baseline_codecpar==initial&&o.codecpar->height==pa.sps.visible_h);
        AVPacket ref=baseline_output(&pa,mode==5?&target:a_raw);exact(&o.filtered_packet,&ref);av_packet_unref(&ref);
        printf("PASS coexist mode=%d short ignored extradata preserves original A baseline restore semantics\n",mode);
    }else{
        assert(!ret&&o.h264_crop_disabled&&!o.h264_baseline_codecpar&&!o.h264_crop_committed&&!o.h264_baseline_restore_pending&&!o.h264_next_bsfc&&!o.hevc_next_csd&&!o.hevc_next_codecpar&&!o.hevc_active_csd);
        int width=mode<2?480:640,height=mode<2||mode<4?272:360;
        assert(o.codecpar->width==width&&o.codecpar->height==height&&o.bsfc->par_out->width==width&&o.bsfc->par_out->height==height);
        assert(o.aformat_need_recreate==(mode<2||mode==8));assert(surface_change==(mode<2||mode==8));
        AVCodecParameters *old_control=parameters(&pa,4);int updated=ijk_amc_update_h264_extradata(old_control,&target);assert(updated==(mode==8?0:1));
        AVBSFContext *reference=NULL;assert(!ijk_amc_create_bsf(&reference,old_control,st.time_base));AVPacket ref=filter(reference,&target);exact(&o.filtered_packet,&ref);av_packet_unref(&ref);av_bsf_free(&reference);avcodec_parameters_free(&old_control);
        // Compare the next real flush to the old path after its authoritative
        // update. The real Surface handler still owns the existing format request.
        AVPacket next={0};assert(!av_packet_ref(&next,&target));av_packet_free_side_data(&next);
        marker_serial=++q.serial;send_marker=1;queued_source=&next;d.packet_pending=0;
        assert(!prepare_input_packet(&node,true)&&!o.h264_baseline_restore_pending&&!o.h264_crop_committed&&!o.h264_baseline_codecpar);
        assert(o.codecpar->width==width&&o.codecpar->height==height);
        assert(!ijk_amc_create_bsf(&reference,o.codecpar,st.time_base));ref=filter(reference,&next);exact(&o.filtered_packet,&ref);
        printf("PASS coexist mode=%d authoritative description=%dx%d survives next flush; old format request=%d\n",mode,width,height,o.aformat_need_recreate);
        av_packet_unref(&ref);av_packet_unref(&next);av_bsf_free(&reference);
    }
    cleanup(&o);av_packet_unref(&target);avcodec_parameters_free(&description);
}
int main(int argc,char **argv){
    assert(argc==7);AVPacket a=load(argv[1]),b=load(argv[2]),a_raw=load(argv[3]),b_raw=load(argv[4]),extra=load(argv[5]),c=load(argv[6]);
    for(int mode=0;mode<=8;++mode)coexist(&a,&b,&a_raw,&b_raw,&extra,&c,mode);
    av_packet_unref(&a);av_packet_unref(&b);av_packet_unref(&a_raw);av_packet_unref(&b_raw);av_packet_unref(&extra);av_packet_unref(&c);return 0;
}
