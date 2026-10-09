static int assertions,failures;
static void check(int ok,const char *name) { ++assertions; if(!ok){++failures;fprintf(stderr,"FAIL %s\n",name);} }
static void near(double got,double want,const char *name) { ++assertions; if(!isfinite(got)||fabs(got-want)>1e-7){++failures;fprintf(stderr,"FAIL %s got=%.9f expected=%.9f\n",name,got,want);} }
static SDL_mutex *new_mutex(void) { SDL_mutex*m=malloc(sizeof(*m)); if(!m||pthread_mutex_init(m,NULL))abort(); return m; }
static SDL_cond *new_cond(void) { SDL_cond*c=malloc(sizeof(*c)); if(!c||pthread_cond_init(c,NULL))abort(); return c; }
static void setup(FFPlayer*f,VideoState*s) {
    memset(f,0,sizeof(*f));memset(s,0,sizeof(*s));f->is=s;active_player=f;f->pf_playback_rate=1.0;
    s->eof_clock.mutex=new_mutex();s->sampq.mutex=new_mutex();s->sampq.cond=new_cond();s->pictq.mutex=new_mutex();s->pictq.cond=new_cond();s->audioq.mutex=new_mutex();s->videoq.mutex=new_mutex();
    s->sampq.pktq=&s->audioq;s->pictq.pktq=&s->videoq;s->sampq.max_size=s->pictq.max_size=4;
    s->audioq.serial=s->videoq.serial=s->auddec.pkt_serial=s->audio_clock_serial=6;
    s->eof_clock.video_applied_rate=1.0;s->audio_buffer_media_rate=1.0;
    s->eof_clock.audio_serial=s->eof_clock.audio_finished_serial=s->eof_clock.video_serial=-1;
    s->eof_clock.audio_end=s->eof_clock.video_pts=s->eof_clock.video_end=NAN;
    s->audclk.queue_serial=&s->audioq.serial;s->vidclk.queue_serial=&s->videoq.serial;s->extclk.queue_serial=&s->extclk.serial;
    s->audclk.speed=s->vidclk.speed=s->extclk.speed=1;
    s->audio_st=s->video_st=s;s->av_sync_type=f->av_sync_type=AV_SYNC_AUDIO_MASTER;
    s->audio_tgt.frame_size=4;s->audio_tgt.bytes_per_sec=4000;s->audio_volume=128;s->latest_audio_seek_load_serial=-1;
    now_us=100000000;decode_delay_us=latency_delay_us=manual_delay_us=0;manual_abort=manual_seek=0;backend_latency=.1;decode_probe=NULL;decode_mode=0;decode_calls=latency_calls=notify_calls=0;
    set_clock(&s->audclk,7.9,6);set_clock(&s->vidclk,7.5,6);set_clock(&s->extclk,7.9,6);
    atomic_store(&wait_entered,0);atomic_store(&hold_before_wait,0);atomic_store(&allow_wait,0);
}
static void teardown(VideoState*s) {
    SDL_mutex*ms[]={s->eof_clock.mutex,s->sampq.mutex,s->pictq.mutex,s->audioq.mutex,s->videoq.mutex};
    for(unsigned i=0;i<sizeof(ms)/sizeof(ms[0]);i++){if(pthread_mutex_destroy(ms[i]))abort();free(ms[i]);}
    pthread_cond_destroy(s->sampq.cond);pthread_cond_destroy(s->pictq.cond);free(s->sampq.cond);free(s->pictq.cond);
}
static void terminal(FFPlayer*f,VideoState*s,double audio_end,double video_pts,double duration) {
    setup(f,s);eof_clock_audio_delivered(s,audio_end,6,0,.1,100.0);eof_clock_video_presented(s,video_pts,duration,6);eof_clock_audio_finished(s,6);eof_clock_demux(s,1);s->auddec.finished=6;
    now_us=100100000;
}
