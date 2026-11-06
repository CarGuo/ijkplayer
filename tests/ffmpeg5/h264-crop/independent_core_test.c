#define main frozen_parameter_test_main
#include "crop_parameters_test.c"
#undef main

static void denied(const HCPair *initial, const AVPacket *raw) {
    AVCodecParameters *current=parameters(initial,0),*next=NULL,*csd=NULL;
    AVBSFContext *staged=NULL;
    uint8_t *identity=current->extradata;
    assert(ijk_amc_h264_crop_parameters(current,current,raw,raw,(AVRational){1,90000},&next,&csd,&staged)==2);
    assert(current->extradata==identity&&!next&&!csd&&!staged);
    avcodec_parameters_free(&current);
}
static void repeat_transitions(const AVPacket *a,const AVPacket *b) {
    HCPair pa=pair(a);
    AVCodecParameters *current=parameters(&pa,4),*active=NULL;
    AVBSFContext *bsf=NULL;
    assert(!ijk_amc_create_bsf(&bsf,current,(AVRational){1,90000}));
    for(int i=0;i<40;++i) {
        const AVPacket *source=(i&1)?a:b;
        AVPacket raw=framing(source,4,0),filtered=filter(bsf,&raw),tail={0};
        AVCodecParameters *next=NULL,*csd=NULL;AVBSFContext *staged=NULL;
        assert(ijk_amc_h264_crop_parameters(current,active?active:bsf->par_out,&raw,&filtered,(AVRational){1,90000},&next,&csd,&staged)==1);
        assert(av_bsf_receive_packet(bsf,&tail)==AVERROR(EAGAIN));
        av_bsf_free(&bsf);avcodec_parameters_free(&current);avcodec_parameters_free(&active);
        bsf=staged;current=next;active=csd;
        assert(current->height==((i&1)?360:272));
        av_packet_unref(&raw);av_packet_unref(&filtered);
    }
    av_bsf_free(&bsf);avcodec_parameters_free(&current);avcodec_parameters_free(&active);
    puts("PASS 40 alternating complete-source A/B crop transitions with fresh BSFs");
}
static void extension(const AVPacket *a,const AVPacket *b) {
    HCPair pa=pair(a); assert(pa.sps.raw[1]==100);
    for(int length=2;length<=4;length+=2) {
        AVCodecParameters *current=parameters(&pa,length),*next=NULL,*csd=NULL;
        uint8_t *out=av_mallocz(current->extradata_size+4+AV_INPUT_BUFFER_PADDING_SIZE);assert(out);
        memcpy(out,current->extradata,current->extradata_size);
        memcpy(out+current->extradata_size,"\xfd\xf8\xf8\0",4);
        av_freep(&current->extradata);current->extradata=out;current->extradata_size+=4;
        AVBSFContext *bsf=NULL,*staged=NULL;assert(!ijk_amc_create_bsf(&bsf,current,(AVRational){1,90000}));
        AVPacket raw=framing(b,length,0),filtered=filter(bsf,&raw);
        assert(ijk_amc_h264_crop_parameters(current,bsf->par_out,&raw,&filtered,(AVRational){1,90000},&next,&csd,&staged)==1);
        assert((next->extradata[4]&3)+1==length);
        assert(!memcmp(next->extradata+next->extradata_size-4,"\xfd\xf8\xf8\0",4));
        avcodec_parameters_free(&current);avcodec_parameters_free(&next);avcodec_parameters_free(&csd);
        av_bsf_free(&bsf);av_bsf_free(&staged);av_packet_unref(&raw);av_packet_unref(&filtered);
    }
    puts("PASS supported avcC High-profile extensions remain byte-exact for 2/4-byte lengths");
}
static AVPacket duplicate_nal(const AVPacket *source,int type) {
    size_t cur=0,len;const uint8_t *nal;
    while(hc_nal(source->data,source->size,&cur,&nal,&len)>0) if((nal[0]&31)==type) {
        AVPacket out={0};assert(!av_new_packet(&out,source->size+len+4));
        if(type==7) {memcpy(out.data,"\0\0\0\1",4);memcpy(out.data+4,nal,len);memcpy(out.data+len+4,source->data,source->size);}
        else {memcpy(out.data,source->data,source->size);memcpy(out.data+source->size,"\0\0\0\1",4);memcpy(out.data+source->size+4,nal,len);}
        return out;
    }
    abort();
}
static void unsupported_shapes(const AVPacket *a,const AVPacket *b) {
    HCPair pa=pair(a);
    AVPacket duplicate=duplicate_nal(b,7);denied(&pa,&duplicate);av_packet_unref(&duplicate);
    duplicate=duplicate_nal(b,5);denied(&pa,&duplicate);av_packet_unref(&duplicate);
    AVPacket nonidr=framing(b,0,0);size_t cur=0,len;const uint8_t *nal;
    while(hc_nal(nonidr.data,nonidr.size,&cur,&nal,&len)>0) if((nal[0]&31)==5){((uint8_t*)nal)[0]=(nal[0]&~31)|1;break;}
    denied(&pa,&nonidr);av_packet_unref(&nonidr);
    puts("PASS duplicate source SPS, multi-slice AU, and non-IDR parameter changes cannot stage");
}
static uint32_t random_state=0x13579bdf;
static uint32_t random32(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void parser_mutation(const AVPacket *a,const AVPacket *b) {
    HCPair seeds[2]={pair(a),pair(b)};
    for(int i=0;i<100000;++i) {
        HCPair *seed=&seeds[i&1];uint8_t bytes[HC_SPS_BYTES+4];HCSPS s;HCPPS p;
        size_t length=(i%3)?seed->sps.raw_size:random32()%sizeof(bytes);
        memset(bytes,0,sizeof(bytes));memcpy(bytes,seed->sps.raw,seed->sps.raw_size);
        for(int j=0;j<1+(i%5);++j)bytes[random32()%sizeof(bytes)]^=random32();
        int ret=hc_parse_sps(bytes,length,&s);
        if(!ret){assert(s.valid&&s.width>0&&s.height>0&&s.visible_w>0&&s.visible_h>0&&s.crop_begin<s.crop_end&&s.crop_end<=s.payload_bits);}
        length=(i%3)?seed->pps.raw_size:random32()%sizeof(bytes);
        memset(bytes,0,sizeof(bytes));memcpy(bytes,seed->pps.raw,seed->pps.raw_size);
        for(int j=0;j<1+(i%5);++j)bytes[random32()%sizeof(bytes)]^=random32();
        ret=hc_parse_pps(bytes,length,&p);
        if(!ret)assert(p.valid&&p.id>=0&&p.id<=255&&p.sps_id>=0&&p.sps_id<=31);
    }
    puts("PASS 100000 deterministic SPS/PPS mutation pairs under bounded parser invariants");
}
int main(int argc,char **argv) {
    assert(argc==3);AVPacket a=load(argv[1]),b=load(argv[2]);
    extension(&a,&b);repeat_transitions(&a,&b);unsupported_shapes(&a,&b);parser_mutation(&a,&b);
    av_packet_unref(&a);av_packet_unref(&b);return 0;
}
