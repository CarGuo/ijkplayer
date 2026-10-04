static void setup_case(FFPlayer *f,VideoState *s){
 setup(f,s);s->play_mutex=new_mutex();s->continue_read_thread=new_cond();s->ic=&s->format;s->format.duration=8000000;s->seek_pos=AV_NOPTS_VALUE;s->requested_av_sync_type=AV_SYNC_AUDIO_MASTER;
 set_clock_at(&s->audclk,.156,6,100.0);s->audio_tgt.bytes_per_sec=192000;backend_latency=.1776666666667;
 atomic_store(&probe_lock_enabled,0);atomic_store(&probe_lock_entered,0);
}
static void close_case(VideoState*s){check(held_count==0,"all clock locks released");pthread_mutex_destroy(s->play_mutex);free(s->play_mutex);pthread_cond_destroy(s->continue_read_thread);free(s->continue_read_thread);teardown(s);}
static SDL_AudioSubmission stage(FFPlayer*f,double end,int serial,int pending){SDL_AudioSubmission sub;eof_clock_audio_stage(f->is,end,serial,pending,&sub);return sub;}
static int commit(FFPlayer*f,SDL_AudioSubmission*s){return sdl_audio_submission_callback(f,s,SDL_AUDIO_SUBMIT_COMMIT,0);}
static void request_and_dispatch_pause(void){
 FFPlayer f;VideoState s;setup_case(&f,&s);IjkMediaPlayer mp={.ffplayer=&f};now_us=100165000;
 check(ijkmp_pause_l(&mp)==0,"actual public pause request accepted");check(!s.paused,"public pause remains asynchronous");long before=ffp_get_current_position_l(&f);
 now_us+=11000;ffp_pause_l(&f);long held=ffp_get_current_position_l(&f);now_us+=500000;long after=ffp_get_current_position_l(&f);
 printf("async pause: first=%ld captured=%ld after500=%ld delta=%ld\n",before,held,after,after-before);
 check(labs(before-321)<=1&&labs(held-332)<=1&&after==held,"pause stores extrapolated audio position");check(labs(after-before)<=100,"unchanged absolute100ms pause oracle passes");check(ffp_is_paused_l(&f),"core reports paused");
 ffp_pause_l(&f);now_us+=500000;check(ffp_get_current_position_l(&f)==held,"repeated pause does not reanchor or rewind");close_case(&s);
}
static void ack_orders(void){
 FFPlayer f;VideoState s;setup_case(&f,&s);double next=.156+backend_latency+256.0/192000.0;SDL_AudioSubmission sub=stage(&f,next,6,1);
 now_us=100150000;check(commit(&f,&sub)==SDL_AUDIO_SUBMIT_READY,"ACK before pause accepted");now_us=100165000;long before=ffp_get_current_position_l(&f);ffp_pause_l(&f);check(ffp_get_current_position_l(&f)==before,"pause captures latest ACK extrapolation");close_case(&s);
 setup_case(&f,&s);sub=stage(&f,next,6,1);now_us=100165000;ffp_pause_l(&f);before=ffp_get_current_position_l(&f);now_us+=10000;
 check(commit(&f,&sub)==SDL_AUDIO_SUBMIT_READY,"ACK after pause accepted");check(!s.eof_clock.audio_submit_pending,"paused ACK releases ownership");check(ffp_get_current_position_l(&f)==before,"same-generation paused ACK preserves public snapshot");near(s.eof_clock.audio_end,next,"paused ACK publishes real endpoint");near(s.eof_clock.audio_deadline,100.165+backend_latency,"paused ACK retains modeled output latency");
 now_us=100665000;ffp_start_l(&f);long resumed=ffp_get_current_position_l(&f);check(resumed==before,"resume starts from captured position");near(s.eof_clock.audio_deadline,100.665+backend_latency,"resume shifts deadline by complete pause interval");
 next+=256.0/192000.0;sub=stage(&f,next,6,1);now_us+=1000;check(commit(&f,&sub)==SDL_AUDIO_SUBMIT_READY,"first resumed ACK accepted");near(get_clock(&s.audclk),next-backend_latency,"resumed ACK keeps ordinary source anchor without clamp");printf("resumed ordinary ACK: before=%ld after=%ld (source correction retained)\n",resumed,ffp_get_current_position_l(&f));close_case(&s);
}
static void terminal_ack(void){
 FFPlayer f;VideoState s;setup_case(&f,&s);s.video_st=NULL;
 eof_clock_audio_delivered(&s,.156+backend_latency,6,1,backend_latency,100.0);eof_clock_audio_finished(&s,6);eof_clock_demux(&s,1);
 SDL_AudioSubmission sub=stage(&f,.156+backend_latency+256.0/192000.0,6,0);
 now_us=100165000;long before=ffp_get_current_position_l(&f);ffp_pause_l(&f);check(ffp_get_current_position_l(&f)==before,"terminal pause captures existing bounded interval");now_us+=10000;
 check(commit(&f,&sub)==SDL_AUDIO_SUBMIT_READY,"terminal paused ACK accepted");check(!s.eof_clock.audio_submit_pending,"terminal paused ACK ownership committed");check(ffp_get_current_position_l(&f)==before,"terminal late ACK cannot rewind paused public clock");
 near(eof_clock_view(&s).audio_clock,.156+256.0/192000.0,"terminal drain model is still updated independently");check(!audio_tail_drained(&s),"paused terminal deadline not yet drained");
 now_us=110165000;check(ffp_get_current_position_l(&f)==before&&!audio_tail_drained(&s),"ten paused seconds neither move position nor drain");ffp_start_l(&f);near(s.eof_clock.audio_deadline,110.165+backend_latency,"full ten-second pause excluded from deadline");
 now_us=110342000;check(!audio_tail_drained(&s),"remaining latency remains mandatory");now_us=110343000;check(audio_tail_drained(&s),"active resumed time drains latency");close_case(&s);
}
static void seek_step_and_unknown(void){
 FFPlayer f;VideoState s;setup_case(&f,&s);now_us=100165000;ffp_pause_l(&f);long before=ffp_get_current_position_l(&f);s.seek_pos=4000000;eof_clock_seek(&s,1);
 check(ffp_get_current_position_l(&f)==before,"pending seek preserves paused clock");eof_clock_seek(&s,0);check(ffp_get_current_position_l(&f)==before,"failed seek preserves paused clock");
 SDL_LockMutex(s.play_mutex);step_to_next_frame_l(&f);SDL_UnlockMutex(s.play_mutex);check(!s.paused&&s.audclk.paused,"step can run video while audio stays paused");
 SDL_AudioSubmission sub=stage(&f,.156+backend_latency+256.0/192000.0,6,1);now_us+=10000;check(commit(&f,&sub)==SDL_AUDIO_SUBMIT_READY,"stepped same-generation ACK accepted");check(ffp_get_current_position_l(&f)==before,"step uses audio paused flag for snapshot protection");
 eof_clock_seek(&s,1);++s.audioq.serial;++s.videoq.serial;check(ffp_get_current_position_l(&f)==4000,"successful seek invalidates old snapshot");eof_clock_seek(&s,0);
 check(commit(&f,&sub)==SDL_AUDIO_SUBMIT_STALE,"old-generation ACK stays stale");check(ffp_get_current_position_l(&f)==4000,"stale ACK cannot revive old snapshot");
 sub=stage(&f,4.5,7,1);check(commit(&f,&sub)==SDL_AUDIO_SUBMIT_READY,"new-generation paused ACK initializes clock");near(get_clock(&s.audclk),4.5-backend_latency,"new seek uses actual accepted anchor");long initialized=ffp_get_current_position_l(&f);
 sub=stage(&f,4.5+256.0/192000.0,7,1);check(commit(&f,&sub)==SDL_AUDIO_SUBMIT_READY,"second new-generation paused ACK accepted");check(ffp_get_current_position_l(&f)==initialized,"subsequent paused ACK retains new snapshot");close_case(&s);
 for(int n=0;n<3;n++){
  setup_case(&f,&s);set_clock_at(&s.audclk,n==0?NAN:n==1?INFINITY:-INFINITY,6,100);now_us=100165000;ffp_pause_l(&f);
  sub=stage(&f,2.0,6,1);check(commit(&f,&sub)==SDL_AUDIO_SUBMIT_READY,"unavailable same-generation paused clock can initialize");near(get_clock(&s.audclk),2.0-backend_latency,"finite first ACK initializes unavailable paused clock");close_case(&s);
 }
 setup_case(&f,&s);s.video_st=NULL;eof_clock_demux(&s,1);eof_clock_audio_finished(&s,6);eof_clock_audio_delivered(&s,NAN,6,1,.1,100);now_us=100165000;ffp_pause_l(&f);
 sub=stage(&f,NAN,6,0);check(commit(&f,&sub)==SDL_AUDIO_SUBMIT_READY,"unknown terminal ACK still commits");check(!s.eof_clock.audio_submit_pending&&isfinite(s.eof_clock.audio_deadline),"unknown real PCM keeps finite deadline");check(isnan(get_master_clock(&s)),"unknown terminal source does not revive older clock");check(ffp_get_current_position_l(&f)==0,"unknown public terminal position remains unavailable");close_case(&s);
}
static void preserved_modes(void){
 FFPlayer f;VideoState s;setup_case(&f,&s);eof_clock_audio_delivered(&s,4,6,0,.1,100);eof_clock_audio_finished(&s,6);eof_clock_demux(&s,1);eof_clock_video_presented(&s,7.5,.5,6);now_us=100600000;ffp_pause_l(&f);
 check(ffp_get_current_position_l(&f)==8000,"drained A/V video handoff endpoint unchanged");close_case(&s);
 setup_case(&f,&s);s.audio_st=NULL;s.av_sync_type=AV_SYNC_VIDEO_MASTER;set_clock_at(&s.vidclk,7.5,6,100);eof_clock_demux(&s,1);eof_clock_video_presented(&s,7.5,.5,6);now_us=100600000;ffp_pause_l(&f);
 check(ffp_get_current_position_l(&f)==7500,"automatic video-only paused stored-frame semantics unchanged");close_case(&s);
}
struct CommitWorker{FFPlayer*f;SDL_AudioSubmission sub;int result;};
static void*commit_worker(void*p){struct CommitWorker*w=p;w->result=commit(w->f,&w->sub);return NULL;}
static void explicit_master_controls(void){
 for(int mode=0;mode<2;mode++){
  FFPlayer f;VideoState s;setup_case(&f,&s);s.av_sync_type=mode?AV_SYNC_EXTERNAL_CLOCK:AV_SYNC_VIDEO_MASTER;
  set_clock_at(&s.vidclk,2.0,6,100);set_clock_at(&s.extclk,3.0,s.extclk.serial,100);now_us=100165000;ffp_pause_l(&f);
  long before=ffp_get_current_position_l(&f);check(before==(mode?3165:2000),"explicit VIDEO/EXTERNAL paused selector preserved");
  SDL_AudioSubmission sub=stage(&f,.156+backend_latency+256.0/192000.0,6,1);now_us+=500000;
  check(commit(&f,&sub)==SDL_AUDIO_SUBMIT_READY,"explicit-master paused audio ACK commits");check(ffp_get_current_position_l(&f)==before,"audio pause snapshot does not replace explicit master");close_case(&s);
 }
}
static void ack_wait_across_resume(void){
 FFPlayer f;VideoState s;setup_case(&f,&s);now_us=100165000;ffp_pause_l(&f);struct CommitWorker w={&f,stage(&f,.5,6,1),-99};
 SDL_LockMutex(s.play_mutex);atomic_store(&probe_lock_enabled,1);pthread_t thread;check(!pthread_create(&thread,NULL,commit_worker,&w),"create waiting ACK thread");while(!atomic_load(&probe_lock_entered))sched_yield();
 now_us=101165000;toggle_pause_l(&f,0);atomic_store(&probe_lock_enabled,0);SDL_UnlockMutex(s.play_mutex);check(!pthread_join(thread,NULL),"join ACK thread");
 check(w.result==SDL_AUDIO_SUBMIT_READY,"waiting ACK commits after resume");near(s.audclk.last_updated,101.165,"clock timestamp taken after play-lock wait");near(get_clock(&s.audclk),.5-backend_latency,"paused waiting time not charged to resumed clock");near(s.eof_clock.audio_deadline,101.165+backend_latency,"EOF commit uses coherent resumed time");close_case(&s);
}
int main(void){request_and_dispatch_pause();ack_orders();terminal_ack();seek_step_and_unknown();preserved_modes();explicit_master_controls();ack_wait_across_resume();core_fixture_mode=1;core_mutex_count=0;test_playback_clock();check(core_mutex_count==5 && held_count==0,"actual repository playback-clock fixture locks initialized and released");printf("%d assertions, %d failures\n",assertions,failures);return failures?1:0;}
