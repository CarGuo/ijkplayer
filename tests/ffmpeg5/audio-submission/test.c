/* Isolated deterministic interleaving simulation: no Android, device, sockets,
 * sound output, background processes, or real-time sleeps. Production functions
 * are extracted unchanged by run_extract.py; only dependencies are stubbed. */
#define __ANDROID__ 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <errno.h>
#include "ijksdl_audio_playback.h"
typedef uint8_t Uint8;
#include "extracted_submission.h"
typedef struct { int held,rank; } SDL_mutex; typedef int SDL_cond;
typedef void JNIEnv; typedef void SDL_Thread; typedef int SDL_Android_AudioTrack;
typedef void (*SDL_AudioCallback)(void *,Uint8 *,int);
typedef struct { SDL_AudioCallback callback;void *userdata;SDL_AudioSubmitCallback callback_submit;SDL_AudioSubmissionCallback submission;SDL_AudioRateAppliedCallback rate_applied;int software_tempo; } SDL_AudioSpec;
typedef struct SDL_Aout_Opaque { SDL_cond *wakeup_cond;SDL_mutex *wakeup_mutex,*position_mutex;SDL_AudioPlaybackTracker playback;uint64_t output_epoch;int frame_size,output_sample_rate;SDL_AudioSpec spec;SDL_Android_AudioTrack *atrack;uint8_t *buffer;int buffer_size;volatile bool need_flush,pause_on,abort_request,need_set_volume;volatile float left_volume,right_volume;SDL_Thread *audio_tid;int audio_session_id;volatile float speed;volatile bool speed_changed;int engine_wakeup;uint64_t requested_rate_sequence;float requested_platform_rate,logical_platform_rate;int requested_preserve_pitch,logical_preserve_pitch;SDL_AudioRateApplied applied_rate;uint64_t notified_rate_sequence,notified_rate_epoch;int notified_rate_generation;} SDL_Aout_Opaque;
typedef struct { SDL_Aout_Opaque *opaque;double latency; } SDL_Aout;
typedef struct { SDL_mutex *mutex;int serial,abort_request; } PacketQueue;
typedef struct { int unused; } Frame;
typedef struct { SDL_mutex *mutex;SDL_cond *cond;PacketQueue *pktq;int size,rindex_shown,rindex,max_size;Frame queue[1]; } FrameQueue;
#include "extracted_eof.h"
typedef struct { double pts,speed,source_soft_base,source_soft_end_media;int serial,paused;SDL_Aout *source_aout;int source_soft; } Clock;
typedef struct VideoState { SDL_mutex *play_mutex;FrameQueue sampq;PacketQueue audioq,videoq;EOFClockState eof_clock;int abort_request,audio_buf_index,show_mode,muted,audio_volume,audio_write_buf_size,audio_clock_serial,latest_audio_seek_load_serial,pause_req;unsigned int audio_buf_size;uint8_t *audio_buf;double audio_clock,audio_buffer_media_rate;struct {int pkt_serial;} auddec;struct {int frame_size,bytes_per_sec;} audio_tgt;Clock audclk,extclk;int64_t latest_seek_load_start_at;SDL_cond *continue_read_thread;uint64_t audio_engine_sequence;int requested_software_rate_mode,audio_engine_drained,platform_rate_capable;double audio_software_rate;int paused,seek_req,platform_rate_mode,software_rate_mode,audio_clock_unsupported,audio_processing_error,av_sync_type;void *audio_st,*video_st;struct {int finished;} viddec;FrameQueue pictq;} VideoState;
typedef struct {int value;} HarnessOptionDictionary;
typedef struct {const char *value;} HarnessOptionEntry;
typedef struct {HarnessOptionDictionary *player_opts;VideoState *is;SDL_Aout *aout;int pf_playback_rate_changed,pf_playback_volume_changed,first_audio_frame_rendered,render_wait_start,start_on_prepared,soundtouch_enable,av_sync_type;float pf_playback_rate,pf_playback_volume;int64_t audio_callback_time;int loop,autoexit,auto_resume,error,prepared,last_error;int64_t start_time;struct {int64_t latest_seek_load_duration;float vdps,vfps,avdelay,avdiff,drop_frame_rate;} stat;} FFPlayer;
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
#define ALOGI(...) ((void)0)
#define ALOGE(...) ((void)0)
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
static int SDL_Android_AudioTrack_pause_checked(JNIEnv*e,SDL_Android_AudioTrack*t){SDL_Android_AudioTrack_pause(e,t);return 0;}
static int SDL_Android_AudioTrack_flush_checked(JNIEnv*e,SDL_Android_AudioTrack*t){SDL_Android_AudioTrack_flush(e,t);return 0;}
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
static void set_clock_speed(Clock*c,double speed){c->speed=speed;}
static void sync_clock_to_slave(Clock*c,Clock*s){}
static int completions,errors;
static void ffp_notify_msg1(FFPlayer*f,int n){if(n==FFP_MSG_COMPLETED)completions++;}
static void ffp_notify_msg2(FFPlayer*f,int n,int b){if(n==FFP_MSG_ERROR){assert(b==-EIO);errors++;}}
static int SDL_Android_AudioTrack_write(JNIEnv*,SDL_Android_AudioTrack*,uint8_t*,int);
static void aout_pause_audio(SDL_Aout*,int);
static int aout_request_platform_rate(SDL_Aout*,float,int,int);
static int SDL_AoutRequestPlatformRate(SDL_Aout*a,float rate,int preserve,int fields){return aout_request_platform_rate(a,rate,preserve,fields);}
/* Device head is unavailable in these legacy submission fixtures. Accept/reset
 * accounting is real; querying it must never invent played frames or PTS. */
static int aout_get_playback_position(SDL_Aout*a,SDL_AudioPlaybackSnapshot*p){
 SDL_LockMutex(a->opaque->position_mutex);
 int result=SDL_AudioPlaybackTracker_GetSnapshot(&a->opaque->playback,p);
 SDL_UnlockMutex(a->opaque->position_mutex);return result;
}
static int SDL_AoutGetPlaybackPosition(SDL_Aout*a,SDL_AudioPlaybackSnapshot*p){return aout_get_playback_position(a,p);}
static SDL_Aout *rate_target;
static int rate_set_result,rate_set_calls,rate_head_reads,rate_head_fail_at;
static int rate_inject_newer,rate_write_case,rate_notify_calls,rate_notify_accept;
static SDL_AudioRateApplied rate_last_notified;
static int ordering_in_setter,ordering_mode,ordering_start_software,ordering_hook_done;
static float ordering_rate;
static void ordering_publish_rate(int inside_setter);
static int SDL_Android_AudioTrack_get_playback_head(JNIEnv*e,SDL_Android_AudioTrack*t,uint32_t*out){
 assert(rate_target && rate_target->opaque->position_mutex->held);
 ++rate_head_reads;if(rate_head_reads==rate_head_fail_at)return -1;
 *out=16+(uint32_t)rate_head_reads;return 0;
}
static int SDL_Android_AudioTrack_set_rate_checked(JNIEnv*e,SDL_Android_AudioTrack*t,float speed,int preserve,float*actual_speed,float*actual_pitch){
 SDL_Aout_Opaque *o=rate_target->opaque;
 assert(o->position_mutex->held && !o->wakeup_mutex->held);++rate_set_calls;
 if(ordering_in_setter){ordering_in_setter=0;ordering_publish_rate(1);}
 if(rate_inject_newer){
  /* Deterministic interleaving: another request owner publishes a new tuple
   * while this owner is inside JNI. This is not a real-thread race test. */
  ++o->requested_rate_sequence;o->requested_platform_rate=3.0f;o->requested_preserve_pitch=0;o->logical_platform_rate=3.0f;o->logical_preserve_pitch=0;rate_inject_newer=0;
 }
 *actual_speed=rate_set_result>0?o->applied_rate.speed:speed;
 *actual_pitch=rate_set_result>0?o->applied_rate.pitch:preserve?1.0f:speed;
 return rate_set_result;
}

#include "extracted_logic.h"
static HarnessOptionDictionary harness_dictionary;
static HarnessOptionDictionary **harness_get_opt_dict(FFPlayer*f,int category){(void)category;return &f->player_opts;}
static void harness_dict_set_int(HarnessOptionDictionary**dict,const char*name,int64_t value,int flags){(void)name;(void)flags;if(!*dict)*dict=&harness_dictionary;(*dict)->value=(int)value;}
static HarnessOptionEntry *harness_dict_get(HarnessOptionDictionary*dict,const char*name,const void*prev,int flags){(void)dict;(void)name;(void)prev;(void)flags;return NULL;}
/* Private aliases allow independent unchanged review probes to include their
 * own public-function extraction without symbol/type collisions. */
#define AVDictionary HarnessOptionDictionary
#define AVDictionaryEntry HarnessOptionEntry
#define ffp_get_opt_dict harness_get_opt_dict
#define av_dict_set_int harness_dict_set_int
#define av_dict_get harness_dict_get
#define ffp_set_option_int harness_ffp_set_option_int
#define ffp_set_playback_rate harness_ffp_set_playback_rate
#define ffp_get_property_float harness_ffp_get_property_float
#define FFP_OPT_CATEGORY_PLAYER 4
#define FFP_SOUNDTOUCH_MIN_RATE .25
#define FFP_SOUNDTOUCH_MAX_RATE 4.0
#include "extracted_public_controls.h"
#undef AVDictionary
#undef AVDictionaryEntry
#undef ffp_get_opt_dict
#undef av_dict_set_int
#undef av_dict_get
#undef ffp_set_option_int
#undef ffp_set_playback_rate
#undef ffp_get_property_float
#undef FFP_OPT_CATEGORY_PLAYER
#undef FFP_SOUNDTOUCH_MIN_RATE
#undef FFP_SOUNDTOUCH_MAX_RATE

static VideoState state;static FFPlayer player;static SDL_Aout aout;static SDL_Aout_Opaque backend;
static SDL_mutex mplay,msamp,maudio,mvideo,meof,mwake,mposition;static SDL_AudioPlaybackSpan playback_spans[32];static SDL_cond cond;static int track;
static uint8_t pcm[768],new_pcm[256],buffer[256];
static int writes,scenario,accepted_real,waits,decoded_new;
static int engine_mode,engine_commits,engine_rejects,engine_cancels,engine_superseded,idle_engine_wake;
static int engine_seek_before_commit,engine_publish_before_commit,active_pcm_write;
static void test_engine_on_wait(void);
static SDL_AudioSubmission saved_old;
enum { NORMAL,SHORT,PAUSE,FAILED_SEEK,SUCCESS_SEEK,ABORT_WRITE,ZERO,ERROR_WRITE,PARTIAL_ERROR,ZERO_PAUSE,MIXED_SERIAL,LEGACY,FULL_FAILED_SEEK,FULL_SUCCESS_SEEK,SAME_FLUSH,MULTI_BUFFER,ABORT_WAIT,ERROR_PAUSE,ENTRY_STALE,IDLE_ERROR_AFTER,IDLE_ERROR_BEFORE,IDLE_SEEK,PREDRAIN_SILENCE_ERROR,IDLE_FAILED_SEEK,IDLE_CLOSE,IDLE_RESTART,IDLE_REOPEN,MAX_CASE };
static void ffp_statistic_l(FFPlayer*f){}
static void ffp_toggle_buffering(FFPlayer*f,int b){}
static void stream_seek(VideoState*v,int64_t p,int64_t r,int b){v->seek_req=1;eof_clock_seek(v,1);}
static void toggle_pause(FFPlayer*f,int p){f->is->paused=p;eof_clock_pause(f->is,p);aout_pause_audio(f->aout,p);}
#include "extracted_read_step.h"
static void setup(void){
 memset(&state,0,sizeof state);memset(&player,0,sizeof player);memset(&backend,0,sizeof backend);
 assert(lock_count==0);mplay.rank=1;msamp.rank=2;maudio.rank=3;mvideo.rank=4;meof.rank=5;mwake.rank=6;mposition.rank=7;state.play_mutex=&mplay;fake_now=10;writes=accepted_real=warning_count=freed_track=0;
 completions=errors=waits=plays=pauses=flushes=decoded_new=physical_playing=0;memset(&saved_old,0,sizeof saved_old);
 state.audioq=(PacketQueue){&maudio,1,0};state.videoq=(PacketQueue){&mvideo,1,0};
 state.sampq=(FrameQueue){.mutex=&msamp,.cond=&cond,.pktq=&state.audioq,.max_size=1};
 state.eof_clock=(EOFClockState){.mutex=&meof,.audio_finished_serial=1,.audio_serial=1,.audio_pending=1,.audio_end=4.008,.audio_deadline=9.9,.video_pts=NAN,.demux_eof=1};
 memset(pcm,0xa5,sizeof pcm);memset(new_pcm,0xb6,sizeof new_pcm);state.audio_buf=pcm;state.audio_buf_size=scenario==MIXED_SERIAL?128:scenario==MULTI_BUFFER?768:256;state.audio_clock=4.010666667;state.audio_clock_serial=1;state.auddec.pkt_serial=1;state.audio_tgt.frame_size=2;state.audio_tgt.bytes_per_sec=96000;state.audio_volume=128;state.show_mode=SHOW_MODE_VIDEO;state.latest_audio_seek_load_serial=-1;
 if(scenario==MULTI_BUFFER)state.eof_clock.audio_end=state.audio_clock-768.0/96000;
 if(scenario==ENTRY_STALE){state.audioq.serial=2;state.eof_clock.demux_eof=0;}
 state.continue_read_thread=&cond;state.audio_st=&track;
 aout=(SDL_Aout){&backend,0.177666667};player=(FFPlayer){.is=&state,.aout=&aout,.pf_playback_rate=1.0,.first_audio_frame_rendered=1,.loop=1,.prepared=1,.start_time=AV_NOPTS_VALUE};
 backend.logical_platform_rate=1.0f;backend.logical_preserve_pitch=0;
 backend.position_mutex=&mposition;backend.frame_size=2;backend.output_sample_rate=48000;backend.output_epoch=1;
 assert(SDL_AudioPlaybackTracker_Init(&backend.playback,playback_spans,32)==0);
 assert(SDL_AudioPlaybackTracker_Reset(&backend.playback,-1,backend.output_epoch)==0);
 state.eof_clock.video_applied_rate=1.0;state.audio_buffer_media_rate=1.0;
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
 if(idle_engine_wake){assert(waits==1 && !writes && !flushes && !backend.engine_wakeup);backend.abort_request=true;return;}
 if(engine_mode){test_engine_on_wait();return;}
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
  else {assert(plays==1 && !physical_playing);state.audioq.serial=2;state.eof_clock.demux_eof=0;}
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
 assert(physical_playing); /* Every JNI write requires a preceding play, including idle restart. */
 if(active_pcm_write){assert(backend.spec.software_tempo && state.software_rate_mode && backend.applied_rate.speed==1.0f && backend.applied_rate.pitch==1.0f);assert(n==256 && d==buffer);accepted_real+=n;return n;}
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
   assert(n==256);saved_old=(SDL_AudioSubmission){.token=state.eof_clock.audio_submit_token,.generation=state.eof_clock.audio_submit_serial};
   if(scenario==ZERO)return 0;
   if(scenario==ERROR_WRITE)return -1;
   if(scenario==ERROR_PAUSE){backend.pause_on=1;eof_clock_pause(&state,1);return -1;}
   if(scenario==FULL_FAILED_SEEK || scenario==FULL_SUCCESS_SEEK){eof_clock_seek(&state,1);accepted_real+=n;return n;}
   if(scenario==ABORT_WRITE){state.abort_request=1;accepted_real+=n;return n;}
   if(scenario==ZERO_PAUSE){backend.pause_on=1;eof_clock_pause(&state,1);return 0;}
   if(scenario==SHORT || scenario==PAUSE || scenario==FAILED_SEEK || scenario==SUCCESS_SEEK || scenario==PARTIAL_ERROR || scenario==SAME_FLUSH || scenario==ABORT_WAIT){
    if(rate_write_case)assert(aout_request_platform_rate(&aout,2.0f,0,SDL_AUDIO_RATE_SPEED|SDL_AUDIO_RATE_PITCH_POLICY)==0);
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
  /* Transactional output binds/resumes after callback preparation. */
  assert(!physical_playing && !backend.pause_on && state.audioq.serial==2);
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
  if(mode==4 || mode==5){assert(writes==1 && plays==1 && pauses==(mode==5?2:1) && errors==0 && completions==0 && !state.eof_clock.audio_submit_pending);}
  else if(mode==7){assert(writes==1 && plays==1 && pauses==1 && errors==1 && completions==0 && state.eof_clock.audio_output_error==-EIO);}
  else {
   assert(writes==3 && errors==0 && completions==1);
   /* Checked resets pause before every flush. No speculative play occurs
    * between callbacks, so re-pausing an already-paused track adds no command. */
   assert(plays==2);
   assert(pauses==(mode==2 || mode==8?3:mode==3?1:2));
   assert(flushes==(mode==3?0:mode==8?2:1));
   assert(accepted_real==(mode==3?256:384));
  }
  printf("PASS %s writes=%d plays=%d pauses=%d flushes=%d errors=%d completed=%d\n",names[mode],writes,plays,pauses,flushes,errors,completions);
 }
 probe_mode=0;
}

static int rate_notification(void *opaque,const SDL_AudioRateApplied *applied){
 (void)opaque;assert(lock_count==0);++rate_notify_calls;rate_last_notified=*applied;return rate_notify_accept;
}
static void setup_rate_owner(void){
 scenario=NORMAL;setup();rate_target=&aout;
 rate_set_result=rate_set_calls=rate_head_reads=rate_head_fail_at=rate_inject_newer=rate_write_case=rate_notify_calls=0;rate_notify_accept=1;
 backend.spec.rate_applied=rate_notification;
 backend.requested_rate_sequence=1;backend.requested_platform_rate=1.0f;backend.requested_preserve_pitch=1;backend.logical_platform_rate=1.0f;backend.logical_preserve_pitch=1;
 backend.applied_rate=(SDL_AudioRateApplied){.sequence=1,.speed=1.0f,.pitch=1.0f,.preserve_pitch=1,.generation=1,.output_epoch=1};
}
static void rate_owner_cases(void){
 setup_rate_owner();
 SDL_AudioSubmission sub={0};sdl_audio_submit_callback(&player,buffer,256,&sub);
 int track_paused=1;assert(aout_bind_generation(NULL,&aout,1,&track_paused)==0);
 assert(aout_accept_prefix(&aout,&sub,0,128)==0);
 SDL_AudioPlaybackTracker before=backend.playback;SDL_AudioPlaybackSpan spans_before[32];memcpy(spans_before,playback_spans,sizeof spans_before);
 SDL_AudioSubmission sub_before=sub;uint8_t suffix_before[128];memcpy(suffix_before,buffer+128,sizeof suffix_before);
 assert(aout_request_platform_rate(&aout,2.0f,0,SDL_AUDIO_RATE_SPEED|SDL_AUDIO_RATE_PITCH_POLICY)==0);
 rate_set_result=1;assert(aout_apply_platform_rate(NULL,&aout)==0);
 assert(rate_set_calls==1 && rate_head_reads==2 && backend.applied_rate.sequence==2 && backend.applied_rate.rejected);
 assert(backend.applied_rate.speed==1.0f && backend.applied_rate.pitch==1.0f && backend.applied_rate.preserve_pitch==1);
 assert(backend.requested_platform_rate==2.0f && backend.requested_preserve_pitch==0);
 assert(!memcmp(&before,&backend.playback,sizeof before) && !memcmp(spans_before,playback_spans,sizeof spans_before));
 assert(!memcmp(&sub_before,&sub,sizeof sub) && !memcmp(suffix_before,buffer+128,sizeof suffix_before));
 assert(backend.output_epoch==1 && state.eof_clock.audio_submit_pending && !state.eof_clock.audio_output_error && !plays && !pauses && !flushes && !writes);
 assert(aout_apply_platform_rate(NULL,&aout)==0 && rate_set_calls==1 && rate_head_reads==2);
 aout_notify_platform_rate(&aout);aout_notify_platform_rate(&aout);
 assert(rate_notify_calls==1 && rate_last_notified.sequence==2 && rate_last_notified.rejected && rate_last_notified.speed==1.0f);
 puts("PASS platform-rate rejection handled once; actual restored tuple notified; accepted prefix, pending suffix, epoch and EOF ownership unchanged");

 setup_rate_owner();assert(aout_request_platform_rate(&aout,2.0f,0,SDL_AUDIO_RATE_SPEED|SDL_AUDIO_RATE_PITCH_POLICY)==0);
 rate_set_result=1;rate_inject_newer=1;assert(aout_apply_platform_rate(NULL,&aout)==0);
 assert(backend.applied_rate.sequence==2 && backend.requested_rate_sequence==3 && backend.requested_platform_rate==3.0f && backend.requested_preserve_pitch==0);
 rate_set_result=0;assert(aout_apply_platform_rate(NULL,&aout)==0);
 assert(rate_set_calls==2 && backend.applied_rate.sequence==3 && !backend.applied_rate.rejected && backend.applied_rate.speed==3.0f && backend.applied_rate.pitch==3.0f);
 assert(aout_apply_platform_rate(NULL,&aout)==0 && rate_set_calls==2);
 puts("PASS request arriving inside checked setter remains pending and applies exactly once");

 setup_rate_owner();backend.playback.generation=1;rate_notify_accept=0;
 aout_notify_platform_rate(&aout);assert(rate_notify_calls==1 && !backend.notified_rate_sequence);
 rate_notify_accept=1;aout_notify_platform_rate(&aout);aout_notify_platform_rate(&aout);assert(rate_notify_calls==2 && backend.notified_rate_sequence==1);
 assert(SDL_AudioPlaybackTracker_Reset(&backend.playback,2,2)==0);aout_notify_platform_rate(&aout);aout_notify_platform_rate(&aout);
 assert(rate_notify_calls==3 && rate_last_notified.generation==2 && rate_last_notified.output_epoch==2);
 puts("PASS applied notification retries until accepted and rebinds once after a new output epoch");

 setup_rate_owner();scenario=SHORT;rate_set_result=1;rate_write_case=1;
 aout_thread_n(NULL,&aout);assert(freed_track==1 && writes==3 && accepted_real==256 && completions==1 && !errors && !lock_count);
 assert(rate_set_calls==1 && backend.applied_rate.sequence==2 && backend.applied_rate.rejected && backend.output_epoch==1 && !flushes);
 assert(backend.playback.accepted_frames==256 && !state.eof_clock.audio_submit_pending && !state.eof_clock.audio_output_error);
 rate_write_case=0;
 puts("PASS recoverable rate rejection between partial writes resumes exact 128-byte suffix without retry, flush, PCM loss or false EOF");

 for(int fail_at=0;fail_at<=2;fail_at++){
  setup_rate_owner();assert(aout_request_platform_rate(&aout,2.0f,0,SDL_AUDIO_RATE_SPEED)==0);rate_head_fail_at=fail_at;rate_set_result=fail_at?0:-1;
  aout_thread_n(NULL,&aout);test_read_step();
  assert(freed_track==1 && !writes && errors==1 && !completions && state.eof_clock.audio_output_error==-EIO);
  assert(rate_set_calls==(fail_at?fail_at-1:1) && rate_head_reads==(fail_at?fail_at:1) && backend.applied_rate.sequence==1 && backend.requested_rate_sequence==2 && backend.output_epoch==1 && !backend.playback.accepted_frames);
  assert(!lock_count && !flushes && !plays);
 }
 puts("PASS fatal checked setter or playback-head read before/after setter remains device error without writes or accepted publication");
 rate_target=NULL;
}

static int engine_submission(void *opaque,const SDL_AudioSubmission *sub,int action,int error){
 assert(lock_count==0);
 if(action==SDL_AUDIO_SUBMIT_ENGINE_COMMIT && engine_seek_before_commit){
  ++state.audioq.serial;state.eof_clock.demux_eof=0;engine_seek_before_commit=0;
 }
 if(action==SDL_AUDIO_SUBMIT_ENGINE_COMMIT && engine_publish_before_commit){
  /* A current setter publishes under play_mutex after JNI, before player
   * commit. The requested hardware tuple reflects the still-active engine. */
  SDL_LockMutex(state.play_mutex);++state.audio_engine_sequence;
  if(engine_publish_before_commit==1)__atomic_store_n(&state.requested_software_rate_mode,0,__ATOMIC_RELEASE);
  player.pf_playback_rate=engine_publish_before_commit==1?3.0f:4.0f;
  assert(SDL_AoutRequestPlatformRate(&aout,player.pf_playback_rate,0,SDL_AUDIO_RATE_SPEED)==0);
  SDL_UnlockMutex(state.play_mutex);engine_publish_before_commit=0;
 }
 if(action==SDL_AUDIO_SUBMIT_ENGINE_COMMIT)++engine_commits;
 if(action==SDL_AUDIO_SUBMIT_ENGINE_REJECT)++engine_rejects;
 if(action==SDL_AUDIO_SUBMIT_CANCEL)++engine_cancels;
 int result=sdl_audio_submission_callback(opaque,sub,action,error);
 if(result==SDL_AUDIO_SUBMIT_SUPERSEDED)++engine_superseded;
 return result;
}
static void test_engine_on_wait(void){
 assert(!writes && !flushes && !rate_set_calls && !engine_commits && !engine_rejects);
 assert(backend.output_epoch==1 && backend.playback.accepted_frames==128 && !backend.playback.played_frames);
 assert(state.audio_engine_drained && !state.software_rate_mode && state.platform_rate_mode);
 assert(state.eof_clock.audio_transform_pending && !state.eof_clock.audio_output_error);
 if(engine_mode==1){
  assert(waits<=3);
  if(waits==1){assert(physical_playing);aout_pause_audio(&aout,1);eof_clock_pause(&state,1);}
  else if(waits==2){assert(!physical_playing && pauses==1);aout_pause_audio(&aout,0);eof_clock_pause(&state,0);}
  else {assert(physical_playing && plays==2);assert(SDL_AudioPlaybackTracker_Observe(&backend.playback,128)==0);}
 }else {
  assert(waits==1);
  if(engine_mode==2)assert(SDL_AudioPlaybackTracker_Observe(&backend.playback,128)==0);
  else if(engine_mode==5){
   SDL_LockMutex(state.play_mutex);++state.audio_engine_sequence;
   __atomic_store_n(&state.requested_software_rate_mode,0,__ATOMIC_RELEASE);player.pf_playback_rate=3.0f;
   assert(SDL_AoutRequestPlatformRate(&aout,3.0f,0,SDL_AUDIO_RATE_SPEED)==0);SDL_UnlockMutex(state.play_mutex);
   assert(SDL_AudioPlaybackTracker_Observe(&backend.playback,128)==0);
  }
  else if(engine_mode==3){++state.audioq.serial;state.eof_clock.demux_eof=0;}
  else {assert(engine_mode==4);backend.abort_request=true;}
 }
}
static void engine_handoff_cases(void){
 const char *names[]={"","queued-head-drain-pause-resume-commit","rejected-unit-rate-retains-platform-engine","seek-cancels-old-control","abort-stops-control-wait","newer-engine-and-rate-request-retained"};
 for(engine_mode=1;engine_mode<=5;engine_mode++){
  setup_rate_owner();engine_commits=engine_rejects=engine_cancels=engine_superseded=0;
  backend.spec.submission=engine_submission;
  backend.requested_platform_rate=2.0f;backend.requested_preserve_pitch=0;backend.logical_platform_rate=2.0f;backend.logical_preserve_pitch=0;
  backend.applied_rate.speed=backend.applied_rate.pitch=2.0f;backend.applied_rate.preserve_pitch=0;
  state.platform_rate_capable=state.platform_rate_mode=1;state.requested_software_rate_mode=1;
  state.audio_engine_sequence=5;state.audio_engine_drained=1;state.audclk.source_aout=&aout;player.pf_playback_rate=2.0f;
  state.eof_clock.audio_transform_pending=1;state.eof_clock.audio_transform_serial=1;
  state.audio_buf_index=state.audio_buf_size;
  int track_paused=1;assert(aout_bind_generation(NULL,&aout,1,&track_paused)==0);
  assert(SDL_AudioPlaybackTracker_AcceptRate(&backend.playback,128,1,4.0,4.0+128.0/48000,1.0/48000,1.0)==0);
  assert(SDL_AudioPlaybackTracker_Observe(&backend.playback,0)==0);
  SDL_Android_AudioTrack_play(NULL,&track);track_paused=0;
  SDL_AudioSubmission control={0};memset(buffer,0x5a,sizeof buffer);
  sdl_audio_submit_callback(&player,buffer,sizeof buffer,&control);
  assert(!control.token && !control.output_bytes && control.engine_switch==2 && control.engine_sequence==5 && control.generation==1 && control.engine_rate==2.0f);
  assert(!state.eof_clock.audio_callback_busy && state.eof_clock.audio_transform_pending);
  for(unsigned n=0;n<sizeof buffer;n++)assert(!buffer[n]);
  if(engine_mode==2)rate_set_result=1;
  int result=aout_write_submitted_buffer(NULL,&aout,buffer,control.output_bytes,&control,&track_paused);
  assert(!writes && !errors && !completions && !lock_count && !state.eof_clock.audio_output_error);
  if(engine_mode==1){
   assert(result==0 && waits==3 && plays==2 && pauses==1 && !flushes && rate_set_calls==1);
   assert(engine_commits==1 && !engine_rejects && !engine_cancels);
   assert(state.software_rate_mode && !state.platform_rate_mode && player.soundtouch_enable && state.audio_software_rate==2.0f);
   assert(!state.audio_engine_drained && !state.eof_clock.audio_transform_pending);
   assert(backend.applied_rate.speed==1.0f && backend.applied_rate.pitch==1.0f && backend.applied_rate.preserve_pitch==1);
   assert(backend.output_epoch==1 && backend.playback.played_frames==128 && backend.playback.accepted_frames==128);
  }else if(engine_mode==2){
   assert(result==0 && waits==1 && plays==1 && !pauses && !flushes && rate_set_calls==1);
   assert(!engine_commits && engine_rejects==1 && !engine_cancels);
   assert(!state.software_rate_mode && state.platform_rate_mode && !player.soundtouch_enable && !state.requested_software_rate_mode);
   assert(!state.audio_engine_drained && !state.eof_clock.audio_transform_pending && backend.applied_rate.rejected);
   assert(backend.applied_rate.speed==2.0f && backend.applied_rate.pitch==2.0f && backend.applied_rate.preserve_pitch==0);
   assert(backend.output_epoch==1 && backend.playback.played_frames==128 && backend.playback.accepted_frames==128);
  }else if(engine_mode==3){
   assert(result==0 && waits==1 && !rate_set_calls && !engine_commits && !engine_rejects && engine_cancels==1);
   assert(state.audioq.serial==2 && !state.software_rate_mode && state.platform_rate_mode);
   assert(flushes==1 && pauses==1 && backend.output_epoch==2 && !backend.playback.accepted_frames);
  }else if(engine_mode==4){
   assert(result==-1 && waits==1 && !rate_set_calls && !engine_commits && !engine_rejects && engine_cancels==1);
   assert(!state.software_rate_mode && state.platform_rate_mode && !flushes && !pauses);
   assert(backend.output_epoch==1 && backend.playback.accepted_frames==128 && !backend.playback.played_frames);
  }else {
   /* SUPERSEDED intentionally prevents the old, cancelled boundary from
    * temporarily publishing the wrong engine, rather than switching twice. */
   assert(result==0 && waits==1 && rate_set_calls==2 && engine_commits==1 && engine_superseded==1 && !engine_rejects && !engine_cancels);
   assert(!state.software_rate_mode && state.platform_rate_mode && !backend.spec.software_tempo);
   assert(!state.requested_software_rate_mode && state.audio_engine_sequence==6 && player.pf_playback_rate==3.0f);
   assert(control.engine_switch==2 && control.engine_sequence==5 && control.engine_rate==2.0f);
   assert(!player.soundtouch_enable && !state.audio_engine_drained && !state.eof_clock.audio_transform_pending);
   assert(backend.applied_rate.speed==3.0f && backend.applied_rate.pitch==3.0f && !backend.applied_rate.preserve_pitch);
   assert(!writes && !flushes && backend.output_epoch==1 && backend.playback.played_frames==128 && backend.playback.accepted_frames==128);
  }
  printf("PASS engine handoff %s: writes=%d flushes=%d waits=%d commits=%d rejects=%d cancels=%d\n",names[engine_mode],writes,flushes,waits,engine_commits,engine_rejects,engine_cancels);
 }
 engine_mode=0;
 setup_rate_owner();state.audclk.source_aout=&aout;backend.playback.generation=1;
 assert(SDL_AudioPlaybackTracker_Observe(&backend.playback,0)==0);
 state.eof_clock.audio_pending=0;state.eof_clock.audio_output_epoch=1;state.eof_clock.audio_output_end_frame=0;state.eof_clock.audio_deadline=fake_now-1;
 assert(audio_tail_drained(&state));
 SDL_AudioSubmission idle={.generation=1};int track_paused=1;
 assert(aout_request_platform_rate(&aout,1.0f,1,SDL_AUDIO_RATE_ENGINE_WAKE)==0 && backend.engine_wakeup);
 assert(aout_write_submitted_buffer(NULL,&aout,buffer,0,&idle,&track_paused)==0);
 assert(!backend.engine_wakeup && !writes && !waits && !flushes && !rate_set_calls && backend.requested_rate_sequence==1);
 idle_engine_wake=1;assert(aout_write_submitted_buffer(NULL,&aout,buffer,0,&idle,&track_paused)==-1);idle_engine_wake=0;
 assert(waits==1 && !writes && !flushes && !plays && !rate_set_calls && !state.eof_clock.audio_output_error);
 puts("PASS EOF engine wake returns to producer once; consumed wake cannot spin or write synthetic silence");
 rate_target=NULL;
}

static SDL_AudioSubmission setup_drained_engine(int software){
 setup_rate_owner();engine_mode=engine_commits=engine_rejects=engine_cancels=engine_superseded=0;
 engine_seek_before_commit=engine_publish_before_commit=active_pcm_write=0;
 backend.spec.submission=engine_submission;backend.spec.software_tempo=software;
 backend.logical_platform_rate=3.0f;backend.logical_preserve_pitch=0;
 backend.requested_platform_rate=software?1.0f:3.0f;backend.requested_preserve_pitch=software;
 backend.applied_rate.speed=backend.applied_rate.pitch=backend.requested_platform_rate;
 backend.applied_rate.preserve_pitch=software;
 state.platform_rate_capable=1;state.platform_rate_mode=!software;state.software_rate_mode=software;
 state.requested_software_rate_mode=!software;state.audio_engine_sequence=5;state.audio_engine_drained=1;
 state.audio_software_rate=player.pf_playback_rate=3.0f;state.eof_clock.video_applied_rate=3.0f;state.eof_clock.software_control_epoch=1;state.eof_clock.software_control_frame=128;player.soundtouch_enable=software;state.audclk.source_aout=&aout;state.audclk.source_soft=software;
 state.eof_clock.audio_transform_pending=1;state.eof_clock.audio_transform_serial=1;
 state.audio_buf_index=state.audio_buf_size;
 int paused=1;assert(aout_bind_generation(NULL,&aout,1,&paused)==0);
 assert(SDL_AudioPlaybackTracker_AcceptRate(&backend.playback,128,1,4.0,4.0+128.0/48000,1.0/48000,1.0)==0);
 assert(SDL_AudioPlaybackTracker_Observe(&backend.playback,128)==0);
 SDL_AudioSubmission control={0};sdl_audio_submit_callback(&player,buffer,sizeof buffer,&control);
 assert(control.engine_switch==(software?1:2) && control.engine_sequence==5 && control.engine_rate==3.0f && !control.output_bytes && !control.token);
 return control;
}
static void active_engine_policy_cases(void){
 for(int failure=0;failure<5;failure++){
  SDL_AudioSubmission control=setup_drained_engine(1);int track_paused=1;
  engine_seek_before_commit=1;
  assert(aout_write_submitted_buffer(NULL,&aout,buffer,0,&control,&track_paused)==0);
  /* JNI changed the device, but a seek rejected the player handoff. The
   * owner's last committed software policy must survive that stale control. */
  assert(state.audioq.serial==2 && state.software_rate_mode && !state.platform_rate_mode && backend.spec.software_tempo);
  assert(backend.applied_rate.speed==3.0f && backend.applied_rate.pitch==3.0f && rate_set_calls==1);
  assert(engine_commits==1 && engine_cancels==1 && !engine_rejects && flushes==1 && backend.output_epoch==2 && !writes);
  state.audio_engine_drained=0;state.audio_clock_serial=state.auddec.pkt_serial=2;
  state.audio_buf=pcm;state.audio_buf_size=256;state.audio_buf_index=0;state.audio_clock=8.0;state.audio_buffer_media_rate=3.0;
  SDL_AudioSubmission pcm_sub={0};sdl_audio_submit_callback(&player,buffer,sizeof buffer,&pcm_sub);
  assert(pcm_sub.token && pcm_sub.generation==2 && pcm_sub.output_bytes==256 && pcm_sub.media_rate==3.0);
  if(failure==1)rate_set_result=1; /* Restored non-unit tuple is unsafe for software PCM. */
  if(failure==2)rate_set_result=-1; /* Fatal checked-setter readback/rollback failure. */
  if(failure==3)rate_head_fail_at=rate_head_reads+1;
  if(failure==4)rate_head_fail_at=rate_head_reads+2;
  active_pcm_write=1;int result=aout_write_submitted_buffer(NULL,&aout,buffer,256,&pcm_sub,&track_paused);active_pcm_write=0;
  assert(backend.spec.software_tempo && state.software_rate_mode && !state.platform_rate_mode && flushes==1 && backend.output_epoch==2 && !lock_count);
  if(!failure){
   assert(result==0 && writes==1 && accepted_real==256 && backend.applied_rate.speed==1.0f && backend.applied_rate.pitch==1.0f);
   assert(rate_set_calls==2 && backend.playback.accepted_frames==128 && !state.eof_clock.audio_submit_pending && !state.eof_clock.audio_output_error);
  }else {
   test_read_step();assert(result==-1 && !writes && !backend.playback.accepted_frames && errors==1 && !completions && state.eof_clock.audio_output_error==-EIO);
   assert(!state.eof_clock.audio_submit_pending && rate_set_calls==(failure==3?1:2));
  }
  printf("PASS stale software-to-platform seek policy repair outcome=%d: writes=%d errors=%d setter_calls=%d\n",failure,writes,errors,rate_set_calls);
 }

 setup_rate_owner();backend.spec.software_tempo=1;
 SDL_AudioPlaybackTracker before=backend.playback;
 for(int n=0;n<10000;n++)assert(aout_apply_active_rate(NULL,&aout)==0);
 assert(!rate_set_calls && !rate_head_reads && backend.applied_rate.sequence==1 && backend.requested_rate_sequence==1);
 assert(!memcmp(&before,&backend.playback,sizeof before) && !plays && !pauses && !flushes && !writes && !lock_count);
 puts("PASS 10000 steady software owner checks use cached verified unit tuple with zero JNI setter/head calls");

 for(int paused=0;paused<2;paused++){
  SDL_AudioSubmission control=setup_drained_engine(0);int track_paused=1;
  if(paused)toggle_pause(&player,1);
  engine_publish_before_commit=paused?2:1;
  assert(aout_write_submitted_buffer(NULL,&aout,buffer,0,&control,&track_paused)==0);
  assert(state.audio_engine_sequence==6);
  if(paused){
   assert(state.software_rate_mode && !state.platform_rate_mode && backend.spec.software_tempo && player.soundtouch_enable);
   assert(state.audio_software_rate==4.0f && backend.requested_platform_rate==1.0f && backend.requested_preserve_pitch);
   assert(aout_apply_active_rate(NULL,&aout)==0 && backend.applied_rate.speed==1.0f && backend.applied_rate.pitch==1.0f);
   assert(state.paused && state.eof_clock.paused && state.eof_clock.video_applied_rate==4.0f);
   assert(state.eof_clock.video_applied_at==state.eof_clock.pause_at && state.requested_software_rate_mode);
  }else {
   /* The latest option cancels the uncommitted boundary in this generation. */
   assert(engine_superseded==1 && !state.requested_software_rate_mode);
   assert(!state.software_rate_mode && state.platform_rate_mode && !backend.spec.software_tempo && !player.soundtouch_enable);
   assert(!state.audio_engine_drained && !state.eof_clock.audio_transform_pending);
   assert(aout_apply_active_rate(NULL,&aout)==0 && backend.applied_rate.speed==3.0f && backend.applied_rate.pitch==3.0f);
  }
  assert(!writes && !flushes && backend.output_epoch==1 && backend.playback.played_frames==128 && backend.playback.accepted_frames==128 && !state.eof_clock.audio_output_error);
  printf("PASS post-JNI %s publication survives player commit with hardware tuple serialized to active engine\n",paused?"newer paused rate":"last option flip");
 }
 rate_target=NULL;
}

static void ordering_publish_rate(int inside_setter){
 if(inside_setter){
  /* Execute the exact public setter as another logical thread while the
   * output owner is in JNI. Only its lock-rank stack is separate; the device
   * position lock remains held and the setter does not acquire it. */
  int owner_count=lock_count,owner_stack[8];memcpy(owner_stack,lock_stack,sizeof owner_stack);lock_count=0;
  harness_ffp_set_playback_rate(&player,ordering_rate);
  assert(!lock_count && backend.position_mutex->held);lock_count=owner_count;memcpy(lock_stack,owner_stack,sizeof owner_stack);
 }else harness_ffp_set_playback_rate(&player,ordering_rate);
 assert(player.pf_playback_rate==ordering_rate);
}
static int ordering_submission(void *opaque,const SDL_AudioSubmission *sub,int action,int error){
 if(action==SDL_AUDIO_SUBMIT_ENGINE_COMMIT && !ordering_hook_done && ordering_mode>=2 && ordering_mode<=3){
  ordering_hook_done=1;ordering_publish_rate(0);
  if(ordering_mode==2){eof_clock_seek(&state,1);++state.audioq.serial;state.eof_clock.demux_eof=0;eof_clock_seek(&state,0);}
  harness_ffp_set_option_int(&player,4,"soundtouch",ordering_start_software);
 }
 return engine_submission(opaque,sub,action,error);
}
static void assert_active_logical_tuple(void){
 assert(aout_apply_active_rate(NULL,&aout)==0);
 if(state.platform_rate_mode && backend.playback.generation!=state.audioq.serial){
  int track_paused=1;assert(aout_bind_generation(NULL,&aout,state.audioq.serial,&track_paused)==0);
  assert(SDL_AudioPlaybackTracker_Observe(&backend.playback,0)==0);
 }
 aout_notify_platform_rate(&aout);
 assert(backend.logical_platform_rate==ordering_rate && !backend.logical_preserve_pitch && player.pf_playback_rate==ordering_rate);
 assert(backend.spec.software_tempo==state.software_rate_mode);
 float public_rate=harness_ffp_get_property_float(&player,FFP_PROP_FLOAT_PLAYBACK_RATE,-999.0f);
 assert(public_rate==(state.software_rate_mode?state.audio_software_rate:backend.applied_rate.speed));
 if(state.software_rate_mode){
  assert(backend.applied_rate.speed==1.0f && backend.applied_rate.pitch==1.0f && backend.applied_rate.preserve_pitch);
 }else {
  assert(backend.applied_rate.speed==ordering_rate && backend.applied_rate.pitch==ordering_rate && !backend.applied_rate.preserve_pitch);
 }
}
static int cancel_for_eight_submission(void *opaque,const SDL_AudioSubmission *sub,int action,int error){
 if(action==SDL_AUDIO_SUBMIT_ENGINE_COMMIT){
  assert(!state.software_rate_mode && state.platform_rate_mode);
  harness_ffp_set_option_int(&player,4,"soundtouch",0);
  harness_ffp_set_playback_rate(&player,8.0f);
  assert(player.pf_playback_rate==8.0f && !state.requested_software_rate_mode);
 }
 int result=engine_submission(opaque,sub,action,error);
 assert(!state.software_rate_mode && state.platform_rate_mode && !backend.spec.software_tempo);
 return result;
}
static void logical_request_ordering_cases(void){
 setup_rate_owner();
 assert(aout_request_platform_rate(&aout,3.0f,0,SDL_AUDIO_RATE_SPEED|SDL_AUDIO_RATE_PITCH_POLICY)==0);
 assert(backend.logical_platform_rate==3.0f && !backend.logical_preserve_pitch && backend.requested_platform_rate==3.0f);
 assert(aout_request_platform_rate(&aout,1.0f,1,SDL_AUDIO_RATE_SPEED|SDL_AUDIO_RATE_PITCH_POLICY|SDL_AUDIO_RATE_TRANSIENT)==0);
 assert(backend.logical_platform_rate==3.0f && !backend.logical_preserve_pitch && backend.requested_platform_rate==1.0f && backend.requested_preserve_pitch);
 assert(aout_apply_platform_rate(NULL,&aout)==0);int calls=rate_set_calls;uint64_t sequence=backend.requested_rate_sequence;
 backend.spec.software_tempo=1;
 assert(aout_request_platform_rate(&aout,4.0f,0,SDL_AUDIO_RATE_SPEED|SDL_AUDIO_RATE_LOGICAL_ONLY)==0);
 assert(backend.logical_platform_rate==4.0f && backend.requested_platform_rate==1.0f && backend.requested_rate_sequence==sequence);
 assert(aout_apply_active_rate(NULL,&aout)==0 && rate_set_calls==calls && backend.applied_rate.speed==1.0f && backend.applied_rate.pitch==1.0f);
 backend.spec.software_tempo=0;assert(aout_apply_active_rate(NULL,&aout)==0);
 assert(backend.logical_platform_rate==4.0f && backend.applied_rate.speed==4.0f && backend.applied_rate.pitch==4.0f && !backend.applied_rate.preserve_pitch);
 assert(!writes && !flushes && !lock_count);
 puts("PASS normal/logical-only/transient requests preserve independent logical and physical tuples");

 const float rates[]={1.0f,3.0f,4.0f};
 const char *orders[]={"rate-before-JNI","rate-during-JNI","seek-and-option-cancel","same-generation-option-flip","rate-after-commit","rate-and-option-after-commit"};
 for(int software=0;software<=1;software++)for(unsigned rate=0;rate<sizeof rates/sizeof rates[0];rate++)for(int order=0;order<6;order++){
  SDL_AudioSubmission control=setup_drained_engine(software);int track_paused=1;
  ordering_start_software=software;ordering_rate=rates[rate];ordering_mode=order;ordering_hook_done=ordering_in_setter=0;
  backend.spec.submission=ordering_submission;backend.spec.rate_applied=sdl_audio_rate_applied;
  if(order==0)ordering_publish_rate(0);
  if(order==1)ordering_in_setter=1;
  assert(aout_write_submitted_buffer(NULL,&aout,buffer,0,&control,&track_paused)==0);
  assert(!ordering_in_setter);
  if(order>=4){
   ordering_publish_rate(0);
   if(order==5){
    harness_ffp_set_option_int(&player,4,"soundtouch",software);
   }
  }
  assert_active_logical_tuple();
  assert(!writes && !errors && !completions && !lock_count && !state.eof_clock.audio_output_error);
  if(order==2){
   assert(state.audioq.serial==2 && state.software_rate_mode==software && state.requested_software_rate_mode==software);
   assert(flushes==1 && backend.output_epoch==2 && !backend.playback.accepted_frames && engine_cancels==1);
  }else if(order==3){
   assert(state.audioq.serial==1 && state.software_rate_mode==software && state.requested_software_rate_mode==software);
   assert(engine_superseded==1 && !state.audio_engine_drained && !state.eof_clock.audio_transform_pending);
   assert(!flushes && backend.output_epoch==1 && backend.playback.accepted_frames==128 && backend.playback.played_frames==128);
  }else {
   assert(state.audioq.serial==1 && state.software_rate_mode==!software && !flushes && backend.output_epoch==1);
   assert(backend.playback.accepted_frames==128 && backend.playback.played_frames==128);
   if(state.software_rate_mode)assert(state.audio_software_rate==(order<4?ordering_rate:3.0f));
   if(order==5){
    assert(state.requested_software_rate_mode==software && state.audio_engine_sequence==6);
    state.audio_engine_drained=1;state.eof_clock.audio_transform_pending=1;state.audio_buf_index=state.audio_buf_size;
    SDL_AudioSubmission latest={0};sdl_audio_submit_callback(&player,buffer,sizeof buffer,&latest);
    assert(latest.engine_switch==(software?2:1) && latest.engine_rate==ordering_rate && latest.engine_sequence==6);
    assert(aout_write_submitted_buffer(NULL,&aout,buffer,0,&latest,&track_paused)==0);
    assert_active_logical_tuple();
    assert(state.software_rate_mode==software && state.requested_software_rate_mode==software && engine_commits==2);
   }
  }
  assert(!writes && !errors && !state.eof_clock.audio_output_error && backend.logical_platform_rate==ordering_rate && player.pf_playback_rate==ordering_rate);
  printf("PASS logical ordering %s-to-%s newer=%.0f %s: active_software=%d physical=%.0f logical=%.0f\n",software?"software":"platform",software?"platform":"software",ordering_rate,orders[order],state.software_rate_mode,backend.applied_rate.speed,backend.logical_platform_rate);
 }
 for(int software=0;software<=1;software++){
  (void)setup_drained_engine(software);backend.spec.rate_applied=sdl_audio_rate_applied;state.audio_engine_drained=0;state.requested_software_rate_mode=software;state.eof_clock.audio_transform_pending=0;
  toggle_pause(&player,1);ordering_in_setter=0;
  const float updates[]={1.0f,3.0f,4.0f};
  for(unsigned n=0;n<sizeof updates/sizeof updates[0];n++){ordering_rate=updates[n];ordering_publish_rate(0);}
  assert(!rate_set_calls && !rate_head_reads && backend.logical_platform_rate==4.0f && player.pf_playback_rate==4.0f);
  assert_active_logical_tuple();int applied_calls=rate_set_calls,head_calls=rate_head_reads;
  for(int n=0;n<10000;n++)assert(aout_apply_active_rate(NULL,&aout)==0);
  assert(rate_set_calls==applied_calls && rate_head_reads==head_calls && rate_set_calls==(software?0:1));
  assert(state.paused && state.eof_clock.paused && backend.pause_on && !writes && !plays && !flushes);
  assert(backend.output_epoch==1 && backend.playback.played_frames==128 && backend.playback.accepted_frames==128);
  toggle_pause(&player,0);assert_active_logical_tuple();
  assert(!state.paused && !state.eof_clock.paused && !backend.pause_on && !writes && !plays && !flushes && !lock_count);
  printf("PASS paused %s rate sequence 1/3/4 keeps latest4; 10000 steady checks add no JNI calls before resume\n",software?"software":"platform");
 }
 SDL_AudioSubmission cancelled=setup_drained_engine(0);int track_paused=1;
 backend.spec.submission=cancel_for_eight_submission;backend.spec.rate_applied=sdl_audio_rate_applied;
 harness_ffp_set_playback_rate(&player,8.0f); /* Pending software still enforces its supported range. */
 assert(player.pf_playback_rate==3.0f && backend.logical_platform_rate==3.0f && state.requested_software_rate_mode);
 assert(aout_write_submitted_buffer(NULL,&aout,buffer,0,&cancelled,&track_paused)==0);
 ordering_rate=8.0f;assert_active_logical_tuple();
 assert(engine_superseded==1 && !state.software_rate_mode && state.platform_rate_mode && !player.soundtouch_enable);
 assert(!state.requested_software_rate_mode && !state.audio_engine_drained && !state.eof_clock.audio_transform_pending);
 assert(harness_ffp_get_property_float(&player,FFP_PROP_FLOAT_PLAYBACK_RATE,-999)==8.0f);
 assert(!writes && !flushes && !state.eof_clock.audio_output_error && backend.output_epoch==1 && backend.playback.played_frames==128 && backend.playback.accepted_frames==128);
 puts("PASS pending software rejects8; cancel-before-commit keeps platform engine and publishes newer8 without unsupported software activation");
 ordering_in_setter=ordering_hook_done=0;rate_target=NULL;
}

int main(void){
 setvbuf(stdout,NULL,_IONBF,0); /* Keep passed scenario evidence if a later assertion aborts. */
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
 rate_owner_cases();
 engine_handoff_cases();
 active_engine_policy_cases();
 logical_request_ordering_cases();
 puts("PASS exact production EOF/read completion/error paths with fake JNI and time; normal output preserves the latency guard; synthetic writes do not starve EOF");
 return 0;
}
