#!/usr/bin/env python3
"""Exact-source/real dependency host regression; no Android/device qualification."""
from pathlib import Path
import argparse,json,subprocess,os,hashlib,re
p=argparse.ArgumentParser();p.add_argument('--soundtouch-root',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--only',choices=['ledger','rate','soundtouch']);p.add_argument('--source-revision',help='git revision for extracted C regressions (use --only rate for pre-ledger revisions)');a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[2];out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
def extract(s,name):
 start=s.index(name);start=s.rfind('\n',0,start)+1;br=s.index('{',start);d=1;i=br+1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
def read(rel):
 return subprocess.check_output(['git','show',a.source_revision+':'+rel],cwd=root,text=True) if a.source_revision else (root/rel).read_text()
s=read('ijkmedia/ijkplayer/ff_ffplay.c');defs=read('ijkmedia/ijkplayer/ff_ffplay_def.h')
clock=extract(defs,'typedef struct Clock')+' Clock;'
(out/'ledger_extracted.h').write_text('''typedef SDL_AudioPlaybackTracker SDL_Aout;
#define ALOGI(...) ((void)0)
static double av_gettime_relative(void){return 0;}
static void SDL_AoutGetPlaybackPosition(SDL_Aout *a,SDL_AudioPlaybackSnapshot *p){SDL_AudioPlaybackTracker_GetSnapshot(a,p);}
'''+clock+'\n'+extract(s,'static double get_clock('))
jni=read('ijkmedia/ijkj4a/j4au/class/android/media/AudioTrack.util.c')
rate_flags=re.search(r'enum \{ SDL_AUDIO_RATE_SPEED =.*?\};',(root/'ijkmedia/ijksdl/ijksdl_aout.h').read_text(),re.S).group()
requests=(here/'requests.h').read_text().replace('/* PRODUCTION_RATE_FLAGS */',rate_flags).replace('/* PRODUCTION_SETTER */',extract(s,'void ffp_set_playback_rate('))
(out/'rate_extracted.h').write_text(extract(jni,'int J4AC_android_media_AudioTrack__setParamsChecked(')+'\n'+('#define FFP_TEST_HAVE_LEGACY 1\n'+extract(jni,'int J4AC_android_media_AudioTrack__setLegacyRateChecked(') if 'int J4AC_android_media_AudioTrack__setLegacyRateChecked(' in jni else '')+'\n'+requests)
sources=['AAFilter','FIFOSampleBuffer','FIRFilter','cpu_detect_x86','sse_optimized','RateTransposer','InterpolateCubic','InterpolateLinear','InterpolateShannon','TDStretch','SoundTouch','mmx_optimized']
provenance=dict(source_revision=a.source_revision,extracted_sources={n:hashlib.sha256(read(n).encode()).hexdigest() for n in ['ijkmedia/ijkplayer/ff_ffplay.c','ijkmedia/ijkplayer/ff_ffplay_def.h','ijkmedia/ijkj4a/j4au/class/android/media/AudioTrack.util.c']},compiled_sources={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in ['ijkmedia/ijkplayer/ff_soundtouch.cpp','ijkmedia/ijkplayer/ff_soundtouch.h','ijkmedia/ijksdl/ijksdl_audio_playback.h']},soundtouch_sources={n:hashlib.sha256((a.soundtouch_root/'source/SoundTouch'/f'{n}.cpp').read_bytes()).hexdigest() for n in sources})
(out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
results=[]
for mode,flags in [('normal',['-O0']),('optimized',['-O3','-fno-fast-math']),('sanitizers',['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
 for test in ([a.only] if a.only else ['ledger','rate','soundtouch']):
  cmd=['c++' if test=='soundtouch' else 'cc','-g',*flags,'-I'+str(out),'-I'+str(root/'ijkmedia/ijksdl'),'-I'+str(root/'ijkmedia/ijkplayer')]
  if test!='soundtouch':cmd+=['-std=c11',str(here/f'{test}.c'),'-lm']
  else:cmd+=['-std=c++11','-DANDROID','-DSOUNDTOUCH_DISABLE_X86_OPTIMIZATIONS','-I'+str(a.soundtouch_root/'include'),str(here/'soundtouch.cpp'),str(root/'ijkmedia/ijkplayer/ff_soundtouch.cpp'),*[str(a.soundtouch_root/'source/SoundTouch'/f'{n}.cpp') for n in sources]]
  binary=out/f'{test}-{mode}';cmd+=['-o',str(binary)]
  b=subprocess.run(cmd,text=True,capture_output=True);(out/f'{test}-{mode}-build.log').write_text(b.stdout+b.stderr)
  if b.returncode:raise SystemExit(b.stderr)
  r=subprocess.run([str(binary)],text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'},timeout=60)
  (out/f'{test}-{mode}.log').write_text(r.stdout+r.stderr);print(test,mode,r.returncode,r.stdout,r.stderr)
  if r.returncode:raise SystemExit(r.returncode)
  results.append(dict(test=test,mode=mode,command=cmd,exit=r.returncode))
(out/'result.json').write_text(json.dumps(dict(results=results,provenance=provenance),indent=2)+'\n')
