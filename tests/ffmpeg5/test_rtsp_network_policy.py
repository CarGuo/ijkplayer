#!/usr/bin/env python3
"""Compile the production IJK read/error block with deterministic platform stubs.
This is a C control-flow regression, not an Android/player-device execution.
"""
import argparse, re, subprocess, tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('--ffmpeg-source',required=True);p.add_argument('--ffmpeg-build',required=True);p.add_argument('--baseline',action='store_true');a=p.parse_args()
s=Path(a.source).read_text(); patched='static int stream_is_rtsp_io_error' in s

def func(name):
    m=re.search(r'^static [^\n]*\b'+name+r'\([^\n]*\)\n\{',s,re.M);assert m,name
    end=s.index('{',m.start()); depth=1;end+=1
    while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[m.start():end]
start=s.index('        pkt->flags = 0;\n        ret = av_read_frame(ic, pkt);')
if patched:start=s.rfind('        /* Let the existing audio/video tail drain',0,start)
end=s.index('        if (pkt->flags & AV_PKT_FLAG_DISCONTINUITY)',start)
block=s[start:end]
seek_start=s.index('            is->eof = ',s.index('            is->seek_req = 0;'))
seek_end=s.index('            eof_clock_seek(is, 0);',seek_start)
seek=s[seek_start:seek_end]
notify_start=s.index('                    completed = 1;')
notify_end=s.index('\n                }',notify_start)
notify=s[notify_start:notify_end]
preamble=r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "libavformat/avformat.h"
#include "libavutil/error.h"
typedef struct { int count; } Queue;
typedef struct { int eof, video_stream, audio_stream, subtitle_stream, pause_req; Queue videoq,audioq,subtitleq; void *continue_read_thread; } VideoState;
typedef struct { VideoState *is; int error, auto_resume; } FFPlayer;
static int next_read, read_calls, wait_calls, error_msgs, completed_msgs, error_code;
static int test_read(AVFormatContext *c, AVPacket *p) {(void)c;(void)p;++read_calls;return next_read;}
/* Wrapper contract is covered by test_ijklivehook.c, not this direct-RTSP fixture. */
static int ijklivehook_is_terminal_error(const AVFormatContext*c,int e){return !strcmp(c->iformat->name,"ijklivehook") && e==AVERROR(ENOSYS);}
static int test_feof(AVIOContext *p) {return p ? p->eof_reached : 0;}
static void packet_queue_put_nullpacket(Queue *q,int i){(void)i;q->count++;}
static void eof_clock_demux(VideoState*s,int e){(void)s;(void)e;}
static void ffp_check_buffering_l(FFPlayer*f){(void)f;}
static void ffp_toggle_buffering(FFPlayer*f,int b){(void)f;(void)b;}
static void toggle_pause(FFPlayer*f,int b){f->is->pause_req=b;}
static void SDL_Delay(int ms){(void)ms;}
static void SDL_LockMutex(void*m){(void)m;}
static void SDL_UnlockMutex(void*m){(void)m;}
static void SDL_CondWaitTimeout(void*c,void*m,int ms){(void)c;(void)m;(void)ms;++wait_calls;}
static void ffp_statistic_l(FFPlayer*f){(void)f;}
#define FFP_MSG_ERROR 1
#define FFP_MSG_COMPLETED 2
static void ffp_notify_msg1(FFPlayer*f,int msg){(void)f;if(msg==1)++error_msgs;else ++completed_msgs;}
static void ffp_notify_msg2(FFPlayer*f,int msg,int code){ffp_notify_msg1(f,msg);error_code=code;}
#define av_read_frame test_read
#define avio_feof test_feof
#define av_log(...) ((void)0)
'''
body=preamble+(func('stream_is_rtsp_io_error') if patched else '')+'''
static void step(FFPlayer *ffp, AVFormatContext *ic, int *latch) {
    VideoState *is=ffp->is; AVPacket packet={0}, *pkt=&packet;
    int ret, rtsp_io_error=*latch; void *wait_mutex=NULL;
    for(int iteration=0;iteration<1;iteration++) {
'''+block+'''
    }
    *latch=rtsp_io_error;
}
static void seek_result(FFPlayer*ffp,int*latch,int ret) {
    VideoState*is=ffp->is;int rtsp_io_error=*latch;
'''+seek+'''
    *latch=rtsp_io_error;
}
static void drained_notification(FFPlayer*ffp) {
    int completed=0; int rtsp_io_error=ffp->error;
'''+notify+'''
    assert(completed);
}
int main(void) {
    AVInputFormat fmt={.name="rtsp"};AVFormatContext ic={.iformat=&fmt};
    VideoState is={.video_stream=0,.audio_stream=1,.subtitle_stream=-1};FFPlayer f={.is=&is};int latch=0;
    next_read=0;step(&f,&ic,&latch);assert(!latch&&!is.eof);
    next_read=AVERROR(ETIMEDOUT);step(&f,&ic,&latch);
'''
if a.baseline:
    body+='''    assert(!latch&&!is.eof&&f.error==0);puts("REPRODUCED baseline: RTSP ETIMEDOUT with pb=NULL is discarded, no terminal state");return 0; }'''
else:
    body+='''    assert(latch==AVERROR(ETIMEDOUT)&&f.error==latch&&is.eof);
    assert(is.audioq.count==1&&is.videoq.count==1);
    int calls=read_calls;next_read=AVERROR_EOF;
    for(int i=0;i<10;i++)step(&f,&ic,&latch);
    assert(read_calls==calls&&f.error==AVERROR(ETIMEDOUT));
    assert(is.audioq.count==1&&is.videoq.count==1);
    drained_notification(&f);assert(error_msgs==1&&completed_msgs==0&&error_code==AVERROR(ETIMEDOUT));
    seek_result(&f,&latch,AVERROR(EIO));assert(latch&&is.eof&&f.error);
    seek_result(&f,&latch,0);assert(!latch&&!is.eof&&!f.error);
    next_read=0;step(&f,&ic,&latch);assert(read_calls==calls+1);
    next_read=AVERROR(EAGAIN);step(&f,&ic,&latch);assert(!latch&&!is.eof);
    next_read=AVERROR_INVALIDDATA;step(&f,&ic,&latch);assert(!latch&&!is.eof);
    fmt.name="mov";next_read=AVERROR(ETIMEDOUT);step(&f,&ic,&latch);assert(!latch);
    fmt.name="ijklivehook";step(&f,&ic,&latch);assert(!latch);
    next_read=AVERROR(ENOSYS);step(&f,&ic,&latch);assert(latch==AVERROR(ENOSYS));
    calls=read_calls;step(&f,&ic,&latch);assert(read_calls==calls);
    puts("PASS exact IJK error block: one drain, preserved timeout, no rereads, one error after tail, failed/successful seek, EAGAIN/INVALIDDATA and non-RTSP controls");return 0; }
'''
with tempfile.TemporaryDirectory() as td:
    c=Path(td)/'test.c';binary=Path(td)/'test';c.write_text(body)
    subprocess.run(['cc','-std=gnu11','-O2','-Wall','-Wextra','-Wno-unused-parameter','-Wno-unused-function','-I'+a.ffmpeg_source,'-I'+a.ffmpeg_build,str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
if patched:
    for url in ('is->filename','file_name'):
        assert f'if (av_stristart({url}, "rtmp", NULL))' in s
    assert 'rtsp_io_error ? rtsp_io_error : AVERROR_EOF' in s
    assert '!rtsp_io_error && ffp->loop != 1' in s
    print('PASS RTSP timeout retention and terminal-error auto-loop/auto-exit guards')
