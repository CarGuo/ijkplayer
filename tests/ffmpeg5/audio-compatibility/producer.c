/* Exact production producer with real pinned SWR/SoundTouch. Queue/SDL dependencies are deterministic stubs. */
#define __ANDROID__ 1
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <libavutil/frame.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavutil/log.h>
#include <libswresample/swresample.h>
#include "ff_soundtouch.h"
#define FFP_MSG_AUDIO_DECODED_START 1
#define FFMIN(a,b) ((a)<(b)?(a):(b))
#define AV_SYNC_AUDIO_MASTER 1
#define ALOGD(...) ((void)0)
typedef uint64_t Uint64;
typedef struct{int held;}SDL_mutex;
static void SDL_LockMutex(SDL_mutex*m){assert(!m->held);m->held=1;}
static void SDL_UnlockMutex(SDL_mutex*m){assert(m->held);m->held=0;}
typedef struct{int freq;enum AVSampleFormat fmt;int channels;int64_t channel_layout;int frame_size,bytes_per_sec;}AudioParams;
typedef struct{AVFrame *frame;int serial;double pts;}Frame;
typedef struct{int serial;}PacketQueue;
typedef struct{int unused;}FrameQueue;
typedef struct {void*source_aout;int source_soft;double pts;}Clock;
typedef struct {double played_media;}SDL_AudioPlaybackSnapshot;
static void SDL_AoutGetPlaybackPosition(void*a,SDL_AudioPlaybackSnapshot*s){assert(a);s->played_media=0;}
static double get_clock(Clock*c){return c->pts;}
static void set_clock(Clock*c,double p,int serial){(void)serial;c->pts=p;}
typedef struct VideoState {
 int paused,step,audio_clock_unsupported,abort_request;void*video_st;Clock audclk;
 struct{int first_frame_decoded,finished;Uint64 first_frame_decoded_time;}viddec,auddec;
 FrameQueue sampq;PacketQueue videoq,audioq;AudioParams audio_src,audio_tgt;
 struct SwrContext*swr_ctx;uint8_t*audio_buf1,*audio_buf;unsigned audio_buf1_size;
 double audio_clock;int audio_clock_serial,audio_hw_buf_size;
 SDL_mutex*play_mutex;struct{SDL_mutex*mutex;int audio_transform_serial,audio_transform_pending;}eof_clock;
 int audio_transform_serial,audio_transform_draining,software_rate_mode;
 int requested_software_rate_mode,audio_engine_drained;
 int audio_processing_error;double audio_resample_end;
 double audio_input_end,audio_output_cursor,audio_buffer_media_rate,audio_software_rate;
 double audio_soundtouch_pts_anchor;int64_t audio_soundtouch_input_samples;
 short*audio_new_buf;unsigned audio_new_buf_size;void*handle;
}VideoState;
typedef struct{VideoState*is;int sync_av_start;int64_t audio_callback_time;AVDictionary*swr_opts;double preset_5_1_center_mix_level;float pf_playback_rate;}FFPlayer;
static Frame frames[64];static int read_index,frame_count,wanted_override,decode_started,master_type=1;
static Uint64 SDL_GetTickHR(void){return 5000;}
static Frame*audio_frame_queue_peek_readable(VideoState*s){(void)s;return read_index<frame_count?&frames[read_index]:NULL;}
static void frame_queue_next(FrameQueue*q){(void)q;read_index++;}
static int synchronize_audio(VideoState*s,int n){(void)s;return wanted_override?wanted_override:n;}
static void ffp_notify_msg1(FFPlayer*f,int n){(void)f;(void)n;decode_started++;}
static int get_master_sync_type(VideoState*s){(void)s;return master_type;}
static int soundtouch_instances;
static void *counted_soundtouch_create(int sample_rate, int channels, double tempo) {
 ++soundtouch_instances;
 return ffp_soundtouch_create(sample_rate, channels, tempo);
}
#define ffp_soundtouch_create counted_soundtouch_create
#include "extracted_decode.h"
#undef ffp_soundtouch_create
static SDL_mutex pm,em;static VideoState s;static FFPlayer f;
static void setup(int target,int software,double rate){
 assert(!pm.held&&!em.held);memset(&s,0,sizeof s);memset(&f,0,sizeof f);
 s.audioq.serial=1;s.audclk.source_aout=&s;s.audclk.pts=NAN;s.audio_transform_serial=-1;s.software_rate_mode=s.requested_software_rate_mode=software;s.audio_software_rate=rate;
 s.audio_tgt=(AudioParams){.freq=target,.fmt=AV_SAMPLE_FMT_S16,.channels=2,.channel_layout=AV_CH_LAYOUT_STEREO,.frame_size=4,.bytes_per_sec=target*4};
 s.play_mutex=&pm;s.eof_clock.mutex=&em;f.is=&s;f.pf_playback_rate=rate;read_index=frame_count=wanted_override=decode_started=soundtouch_instances=0;master_type=1;
}
static void enqueue(int source,int count,double pts,int serial){
 assert(frame_count<64);AVFrame*a=av_frame_alloc();assert(a);a->format=AV_SAMPLE_FMT_FLTP;a->sample_rate=source;a->channels=2;a->channel_layout=AV_CH_LAYOUT_STEREO;a->nb_samples=count;assert(av_frame_get_buffer(a,0)==0);assert(av_samples_set_silence(a->extended_data,0,count,2,a->format)==0);frames[frame_count++]=(Frame){a,serial,pts};
}
static void cleanup(void){swr_free(&s.swr_ctx);ffp_soundtouch_destroy(s.handle);av_freep(&s.audio_buf1);av_freep(&s.audio_new_buf);for(int i=0;i<frame_count;i++)av_frame_free(&frames[i].frame);}
static int drain(const char*name){int n,total=0,call=0;
 while((n=audio_decode_frame(&f))>=0){assert(n>0);assert(++call<1000);total+=n/4;printf("%s call=%d consumed=%d out=%d pts=%.9f rate=%.2f pending=%d\n",name,call,read_index,n/4,s.audio_clock,s.audio_buffer_media_rate,s.eof_clock.audio_transform_pending);}
 assert(!s.audio_clock_unsupported&&!s.audio_processing_error);assert(read_index==frame_count);assert(!s.eof_clock.audio_transform_pending);assert(!s.swr_ctx&&!s.handle);return total;
}
/* #4274: exact source PTS cadence 0, -15, -29 samples relative to
 * an uninterrupted 44.1 kHz stream; its timestamps are not rewritten. */
static uint64_t soundtouch_pts_jitter(int jitter, double rate){
 static const int offset[]={0,-15,-29};
 setup(44100,1,rate);
 for(int i=0;i<24;i++){
  enqueue(44100,1024,4+(i*1024+(jitter?offset[i%3]:0))/44100.,1);
  AVFrame *a=frames[i].frame;
  for(int j=0;j<a->nb_samples;j++){
   float tone=(float)(.3*sin(2*3.141592653589793*241*(i*1024+j)/44100.));
   ((float*)a->extended_data[0])[j]=tone;
   ((float*)a->extended_data[1])[j]=-tone;
  }
 }
 int n,total=0;uint64_t hash=UINT64_C(14695981039346656037);
 while((n=audio_decode_frame(&f))>0){
  for(int i=0;i<n;i++)hash=(hash^s.audio_buf[i])*UINT64_C(1099511628211);
  total+=n/4;
 }
 assert(soundtouch_instances==1&&read_index==frame_count);
 assert(!s.audio_processing_error&&!s.eof_clock.audio_transform_pending&&!s.handle);
 assert(total==(int)llround(24*1024/rate));
 assert(fabs(s.audio_clock-(4+24*1024./44100))<1e-9);
 cleanup();
 return hash;
}
/* A genuine same-serial gap must finish the old transform, then accept the
 * first new frame once. Compare actual stereo PCM to independently drained
 * transforms of the two distinct segments, not just frame totals. */
static uint64_t soundtouch_real_gap(int first, int second, uint64_t hash){
 const double rate=1.5;
 setup(44100,1,rate);
 if(first){
  enqueue(44100,4096,4,1);
  for(int j=0;j<4096;j++){
   ((float*)frames[0].frame->extended_data[0])[j]=.30f*sin(2*3.141592653589793*241*j/44100.);
   ((float*)frames[0].frame->extended_data[1])[j]=.20f*cos(2*3.141592653589793*241*j/44100.);
  }
 }
 if(second){
  enqueue(44100,4096,4.125,1);
  AVFrame *a=frames[frame_count-1].frame;
  for(int j=0;j<4096;j++){
   ((float*)a->extended_data[0])[j]=.25f*sin(2*3.141592653589793*373*j/44100.);
   ((float*)a->extended_data[1])[j]=.15f*cos(2*3.141592653589793*373*j/44100.);
  }
 }
 int n,total=0,old_frames=0,new_frames=0;
 double last_old_pts=NAN;
 while((n=audio_decode_frame(&f))>0){
  assert(s.audio_clock_serial==1&&s.audio_buffer_media_rate==rate);
  for(int i=0;i<n;i++)hash=(hash^s.audio_buf[i])*UINT64_C(1099511628211);
  total+=n/4;
  if(s.audio_clock<4.125){old_frames+=n/4;last_old_pts=s.audio_clock;}
  else new_frames+=n/4;
 }
 const int expected=(int)llround(4096/rate);
 assert(soundtouch_instances==first+second&&read_index==frame_count);
 assert(!s.audio_processing_error&&!s.eof_clock.audio_transform_pending&&!s.handle);
 assert(old_frames==first*expected&&new_frames==second*expected&&total==(first+second)*expected);
 if(first)assert(fabs(last_old_pts-(4+expected*rate/44100.))<1e-9);
 if(second)assert(fabs(s.audio_clock-(4.125+expected*rate/44100.))<1e-9);
 cleanup();
 return hash;
}
static void soundtouch_pts_bounds(void){
 assert(soundtouch_pts_jitter(0,1)==soundtouch_pts_jitter(1,1));
 assert(soundtouch_pts_jitter(0,1.5)==soundtouch_pts_jitter(1,1.5));
 const uint64_t seed=UINT64_C(14695981039346656037);
 const uint64_t separate=soundtouch_real_gap(0,1,soundtouch_real_gap(1,0,seed));
 assert(soundtouch_real_gap(1,1,seed)==separate);
 /* Each adjacent deviation is <1 ms, but cumulative drift crosses
  * the bound on the fourth frame. A last-frame-only tolerance fails. */
 setup(44100,1,1);
 for(int i=0;i<4;i++)enqueue(44100,1024,4+i*(1024+16)/44100.,1);
 assert(drain("soundtouchBoundedDrift")==4096&&soundtouch_instances==2);cleanup();
 setup(44100,1,1);
 enqueue(44100,1024,4,1);
 enqueue(48000,1024,4+1024./44100,1);
 assert(abs(drain("soundtouchFmtChange")-(1024+(int)llround(1024.*44100/48000)))<=1);
 assert(soundtouch_instances==2);cleanup();
}
int main(int argc,char**argv){
 setvbuf(stdout,NULL,_IONBF,0);
 if(argc>1){
  if(!strcmp(argv[1],"swr_endpoint")){setup(48000,0,1);enqueue(96000,1024,4,1);assert(drain("swrEndpoint")==512);assert(fabs(s.audio_clock-(4+1024./96000))<1e-9);cleanup();return 0;}
  if(!strcmp(argv[1],"pts_discontinuity")){setup(48000,1,1);enqueue(48000,1024,4,1);enqueue(48000,1024,10,1);assert(drain("ptsDiscontinuity")==2048);assert(fabs(s.audio_clock-(10+1024./48000))<1e-9);cleanup();return 0;}
  if(!strcmp(argv[1],"soundtouch_pts_jitter")){soundtouch_pts_bounds();return 0;}
  return 2;
 }
 setup(48000,0,1);enqueue(96000,1024,4,1);assert(drain("96to48")==512);cleanup();
 setup(24000,0,1);enqueue(48000,1024,4,1);assert(drain("lateSBR")==512);cleanup();
 setup(48000,0,1);enqueue(48000,1024,4,1);wanted_override=1050;assert(drain("compensated")==1050);cleanup();
 setup(48000,0,1);enqueue(96000,1024,4,1);enqueue(48000,1024,4+1024./96000,1);assert(drain("formatChange")==1536);assert(decode_started==1);cleanup();
 setup(48000,1,3);enqueue(96000,1024,4,1);assert(drain("software3resample")==171);cleanup();
 setup(48000,1,1);enqueue(48000,1024,4,1);assert(drain("software1short")==1024);cleanup();
 setup(48000,1,.5);enqueue(48000,1024,4,1);assert(drain("softwareHalfShort")==2048);cleanup();
 setup(48000,1,3);enqueue(48000,1,4,1);int tiny=drain("software3one");assert(tiny==0);cleanup();
 setup(48000,0,1);master_type=2;enqueue(48000,1024,4,1);assert(drain("videoMaster")==1024);assert(s.audclk.source_soft);cleanup();
 setup(48000,1,1);enqueue(48000,1024,4,1);enqueue(48000,1024,10,1);assert(drain("finitePtsJump")==2048);assert(fabs(s.audio_clock-(10+1024./48000))<1e-9);cleanup();
 setup(48000,1,1);enqueue(48000,48000,0,1);enqueue(48000,144000,1,1);enqueue(48000,48000,4,1);
 int out1=0,out3=0,switched3=0,switched1=0,calls=0,nn;
 while((nn=audio_decode_frame(&f))>=0){
  assert(nn>0&&++calls<100);printf("rateSwitch call=%d consumed=%d output=%d effective=%.1f pts=%.9f requested=%.1f\n",calls,read_index,nn/4,s.audio_buffer_media_rate,s.audio_clock,f.pf_playback_rate);
  if(s.audio_buffer_media_rate==1)out1+=nn/4;else{assert(s.audio_buffer_media_rate==3);out3+=nn/4;}
  if(!switched3){f.pf_playback_rate=3;switched3=1;}else if(!switched1&&s.audio_buffer_media_rate==3){f.pf_playback_rate=1;switched1=1;}
 }
 assert(switched3&&switched1&&out1==96000&&out3==48000&&!s.eof_clock.audio_transform_pending);assert(fabs(s.audio_clock-5)<1e-9);cleanup();
 setup(48000,0,1);enqueue(96000,1024,4,1);int pre=audio_decode_frame(&f);assert(pre==496*4&&s.eof_clock.audio_transform_pending);s.paused=1;assert(audio_decode_frame(&f)==-1&&s.eof_clock.audio_transform_pending);s.paused=0;assert(drain("pauseRetainsSWR")==16);cleanup();
 setup(48000,1,.5);enqueue(48000,10240,4,1);assert(audio_decode_frame(&f)>0&&s.handle&&s.eof_clock.audio_transform_pending);s.paused=1;assert(audio_decode_frame(&f)==-1&&s.handle);s.audioq.serial=2;enqueue(48000,1024,10,2);s.paused=0;int sought=drain("seekResetsST");assert(sought==2048&&s.audio_clock_serial==2&&fabs(s.audio_clock-(10+1024./48000))<1e-9);cleanup();
 setup(48000,0,1);enqueue(44100,1024,0,1);int converted=drain("44to48");assert(abs(converted-(int)llround(1024.*48000/44100))<=1);cleanup();
 setup(48000,0,1);master_type=3;enqueue(48000,1024,4,1);assert(drain("externalMaster")==1024&&s.audclk.source_soft);cleanup();
 setup(48000,1,1);enqueue(48000,1024,4,1);enqueue(48000,1024,NAN,1);enqueue(48000,1024,6,1);
 int known=0,unknown=0,total=0,n;
 while((n=audio_decode_frame(&f))>0){total+=n/4;if(isfinite(s.audio_clock))known++;else unknown++;}
 assert(!s.audio_processing_error&&!s.eof_clock.audio_transform_pending&&total==3072&&known&&unknown);
 assert(fabs(s.audio_clock-(6+1024./48000))<1e-9);cleanup();
 /* Producer must stop at a control boundary without consuming the first input
  * of the new engine. Backend/JNI acknowledgement is covered by owner tests. */
 setup(48000,0,3);enqueue(48000,1024,0,1);s.requested_software_rate_mode=1;
 assert(audio_decode_frame(&f)==-1&&s.audio_engine_drained&&s.eof_clock.audio_transform_pending&&read_index==0);
 s.software_rate_mode=1;s.audio_software_rate=3;s.audio_engine_drained=0;s.eof_clock.audio_transform_pending=0;
 assert(drain("producerLiveSoftware")==341);cleanup();
 setup(48000,1,.5);enqueue(48000,48000,0,1);enqueue(48000,2400,1,1);
 int old=audio_decode_frame(&f)/4;assert(old>0);s.requested_software_rate_mode=0;
 while((n=audio_decode_frame(&f))>0){assert(s.audio_buffer_media_rate==.5);old+=n/4;}
 assert(old==96000&&read_index==1&&s.audio_engine_drained&&s.eof_clock.audio_transform_pending);
 s.software_rate_mode=0;s.audio_engine_drained=0;s.eof_clock.audio_transform_pending=0;
 assert(drain("producerLivePlatform")==2400);cleanup();
 setup(48000,1,1);enqueue(48000,1024,0,1);assert(drain("beforeIdleRate")==1024);
 f.pf_playback_rate=3;assert(audio_decode_frame(&f)==-1&&s.audio_engine_drained&&s.eof_clock.audio_transform_pending);
 s.audio_software_rate=3;s.audio_engine_drained=0;s.eof_clock.audio_transform_pending=0;
 assert(audio_decode_frame(&f)==-1&&!s.eof_clock.audio_transform_pending);cleanup();
 soundtouch_pts_bounds();
 puts("PASS exact producer: resampling/compensation/tails, timestamps, tempo transitions, pause/seek and live control boundaries");
}
