/* Production packet queues/live filter + real FFmpeg demux/decode.
 * The consumer simulates audible-media pacing; no Android device is claimed. */
#include "../../ijkmedia/ijkplayer/ff_ffplay.c"
#include <pthread.h>

Uint64 SDL_GetTickHR(void) { return av_gettime_relative() / 1000; }
void SDL_ProfilerReset(SDL_Profiler *p, int max_sample) { memset(p, 0, sizeof(*p)); }
float SDL_SpeedSamplerAdd(SDL_SpeedSampler *s, int log, const char *tag) { return 0; }
static int hardware_flushes;
int ffpipenode_flush(IJKFF_Pipenode *node) { hardware_flushes++; return 0; }
static SDL_AudioPlaybackSnapshot fake_position;
static int get_fake_position(SDL_Aout *aout, SDL_AudioPlaybackSnapshot *position)
{ *position = fake_position; return 0; }

static void initialize(FFPlayer *ffp, VideoState *is, AVFormatContext *ic)
{
    memset(ffp, 0, sizeof(*ffp)); memset(is, 0, sizeof(*is));
    ffp->is = is; is->ic = ic; is->audio_stream = is->video_stream = is->subtitle_stream = -1;
    ffp->rtsp_live_max_buffer_ms = 1200; ffp->pf_playback_rate = 1;
    ffp->dcc.max_buffer_size = MAX_QUEUE_SIZE;
    assert(packet_queue_init(&is->audioq) == 0);
    assert(packet_queue_init(&is->videoq) == 0);
    assert(packet_queue_init(&is->subtitleq) == 0);
    packet_queue_start(&is->audioq); packet_queue_start(&is->videoq); packet_queue_start(&is->subtitleq);
    init_clock(&is->extclk, &is->extclk.serial);
    init_clock(&is->audclk, &is->audioq.serial);
    init_clock(&is->vidclk, &is->videoq.serial);
    is->sampq.mutex = SDL_CreateMutex();
    is->eof_clock.mutex = SDL_CreateMutex();
    is->play_mutex = SDL_CreateMutex();
    assert(is->sampq.mutex && is->eof_clock.mutex && is->play_mutex);
}
static void destroy(VideoState *is)
{
    packet_queue_destroy(&is->audioq); packet_queue_destroy(&is->videoq); packet_queue_destroy(&is->subtitleq);
    SDL_DestroyMutex(is->sampq.mutex); SDL_DestroyMutex(is->eof_clock.mutex); SDL_DestroyMutex(is->play_mutex);
}
static AVPacket make_packet(int stream, int64_t pts, int key)
{
    AVPacket p = { 0 }; assert(av_new_packet(&p, 160) == 0);
    p.stream_index = stream; p.pts = p.dts = pts; p.flags = key ? AV_PKT_FLAG_KEY : 0; p.duration = 20;
    return p;
}
static void unit_tests(void)
{
    AVFormatContext *ic = avformat_alloc_context();
    ic->iformat = av_find_input_format("rtsp"); ic->duration = AV_NOPTS_VALUE;
    AVStream *a = avformat_new_stream(ic, NULL), *v = avformat_new_stream(ic, NULL);
    a->time_base = v->time_base = (AVRational){1,1000};
    FFPlayer ffp; VideoState is; initialize(&ffp, &is, ic);
    is.audio_stream=0; is.video_stream=1; is.audio_st=a; is.video_st=v;
    assert(rtsp_live_buffer_enabled(&ffp));
    assert(rtsp_live_buffer_option_valid(0) && rtsp_live_buffer_option_valid(500) && rtsp_live_buffer_option_valid(60000));
    assert(!rtsp_live_buffer_option_valid(1) && !rtsp_live_buffer_option_valid(499) && !rtsp_live_buffer_option_valid(60001));
    ffp.dcc.current_high_water_mark_in_ms=5000;
    assert(rtsp_live_update_high_water_mark(&ffp));
    assert(ffp.dcc.current_high_water_mark_in_ms==600);
    ic->duration=AV_TIME_BASE; assert(!rtsp_live_buffer_enabled(&ffp)); ic->duration=AV_NOPTS_VALUE;
    is.pause_req=1; assert(!rtsp_live_buffer_enabled(&ffp)); is.pause_req=0;
    is.step=1; assert(!rtsp_live_buffer_enabled(&ffp)); is.step=0;
    is.seek_req=1; assert(!rtsp_live_buffer_enabled(&ffp)); is.seek_req=0;
    ffp.pf_playback_rate=2; assert(!rtsp_live_buffer_enabled(&ffp)); ffp.pf_playback_rate=1;
    const AVInputFormat *rtsp=ic->iformat; ic->iformat=av_find_input_format("mov");
    assert(!rtsp_live_buffer_enabled(&ffp)); ic->iformat=rtsp;
    ffp.rtsp_live_max_buffer_ms=0; assert(!rtsp_live_buffer_enabled(&ffp)); ffp.rtsp_live_max_buffer_ms=1200;
    AVPacket p=make_packet(0,0,1); assert(packet_queue_put(&is.audioq,&p)==0);
    p=make_packet(1,0,1); assert(packet_queue_put(&is.videoq,&p)==0);
    int as=is.audioq.serial, vs=is.videoq.serial;
    /* Simulate an in-flight AudioTrack write before the live flush. */
    SDL_AudioSubmission old_submission={0};
    eof_clock_audio_stage(&is,1.0,as,0,&old_submission);
    is.eof_clock.audio_end=0.5;
    p=make_packet(0,1220,1); assert(rtsp_live_filter_packet(&ffp,&p)==0); av_packet_unref(&p);
    assert(is.audioq.serial==as+1 && is.videoq.serial==vs+1 && is.rtsp_live_wait_keyframe);
    assert(is.audioq.nb_packets==1 && is.videoq.nb_packets==1); /* flush markers only */
    double accepted_end=-1;
    assert(eof_clock_audio_submission(&is,&old_submission,SDL_AUDIO_SUBMIT_COMMIT,0,0.1,10,&accepted_end,NULL)==SDL_AUDIO_SUBMIT_STALE);
    assert(accepted_end==-1 && is.eof_clock.audio_end==0.5);
    SDL_AudioSubmission newer={0};
    eof_clock_audio_stage(&is,2.5,is.audioq.serial,0,&newer);
    assert(eof_clock_audio_submission(&is,&old_submission,SDL_AUDIO_SUBMIT_CANCEL,0,0,10,&accepted_end,NULL)==SDL_AUDIO_SUBMIT_READY);
    assert(is.eof_clock.audio_submit_pending && is.eof_clock.audio_submit_token==newer.token);
    assert(eof_clock_audio_submission(&is,&newer,SDL_AUDIO_SUBMIT_COMMIT,0,0.1,10,&accepted_end,NULL)==SDL_AUDIO_SUBMIT_READY);
    assert(accepted_end==2.5 && is.eof_clock.audio_serial==is.audioq.serial);
    SDL_Aout backend={0}; backend.func_get_playback_position=get_fake_position;
    is.audclk.source_aout=&backend; is.platform_rate_mode=1;
    fake_position=(SDL_AudioPlaybackSnapshot){.valid=1,.generation=is.audioq.serial,.epoch=7};
    SDL_AudioRateApplied rate={.sequence=1,.output_epoch=7,.generation=as,.speed=2,.pitch=1,.apply_before=10,.apply_after=10.01,.observed_at=10.02};
    assert(!eof_clock_rate_applied(&is,&rate)); /* delayed old-generation rate ACK */
    rate.generation=is.audioq.serial;
    assert(eof_clock_rate_applied(&is,&rate));
    assert(is.eof_clock.video_applied_rate==2);
    for(int n=0;n<1000;n++) {
        p=make_packet(1,1240+n*40,0); assert(rtsp_live_filter_packet(&ffp,&p)==0); av_packet_unref(&p);
    }
    assert(is.audioq.nb_packets==1 && is.videoq.nb_packets==1); /* absent keyframe stays bounded */
    ffp.pf_playback_rate=2;
    p=make_packet(1,1600,0); assert(rtsp_live_filter_packet(&ffp,&p)==0); av_packet_unref(&p);
    p=make_packet(1,2000,1); assert(rtsp_live_filter_packet(&ffp,&p)==1); av_packet_unref(&p);
    p=make_packet(0,1980,1); assert(rtsp_live_filter_packet(&ffp,&p)==0); av_packet_unref(&p);
    p=make_packet(0,2000,1); assert(rtsp_live_filter_packet(&ffp,&p)==1); av_packet_unref(&p);
    assert(!is.rtsp_live_wait_keyframe && !is.rtsp_live_drop_audio_before);
    ffp.pf_playback_rate=1;
    /* A/V discontinuity does not retain old input even when PTS are absent. */
    ffp.dcc.max_buffer_size=600;
    p=make_packet(0,AV_NOPTS_VALUE,1); assert(packet_queue_put(&is.audioq,&p)==0);
    p=make_packet(0,AV_NOPTS_VALUE,1); assert(rtsp_live_queue_over_limit(&ffp,&p)); av_packet_unref(&p);
    destroy(&is);
    initialize(&ffp,&is,ic); is.audio_stream=0; is.audio_st=a;
    p=make_packet(0,0,1); packet_queue_put(&is.audioq,&p);
    p=make_packet(0,1220,1); assert(rtsp_live_filter_packet(&ffp,&p)==1); av_packet_unref(&p);
    assert(!is.rtsp_live_wait_keyframe && is.audioq.serial==2);
    destroy(&is);
    initialize(&ffp,&is,ic); is.video_stream=1; is.video_st=v;
    p=make_packet(1,0,1); packet_queue_put(&is.videoq,&p);
    p=make_packet(1,1240,0); assert(rtsp_live_filter_packet(&ffp,&p)==0); av_packet_unref(&p);
    p=make_packet(1,2000,1); assert(rtsp_live_filter_packet(&ffp,&p)==1); av_packet_unref(&p);
    destroy(&is);
    initialize(&ffp,&is,ic); is.audio_stream=0; is.audio_st=a;
    p=make_packet(0,1000000,1); packet_queue_put(&is.audioq,&p);
    p=make_packet(0,0,1); assert(rtsp_live_filter_packet(&ffp,&p)==1);
    assert(is.audioq.serial==2); packet_queue_put(&is.audioq,&p);
    for(int n=1;n<=250;n++) {
        p=make_packet(0,n*20,1); assert(rtsp_live_filter_packet(&ffp,&p)==1);
        packet_queue_put(&is.audioq,&p);
    }
    assert(is.audioq.serial>=5 && is.audioq.nb_packets<64); /* old epoch cannot hide new backlog */
    packet_queue_flush(&is.audioq); packet_queue_put(&is.audioq,&flush_pkt);
    p=make_packet(0,1000,1); p.dts=AV_NOPTS_VALUE; packet_queue_put(&is.audioq,&p);
    p=make_packet(0,960,1); p.dts=AV_NOPTS_VALUE;
    assert(!rtsp_live_queue_over_limit(&ffp,&p)); av_packet_unref(&p); /* PTS reordering */
    ffp.dcc.max_buffer_size=1000;
    av_init_packet(&p); assert(av_new_packet(&p,2000)==0); p.stream_index=0; p.pts=p.dts=1040;
    assert(rtsp_live_filter_packet(&ffp,&p)==1); packet_queue_put(&is.audioq,&p);
    assert(is.audioq.size>1000 && is.audioq.size<=2000+2*(int)sizeof(MyAVPacketList));
    destroy(&is);
    /* Exercise production buffering itself, including a restored oversized
     * first-frame HWM. Two real packets per stream must reach resume. */
    initialize(&ffp,&is,ic); is.audio_stream=0; is.video_stream=1; is.audio_st=a; is.video_st=v;
    ffp.rtsp_live_max_buffer_ms=500; ffp.packet_buffering=1;
    ffp.dcc.current_high_water_mark_in_ms=5000;
    ffp.dcc.next_high_water_mark_in_ms=1000; ffp.dcc.last_high_water_mark_in_ms=5000;
    ffp.dcc.high_water_mark_in_bytes=256*1024;
    msg_queue_init(&ffp.msg_queue); ffp.msg_queue.abort_request=0;
    is.buffer_indicator_queue=&is.audioq; is.buffering_on=1; is.paused=1;
    for(int n=0;n<13;n++) {
        p=make_packet(0,n*20,1); packet_queue_put(&is.audioq,&p);
        p=make_packet(1,n*20,n==0); packet_queue_put(&is.videoq,&p);
    }
    ffp_statistic_l(&ffp); ffp_check_buffering_l(&ffp);
    assert(!is.buffering_on && !is.paused);
    assert(ffp.dcc.current_high_water_mark_in_ms<=250);
    msg_queue_destroy(&ffp.msg_queue);
    destroy(&is); avformat_free_context(ic);
    puts("PASS live RTSP: opt-in/VOD/protocol/pause/step/seek/rate guards, paired serial flush, keyframe gating, A/V alignment, missing-keyframe byte bound, audio-only/video-only");
}

typedef struct NetworkRun {
    FFPlayer ffp; VideoState is;
    int64_t begin, stop_at;
    int stalls;
    int decoded[2], decode_errors, serial[2], jumps[2];
    double last_pts[2];
    AVCodecContext *decoder[2];
    int64_t peak_bytes;
} NetworkRun;
static int interrupt_read(void *opaque) { return av_gettime_relative() >= ((NetworkRun *)opaque)->stop_at; }

static void consume_one(NetworkRun *r,int track)
{
    PacketQueue *q=track ? &r->is.videoq : &r->is.audioq;
    AVPacket p={0}; int serial;
    if(packet_queue_get(q,&p,0,&serial)<=0) return;
    if(p.data==flush_pkt.data) {
        avcodec_flush_buffers(r->decoder[track]);
        r->serial[track]=serial; r->last_pts[track]=NAN; return;
    }
    AVFrame *frame=av_frame_alloc();
    int ret=avcodec_send_packet(r->decoder[track],&p);
    if(ret<0) r->decode_errors++;
    while((ret=avcodec_receive_frame(r->decoder[track],frame))>=0) {
        AVRational tb=track ? r->is.video_st->time_base : r->is.audio_st->time_base;
        if(frame->best_effort_timestamp!=AV_NOPTS_VALUE)
            r->last_pts[track]=frame->best_effort_timestamp*av_q2d(tb);
        r->decoded[track]++; av_frame_unref(frame);
    }
    if(ret!=AVERROR(EAGAIN) && ret!=AVERROR_EOF) r->decode_errors++;
    av_frame_free(&frame); av_packet_unref(&p);
}
static void *consumer(void *opaque)
{
    NetworkRun *r=opaque; int tick=0,stalls=0,last_report=-1;
    int64_t next=r->begin;
    while(av_gettime_relative()<r->stop_at) {
        int64_t now=av_gettime_relative();
        double elapsed=(now-r->begin)/1000000.0;
        if(r->stalls<0 && stalls==0 && elapsed>=4.0) {
            av_usleep(3000000); next+=3000000; stalls++; continue;
        }
        if(r->stalls && stalls<r->stalls && elapsed>=4.0*(stalls+1)) {
            av_usleep(800000); next+=800000; stalls++; continue;
        }
        if(now<next) { av_usleep(1000); continue; }
        next+=20000;
        consume_one(r,0);
        if(!(tick++%2)) consume_one(r,1);
        SDL_LockMutex(r->is.audioq.mutex); SDL_LockMutex(r->is.videoq.mutex);
        int64_t bytes=(int64_t)r->is.audioq.size+r->is.videoq.size;
        if(bytes>r->peak_bytes) r->peak_bytes=bytes;
        if((int)elapsed!=last_report) {
            last_report=(int)elapsed;
            printf("{\"t\":%.3f,\"wall_us\":%"PRId64",\"audio_pts\":%.6f,\"video_pts\":%.6f,\"queue_bytes\":%"PRId64",\"audio_serial\":%d,\"video_serial\":%d}\n",
                elapsed,now,isfinite(r->last_pts[0])?r->last_pts[0]:-1,
                isfinite(r->last_pts[1])?r->last_pts[1]:-1,bytes,r->is.audioq.serial,r->is.videoq.serial);
            fflush(stdout);
        }
        SDL_UnlockMutex(r->is.videoq.mutex); SDL_UnlockMutex(r->is.audioq.mutex);
    }
    return NULL;
}
static void network_test(const char *url,const char *transport,int cap,int seconds,int stalls)
{
    NetworkRun r={0}; AVFormatContext *ic=avformat_alloc_context(); AVDictionary *opts=NULL;
    r.stop_at=av_gettime_relative()+(seconds+10)*1000000LL;
    ic->interrupt_callback=(AVIOInterruptCB){interrupt_read,&r};
    av_dict_set(&opts,"rtsp_transport",transport,0); av_dict_set(&opts,"timeout","1000000",0);
    av_dict_set(&opts,"probesize","32768",0); av_dict_set(&opts,"analyzeduration","0",0);
    assert(avformat_open_input(&ic,url,NULL,&opts)>=0);
    assert(avformat_find_stream_info(ic,NULL)>=0); av_dict_free(&opts);
    initialize(&r.ffp,&r.is,ic); r.ffp.rtsp_live_max_buffer_ms=cap;
    printf("{\"setup\":true,\"duration_us\":%"PRId64",\"live_policy_active\":%d}\n",ic->duration,rtsp_live_buffer_enabled(&r.ffp));
    r.is.audio_stream=av_find_best_stream(ic,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);
    r.is.video_stream=av_find_best_stream(ic,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);
    assert(r.is.audio_stream>=0 && r.is.video_stream>=0);
    r.is.audio_st=ic->streams[r.is.audio_stream]; r.is.video_st=ic->streams[r.is.video_stream];
    for(int i=0;i<2;i++) {
        AVStream *st=i?r.is.video_st:r.is.audio_st;
        const AVCodec *c=avcodec_find_decoder(st->codecpar->codec_id);
        r.decoder[i]=avcodec_alloc_context3(c); assert(r.decoder[i]);
        assert(avcodec_parameters_to_context(r.decoder[i],st->codecpar)>=0);
        r.decoder[i]->pkt_timebase=st->time_base;
        assert(avcodec_open2(r.decoder[i],c,NULL)>=0); r.last_pts[i]=NAN;
    }
    r.begin=av_gettime_relative(); r.stop_at=r.begin+seconds*1000000LL; r.stalls=stalls==1?4:stalls;
    pthread_t tid; assert(pthread_create(&tid,NULL,consumer,&r)==0);
    int ret, paused_once=0; AVPacket p={0};
    while((ret=av_read_frame(ic,&p))>=0) {
        if(stalls<0 && !paused_once && av_gettime_relative()-r.begin>=4000000) {
            r.is.pause_req=1;
            assert(!rtsp_live_buffer_enabled(&r.ffp));
            av_usleep(3000000);
            r.is.pause_req=0;
            paused_once=1;
        }
        int keep=rtsp_live_filter_packet(&r.ffp,&p); assert(keep>=0);
        if(keep && p.stream_index==r.is.audio_stream) packet_queue_put(&r.is.audioq,&p);
        else if(keep && p.stream_index==r.is.video_stream) packet_queue_put(&r.is.videoq,&p);
        else av_packet_unref(&p);
    }
    pthread_join(tid,NULL);
    printf("{\"summary\":true,\"cap_ms\":%d,\"decoded_audio\":%d,\"decoded_video\":%d,\"decode_errors\":%d,\"peak_queue_bytes\":%"PRId64",\"audio_serial\":%d,\"video_serial\":%d}\n",
       cap,r.decoded[0],r.decoded[1],r.decode_errors,r.peak_bytes,r.is.audioq.serial,r.is.videoq.serial);
    avcodec_free_context(&r.decoder[0]); avcodec_free_context(&r.decoder[1]);
    destroy(&r.is); avformat_close_input(&ic);
    assert(r.decode_errors==0);
}
int main(int argc,char **argv)
{
    flush_pkt.data=(uint8_t *)&flush_pkt; av_log_set_level(AV_LOG_WARNING);
    if(argc==1) unit_tests();
    else { assert(argc==6); network_test(argv[1],argv[2],atoi(argv[3]),atoi(argv[4]),atoi(argv[5])); }
    return 0;
}
