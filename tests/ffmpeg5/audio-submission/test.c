/* Isolated deterministic interleaving simulation: no Android, device, sockets,
 * sound output, background processes, or real-time sleeps. Production functions
 * are extracted unchanged by run_extract.py; only dependencies are stubbed. */
#define __ANDROID__ 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <errno.h>
typedef uint8_t Uint8;
#include "extracted_submission.h"
typedef struct { int held,rank; } SDL_mutex; typedef int SDL_cond;
typedef void JNIEnv; typedef void SDL_Thread; typedef int SDL_Android_AudioTrack;
typedef void (*SDL_AudioCallback)(void *,Uint8 *,int);
typedef struct { SDL_AudioCallback callback;void *userdata;SDL_AudioSubmitCallback callback_submit;SDL_AudioSubmissionCallback submission; } SDL_AudioSpec;
typedef struct SDL_Aout_Opaque { SDL_cond *wakeup_cond;SDL_mutex *wakeup_mutex;SDL_AudioSpec spec;SDL_Android_AudioTrack *atrack;uint8_t *buffer;int buffer_size;volatile bool need_flush,pause_on,abort_request,need_set_volume;volatile float left_volume,right_volume;SDL_Thread *audio_tid;int audio_session_id;volatile float speed;volatile bool speed_changed;} SDL_Aout_Opaque;
typedef struct { SDL_Aout_Opaque *opaque;double latency; } SDL_Aout;
typedef struct { SDL_mutex *mutex;int serial,abort_request; } PacketQueue;
typedef struct { int unused; } Frame;
typedef struct { SDL_mutex *mutex;SDL_cond *cond;PacketQueue *pktq;int size,rindex_shown,rindex,max_size;Frame queue[1]; } FrameQueue;
#include "extracted_eof.h"
typedef struct { double pts;int serial,paused; } Clock;
typedef struct VideoState { SDL_mutex *play_mutex;FrameQueue sampq;PacketQueue audioq,videoq;EOFClockState eof_clock;int abort_request,audio_buf_index,show_mode,muted,audio_volume,audio_write_buf_size,audio_clock_serial,latest_audio_seek_load_serial,pause_req;unsigned int audio_buf_size;uint8_t *audio_buf;double audio_clock;struct {int pkt_serial;} auddec;struct {int frame_size,bytes_per_sec;} audio_tgt;Clock audclk,extclk;int64_t latest_seek_load_start_at;SDL_cond *continue_read_thread;int paused,seek_req;void *audio_st,*video_st;struct {int finished;} viddec;FrameQueue pictq;} VideoState;
typedef struct {VideoState *is;SDL_Aout *aout;int pf_playback_rate_changed,pf_playback_volume_changed,first_audio_frame_rendered,render_wait_start,start_on_prepared,soundtouch_enable,av_sync_type;float pf_playback_rate,pf_playback_volume;int64_t audio_callback_time;int loop,autoexit,auto_resume,error,prepared,last_error;int64_t start_time;struct {int64_t latest_seek_load_duration;} stat;} FFPlayer;
#define FFMAX(a,b) ((a)>(b)?(a):(b))
#define FFMIN(a,b) ((a)<(b)?(a):(b))
#define SDL_THREAD_PRIORITY_HIGH 1
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
#define SHOW_MODE_VIDEO 1
#define SDL_MIX_MAXVOLUME 128
#define AV_SYNC_AUDIO_MASTER 1
#define FFP_MSG_AUDIO_RENDERING_START 1
#define FFP_MSG_AUDIO_SEEK_RENDERING_START 2
#define FFP_MSG_COMPLETED 3
#define FFP_MSG_ERROR 4
#define AVERROR(x) (-(x))
#define AVERROR_EOF (-541478725)
#define AV_NOPTS_VALUE INT64_MIN
#define AV_LOG_INFO 1
#define av_log(...) ((void)0)
#define SDLTRACE(...) ((void)0)
static int warning_count;
#define ALOGW(...) (++warning_count)
static double fake_now;static int lock_count,lock_stack[8];
static int64_t av_gettime_relative(void){return (int64_t)llround(fake_now*1000000);}
static int64_t av_gettime(void){return av_gettime_relative();}
static void SDL_LockMutex(SDL_mutex *m){assert(!m->held && m->rank>0 && lock_count<8);if(lock_count)assert(m->rank>lock_stack[lock_count-1]);m->held=1;lock_stack[lock_count++]=m->rank;}
static void SDL_UnlockMutex(SDL_mutex *m){assert(m->held && lock_count>0 && lock_stack[lock_count-1]==m->rank);m->held=0;--lock_count;}
static int frame_queue_nb_remaining(FrameQueue *q){return q->size-q->rindex_shown;}
static void SDL_CondSignal(SDL_cond *c){}
static void SDL_CondWait(SDL_cond*c,SDL_mutex*m){assert(!"unexpected queue wait");}
static void test_on_wait(void);
static void SDL_CondWaitTimeout(SDL_cond*c,SDL_mutex*m,int t){assert(t>0);SDL_UnlockMutex(m);test_on_wait();SDL_LockMutex(m);}
static void SDL_Delay(int t){assert(!"unexpected render wait");}
static void SDL_SetThreadPriority(int p){}
static int plays,pauses,flushes,physical_playing,probe_mode,probe_cancelled,probe_bad_write,probe_callbacks;
static void SDL_Android_AudioTrack_play(JNIEnv*e,SDL_Android_AudioTrack*t){plays++;physical_playing=1;}
static void SDL_Android_AudioTrack_pause(JNIEnv*e,SDL_Android_AudioTrack*t){pauses++;physical_playing=0;}
static void SDL_Android_AudioTrack_flush(JNIEnv*e,SDL_Android_AudioTrack*t){flushes++;}
static void SDL_Android_AudioTrack_set_volume(JNIEnv*e,SDL_Android_AudioTrack*t,float l,float r){}
static void SDL_Android_AudioTrack_setSpeed(JNIEnv*e,SDL_Android_AudioTrack*t,float s){}
static int freed_track;
static void SDL_Android_AudioTrack_free(JNIEnv*e,SDL_Android_AudioTrack*t){freed_track++;}
static void SDL_AoutSetPlaybackRate(SDL_Aout*a,float r){}
static void SDL_AoutSetPlaybackVolume(SDL_Aout*a,float r){}
static double SDL_AoutGetLatencySeconds(SDL_Aout*a){return a->latency;}
static void SDL_AoutFlushAudio(SDL_Aout*a){a->opaque->need_flush=true;}
static void SDL_MixAudio(uint8_t*d,uint8_t*s,int n,int v){memcpy(d,s,n);}
static int audio_decode_frame(FFPlayer*f);
static void update_sample_display(VideoState*v,int16_t*p,int n){}
static void set_clock_at(Clock*c,double p,int s,double t){c->pts=p;c->serial=s;}
static void sync_clock_to_slave(Clock*c,Clock*s){}
static int completions,errors;
static void ffp_notify_msg1(FFPlayer*f,int n){if(n==FFP_MSG_COMPLETED)completions++;}
static void ffp_notify_msg2(FFPlayer*f,int n,int b){if(n==FFP_MSG_ERROR){assert(b==-EIO);errors++;}}
static int SDL_Android_AudioTrack_write(JNIEnv*,SDL_Android_AudioTrack*,uint8_t*,int);
static void aout_pause_audio(SDL_Aout*,int);
#include "extracted_logic.h"
static VideoState state;static FFPlayer player;static SDL_Aout aout;static SDL_Aout_Opaque backend;
static SDL_mutex mplay,msamp,maudio,mvideo,meof,mwake;static SDL_cond cond;static int track;
static uint8_t pcm[768],new_pcm[256],buffer[256];
static int writes,scenario,accepted_real,waits,decoded_new;
static SDL_AudioSubmission saved_old;
enum { NORMAL,SHORT,PAUSE,FAILED_SEEK,SUCCESS_SEEK,ABORT_WRITE,ZERO,ERROR_WRITE,PARTIAL_ERROR,ZERO_PAUSE,MIXED_SERIAL,LEGACY,FULL_FAILED_SEEK,FULL_SUCCESS_SEEK,SAME_FLUSH,MULTI_BUFFER,ABORT_WAIT,ERROR_PAUSE,ENTRY_STALE,IDLE_ERROR_AFTER,IDLE_ERROR_BEFORE,IDLE_SEEK,PREDRAIN_SILENCE_ERROR,IDLE_FAILED_SEEK,IDLE_CLOSE,IDLE_RESTART,IDLE_REOPEN,MAX_CASE };
static void ffp_statistic_l(FFPlayer*f){}
static void ffp_toggle_buffering(FFPlayer*f,int b){}
static void stream_seek(VideoState*v,int64_t p,int64_t r,int b){v->seek_req=1;eof_clock_seek(v,1);}
static void toggle_pause(FFPlayer*f,int p){f->is->paused=p;eof_clock_pause(f->is,p);aout_pause_audio(f->aout,p);}
#include "extracted_read_step.h"
static void setup(void){
 memset(&state,0,sizeof state);memset(&player,0,sizeof player);memset(&backend,0,sizeof backend);
 assert(lock_count==0);mplay.rank=1;msamp.rank=2;maudio.rank=3;mvideo.rank=4;meof.rank=5;mwake.rank=6;state.play_mutex=&mplay;fake_now=10;writes=accepted_real=warning_count=freed_track=0;
 completions=errors=waits=plays=pauses=flushes=decoded_new=0;memset(&saved_old,0,sizeof saved_old);
 state.audioq=(PacketQueue){&maudio,1,0};state.videoq=(PacketQueue){&mvideo,1,0};
 state.sampq=(FrameQueue){.mutex=&msamp,.cond=&cond,.pktq=&state.audioq,.max_size=1};
 state.eof_clock=(EOFClockState){.mutex=&meof,.audio_finished_serial=1,.audio_serial=1,.audio_pending=1,.audio_end=4.008,.audio_deadline=9.9,.video_pts=NAN,.demux_eof=1};
 memset(pcm,0xa5,sizeof pcm);memset(new_pcm,0xb6,sizeof new_pcm);state.audio_buf=pcm;state.audio_buf_size=scenario==MIXED_SERIAL?128:scenario==MULTI_BUFFER?768:256;state.audio_clock=4.010666667;state.audio_clock_serial=1;state.auddec.pkt_serial=1;state.audio_tgt.frame_size=2;state.audio_tgt.bytes_per_sec=96000;state.audio_volume=128;state.show_mode=SHOW_MODE_VIDEO;state.latest_audio_seek_load_serial=-1;
 if(scenario==MULTI_BUFFER)state.eof_clock.audio_end=state.audio_clock-768.0/96000;
 if(scenario==ENTRY_STALE){state.audioq.serial=2;state.eof_clock.demux_eof=0;}
 state.continue_read_thread=&cond;state.audio_st=&track;
 aout=(SDL_Aout){&backend,0.177666667};player=(FFPlayer){.is=&state,.aout=&aout,.first_audio_frame_rendered=1,.loop=1,.prepared=1,.start_time=AV_NOPTS_VALUE};
 backend.wakeup_mutex=&mwake;backend.wakeup_cond=&cond;backend.atrack=&track;backend.buffer=buffer;
 backend.spec=(SDL_AudioSpec){.callback=sdl_audio_callback,.userdata=&player,.callback_submit=sdl_audio_submit_callback,.submission=sdl_audio_submission_callback};
 if(scenario==LEGACY){backend.spec.callback_submit=NULL;backend.spec.submission=NULL;}
 assert(!audio_tail_drained(&state));
}
static int audio_decode_frame(FFPlayer*f){
 if(probe_mode==8 && state.audioq.serial==3 && state.audio_clock_serial!=3){
  state.audio_buf=new_pcm;state.audio_clock=9.0;state.audio_clock_serial=3;state.auddec.pkt_serial=3;state.eof_clock.audio_finished_serial=3;state.eof_clock.demux_eof=1;return 256;
 }
 if(!decoded_new && (scenario==MIXED_SERIAL || scenario==SUCCESS_SEEK || scenario==FULL_SUCCESS_SEEK || scenario==ENTRY_STALE || ((scenario==IDLE_SEEK || scenario==IDLE_RESTART) && state.audioq.serial==2) || (scenario==IDLE_REOPEN && waits))){
  assert(state.audioq.serial==2 || scenario==MIXED_SERIAL || scenario==IDLE_REOPEN);decoded_new=1;
  state.audioq.serial=scenario==IDLE_REOPEN?1:2;state.eof_clock.audio_finished_serial=state.audioq.serial;state.eof_clock.demux_eof=1;
  state.audio_buf=new_pcm;state.audio_clock=8.0;state.audio_clock_serial=state.audioq.serial;state.auddec.pkt_serial=state.audioq.serial;
  return 256;
 }
 return -1;
}
static void test_on_wait(void){
 ++waits;assert(lock_count==0);
 if(probe_mode){
  if(waits==1){
   assert(backend.pause_on && !physical_playing && state.eof_clock.seek_pending);
   if(probe_mode==4){state.abort_request=1;return;}
   if(probe_mode==3 || probe_mode==7){
    eof_clock_seek(&state,0);aout_pause_audio(&aout,0);eof_clock_pause(&state,0);
    if(probe_mode==7)backend.need_flush=1;
   }else {state.audioq.serial=2;state.eof_clock.demux_eof=0;eof_clock_seek(&state,0);}
   return;
  }
  assert(waits==2 && probe_cancelled && backend.pause_on && !physical_playing);
  if(probe_mode==8){assert(state.audioq.serial==2 && state.eof_clock.seek_pending);state.audioq.serial=3;state.eof_clock.demux_eof=0;eof_clock_seek(&state,0);return;}
  assert(probe_mode==2 || probe_mode==6);aout_pause_audio(&aout,0);eof_clock_pause(&state,0);return;
 }
 if(scenario==IDLE_REOPEN){assert(waits==1 && writes==2 && !completions && !errors && state.audioq.serial==1);state.eof_clock.demux_eof=0;return;}
 if(scenario==IDLE_RESTART){
  assert(writes==2 && completions==1 && !errors && !state.eof_clock.audio_output_error && waits<=2);
  if(waits==1){assert(pauses>0);fake_now+=0.3;eof_clock_pause(&state,0);backend.pause_on=false;state.paused=0;}
  else {assert(plays>=2);state.audioq.serial=2;state.eof_clock.demux_eof=0;}
  return;
 }
 if(scenario==IDLE_FAILED_SEEK){
  assert(writes==2 && !state.eof_clock.audio_output_error && waits<=3);
  if(waits==1){test_read_step();eof_clock_seek(&state,1);}
  else if(waits==2)eof_clock_seek(&state,0);
  else state.abort_request=1;
  return;
 }
 assert(waits==1);
 if(scenario==IDLE_ERROR_AFTER || scenario==IDLE_ERROR_BEFORE || scenario==IDLE_SEEK || scenario==IDLE_CLOSE){
  assert(!state.eof_clock.audio_submit_pending && !state.eof_clock.audio_output_error && writes==2);
  if(scenario==IDLE_ERROR_BEFORE)test_read_step();
  if(scenario==IDLE_SEEK){state.audioq.serial=2;state.eof_clock.demux_eof=0;}
  else {assert(completions==1 && errors==0);if(scenario==IDLE_CLOSE)backend.abort_request=true;else state.abort_request=1;}
  return;
 }
 assert(!audio_tail_drained(&state));
 assert(state.eof_clock.audio_submit_pending);
 if(scenario==PAUSE || scenario==ZERO_PAUSE){
  assert(backend.pause_on && pauses==1);fake_now+=0.300;eof_clock_pause(&state,0);backend.pause_on=false;
 }else if(scenario==ABORT_WAIT){state.abort_request=1;}
 else if(scenario==FAILED_SEEK || scenario==FULL_FAILED_SEEK){eof_clock_seek(&state,0);}
 else if(scenario==SUCCESS_SEEK || scenario==FULL_SUCCESS_SEEK){state.audioq.serial=2;state.eof_clock.demux_eof=0;eof_clock_seek(&state,0);}
 else assert(!"unexpected wait scenario");
}
static void test_late_old_ack(void){
 if(!saved_old.token)return;
 uint64_t current=state.eof_clock.audio_submit_token;
 double end=state.eof_clock.audio_end;
 assert(sdl_audio_submission_callback(&player,&saved_old,SDL_AUDIO_SUBMIT_COMMIT,0)==SDL_AUDIO_SUBMIT_STALE);
 assert(sdl_audio_submission_callback(&player,&saved_old,SDL_AUDIO_SUBMIT_FAIL,-1)==SDL_AUDIO_SUBMIT_STALE);
 assert(sdl_audio_submission_callback(&player,&saved_old,SDL_AUDIO_SUBMIT_CANCEL,0)==SDL_AUDIO_SUBMIT_READY);
 assert(state.eof_clock.audio_submit_pending && state.eof_clock.audio_submit_token==current && state.eof_clock.audio_end==end && !state.eof_clock.audio_output_error);
}
static int SDL_Android_AudioTrack_write(JNIEnv*e,SDL_Android_AudioTrack*t,uint8_t*d,int n){
 ++writes;assert(writes<7);assert(lock_count==0);
 if(probe_mode && writes==1){
  assert(physical_playing && n==256 && d==buffer);accepted_real+=128;
  aout_pause_audio(&aout,1);eof_clock_pause(&state,1);eof_clock_seek(&state,1);
  return 128;
 }
 if(probe_mode && writes==2){
  assert(!backend.pause_on && !state.eof_clock.seek_pending);
  assert(state.audioq.serial==(probe_mode==3?1:probe_mode==8?3:2));
  assert(probe_cancelled==(probe_mode==3?0:probe_mode==8?2:1));
  probe_bad_write=!physical_playing;
  printf("OBSERVED mode=%d generation=%d requested_pause=%d physical_playing=%d plays=%d pauses=%d flushes=%d\n",probe_mode,state.audioq.serial,backend.pause_on,physical_playing,plays,pauses,flushes);
  assert(!probe_bad_write); /* No write may follow the lost-pause schedules without play. */
 }
 bool real=d[0]!=0;
 if(real){
  int expected=(scenario==MIXED_SERIAL || scenario==ENTRY_STALE || ((scenario==IDLE_SEEK || scenario==IDLE_RESTART || scenario==IDLE_REOPEN) && writes>2) || ((scenario==SUCCESS_SEEK || scenario==FULL_SUCCESS_SEEK) && writes>1))?0xb6:0xa5;
  for(int i=0;i<n;i++)assert(d[i]==expected);
  if(scenario==LEGACY){fake_now+=0.2;test_read_step();assert(completions==1);backend.abort_request=true;return n;}
  assert(state.eof_clock.audio_submit_pending && !state.eof_clock.audio_callback_busy);
  if(scenario==MULTI_BUFFER)assert(fabs(state.eof_clock.audio_end-(state.audio_clock-(768.0-accepted_real)/96000))<1e-8);
  fake_now+=0.2;test_read_step();assert(completions==((scenario==IDLE_RESTART && writes>2)?1:0) && !errors && !audio_tail_drained(&state));
  if(writes==1){
   assert(n==256);saved_old=(SDL_AudioSubmission){state.eof_clock.audio_submit_token,state.eof_clock.audio_submit_serial};
   if(scenario==ZERO)return 0;
   if(scenario==ERROR_WRITE)return -1;
   if(scenario==ERROR_PAUSE){backend.pause_on=1;eof_clock_pause(&state,1);return -1;}
   if(scenario==FULL_FAILED_SEEK || scenario==FULL_SUCCESS_SEEK){eof_clock_seek(&state,1);accepted_real+=n;return n;}
   if(scenario==ABORT_WRITE){state.abort_request=1;accepted_real+=n;return n;}
   if(scenario==ZERO_PAUSE){backend.pause_on=1;eof_clock_pause(&state,1);return 0;}
   if(scenario==SHORT || scenario==PAUSE || scenario==FAILED_SEEK || scenario==SUCCESS_SEEK || scenario==PARTIAL_ERROR || scenario==SAME_FLUSH || scenario==ABORT_WAIT){
    accepted_real+=128;
    if(scenario==SAME_FLUSH)backend.need_flush=1;
    if(scenario==ABORT_WAIT){backend.pause_on=1;eof_clock_pause(&state,1);}
    if(scenario==PAUSE){backend.pause_on=1;eof_clock_pause(&state,1);}
    if(scenario==FAILED_SEEK || scenario==SUCCESS_SEEK)eof_clock_seek(&state,1);
    return 128;
   }
  }else{
   if(scenario==SHORT || scenario==PAUSE || scenario==FAILED_SEEK || scenario==PARTIAL_ERROR){assert(n==128 && d==buffer+128);}
   if(scenario==ZERO_PAUSE){assert(n==256 && d==buffer);assert(plays>=2);}
   if(scenario==SUCCESS_SEEK || scenario==FULL_SUCCESS_SEEK){assert(n==256 && d==buffer && flushes>0);test_late_old_ack();}
   if(scenario==PARTIAL_ERROR)return -1;
  }
  accepted_real+=n;return n;
 }
 /* A silence-only write does not hold the real-PCM completion gate. */
 assert(!state.eof_clock.audio_submit_pending);
 assert(!audio_tail_drained(&state));
 if(scenario==PREDRAIN_SILENCE_ERROR)return -1;
 if(scenario==IDLE_REOPEN && waits && !decoded_new)return n;
 fake_now=state.eof_clock.audio_deadline+0.001;
 if((scenario==IDLE_ERROR_AFTER || scenario==IDLE_ERROR_BEFORE || scenario==IDLE_SEEK || scenario==IDLE_FAILED_SEEK || scenario==IDLE_CLOSE || scenario==IDLE_RESTART || scenario==IDLE_REOPEN) && writes==2){
  if(scenario==IDLE_ERROR_AFTER || scenario==IDLE_CLOSE || scenario==IDLE_RESTART)test_read_step();
  return -1;
 }
 test_read_step();assert(completions==(scenario==IDLE_RESTART?2:1) && errors==0);
 backend.abort_request=true;return n;
}
/* Preserve the prior unknown-PTS invariants while exercising the new ACK path. */
static void nonfinite_submission_cases(void){
 double pts[]={NAN,INFINITY,-INFINITY};
 for(unsigned i=0;i<sizeof(pts)/sizeof(pts[0]);i++){
  scenario=NORMAL;setup();state.audio_clock=pts[i];
  SDL_AudioSubmission sub={0};sdl_audio_submit_callback(&player,buffer,256,&sub);
  assert(sub.token && state.eof_clock.audio_submit_pending && !state.eof_clock.audio_callback_busy);
  fake_now=12;assert(!audio_tail_drained(&state));
  assert(sdl_audio_submission_callback(&player,&sub,SDL_AUDIO_SUBMIT_CHECK,0)==SDL_AUDIO_SUBMIT_READY);
  assert(sdl_audio_submission_callback(&player,&sub,SDL_AUDIO_SUBMIT_COMMIT,0)==SDL_AUDIO_SUBMIT_READY);
  assert(!state.eof_clock.audio_submit_pending && !state.eof_clock.audio_pending);
  if(i==0)assert(isnan(state.eof_clock.audio_end)); /* Unknown PCM PTS remains unknown. */
  assert(isfinite(state.eof_clock.audio_deadline)); /* Unknown PTS still records a backend deadline. */
  assert(fabs(state.eof_clock.audio_deadline-(fake_now+aout.latency))<1e-8);
  assert(!audio_tail_drained(&state) && isnan(eof_clock_view(&state).audio_clock));
  fake_now=state.eof_clock.audio_deadline+0.001;
  assert(audio_tail_drained(&state) && isnan(eof_clock_view(&state).audio_clock));
  test_read_step();assert(completions==1 && errors==0);
  printf("PASS nonfinite-PTS-%u real token held until ACK; finite drain deadline; unavailable EOF media clock\n",i);
 }
}

/* Production controls are executed outside every mocked mutex, at explicit
 * scheduling boundaries. Physical state tracks issued AudioTrack play/pause. */
static int probe_submission(void *u,const SDL_AudioSubmission *s,int action,int error){
 int r=sdl_audio_submission_callback(u,s,action,error);
 if(action==SDL_AUDIO_SUBMIT_CANCEL && s->generation<state.audioq.serial){
  assert(!physical_playing && backend.pause_on);probe_cancelled++;
  if(probe_mode==5){backend.abort_request=1;state.abort_request=1;}
  else if(probe_mode==1 || probe_mode==6 || probe_mode==8){aout_pause_audio(&aout,0);eof_clock_pause(&state,0);}
 }
 return r;
}
static void probe_callback(void *u,Uint8*b,int n,SDL_AudioSubmission*s){
 sdl_audio_submit_callback(u,b,n,s);++probe_callbacks;
 if(probe_callbacks==2 && (probe_mode==6 || probe_mode==8)){
  assert(physical_playing && !backend.pause_on && state.audioq.serial==2);
  aout_pause_audio(&aout,1);eof_clock_pause(&state,1);
  if(probe_mode==8)eof_clock_seek(&state,1);
 }
}
static void pause_state_cases(void){
 const char*names[]={"", "resume-before-outer-pause-read", "resume-inside-outer-pause-wait", "paused-failed-seek-resume-exact-suffix", "abort-during-paused-seek-wait", "abort-after-stale-cancel", "repause-during-new-generation-callback", "same-generation-flush-remains-fatal", "two-successive-paused-seek-generations"};
 for(int mode=1;mode<=8;mode++){
  probe_mode=mode;probe_callbacks=probe_cancelled=probe_bad_write=physical_playing=0;
  scenario=(mode==3 || mode==4 || mode==7)?FAILED_SEEK:SUCCESS_SEEK;setup();
  backend.spec.submission=probe_submission;backend.spec.callback_submit=probe_callback;
  aout_thread_n(NULL,&aout);
  if(mode==4 || mode==5 || mode==7)test_read_step();
  assert(freed_track==1 && !lock_count && !probe_bad_write);
  if(mode==4 || mode==5){assert(writes==1 && plays==1 && pauses==1 && errors==0 && completions==0 && !state.eof_clock.audio_submit_pending);}
  else if(mode==7){assert(writes==1 && plays==2 && pauses==1 && errors==1 && completions==0 && state.eof_clock.audio_output_error==-EIO);}
  else {
   assert(writes==3 && errors==0 && completions==1);
   assert(plays==(mode==6 || mode==8?3:2));
   assert(pauses==(mode==2 || mode==6 || mode==8?2:1));
   assert(flushes==(mode==3?0:mode==8?2:1));
   assert(accepted_real==(mode==3?256:384));
  }
  printf("PASS %s writes=%d plays=%d pauses=%d flushes=%d errors=%d completed=%d\n",names[mode],writes,plays,pauses,flushes,errors,completions);
 }
 probe_mode=0;
}

int main(void){
 const char*names[]={"in-flight-final-real","short-suffix","partial-pause-resume","partial-failed-seek","partial-successful-seek-stale-acks","abort-during-write","zero-active-error","negative-error","error-after-partial","zero-while-paused","mid-callback-generation-refill","legacy-backend-unchanged","full-write-failed-seek","full-write-successful-seek","same-generation-flush-error","multiple-real-buffers","abort-while-pause-waiting","negative-error-while-paused","seek-entry-old-retained-pcm","idle-silence-error-after-completion","idle-silence-error-before-completion","idle-error-new-seek-recovers","predrain-silence-error","idle-error-failed-seek-stays-quiet","idle-close-flag-wakes-and-exits","idle-pause-resume-restart","same-generation-data-after-eof"};
 for(scenario=0;scenario<MAX_CASE;scenario++){
  setup();aout_thread_n(NULL,&aout);assert(freed_track==1 && lock_count==0);
  if(scenario==ZERO || scenario==ERROR_WRITE || scenario==PARTIAL_ERROR || scenario==SAME_FLUSH || scenario==ERROR_PAUSE){player.error=0;test_read_step();assert(errors==1 && completions==0);assert(!audio_tail_drained(&state));assert(state.eof_clock.audio_end==4.008);}
  else if(scenario==PREDRAIN_SILENCE_ERROR){test_read_step();assert(errors==1 && completions==0 && accepted_real==256 && state.eof_clock.audio_end==4.010666667);}
  else if(scenario==ABORT_WRITE || scenario==ABORT_WAIT){test_read_step();assert(!completions && !errors && !state.eof_clock.audio_submit_pending && state.eof_clock.audio_end==4.008);}
  else {assert(completions==(scenario==IDLE_RESTART?2:1) && !errors);if(scenario!=LEGACY)assert(accepted_real==(scenario==SUCCESS_SEEK?384:scenario==FULL_SUCCESS_SEEK?512:scenario==MULTI_BUFFER?768:(scenario==IDLE_SEEK || scenario==IDLE_RESTART || scenario==IDLE_REOPEN)?512:256));}
  if(scenario==ENTRY_STALE)assert(flushes>0 && decoded_new);
  if(scenario==PAUSE)assert(waits==1 && pauses>=1 && plays>=2);
  if(scenario==FAILED_SEEK || scenario==SUCCESS_SEEK || scenario==FULL_FAILED_SEEK || scenario==FULL_SUCCESS_SEEK || scenario==ABORT_WAIT)assert(waits==1);
  printf("PASS %s writes=%d waits=%d accepted_real=%d completed=%d errors=%d\n",names[scenario],writes,waits,accepted_real,completions,errors);
 }
 nonfinite_submission_cases();
 pause_state_cases();
 puts("PASS exact production EOF/read completion/error paths with fake JNI and time; normal output preserves the latency guard; synthetic writes do not starve EOF");
 return 0;
}
