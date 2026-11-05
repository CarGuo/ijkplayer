#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../ijkmedia/ijkplayer/android/pipeline/ffamc_ffmpeg.h"
#include "../../../ijkmedia/ijkplayer/android/pipeline/ffamc_h264_crop.h"

static AVPacket load(const char *path) {
    AVPacket p = {0}; FILE *f = fopen(path, "rb"); assert(f);
    assert(!fseek(f, 0, SEEK_END)); long n = ftell(f); assert(n > 0); rewind(f);
    assert(!av_new_packet(&p, n)); assert(fread(p.data, 1, n, f) == (size_t)n); fclose(f);
    p.pts = 1009; p.dts = 900; p.duration = 109; p.flags = AV_PKT_FLAG_KEY; return p;
}
static HCPair pair(const AVPacket *p) {
    HCMap *m = av_mallocz(sizeof(*m)); HCAU au; assert(m);
    assert(!hc_access_unit(m, p->data, p->size, &au)); assert(au.idr && !au.no_output_prior);
    av_free(m); return au.pair;
}
static AVCodecParameters *parameters(const HCPair *p, int length) {
    AVCodecParameters *par = avcodec_parameters_alloc(); assert(par);
    par->codec_id = AV_CODEC_ID_H264; par->codec_type = AVMEDIA_TYPE_VIDEO;
    assert(!hc_replace_extradata(par, p, p, 1));
    if (length) {
        uint8_t b[HC_SPS_BYTES + HC_PPS_BYTES + 32]; size_t n = 0;
        b[n++]=1; b[n++]=p->sps.raw[1]; b[n++]=p->sps.raw[2]; b[n++]=p->sps.raw[3];
        b[n++]=0xfc|(length-1); b[n++]=0xe1;
        b[n++]=p->sps.raw_size>>8; b[n++]=p->sps.raw_size;
        memcpy(b+n,p->sps.raw,p->sps.raw_size); n+=p->sps.raw_size;
        b[n++]=1; b[n++]=p->pps.raw_size>>8; b[n++]=p->pps.raw_size;
        memcpy(b+n,p->pps.raw,p->pps.raw_size); n+=p->pps.raw_size;
        av_freep(&par->extradata); par->extradata=av_mallocz(n+AV_INPUT_BUFFER_PADDING_SIZE);
        assert(par->extradata); memcpy(par->extradata,b,n); par->extradata_size=n;
    }
    return par;
}
static AVPacket framing(const AVPacket *p, int length, int naked) {
    AVPacket out={0}; assert(!av_new_packet(&out,p->size+64));
    size_t cur=0,n=0,len; const uint8_t *nal; int ret;
    while((ret=hc_nal(p->data,p->size,&cur,&nal,&len))>0) {
        if(naked && (nal[0]&31)!=5) continue;
        if(length) { assert(len<((uint64_t)1<<(8*length))); for(int i=length-1;i>=0;--i) out.data[n++]=len>>(i*8); }
        else {memcpy(out.data+n,"\0\0\0\1",4);n+=4;}
        memcpy(out.data+n,nal,len);n+=len;
    }
    assert(!ret);out.size=n;out.pts=p->pts;out.dts=p->dts;out.duration=p->duration;return out;
}
static AVPacket filter(AVBSFContext *b, const AVPacket *p) {
    AVPacket in={0},out={0};assert(!av_packet_ref(&in,p));
    assert(!av_bsf_send_packet(b,&in));assert(!av_bsf_receive_packet(b,&out));return out;
}
static void check_case(const AVPacket *a,const AVPacket *b,int length) {
    HCPair pa=pair(a),pb=pair(b); assert(hc_change(&pa,&pb)==1);
    AVCodecParameters *active=parameters(&pa,length),*next=NULL,*csd=NULL;
    AVBSFContext *old=NULL,*staged=NULL;
    assert(!ijk_amc_create_bsf(&old,active,(AVRational){1,90000}));
    AVPacket raw=framing(b,length,0),held=filter(old,&raw),saved={0};
    assert(!av_packet_ref(&saved,&held)); uint8_t *active_bytes=active->extradata;
    assert(ijk_amc_h264_crop_parameters(active,old->par_out,&raw,&held,(AVRational){1,90000},&next,&csd,&staged)==1);
    assert(active->extradata==active_bytes && active->width==pa.sps.visible_w);
    assert(held.data==saved.data && held.size==saved.size && held.pts==saved.pts && held.dts==saved.dts);
    assert(next->width==pb.sps.visible_w && next->height==pb.sps.visible_h);
    if(length) assert((next->extradata[4]&3)+1==length);
    AVPacket tail={0}; assert(av_bsf_receive_packet(old,&tail)==AVERROR(EAGAIN));
    av_bsf_free(&old);avcodec_parameters_free(&active);old=staged;staged=NULL;active=next;next=NULL;
    av_bsf_flush(old);AVPacket naked=framing(b,length,1),converted=filter(old,&naked);
    if(length) { HCPair check=pair(&converted); assert(!hc_change(&pb,&check)); }
    else assert(converted.size==naked.size && !memcmp(converted.data,naked.data,naked.size));
    // A headerless source cannot claim the PS injected by the filter as fresh.
    assert(!ijk_amc_h264_crop_parameters(active,csd,&naked,&held,(AVRational){1,90000},&next,&csd,&staged));
    av_packet_unref(&tail);av_packet_unref(&raw);av_packet_unref(&held);av_packet_unref(&saved);
    av_packet_unref(&naked);av_packet_unref(&converted);av_bsf_free(&old);avcodec_parameters_free(&active);avcodec_parameters_free(&csd);
    printf("PASS retained AU/cache/flush length=%d\n",length);
}
static void unsupported(const AVPacket *a,const AVPacket *b) {
    HCPair pa=pair(a);AVCodecParameters *current=parameters(&pa,4),*next=NULL,*csd=NULL;
    AVBSFContext *bsf=NULL,*staged=NULL;assert(!ijk_amc_create_bsf(&bsf,current,(AVRational){1,90000}));
    AVPacket raw=framing(b,4,0),held=filter(bsf,&raw);uint8_t *identity=current->extradata;
    current->extradata[5]=0xe2; // duplicate/multiple SPS layout is never rebuilt as one pair
    assert(ijk_amc_h264_crop_parameters(current,bsf->par_out,&raw,&held,(AVRational){1,90000},&next,&csd,&staged)==2);
    assert(!next&&!csd&&!staged&&current->extradata==identity);current->extradata[5]=0xe1;
    assert(av_packet_new_side_data(&raw,AV_PKT_DATA_NEW_EXTRADATA,1));
    assert(ijk_amc_h264_crop_parameters(current,bsf->par_out,&raw,&held,(AVRational){1,90000},&next,&csd,&staged)==2);
    assert(!next&&!csd&&!staged&&current->extradata==identity);av_packet_free_side_data(&raw);
    AVPacket same=framing(a,4,0),same_held=filter(bsf,&same);
    assert(!ijk_amc_h264_crop_parameters(current,bsf->par_out,&same,&same_held,(AVRational){1,90000},&next,&csd,&staged));
    // A valid parameter identity change outside cropping cannot enter the drain.
    size_t cur=0,len;const uint8_t *nal;AVPacket changed=framing(b,0,0);
    while(hc_nal(changed.data,changed.size,&cur,&nal,&len)>0) if((nal[0]&31)==7) {((uint8_t*)nal)[3]++;break;}
    AVCodecParameters *annex=parameters(&pa,0);
    assert(ijk_amc_h264_crop_parameters(annex,annex,&changed,&changed,(AVRational){1,90000},&next,&csd,&staged)==2);
    assert(!next&&!csd&&!staged);av_packet_unref(&changed);avcodec_parameters_free(&annex);
    av_packet_unref(&same);av_packet_unref(&same_held);av_packet_unref(&raw);av_packet_unref(&held);
    avcodec_parameters_free(&current);av_bsf_free(&bsf);puts("PASS unchanged and unsupported layout/side-data/semantic bypass");
}
int main(int argc,char **argv) {
    assert(argc==3);AVPacket a=load(argv[1]),b=load(argv[2]);
    unsupported(&a,&b);
    check_case(&a,&b,0);check_case(&a,&b,2);check_case(&a,&b,4);
    if(a.size<200 && b.size<200) check_case(&a,&b,1);
    av_packet_unref(&a);av_packet_unref(&b);return 0;
}
