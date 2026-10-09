/* Production producer + callback + owner, with real SWR/SoundTouch.
 * Fake AudioTrack holds its head at zero until the 18,448-frame minimum fill.
 * No Android/device operations, sockets, threads, or wall-clock sleeps. */
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
#include <limits.h>
#include <libavutil/frame.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include "ff_soundtouch.h"
#include "ijksdl_audio_playback.h"
typedef uint64_t Uint64;
typedef uint8_t Uint8;
#include "extracted_submission.h"
typedef struct { int held,rank; } SDL_mutex; typedef int SDL_cond;
typedef void JNIEnv; typedef void SDL_Thread; typedef int SDL_Android_AudioTrack;
typedef void (*SDL_AudioCallback)(void *,Uint8 *,int);
typedef struct { SDL_AudioCallback callback;void *userdata;SDL_AudioSubmitCallback callback_submit;SDL_AudioSubmissionCallback submission;SDL_AudioRateAppliedCallback rate_applied;int software_tempo; } SDL_AudioSpec;
typedef struct SDL_Aout_Opaque { SDL_cond *wakeup_cond;SDL_mutex *wakeup_mutex,*position_mutex;SDL_AudioPlaybackTracker playback;uint64_t output_epoch;int frame_size,output_sample_rate;SDL_AudioSpec spec;SDL_Android_AudioTrack *atrack;uint8_t *buffer;int buffer_size;volatile bool need_flush,pause_on,abort_request,need_set_volume;volatile float left_volume,right_volume;SDL_Thread *audio_tid;int audio_session_id;volatile float speed;volatile bool speed_changed;int engine_wakeup;SDL_AudioPlaybackSpan *playback_spans;uint64_t requested_rate_sequence;float requested_platform_rate,logical_platform_rate;int requested_preserve_pitch,logical_preserve_pitch;SDL_AudioRateApplied applied_rate;uint64_t notified_rate_sequence,notified_rate_epoch;int notified_rate_generation;} SDL_Aout_Opaque;
typedef struct { SDL_Aout_Opaque *opaque;double latency; } SDL_Aout;
typedef struct { SDL_mutex *mutex;int serial,abort_request; } PacketQueue;
typedef struct { AVFrame *frame; int serial; double pts; } Frame;
typedef struct { int freq; enum AVSampleFormat fmt; int channels; int64_t channel_layout; int frame_size,bytes_per_sec; } AudioParams;
typedef struct { SDL_mutex *mutex;SDL_cond *cond;PacketQueue *pktq;int size,rindex_shown,rindex,max_size;Frame queue[128]; } FrameQueue;
#include "extracted_eof.h"
typedef struct { double pts,speed,source_soft_base,source_soft_end_media;int serial,paused;SDL_Aout *source_aout;int source_soft; } Clock;
typedef struct VideoState { SDL_mutex *play_mutex;FrameQueue sampq;PacketQueue audioq,videoq;EOFClockState eof_clock;int abort_request,audio_buf_index,show_mode,muted,audio_volume,audio_write_buf_size,audio_clock_serial,latest_audio_seek_load_serial,pause_req;unsigned int audio_buf_size;uint8_t *audio_buf;double audio_clock,audio_buffer_media_rate;struct {int pkt_serial,first_frame_decoded,finished;Uint64 first_frame_decoded_time;} auddec;AudioParams audio_tgt,audio_src;Clock audclk,extclk;int64_t latest_seek_load_start_at;SDL_cond *continue_read_thread;uint64_t audio_engine_sequence;int requested_software_rate_mode,audio_engine_drained,platform_rate_capable;double audio_software_rate;int paused,seek_req,platform_rate_mode,software_rate_mode,audio_clock_unsupported,audio_processing_error,av_sync_type;void *audio_st,*video_st;struct {int finished,first_frame_decoded;Uint64 first_frame_decoded_time;} viddec;FrameQueue pictq;
 int step,audio_transform_serial,audio_transform_draining,audio_hw_buf_size;
 struct SwrContext *swr_ctx;uint8_t *audio_buf1;unsigned audio_buf1_size;
 short *audio_new_buf;unsigned audio_new_buf_size;void *handle;
 double audio_input_end,audio_output_cursor,audio_resample_end;} VideoState;
typedef struct {int value;} HarnessOptionDictionary;
typedef struct {const char *value;} HarnessOptionEntry;
typedef struct {AVDictionary *player_opts,*swr_opts;double preset_5_1_center_mix_level;int sync_av_start;VideoState *is;SDL_Aout *aout;int pf_playback_rate_changed,pf_playback_volume_changed,first_audio_frame_rendered,render_wait_start,start_on_prepared,soundtouch_enable,av_sync_type;float pf_playback_rate,pf_playback_volume;int64_t audio_callback_time;int loop,autoexit,auto_resume,error,prepared,last_error;int64_t start_time;struct {int64_t latest_seek_load_duration;float vdps,vfps,avdelay,avdiff,drop_frame_rate;} stat;} FFPlayer;
#define SDL_THREAD_PRIORITY_HIGH 1
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
#define SHOW_MODE_VIDEO 1
#define SDL_MIX_MAXVOLUME 128
#define AV_SYNC_AUDIO_MASTER 1
#define FFP_MSG_AUDIO_RENDERING_START 1
#define FFP_MSG_AUDIO_SEEK_RENDERING_START 2
#define FFP_MSG_COMPLETED 3
#define FFP_MSG_ERROR 4
#define FFP_MSG_AUDIO_DECODED_START 5
#define ALOGD(...) ((void)0)
#define av_log(...) ((void)0)
#define SDLTRACE(...) ((void)0)
static int warning_count;
#define ALOGW(...) (++warning_count)
#define ALOGI(...) ((void)0)
#define ALOGE(...) ((void)0)
static double fake_now;static int lock_count,lock_stack[8];
#define av_gettime_relative harness_now
#define av_gettime harness_now_abs
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
static int plays,pauses,flushes,physical_playing,decode_started;
static uint64_t physical_frames,physical_real_frames,physical_head;
static int physical_started,pause_fail,flush_fail,waits;
static void (*wait_hook)(void);
static int short_write_control,head_unavailable;
static void SDL_Android_AudioTrack_play(JNIEnv*e,SDL_Android_AudioTrack*t){plays++;physical_playing=1;}
static void SDL_Android_AudioTrack_pause(JNIEnv*e,SDL_Android_AudioTrack*t){pauses++;physical_playing=0;}
static void SDL_Android_AudioTrack_flush(JNIEnv*e,SDL_Android_AudioTrack*t){flushes++;physical_frames=physical_real_frames=physical_head=0;physical_started=0;}
static int SDL_Android_AudioTrack_pause_checked(JNIEnv*e,SDL_Android_AudioTrack*t){if(pause_fail)return -1;SDL_Android_AudioTrack_pause(e,t);return 0;}
static int SDL_Android_AudioTrack_flush_checked(JNIEnv*e,SDL_Android_AudioTrack*t){if(flush_fail)return -1;SDL_Android_AudioTrack_flush(e,t);return 0;}
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
static double get_clock(Clock*c){return c->pts;}
static void set_clock(Clock*c,double p,int s){set_clock_at(c,p,s,fake_now);}
static void set_clock_speed(Clock*c,double speed){c->speed=speed;}
static void sync_clock_to_slave(Clock*c,Clock*s){}
static int completions,errors;
static void ffp_notify_msg1(FFPlayer*f,int n){if(n==FFP_MSG_COMPLETED)completions++;if(n==FFP_MSG_AUDIO_DECODED_START)decode_started++;}
static void ffp_notify_msg2(FFPlayer*f,int n,int b){if(n==FFP_MSG_ERROR){assert(b==-EIO);errors++;}}
static int SDL_Android_AudioTrack_write(JNIEnv*,SDL_Android_AudioTrack*,uint8_t*,int);
static void aout_pause_audio(SDL_Aout*,int);
static int aout_request_platform_rate(SDL_Aout*,float,int,int);
static int SDL_AoutRequestPlatformRate(SDL_Aout*a,float rate,int preserve,int fields){return aout_request_platform_rate(a,rate,preserve,fields);}
static int aout_get_playback_position(SDL_Aout*,SDL_AudioPlaybackSnapshot*);
static int SDL_AoutGetPlaybackPosition(SDL_Aout*a,SDL_AudioPlaybackSnapshot*p){return aout_get_playback_position(a,p);}
#define JNI_OK 0
static int SDL_JNI_SetupThreadEnv(JNIEnv **env){*env=NULL;return JNI_OK;}
static SDL_Aout *rate_target;
static int rate_set_result,rate_set_calls,rate_head_reads,rate_head_fail_at;
static int rate_inject_newer,rate_write_case,rate_notify_calls,rate_notify_accept;
static SDL_AudioRateApplied rate_last_notified;
static int ordering_in_setter,ordering_mode,ordering_start_software,ordering_hook_done;
static float ordering_rate;
static void ordering_publish_rate(int inside_setter);
static int SDL_Android_AudioTrack_get_playback_head(JNIEnv*e,SDL_Android_AudioTrack*t,uint32_t*out){
 assert(rate_target && rate_target->opaque->position_mutex->held);
 ++rate_head_reads;if(head_unavailable||rate_head_reads==rate_head_fail_at)return -1;
 if(physical_frames>=18448)physical_started=1;
 if(physical_playing&&physical_started)physical_head=physical_frames;
 *out=(uint32_t)physical_head;return 0;
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


static Uint64 SDL_GetTickHR(void){return (Uint64)(fake_now*1000);}
static int synchronize_audio(VideoState*s,int n){return n;}
static int get_master_sync_type(VideoState*s){return AV_SYNC_AUDIO_MASTER;}
static void frame_queue_next(FrameQueue*q){assert(q->size>0);q->size--;q->rindex=(q->rindex+1)%q->max_size;}
#include "extracted_logic.h"
#define FFP_OPT_CATEGORY_PLAYER 4
static AVDictionary **ffp_get_opt_dict(FFPlayer *f,int category){return &f->player_opts;}
#include "extracted_controls.h"
static VideoState state;static FFPlayer player;static SDL_Aout aout;static SDL_Aout_Opaque backend;
static SDL_mutex mplay,msamp,maudio,mvideo,meof,mwake,mposition;static SDL_AudioPlaybackSpan playback_spans[2048];static SDL_cond cond;static int track;
static uint8_t buffer[256];static int track_paused,writes,active_real,real_written,engine_commits;
/* The r3 negative control lacks this new production field. It exits at the
 * startup assertion before any flag-specific positive case can be reached. */
static int epoch_has_real(void){
#ifdef STARTUP_HAVE_REAL_EPOCH_FLAG
 return backend.playback.has_real_pcm;
#else
 return -1;
#endif
}
static void ordering_publish_rate(int inside_setter){assert(!"unused ordering hook");}
static int SDL_Android_AudioTrack_write(JNIEnv*e,SDL_Android_AudioTrack*t,uint8_t*p,int n){
 assert(lock_count==0&&physical_playing&&n>0&&n<=256&&n%4==0);
 writes++;
 if(short_write_control&&writes==1){
  n=128;ffp_set_option_int(&player,FFP_OPT_CATEGORY_PLAYER,"soundtouch",1);backend.pause_on=true;
 }
 physical_frames+=n/4;if(active_real){physical_real_frames+=n/4;real_written+=n/4;}
 return n;
}
static void test_on_wait(void){
 assert(lock_count==0);++waits;fake_now+=.01;
 if(wait_hook){wait_hook();return;}
 if(waits==100){
  fprintf(stderr,"STALL after 100 owner waits: accepted=%" PRIu64 " played=%" PRIu64 " physical=%" PRIu64 " real=%" PRIu64 " decoded=%d\n",backend.playback.accepted_frames,backend.playback.played_frames,physical_frames,physical_real_frames,decode_started);
  state.abort_request=1;
 }
 assert(waits<=100);
}
static VideoState *setup(int software,float rate){
 assert(lock_count==0);memset(&state,0,sizeof state);memset(&player,0,sizeof player);memset(&backend,0,sizeof backend);
 mplay.rank=1;msamp.rank=2;maudio.rank=3;mvideo.rank=4;meof.rank=5;mwake.rank=6;mposition.rank=7;
 state.play_mutex=&mplay;state.eof_clock.mutex=&meof;state.audioq=(PacketQueue){&maudio,1,0};state.videoq=(PacketQueue){&mvideo,1,0};
 state.sampq=(FrameQueue){.mutex=&msamp,.cond=&cond,.pktq=&state.audioq,.max_size=128};
 state.audio_tgt=(AudioParams){.freq=48000,.fmt=AV_SAMPLE_FMT_S16,.channels=2,.channel_layout=AV_CH_LAYOUT_STEREO,.frame_size=4,.bytes_per_sec=192000};
 aout=(SDL_Aout){&backend,.1};player=(FFPlayer){.is=&state,.aout=&aout,.soundtouch_enable=software,.pf_playback_rate=rate,.start_on_prepared=1,.av_sync_type=AV_SYNC_AUDIO_MASTER};
 VideoState *is=&state;FFPlayer *ffp=&player;
 #include "extracted_init.h"
 state.audclk.source_aout=&aout;state.audclk.pts=NAN;state.auddec.pkt_serial=1;state.audio_volume=128;state.show_mode=SHOW_MODE_VIDEO;state.latest_audio_seek_load_serial=-1;state.continue_read_thread=&cond;state.audio_st=&track;state.platform_rate_capable=1;state.platform_rate_mode=!software;
 backend.wakeup_mutex=&mwake;backend.position_mutex=&mposition;backend.wakeup_cond=&cond;backend.frame_size=4;backend.output_sample_rate=48000;backend.output_epoch=1;backend.atrack=&track;backend.buffer=buffer;backend.playback_spans=playback_spans;
 backend.spec=(SDL_AudioSpec){.callback=sdl_audio_callback,.userdata=&player,.callback_submit=sdl_audio_submit_callback,.submission=sdl_audio_submission_callback,.rate_applied=sdl_audio_rate_applied,.software_tempo=software};
 backend.logical_platform_rate=backend.requested_platform_rate=1;backend.logical_preserve_pitch=backend.requested_preserve_pitch=software;backend.requested_rate_sequence=1;backend.applied_rate=(SDL_AudioRateApplied){.speed=1,.pitch=1};rate_target=&aout;
 assert(SDL_AudioPlaybackTracker_Init(&backend.playback,playback_spans,2048)==0);assert(SDL_AudioPlaybackTracker_Reset(&backend.playback,-1,1)==0);
 fake_now=10;plays=pauses=flushes=physical_playing=decode_started=physical_started=pause_fail=flush_fail=waits=writes=real_written=engine_commits=0;physical_frames=physical_real_frames=physical_head=0;track_paused=1;wait_hook=NULL;short_write_control=head_unavailable=0;rate_set_result=rate_set_calls=rate_head_reads=rate_head_fail_at=0;return &state;
}
static void enqueue(int count,double pts){
 FrameQueue *q=&state.sampq;assert(q->size<128);AVFrame *a=av_frame_alloc();assert(a);a->format=AV_SAMPLE_FMT_S16;a->sample_rate=48000;a->channels=2;a->channel_layout=AV_CH_LAYOUT_STEREO;a->nb_samples=count;assert(av_frame_get_buffer(a,0)==0);assert(av_samples_set_silence(a->extended_data,0,count,2,a->format)==0);
 q->queue[(q->rindex+q->size)%q->max_size]=(Frame){a,state.audioq.serial,pts};q->size++;
 /* Frames are real PCM even though all sample values are zero. */
}
static void cleanup(void){swr_free(&state.swr_ctx);ffp_soundtouch_destroy(state.handle);av_freep(&state.audio_buf1);av_freep(&state.audio_new_buf);for(int i=0;i<128;i++)av_frame_free(&state.sampq.queue[i].frame);av_dict_free(&player.player_opts);}
static int submit_one(SDL_AudioSubmission *out){
 SDL_AudioSubmission submission={0};sdl_audio_submit_callback(&player,buffer,sizeof buffer,&submission);active_real=submission.real_pcm;
 int result=aout_write_submitted_buffer(NULL,&aout,buffer,submission.output_bytes,&submission,&track_paused);if(submission.engine_switch&&result==0)engine_commits++;if(out)*out=submission;return result;
}
static void synthetic_prefix(int av_wait){
 if(av_wait){player.sync_av_start=1;state.video_st=&track;state.viddec.first_frame_decoded_time=SDL_GetTickHR();state.videoq.serial=1;}
 else state.paused=1;
 SDL_AudioSubmission submission;
 assert(submit_one(&submission)==0&&!submission.real_pcm&&submission.output_bytes==256);
 if(av_wait)state.viddec.first_frame_decoded=1;else state.paused=0;
 /* The actual callback owns a 512-byte silence buffer. Retain its second half. */
 assert(submit_one(&submission)==0&&!submission.real_pcm&&submission.output_bytes==256);
 assert(physical_frames==128&&physical_head==0&&real_written==0&&decode_started==0);
}
static void fill_and_start(void){
 for(int i=0;i<600&&!physical_started;i++)assert(submit_one(NULL)==0);
 assert(physical_started&&physical_head>0&&physical_frames>=18448&&real_written>0&&decode_started==1);
 assert(!state.eof_clock.audio_output_error&&!state.abort_request);
}

static void abort_real_drain(void){
 assert(waits==1&&flushes==0&&epoch_has_real());
 assert(backend.playback.played_frames<backend.playback.accepted_frames);
 assert(!engine_commits);state.abort_request=1;
}
static void resume_partial(void){
 assert(waits==1&&backend.pause_on&&track_paused&&writes==1);
 assert(epoch_has_real()&&backend.playback.accepted_frames==32);
 assert(state.eof_clock.audio_submit_pending&&flushes==0&&real_written==32);
 backend.pause_on=false;wait_hook=NULL;
}
static void finish_failed_seek(void){
 assert(waits==1&&state.eof_clock.seek_pending&&flushes==0&&physical_frames==128);
 eof_clock_seek(&state,0);wait_hook=NULL;
}
static void commit_seek(void){
 assert(waits==1&&state.eof_clock.seek_pending&&flushes==0&&physical_frames==128);
 state.audioq.serial=2;state.auddec.pkt_serial=2;eof_clock_seek(&state,0);wait_hook=NULL;
}
static void abort_wait(void){assert(waits==1&&flushes==0);state.abort_request=1;}
static void discard_queued_frames(void){
 for(int i=0;i<128;i++)av_frame_free(&state.sampq.queue[i].frame);
 state.sampq.rindex=state.sampq.size=0;
}
static SDL_AudioSubmission control_callback(void){
 SDL_AudioSubmission x={0};sdl_audio_submit_callback(&player,buffer,sizeof buffer,&x);
 assert(x.engine_switch&&!x.token&&!x.output_bytes&&!x.real_pcm);
 return x;
}
static int submit_control(SDL_AudioSubmission *x){active_real=0;return aout_write_submitted_buffer(NULL,&aout,buffer,0,x,&track_paused);}
static void test_initial_startup(void){
 setup(1,2);enqueue(48000,0);synthetic_prefix(0);
 int result=submit_one(NULL);
 if(result<0||state.abort_request){puts("FAIL initial 2x startup: synthetic-only epoch prevents first real decode");cleanup();exit(42);}
 assert(engine_commits==1&&flushes==1&&physical_frames==0&&track_paused&&decode_started==0);
 fill_and_start();puts("PASS initial 2x: 128 synthetic frames retired, first real decode fills 18448-frame minimum");cleanup();
 for(int av=0;av<2;av++)for(int i=0;i<6;i++){
  const float rates[]={.25f,.5f,1,2,3,4};setup(1,rates[i]);enqueue(96000,0);synthetic_prefix(av);
  fill_and_start();assert(flushes==(rates[i]!=1));assert(epoch_has_real());
  printf("PASS %s startup %.2fx: decoded and head moving\n",av?"A/V-wait":"paused",rates[i]);cleanup();
 }
}
static void test_prefirst_controls(void){
 for(int from=0;from<2;from++){
  setup(from,1);enqueue(96000,0);synthetic_prefix(0);
  ffp_set_option_int(&player,FFP_OPT_CATEGORY_PLAYER,"soundtouch",!from);ffp_set_playback_rate(&player,2);
  backend.pause_on=true;SDL_AudioSubmission c=control_callback();assert(submit_control(&c)==0);
  assert(flushes==1&&track_paused&&!physical_playing&&backend.pause_on);
  assert(state.software_rate_mode==!from&&state.audioq.serial==1&&backend.playback.generation==1&&backend.output_epoch==2);
  backend.pause_on=false;fill_and_start();printf("PASS pre-first-real engine %d->%d with paused commit\n",from,!from);cleanup();
 }
 setup(1,1);enqueue(96000,0);synthetic_prefix(0);ffp_set_playback_rate(&player,3);ffp_set_playback_rate(&player,1);ffp_set_playback_rate(&player,2);
 fill_and_start();assert(flushes==1&&state.audio_software_rate==2&&state.eof_clock.video_applied_rate==2);
 puts("PASS pre-first-real rate updates retain latest 2x clock/control");cleanup();
}
static void test_seek_and_abort(void){
 setup(1,2);enqueue(48000,0);synthetic_prefix(0);SDL_AudioSubmission c=control_callback();
 eof_clock_seek(&state,1);wait_hook=finish_failed_seek;assert(submit_control(&c)==0);assert(flushes==1&&waits==1&&backend.output_epoch==2);fill_and_start();cleanup();
 setup(1,2);enqueue(48000,0);synthetic_prefix(0);c=control_callback();eof_clock_seek(&state,1);wait_hook=commit_seek;
 assert(submit_control(&c)==0);assert(flushes==1&&waits==1&&backend.playback.generation==-1&&state.software_rate_mode);
 discard_queued_frames();enqueue(96000,10);fill_and_start();assert(state.audio_clock_serial==2&&backend.playback.generation==2);cleanup();
 setup(1,2);enqueue(48000,0);synthetic_prefix(0);c=control_callback();eof_clock_seek(&state,1);wait_hook=abort_wait;
 assert(submit_control(&c)<0&&flushes==0&&physical_frames==128);cleanup();
 puts("PASS pending/failed/committed seek and abort retain generation/reset ownership");
 /* A previous physical epoch may have real PCM; a checked seek reset is the
  * only event that permits a new exclusively-synthetic epoch to be retired. */
 setup(1,1);enqueue(64,0);state.eof_clock.audio_finished_serial=1;
 assert(submit_one(NULL)==0&&real_written==64&&epoch_has_real());
 state.audioq.serial=state.auddec.pkt_serial=2;state.eof_clock.audio_finished_serial=-1;state.audio_buf_index=state.audio_buf_size;
 enqueue(96000,10);state.paused=1;
 assert(submit_one(NULL)==0);assert(flushes==1&&backend.output_epoch==2&&!epoch_has_real());
 state.paused=0;assert(submit_one(NULL)==0);ffp_set_playback_rate(&player,2);
 fill_and_start();assert(flushes==2&&backend.output_epoch==3&&backend.playback.generation==2&&epoch_has_real());
 puts("PASS seek after real PCM resets epoch; new synthetic-only prefix is safely retired");cleanup();
}
static void test_real_retention(void){
 for(int reclaimed=0;reclaimed<2;reclaimed++){
  setup(0,1);enqueue(64,NAN);assert(submit_one(NULL)==0);assert(real_written==64&&epoch_has_real());
  if(reclaimed){physical_head=64;SDL_AudioPlaybackSnapshot p;aout_get_playback_position(&aout,&p);assert(p.played_frames==64&&backend.playback.count==0&&epoch_has_real());}
  state.paused=1;assert(submit_one(NULL)==0);state.paused=0;assert(submit_one(NULL)==0);
  assert(backend.playback.accepted_frames==192&&epoch_has_real());
  ffp_set_option_int(&player,FFP_OPT_CATEGORY_PLAYER,"soundtouch",1);wait_hook=abort_real_drain;
  assert(submit_one(NULL)<0&&flushes==0&&real_written==64);
  printf("PASS zero-valued unknown-PTS real + synthetic padding%s never flushed\n",reclaimed?", real spans reclaimed":"");cleanup();
 }
 setup(0,1);enqueue(128,0);short_write_control=1;wait_hook=resume_partial;
 assert(submit_one(NULL)==0);assert(writes==2&&real_written==64&&flushes==0&&!state.eof_clock.audio_submit_pending);
 short_write_control=0;assert(submit_one(NULL)==0&&real_written==128);
 wait_hook=abort_real_drain;waits=0;assert(submit_one(NULL)<0&&flushes==0&&real_written==128);
 puts("PASS pending short real write, paused suffix, and next callback retained across engine request");cleanup();
}
static void test_tracker_flag(void){
 SDL_AudioPlaybackTracker t;SDL_AudioPlaybackSpan spans[4];
 assert(SDL_AudioPlaybackTracker_Init(&t,spans,4)==0);
 assert(SDL_AudioPlaybackTracker_Reset(&t,1,1)==0);
#ifdef STARTUP_HAVE_REAL_EPOCH_FLAG
 assert(!t.has_real_pcm);
 assert(SDL_AudioPlaybackTracker_Accept(&t,0,1,NAN,NAN)==0&&!t.has_real_pcm);
 assert(SDL_AudioPlaybackTracker_AcceptRate(&t,1,1,NAN,NAN,-1,1)<0&&!t.has_real_pcm);
 assert(SDL_AudioPlaybackTracker_Accept(&t,1,0,NAN,NAN)==0&&!t.has_real_pcm);
 assert(SDL_AudioPlaybackTracker_Accept(&t,1,1,0,0)==0&&t.has_real_pcm);
 assert(SDL_AudioPlaybackTracker_Accept(&t,1,1,0,0)==0&&t.has_real_pcm&&t.count==2);
 assert(SDL_AudioPlaybackTracker_Observe(&t,3)==0&&t.has_real_pcm&&t.count==0);
 assert(SDL_AudioPlaybackTracker_Reset(&t,2,2)==0&&!t.has_real_pcm);
#endif
 puts("PASS real-epoch flag: successful/coalesced accepts only, sticky after reclaim, cleared on reset");
}
static void test_reset_and_platform_failures(void){
 for(int fail=0;fail<3;fail++){
  setup(1,2);enqueue(48000,0);synthetic_prefix(0);if(fail==0)pause_fail=1;else if(fail==1)flush_fail=1;else backend.output_epoch=UINT64_MAX;
  assert(submit_one(NULL)<0&&state.eof_clock.audio_output_error==-EIO&&decode_started==0&&physical_frames==128&&backend.playback.epoch==1);
  assert(flushes==0);cleanup();
 }
 puts("PASS failed checked pause, flush and exhausted epoch remain fatal");
 setup(1,2);enqueue(48000,0);synthetic_prefix(0);head_unavailable=1;wait_hook=abort_wait;
 assert(submit_one(NULL)<0&&flushes==0&&!state.eof_clock.audio_output_error);cleanup();
 puts("PASS stale/unavailable head cannot authorize synthetic reset");
 setup(0,2);enqueue(96000,0);synthetic_prefix(0);
 backend.applied_rate=(SDL_AudioRateApplied){.speed=2,.pitch=2,.sequence=backend.requested_rate_sequence};
 backend.requested_platform_rate=backend.logical_platform_rate=2;ffp_set_option_int(&player,FFP_OPT_CATEGORY_PLAYER,"soundtouch",1);rate_set_result=1;
 assert(submit_one(NULL)==0&&flushes==1&&!state.software_rate_mode&&!state.requested_software_rate_mode);
 assert(backend.applied_rate.speed==2&&backend.applied_rate.pitch==2);rate_set_result=0;fill_and_start();cleanup();
 setup(0,2);enqueue(96000,0);synthetic_prefix(0);ffp_set_option_int(&player,FFP_OPT_CATEGORY_PLAYER,"soundtouch",1);rate_set_result=-1;
 assert(submit_one(NULL)<0&&flushes==1&&state.eof_clock.audio_output_error==-EIO&&!state.software_rate_mode&&decode_started==0);cleanup();
 puts("PASS verified platform rollback preserves engine; fatal platform failure stays fatal");
}
int main(void){
 setvbuf(stdout,NULL,_IONBF,0);test_initial_startup();test_prefirst_controls();test_seek_and_abort();test_real_retention();test_tracker_flag();test_reset_and_platform_failures();
 puts("PASS startup-liveness regression: production init/producer/callback/owner, real SWR/SoundTouch, minimum-fill device model");return 0;
}
