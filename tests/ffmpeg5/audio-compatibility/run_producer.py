#!/usr/bin/env python3
"""Extract production audio producer; compile with real pinned SWR and SoundTouch."""
from pathlib import Path
import argparse,subprocess,os,json,hashlib,re
p=argparse.ArgumentParser();p.add_argument('--ffmpeg-source',type=Path,required=True);p.add_argument('--ffmpeg-build',type=Path,required=True);p.add_argument('--soundtouch-root',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[2];out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
s=(root/'ijkmedia/ijkplayer/ff_ffplay.c').read_text()
def extract(name):
 m=re.search(r'^static [^\n]*\b'+name+r'\([^;]*?\)\s*\{',s,re.M);assert m,name
 i=s.index('{',m.start())+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i]
functions=['audio_use_soft_clock','audio_transform_pending','audio_processing_failed','audio_output_frame','audio_receive_soundtouch','audio_decode_frame']
(out/'extracted_decode.h').write_text('\n\n'.join(extract(n) for n in functions))
sources=['AAFilter','FIFOSampleBuffer','FIRFilter','cpu_detect_x86','sse_optimized','RateTransposer','InterpolateCubic','InterpolateLinear','InterpolateShannon','TDStretch','SoundTouch','mmx_optimized']
inputs=[root/'ijkmedia/ijkplayer/ff_ffplay.c',root/'ijkmedia/ijkplayer/ff_soundtouch.cpp',root/'ijkmedia/ijkplayer/ff_soundtouch.h',here/'producer.c',*[a.soundtouch_root/'source/SoundTouch'/f'{n}.cpp' for n in sources]]
(out/'provenance.json').write_text(json.dumps(dict(functions=functions,inputs={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs},limits='Actual SWR and SoundTouch. Deterministic frame queue and SDL locks; no Android device/JNI or whole-player concurrency.'),indent=2)+'\n')
for mode,flags in [('normal',['-O0']),('optimized',['-O3','-fno-fast-math']),('sanitizers',['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
 obj=out/f'{mode}.o';binary=out/mode
 cmd=['cc','-std=c11','-g','-Wno-deprecated-declarations',*flags,'-I'+str(out),'-I'+str(root/'ijkmedia/ijkplayer'),'-I'+str(a.ffmpeg_build),'-I'+str(a.ffmpeg_source),'-c',str(here/'producer.c'),'-o',str(obj)]
 r=subprocess.run(cmd,text=True,capture_output=True);(out/f'{mode}-compile.log').write_text(r.stdout+r.stderr)
 if r.returncode:raise SystemExit(r.stderr)
 cmd=['c++','-std=c++11','-g',*flags,'-DANDROID','-DSOUNDTOUCH_DISABLE_X86_OPTIMIZATIONS','-I'+str(a.soundtouch_root/'include'),'-I'+str(root/'ijkmedia/ijkplayer'),str(obj),str(root/'ijkmedia/ijkplayer/ff_soundtouch.cpp'),*[str(a.soundtouch_root/'source/SoundTouch'/f'{n}.cpp') for n in sources],str(a.ffmpeg_build/'libswresample/libswresample.a'),str(a.ffmpeg_build/'libavutil/libavutil.a'),'-lm','-pthread','-o',str(binary)]
 r=subprocess.run(cmd,text=True,capture_output=True);(out/f'{mode}-link.log').write_text(r.stdout+r.stderr)
 if r.returncode:raise SystemExit(r.stderr)
 for case in ['', 'swr_endpoint','pts_discontinuity']:
  r=subprocess.run([str(binary),*([case] if case else [])],text=True,capture_output=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'},timeout=30)
  (out/f'{mode}-{case or "all"}.log').write_text(r.stdout+r.stderr);print(mode,case or 'all',r.returncode,r.stdout.splitlines()[-1:] or r.stderr)
  if r.returncode:raise SystemExit(r.returncode)
