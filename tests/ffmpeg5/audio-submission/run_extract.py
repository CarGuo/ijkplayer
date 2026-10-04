from pathlib import Path
import hashlib,json,subprocess,os,argparse,tempfile,shutil
script=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description='Host-only exact-source AudioTrack submission regression; no Android/device/backend access')
parser.add_argument('--source-root',type=Path,default=script.parents[2],help='IJK repository root (defaults to this repository)')
parser.add_argument('--out',type=Path,help='Retain extracted source, executables and logs in this separate output directory')
args=parser.parse_args()
_tmp=None
if args.out is None:
 _tmp=tempfile.TemporaryDirectory(prefix='ijk-audio-submit-')
 out=Path(_tmp.name)
else:
 out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
if out==script:raise SystemExit('--out must be separate from the tracked test directory')
src=args.source_root.resolve()/'ijkmedia'
shutil.copyfile(script/'test.c',out/'test.c')
def extract(path,name):
 text=(src/path).read_text();start=text.index(name);start=text.rfind('\n',0,start)+1;br=text.index('{',start);depth=0
 for i in range(br,len(text)):
  if text[i]=='{':depth+=1
  elif text[i]=='}':
   depth-=1
   if depth==0:return text[start:i+1]
 raise ValueError(name)
inputs=['ijkplayer/ff_ffplay.c','ijkplayer/ff_eof_clock.h','ijkplayer/ff_ffplay_def.h','ijksdl/ijksdl_audio.h','ijksdl/android/ijksdl_aout_android_audiotrack.c']
parts=[(src/'ijkplayer/ff_eof_clock.h').read_text()]
for path,names in [('ijkplayer/ff_ffplay.c',['static void sdl_audio_callback_internal(','static void sdl_audio_callback(','static void sdl_audio_submit_callback(','static int sdl_audio_submission_callback(']),('ijksdl/android/ijksdl_aout_android_audiotrack.c',['static int aout_write_submitted_buffer(','static int aout_thread_n(','static void aout_pause_audio('])]:
 for name in names:parts.append(extract(path,name))
(out/'extracted_eof.h').write_text(extract('ijkplayer/ff_ffplay_def.h','typedef struct EOFClockState')+' EOFClockState;\n')
audio=(src/'ijksdl/ijksdl_audio.h').read_text();start=audio.index('typedef struct SDL_AudioSubmission');end=audio.index('typedef struct SDL_AudioSpec');(out/'extracted_submission.h').write_text(audio[start:end])
(out/'extracted_logic.h').write_text('\n\n'.join(parts))
ff=(src/'ijkplayer/ff_ffplay.c').read_text();start=ff.index('        int audio_output_error = eof_clock_audio_output_error(is);');end=ff.index('#ifdef FFP_MERGE',start);error=ff[start:end]
start=ff.index('        if ((!is->paused || completed) &&');br=ff.index('{',start);depth=0
for i in range(br,len(ff)):
 if ff[i]=='{':depth+=1
 elif ff[i]=='}':
  depth-=1
  if depth==0:completion=ff[start:i+1];break
start=ff.index('    if (!ffp->prepared || !is->abort_request) {');end=ff.index('    SDL_DestroyMutex(wait_mutex);',start);fail=ff[start:end]
(out/'extracted_read_step.h').write_text('''/* Exact read_thread output-error dispatch, completion branch and fail notification;
 * one-step driver supplies locals and excludes demux/blocking background work. */
static void test_read_step(void) {
 FFPlayer *ffp=&player; VideoState *is=&state; int ret=0,last_error=0,completed=0;
 SDL_mutex local_wait={.rank=5};SDL_mutex *wait_mutex=&local_wait;
 for (;;) { if (is->abort_request) break;
'''+error+completion+'\n break; } return;\n fail:\n'+fail+'\n (void)ret; }\n')
(out/'provenance.json').write_text(json.dumps({'inputs':{str(src/p):hashlib.sha256((src/p).read_bytes()).hexdigest() for p in inputs},'extractedFunctions':['EOFClockState','entire ff_eof_clock.h','submission types','sdl_audio_callback_internal','sdl_audio_callback','sdl_audio_submit_callback','sdl_audio_submission_callback','aout_write_submitted_buffer','aout_thread_n','aout_pause_audio','read_thread error/completion/fail branches'],'method':'Exact source extraction; dependencies use deterministic fake clock/JNI/SDL, no Android or hardware'},indent=2)+'\n')
results={}
for name,flags in [('normal',['-O0']),('optimized',['-O3','-fno-fast-math']),('sanitizers',['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
 cmd=['cc','-std=c11','-g','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-parameter']+flags+[str(out/'test.c'),'-lm','-o',str(out/f'test-{name}')]
 r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/f'build-{name}.log').write_text(r.stdout+r.stderr)
 if r.returncode:raise SystemExit(r.stderr)
 env=os.environ.copy()
 if name=='sanitizers':env['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
 r=subprocess.run([str(out/f'test-{name}')],capture_output=True,text=True,env=env,timeout=15);(out/f'result-{name}.txt').write_text(r.stdout+r.stderr);print(name+':\n'+r.stdout+r.stderr);results[name]=r.returncode
 if r.returncode:raise SystemExit(r.returncode)
(out/'result.json').write_text(json.dumps(results,indent=2)+'\n')

print('PASS all current-production source regressions; ASan+UBSan, leak detection disabled because traced runners do not support LSan')
if args.out:print('Retained evidence:',out)
