#!/usr/bin/env python3
"""Low-resource bounded soak; live status and raw records survive interruption."""
import argparse
import json
import os
from pathlib import Path
import selectors
import subprocess
import time
from rtsp_fixture import RTSPFixture

ap=argparse.ArgumentParser()
ap.add_argument('--binary',required=True)
ap.add_argument('--seconds',type=int,default=1200)
ap.add_argument('--stalls',type=int,default=250)
ap.add_argument('--cap',type=int,default=1200)
ap.add_argument('--output',required=True)
a=ap.parse_args()
root=Path(a.output).resolve();root.mkdir(parents=True,exist_ok=True)
fixture=RTSPFixture(duration=a.seconds+15,video=Path(__file__).with_name('fixture.h264')).start()
log=(root/'stderr.log').open('w')
p=subprocess.Popen([str(Path(a.binary).resolve()),fixture.url,'tcp',str(a.cap),str(a.seconds),str(a.stalls)],stdout=subprocess.PIPE,stderr=log)
select=selectors.DefaultSelector();select.register(p.stdout,selectors.EVENT_READ)
started=time.monotonic();buf=b'';records=[];rss=[];last_status=0
raw=(root/'records.jsonl').open('w')
try:
 while p.poll() is None or select.get_map():
  now=time.monotonic()
  if now-started>a.seconds+25:
   p.kill();raise TimeoutError('bounded soak did not terminate')
  for key,_ in select.select(timeout=1):
   chunk=os.read(key.fd,65536)
   if not chunk:select.unregister(key.fileobj);continue
   buf+=chunk
   while b'\n' in buf:
    line,buf=buf.split(b'\n',1)
    if not line.startswith(b'{'):continue
    row=json.loads(line)
    if 'wall_us' in row and fixture.start_times:
     for track in ['audio','video']:
      row[track+'_age_ms']=(row['wall_us']/1e6-fixture.start_times[0]-row[track+'_pts'])*1000 if row[track+'_pts']>=0 else None
    records.append(row);raw.write(json.dumps(row)+'\n');raw.flush()
  if now-last_status>=5:
   last_status=now
   try:
    proc=Path(f'/proc/{p.pid}/status').read_text()
    memory={line.split(':',1)[0]:line.split(':',1)[1].strip() for line in proc.splitlines() if line.startswith(('VmRSS:','VmHWM:','Threads:'))}
   except FileNotFoundError:memory={}
   rss.append(dict(elapsed=round(now-started,2),**memory))
   status=dict(running=p.poll() is None,elapsed=round(now-started,2),requested_seconds=a.seconds,
               periodic_stalls=a.stalls,latest=records[-1] if records else None,memory=memory,
               scope='real IJK queues/filter and FFmpeg5 demux/decode; deterministic consumer, not Android rendering')
   (root/'status.json').write_text(json.dumps(status,indent=2))
 ret=p.wait(timeout=5)
 result=dict(returncode=ret,seconds=a.seconds,stalls=a.stalls,cap_ms=a.cap,
             summary=records[-1] if records else None,rss_samples=rss,
             rtsp_methods=[e['method'] for e in fixture.events],
             scope='real IJK queues/filter and FFmpeg5 demux/decode; deterministic consumer, not Android rendering')
 (root/'result.json').write_text(json.dumps(result,indent=2))
 (root/'status.json').write_text(json.dumps(dict(running=False,returncode=ret,elapsed=time.monotonic()-started,summary=result['summary']),indent=2))
 print(json.dumps(result['summary']),flush=True)
 if ret:raise SystemExit(ret)
finally:
 if p.poll() is None:p.kill();p.wait()
 fixture.close();raw.close();log.close()
