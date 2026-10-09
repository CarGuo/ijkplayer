#!/usr/bin/env python3
"""Exact production init/producer/callback/owner startup regression, local only."""
from pathlib import Path
import argparse, subprocess, os, json, hashlib, re, shutil
p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path);p.add_argument('--ffmpeg-source',type=Path,required=True);p.add_argument('--ffmpeg-build',type=Path,required=True);p.add_argument('--soundtouch-root',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--only',choices=['normal','optimized','sanitizers']);a=p.parse_args()
here=Path(__file__).resolve().parent;root=(a.source_root or here.parents[2]).resolve();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
ff=root/'ijkmedia/ijkplayer/ff_ffplay.c';owner=root/'ijkmedia/ijksdl/android/ijksdl_aout_android_audiotrack.c';s=ff.read_text();o=owner.read_text()
def extract(s,name):
 m=re.search(r'^(?:static [^\n]*|void )\b'+re.escape(name)+r'\([^;]*?\)\s*\{',s,re.M);assert m,name
 i=s.index('{',m.start())+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i]
inputs=[ff,owner,root/'ijkmedia/ijksdl/ijksdl_audio_playback.h',root/'ijkmedia/ijkplayer/ff_eof_clock.h',root/'ijkmedia/ijkplayer/ff_ffplay_def.h',root/'ijkmedia/ijksdl/ijksdl_audio.h',root/'ijkmedia/ijkplayer/ff_soundtouch.cpp',root/'ijkmedia/ijkplayer/ff_soundtouch.h',here/'startup.c']
for h,rel in [('ff_rate_clock.h','ijkplayer/ff_rate_clock.h'),('ijksdl_audio_playback.h','ijksdl/ijksdl_audio_playback.h')]:shutil.copyfile(root/'ijkmedia'/rel,out/h)
d=(root/'ijkmedia/ijkplayer/ff_ffplay_def.h').read_text();start=d.index('typedef struct EOFClockState');end=d.index('} EOFClockState;',start)+len('} EOFClockState;');(out/'extracted_eof.h').write_text(d[start:end])
d=(root/'ijkmedia/ijksdl/ijksdl_audio.h').read_text();(out/'extracted_submission.h').write_text(d[d.index('typedef struct SDL_AudioSubmission'):d.index('typedef struct SDL_AudioSpec')])
flags=re.search(r'enum \{ SDL_AUDIO_RATE_SPEED =.*?\};',(root/'ijkmedia/ijksdl/ijksdl_aout.h').read_text(),re.S).group()
producer=['audio_use_soft_clock','audio_transform_pending','audio_processing_failed','audio_output_frame','audio_receive_soundtouch','audio_decode_frame']
callbacks=['sdl_audio_callback_internal','sdl_audio_callback','sdl_audio_submit_callback','sdl_audio_submission_callback','sdl_audio_rate_applied']
owners=['aout_get_playback_position','aout_reset_playback','aout_bind_generation','aout_device_failed','aout_accept_prefix','aout_request_platform_rate','aout_apply_platform_rate','aout_apply_active_rate','aout_notify_platform_rate','aout_write_submitted_buffer','aout_pause_audio']
(out/'extracted_logic.h').write_text('\n\n'.join([flags,(root/'ijkmedia/ijkplayer/ff_eof_clock.h').read_text(),*[extract(s,n) for n in producer+callbacks],*[extract(o,n) for n in owners]]))
init=[]
for begin,end in [('    is->software_rate_mode = ffp->soundtouch_enable != 0;','    is->platform_rate_capable ='),('    is->audio_clock_serial = -1;','    if (ffp->startup_volume < 0)'),('    is->eof_clock.audio_finished_serial =','    ffp->is = is;')]:
 start=s.index(begin,s.index('static VideoState *stream_open('));finish=s.index(end,start);init.append(s[start:finish])
(out/'extracted_init.h').write_text('\n'.join(init))
controls=extract(s,'ffp_set_playback_rate')+'\n'+extract(s,'ffp_set_option_int')
(out/'extracted_controls.h').write_text(controls)
stsources=[a.soundtouch_root/'source/SoundTouch'/f'{n}.cpp' for n in ['AAFilter','FIFOSampleBuffer','FIRFilter','cpu_detect_x86','sse_optimized','RateTransposer','InterpolateCubic','InterpolateLinear','InterpolateShannon','TDStretch','SoundTouch','mmx_optimized']]
inputs+=stsources
inputs += [here/'run.py', root/'ijkmedia/ijksdl/ijksdl_aout.h',
           root/'ijkmedia/ijkplayer/ff_rate_clock.h',
           a.ffmpeg_build/'libswresample/libswresample.a',
           a.ffmpeg_build/'libavutil/libavutil.a']
(out/'provenance.json').write_text(json.dumps(dict(inputs={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs},functions=producer+callbacks+owners,initialization='Exact stream_open mode/rate/clock initialization blocks',limits='Real pinned SWR/SoundTouch. Deterministic queue/SDL/JNI model; not device or real-thread evidence.'),indent=2)+'\n')
results=[]
for mode,flags in [('normal',['-O0']),('optimized',['-O3','-fno-fast-math']),('sanitizers',['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
 if a.only and a.only!=mode:continue
 obj=out/f'{mode}.o';binary=out/mode
 feature_flags=['-DSTARTUP_HAVE_REAL_EPOCH_FLAG'] if 'int has_real_pcm;' in (root/'ijkmedia/ijksdl/ijksdl_audio_playback.h').read_text() else []
 cmds=[['cc','-std=c11','-g','-Wno-deprecated-declarations',*flags,*feature_flags,'-I'+str(out),'-I'+str(root/'ijkmedia/ijkplayer'),'-I'+str(a.ffmpeg_build),'-I'+str(a.ffmpeg_source),'-c',str(here/'startup.c'),'-o',str(obj)],['c++','-std=c++11','-g',*flags,'-DANDROID','-DSOUNDTOUCH_DISABLE_X86_OPTIMIZATIONS','-I'+str(a.soundtouch_root/'include'),'-I'+str(root/'ijkmedia/ijkplayer'),str(obj),str(root/'ijkmedia/ijkplayer/ff_soundtouch.cpp'),*[str(x) for x in stsources],str(a.ffmpeg_build/'libswresample/libswresample.a'),str(a.ffmpeg_build/'libavutil/libavutil.a'),'-lm','-pthread','-o',str(binary)]]
 for i,cmd in enumerate(cmds):
  r=subprocess.run(cmd,text=True,capture_output=True);(out/f'{mode}-build{i}.log').write_text(r.stdout+r.stderr)
  if r.returncode:raise SystemExit(r.stderr)
 r=subprocess.run([str(binary)],text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'},timeout=30)
 (out/f'{mode}.log').write_text(r.stdout+r.stderr);print(mode,r.returncode,r.stdout,r.stderr,flush=True);results.append(dict(mode=mode,exit=r.returncode,commands=cmds));(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
 if r.returncode:raise SystemExit(r.returncode)
