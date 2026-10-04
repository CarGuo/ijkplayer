#!/usr/bin/env python3
"""Compile exact production callback/clock functions against deterministic stubs.
No Android SDK, codec or device simulation is claimed. Usage: SCRIPT REPO [--baseline].
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
repo = Path(sys.argv[1]).resolve()
source_path = 'ijkmedia/ijkplayer/ff_ffplay.c'
source = (subprocess.check_output(['git', 'show', 'HEAD:' + source_path], cwd=repo).decode()
          if '--baseline' in sys.argv else (repo / source_path).read_text())

def extract(name):
    import re
    match = re.search(r'^static [^\n]*\b' + name + r'\([^\n]*\)\n\{', source, re.M)
    assert match, name
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]

preamble = r'''
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
typedef uint8_t Uint8;
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
#define SDL_MIX_MAXVOLUME 128
#define SHOW_MODE_VIDEO 0
#define AV_SYNC_AUDIO_MASTER 1
#define AV_NOSYNC_THRESHOLD 10.0
#define FFP_MSG_AUDIO_RENDERING_START 1
#define FFP_MSG_AUDIO_SEEK_RENDERING_START 2
typedef struct { double pts, last_updated, pts_drift, speed; int paused, serial; int *queue_serial; } Clock;
typedef struct {
    int audio_buf_index, audio_buf_size, audio_write_buf_size, audio_volume;
    int muted, show_mode, audio_clock_serial, latest_audio_seek_load_serial;
    int pause_req, abort_request;
    struct { int frame_size, bytes_per_sec; } audio_tgt;
    struct { int pkt_serial; } auddec;
    struct { int serial; } audioq;
    Uint8 *audio_buf;
    double audio_clock;
    Clock audclk, extclk;
} VideoState;
typedef struct {
    VideoState *is;
    int64_t audio_callback_time;
    int pf_playback_rate_changed, soundtouch_enable, pf_playback_volume_changed;
    double pf_playback_rate, pf_playback_volume;
    int first_audio_frame_rendered, av_sync_type;
    int render_wait_start, start_on_prepared;
    void *aout;
} FFPlayer;
static int64_t now_us, decode_delay, latency_delay, wait_delay;
static int abort_wait, seek_wait, latency_calls, notify_calls, decode_calls;
static double backend_latency;
static FFPlayer *active;
static Uint8 pcm[4000];
static int64_t av_gettime_relative(void) { return now_us; }
static int audio_decode_frame(FFPlayer *f) {
    ++decode_calls; now_us += decode_delay;
    f->is->audio_buf = pcm;
    return sizeof(pcm);
}
static double SDL_AoutGetLatencySeconds(void *a) {
    (void)a; ++latency_calls; now_us += latency_delay; return backend_latency;
}
static void SDL_Delay(int ms) {
    (void)ms; now_us += wait_delay;
    if (seek_wait) ++active->is->audioq.serial;
    if (abort_wait) active->is->abort_request = 1;
    else { active->is->pause_req = 0; active->is->audclk.paused = 0; }
}
static void SDL_AoutSetPlaybackRate(void *a, double b) { (void)a; (void)b; }
static void SDL_AoutSetPlaybackVolume(void *a, double b) { (void)a; (void)b; }
static void SDL_AoutFlushAudio(void *a) { (void)a; }
static void update_sample_display(VideoState *s, int16_t *p, int n) { (void)s; (void)p; (void)n; }
static void SDL_MixAudio(Uint8 *d, Uint8 *s, int n, int v) { (void)v; memcpy(d,s,n); }
static void ffp_notify_msg1(FFPlayer *f, int m) { (void)f; (void)m; ++notify_calls; }
static void ffp_notify_msg2(FFPlayer *f, int m, int v) { (void)f; (void)m; (void)v; ++notify_calls; }
'''
main = r'''
static void check(int ok, const char *name) {
    if (!ok) { fprintf(stderr,"FAIL: %s\n",name); exit(1); }
}
static void close_to(double actual, double expected, const char *name) {
    if (!isfinite(actual) || !isfinite(expected) || fabs(actual-expected)>1e-8) {
        fprintf(stderr,"FAIL: %s: %.9f != %.9f\n",name,actual,expected); exit(1);
    }
}
static void setup(FFPlayer *f, VideoState *s) {
    memset(f,0,sizeof(*f)); memset(s,0,sizeof(*s)); f->is=s; active=f;
    now_us=100000000; decode_delay=latency_delay=wait_delay=0; abort_wait=seek_wait=0;
    latency_calls=notify_calls=decode_calls=0; backend_latency=0.1;
    s->audio_tgt.frame_size=4; s->audio_tgt.bytes_per_sec=4000;
    s->audio_volume=128; s->audioq.serial=s->auddec.pkt_serial=s->audio_clock_serial=7;
    s->latest_audio_seek_load_serial=-1; s->audio_clock=1.0;
    s->audclk.speed=s->extclk.speed=1.0;
    s->audclk.queue_serial=&s->audioq.serial; s->extclk.queue_serial=&s->extclk.serial;
    s->audclk.serial=s->extclk.serial=7;
    s->audclk.pts=s->extclk.pts=NAN; s->audclk.pts_drift=s->extclk.pts_drift=NAN;
}
static void run_case(const char *label, int64_t decode, int64_t latency, int64_t wait, int paused) {
    FFPlayer f; VideoState s; Uint8 out[1000]; setup(&f,&s);
    decode_delay=decode; latency_delay=latency; wait_delay=wait;
    s.audclk.paused=paused;
    if (wait) { f.render_wait_start=1; s.pause_req=1; s.audclk.paused=1; }
    s.latest_audio_seek_load_serial=7;
    sdl_audio_callback(&f,out,sizeof(out));
    /* 1 second decoded, .75 second retained, .1 second backend latency. */
    double expected=0.15;
#if !defined(__ANDROID__)
    if (!s.audclk.paused) expected += (decode+latency+wait)/1000000.0;
#endif
    close_to(get_clock(&s.audclk),expected,label);
#if defined(__ANDROID__)
    close_to(s.audclk.last_updated,now_us/1000000.0,"output-ready anchor");
    close_to(get_clock(&s.extclk),0.15,"external clock starts from corrected audio time");
#else
    close_to(s.audclk.last_updated,100.0,"unchanged non-Android entry anchor");
#endif
    check(f.audio_callback_time==100000000,"entry timestamp preserved");
    check(latency_calls==1,"one latency query");
    check(notify_calls==2,"render and seek notifications preserved");
    check(s.audio_write_buf_size==3000,"retained PCM accounted");
    now_us += 250000;
    close_to(get_clock(&s.audclk),expected+(s.audclk.paused ? 0 : .25),"subsequent progression or pause");
    printf("PASS %s\n",label);
}
int main(void) {
    memset(pcm,0x5a,sizeof(pcm));
    run_case("ordinary callback",0,0,0,0);
    run_case("6.5 second codec startup",6500000,0,0,0);
    run_case("latency query blocking",6500000,25000,0,0);
    run_case("manual start after decode",6500000,25000,4000000,0);
    run_case("paused clock stays frozen",6500000,25000,0,1);
    FFPlayer f; VideoState s; Uint8 out[1000]; setup(&f,&s);
    s.audio_clock=NAN; sdl_audio_callback(&f,out,sizeof(out));
    check(isnan(get_clock(&s.audclk)) && !latency_calls,"unknown PTS not published");
    puts("PASS unknown PTS");
#if defined(__ANDROID__)
    setup(&f,&s); abort_wait=1; wait_delay=4000000;
    f.render_wait_start=1; s.pause_req=1;
    sdl_audio_callback(&f,out,sizeof(out));
    check(isnan(get_clock(&s.audclk)) && !latency_calls,"abort during manual wait not published");
    puts("PASS manual-start abort");
#endif
    setup(&f,&s); s.audio_clock=-1;
    sdl_audio_callback(&f,out,sizeof(out));
    close_to(get_clock(&s.audclk),-1.85,"negative PTS remains unclamped");
    puts("PASS negative PTS remains unclamped");
    setup(&f,&s); sdl_audio_callback(&f,out,sizeof(out));
    check(decode_calls==1,"first callback decodes PCM");
    now_us += 250000; decode_delay=6500000;
    sdl_audio_callback(&f,out,sizeof(out));
    check(decode_calls==1,"retained PCM needs no second decode");
    check(s.audio_write_buf_size==2000,"second callback retained bytes");
    close_to(get_clock(&s.audclk),0.4,"retained PCM publishes fresh clock");
    puts("PASS repeated callback with retained PCM");
    setup(&f,&s); s.audio_clock_serial=6;
    sdl_audio_callback(&f,out,sizeof(out));
    check(isnan(get_clock(&s.audclk)),"stale clock serial rejected");
    check(isnan(get_clock(&s.extclk)),"stale clock never reaches external clock");
    puts("PASS stale clock serial");
    setup(&f,&s); s.audio_clock_serial=-1;
    sdl_audio_callback(&f,out,sizeof(out));
    check(isnan(get_clock(&s.audclk)),"invalid clock serial rejected");
    check(isnan(get_clock(&s.extclk)),"invalid clock never reaches external clock");
    puts("PASS invalid clock serial");
#if defined(__ANDROID__)
    setup(&f,&s); seek_wait=1; wait_delay=4000000;
    f.render_wait_start=1; s.pause_req=1;
    sdl_audio_callback(&f,out,sizeof(out));
    check(s.audioq.serial==8,"seek serial changed during wait");
    check(isnan(get_clock(&s.audclk)),"manual-wait seek invalidates old audio");
    check(isnan(get_clock(&s.extclk)),"manual-wait seek cannot revive external clock");
    puts("PASS seek during manual-start wait");
#endif
    return 0;
}
'''
body = preamble + '\n' + '\n'.join(extract(n) for n in (
    'get_clock','set_clock_at','set_clock','sync_clock_to_slave','sdl_audio_callback')) + '\n' + main
with tempfile.TemporaryDirectory(prefix='ijk-audio-ready-') as tmp:
    c = Path(tmp)/'test.c'; c.write_text(body)
    for platform in ('android','other'):
        exe=Path(tmp)/platform
        flags=['-D__ANDROID__'] if platform=='android' else []
        cmd=[os.environ.get('CC','cc'),'-std=c11','-O3','-fno-fast-math','-Wall','-Wextra','-Werror']
        if os.environ.get('SANITIZE'): cmd+=['-fsanitize='+os.environ['SANITIZE'],'-fno-omit-frame-pointer']
        subprocess.run(cmd+flags+[str(c),'-lm','-o',str(exe)],check=True)
        print('Platform: '+platform,flush=True)
        subprocess.run([str(exe)],check=True)
