#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <signal.h>
#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMIN(a,b) ((a) < (b) ? (a) : (b))
#define AV_SYNC_VIDEO_MASTER 0
#define AV_SYNC_AUDIO_MASTER 1
#define AV_SYNC_EXTERNAL_CLOCK 2
#define AV_NOSYNC_THRESHOLD 10.0
#define AV_SYNC_THRESHOLD_MIN .04
#define AV_SYNC_THRESHOLD_MAX .1
#define AV_SYNC_FRAMEDUP_THRESHOLD .1
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
#define SDL_MIX_MAXVOLUME 128
#define SHOW_MODE_VIDEO 0
#define FFP_MSG_AUDIO_RENDERING_START 1
#define FFP_MSG_AUDIO_SEEK_RENDERING_START 2
typedef uint8_t Uint8;
typedef pthread_mutex_t SDL_mutex;
typedef pthread_cond_t SDL_cond;
/* PRODUCTION_DECLARATIONS */
typedef struct { SDL_mutex *mutex; int serial, abort_request; } PacketQueue;
typedef struct { int serial; double pts, duration; } Frame;
typedef struct { SDL_mutex *mutex; SDL_cond *cond; PacketQueue *pktq; int size,rindex_shown,rindex,max_size; Frame queue[4]; } FrameQueue;
typedef struct {
    EOFClockState eof_clock;
    Clock audclk,vidclk,extclk;
    FrameQueue sampq,pictq;
    PacketQueue audioq,videoq;
    struct {int pkt_serial,finished;} auddec,viddec;
    void *audio_st,*video_st;
    int av_sync_type,requested_av_sync_type,paused,seek_req;
    unsigned int audio_buf_size;
    int audio_buf_index,audio_write_buf_size,audio_volume,muted,show_mode;
    int audio_clock_serial,latest_audio_seek_load_serial,pause_req,abort_request;
    struct { int frame_size,bytes_per_sec; } audio_tgt;
    Uint8 *audio_buf;
    double audio_clock;
} VideoState;
typedef struct {
    VideoState *is;
    int64_t audio_callback_time;
    int pf_playback_rate_changed,soundtouch_enable,pf_playback_volume_changed;
    double pf_playback_rate,pf_playback_volume;
    int first_audio_frame_rendered,av_sync_type,render_wait_start,start_on_prepared;
    void *aout;
    struct { double avdelay,avdiff; } stat;
} FFPlayer;
static int64_t now_us,decode_delay_us,latency_delay_us,manual_delay_us;
static int manual_abort,manual_seek;
static FFPlayer *active_player;
static int decode_mode,decode_calls,latency_calls,notify_calls;
static double backend_latency;
static void (*decode_probe)(VideoState *);
static Uint8 pcm[4096];
static atomic_int wait_entered,hold_before_wait,allow_wait;
static int64_t av_gettime_relative(void) { return now_us; }
static void SDL_LockMutex(SDL_mutex *m) { int r=pthread_mutex_lock(m); if(r) abort(); }
static void SDL_UnlockMutex(SDL_mutex *m) { int r=pthread_mutex_unlock(m); if(r) abort(); }
static void SDL_CondSignal(SDL_cond *c) { if(pthread_cond_signal(c)) abort(); }
static void SDL_CondWait(SDL_cond *c, SDL_mutex *m) {
    atomic_store(&wait_entered,1);
    while(atomic_load(&hold_before_wait) && !atomic_load(&allow_wait)) sched_yield();
    if(pthread_cond_wait(c,m)) abort();
}
static int audio_decode_frame(FFPlayer *f) {
    ++decode_calls;now_us+=decode_delay_us;
    if(decode_probe) decode_probe(f->is);
    if(decode_mode<=0) return -1;
    f->is->audio_buf=pcm;
    if(decode_mode==2) decode_mode=0;
    return 400;
}
static double SDL_AoutGetLatencySeconds(void *a) { (void)a; ++latency_calls; now_us+=latency_delay_us; return backend_latency; }
static void SDL_AoutSetPlaybackRate(void*a,double b) {(void)a;(void)b;}
static void SDL_AoutSetPlaybackVolume(void*a,double b) {(void)a;(void)b;}
static void SDL_AoutFlushAudio(void*a) {(void)a;}
static void SDL_Delay(int ms) {
    (void)ms;now_us+=manual_delay_us;
    if(manual_seek){++active_player->is->audioq.serial;++active_player->is->videoq.serial;}
    if(manual_abort)active_player->is->abort_request=1;
    else {active_player->is->pause_req=0;active_player->is->audclk.paused=0;}
}
static void update_sample_display(VideoState*s,int16_t*p,int n) {(void)s;(void)p;(void)n;}
static void SDL_MixAudio(Uint8*d,Uint8*s,int n,int v) {(void)v;memcpy(d,s,n);}
static void ffp_notify_msg1(FFPlayer*f,int m) {(void)f;(void)m;++notify_calls;}
static void ffp_notify_msg2(FFPlayer*f,int m,int v) {(void)f;(void)m;(void)v;++notify_calls;}
