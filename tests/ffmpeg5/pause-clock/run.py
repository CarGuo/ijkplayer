from pathlib import Path
import hashlib,json,re,subprocess,os,argparse
HERE=Path(__file__).resolve().parent
BASE=HERE.parents[2]
p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=BASE);p.add_argument('--overlay-root',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
srcroot=a.source_root.resolve()
def source_path(rel):
 p=(a.overlay_root.resolve() if a.overlay_root else srcroot)/rel
 return p if p.is_file() else srcroot/rel
def source_text(rel):return source_path(rel).read_text()
src=source_text('ijkmedia/ijkplayer/ff_ffplay.c');defs=source_text('ijkmedia/ijkplayer/ff_ffplay_def.h');eof=source_text('ijkmedia/ijkplayer/ff_eof_clock.h');audio=source_text('ijkmedia/ijksdl/ijksdl_audio.h');mp=source_text('ijkmedia/ijkplayer/ijkplayer.c')

def extract(s,name):
 m=re.search(r'^(?:static )?[^\n]*\b'+name+r'\([^;]*?\)\s*\{',s,re.M);assert m,name
 i=s.index('{',m.start())+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i]
def decl(name):return re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',defs,re.S).group()
h=(HERE/'harness.h').read_text()
rate_flags=re.search(r'enum \{ SDL_AUDIO_RATE_SPEED =.*?\};',source_text('ijkmedia/ijksdl/ijksdl_aout.h'),re.S).group()
h=h.replace('/* PRODUCTION_DECLARATIONS */',rate_flags+'\n'+audio[audio.index('typedef struct SDL_AudioSubmission'):audio.index('typedef struct SDL_AudioSpec')]+decl('EOFClockState')+'\n'+decl('Clock'))
h=h.replace('    double audio_clock;','    double audio_clock;\n    SDL_mutex *play_mutex;\n    double frame_timer;\n    int step, buffering_on;\n    int64_t seek_pos;\n    struct FormatContext { int64_t start_time, duration; } format, *ic;\n    SDL_cond *continue_read_thread;')
h=h.replace('    void *aout;','    void *aout;\n    int no_time_adjust, auto_resume;')
h+='''\n#include <assert.h>
#include <errno.h>
#define AV_NOPTS_VALUE INT64_MIN
#define AV_TIME_BASE 1000000
#define EIJK_NULL_IS_PTR -1
#define AVERROR(x) (-(x))
static int64_t av_rescale(int64_t a,int64_t b,int64_t c){return (int64_t)roundl((long double)a*b/c);}
static void SDL_AoutPauseAudio(void *a,int paused){(void)a;(void)paused;}
'''
h+=re.search(r'^#define fftime_to_milliseconds.*$',defs,re.M).group()+'\n'
names=['frame_queue_nb_remaining','get_clock','set_clock_at','set_clock','set_clock_speed','init_clock','sync_clock_to_slave']
# Stateful dependency instrumentation verifies actual lock order and lets one
# worker ACK wait behind a control transition, without altering extracted code.
h=h.replace('static void SDL_LockMutex(SDL_mutex *m) { int r=pthread_mutex_lock(m); if(r) abort(); }',r'''static int core_fixture_mode,core_mutex_count;
static SDL_mutex *core_mutexes[5];
static _Thread_local SDL_mutex *held_locks[16];
static _Thread_local int held_count;
static atomic_int probe_lock_enabled,probe_lock_entered;
static int lock_rank(SDL_mutex*m){if(core_fixture_mode){for(int i=0;i<5;i++)if(core_mutexes[i]==m)return i+1;abort();}VideoState*s=active_player->is;if(m==s->play_mutex)return 1;if(m==s->sampq.mutex||m==s->pictq.mutex)return 2;if(m==s->audioq.mutex)return 3;if(m==s->videoq.mutex)return 4;if(m==s->eof_clock.mutex)return 5;abort();}
static void SDL_LockMutex(SDL_mutex *m) {
 if(atomic_load(&probe_lock_enabled) && m==active_player->is->play_mutex)atomic_store(&probe_lock_entered,1);
 if(held_count && lock_rank(m)<=lock_rank(held_locks[held_count-1]))abort();
 int r=pthread_mutex_lock(m);if(r)abort();held_locks[held_count++]=m;
}''')
h=h.replace('static void SDL_UnlockMutex(SDL_mutex *m) { int r=pthread_mutex_unlock(m); if(r) abort(); }','static void SDL_UnlockMutex(SDL_mutex *m) { if(!held_count || held_locks[held_count-1]!=m)abort();--held_count;int r=pthread_mutex_unlock(m);if(r)abort(); }')
h=h.replace('static double SDL_AoutGetLatencySeconds(void *a) { (void)a;', 'static double SDL_AoutGetLatencySeconds(void *a) { if(held_count)abort(); (void)a;')
for header,rel in [('ff_rate_clock.h','ijkmedia/ijkplayer/ff_rate_clock.h'),('ijksdl_audio_playback.h','ijkmedia/ijksdl/ijksdl_audio_playback.h')]:
 (a.out/header).write_text(source_text(rel))
h+='\n'.join(extract(src,n) for n in names)+'\n'+eof+'\n'
other=['video_update_platform_rate_l','get_default_master_sync_type','get_master_sync_type','get_master_clock','stream_toggle_pause_l','step_to_next_frame_l','stream_update_pause_l','toggle_pause_l','toggle_pause','ffp_start_l','ffp_pause_l','ffp_is_paused_l','ffp_get_current_position_l','sdl_audio_submission_callback']
h+='\n'.join(extract(src,n) for n in other)+'\n'
h+=(HERE/'setup.c').read_text()+'\n'
# Real public pause request function, with state validation/removal mocked and a
# single-thread event schedule choosing when the normal ffp_pause_l is dispatched.
h+='''\n#define FFP_REQ_START 200
#define FFP_REQ_PAUSE 201
#define MP_RET_IF_FAILED(x) do { int result=(x); if(result<0)return result; } while(0)
typedef struct {FFPlayer *ffplayer;int mp_state;} IjkMediaPlayer;
static int ikjmp_chkst_pause_l(int state){(void)state;return 0;}
static void ffp_remove_msg(FFPlayer *f,int what){(void)f;(void)what;}
'''+extract(mp,'ijkmp_pause_l')+'\n'
h+=r'''
typedef struct FormatContext AVFormatContext;
typedef struct {int unused;} AVStream;
static SDL_mutex *SDL_CreateMutex(void){SDL_mutex*m=new_mutex();if(core_fixture_mode){if(core_mutex_count>=5)abort();core_mutexes[core_mutex_count++]=m;}return m;}
static void SDL_DestroyMutex(SDL_mutex*m){if(pthread_mutex_destroy(m))abort();free(m);}
'''
h+=extract(source_text('tests/ffmpeg5/test_core.c'),'test_playback_clock')+'\n'
h+=(Path(__file__).parent/'cases.c').read_text();c=a.out/'extracted.c';c.write_text(h)
results=[]
for name,flags in [('normal',['-O1']),('optimized',['-O3','-fno-fast-math']),('sanitizers',['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
 binary=a.out/name;cmd=['cc','-std=c11','-g','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-variable','-Wno-sign-compare','-pthread',*flags,str(c),'-lm','-o',str(binary)]
 built=subprocess.run(cmd,text=True,capture_output=True);(a.out/('build-'+name+'.log')).write_text(built.stdout+built.stderr);assert built.returncode==0,built.stderr
 run=subprocess.run([str(binary)],text=True,capture_output=True,timeout=20,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'});(a.out/(name+'.txt')).write_text(run.stdout+run.stderr);print(name,run.returncode,run.stdout);assert run.returncode==0,run.stderr
 results.append(dict(mode=name,command=cmd,exit=run.returncode,output=run.stdout,compiler_stderr=built.stderr))
paths=['ijkmedia/ijksdl/ijksdl_aout.h','ijkmedia/ijkplayer/ff_rate_clock.h','ijkmedia/ijksdl/ijksdl_audio_playback.h','ijkmedia/ijkplayer/ff_ffplay.c','ijkmedia/ijkplayer/ff_ffplay_def.h','ijkmedia/ijkplayer/ff_eof_clock.h','ijkmedia/ijkplayer/ijkplayer.c','ijkmedia/ijksdl/ijksdl_audio.h','tests/ffmpeg5/test_core.c']
(a.out/'result.json').write_text(json.dumps(dict(source_root=str(srcroot),sources={str(source_path(n)):hashlib.sha256(source_path(n).read_bytes()).hexdigest() for n in paths},functions=names+other+['ijkmp_pause_l','test_playback_clock'],extraction_sha256=hashlib.sha256(c.read_bytes()).hexdigest(),results=results,limits='Exact extracted production functions; deterministic mock monotonic clock and AudioTrack latency/pause; real pthread core locks. Pause event dispatch schedule is selected explicitly. No JNI, Android, whole-player threading or audible output. Leak detection disabled.'),indent=2)+'\n')
