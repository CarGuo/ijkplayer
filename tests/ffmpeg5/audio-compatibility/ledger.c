/* Actual tracker and extracted production clock. No Java or device emulation. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include "ijksdl_audio_playback.h"
#include "ledger_extracted.h"
static void near(double a, double b) { assert(isfinite(a) && fabs(a-b)<1e-9); }
int main(void) {
 SDL_AudioPlaybackSpan spans[16]; SDL_AudioPlaybackTracker t;
 assert(!SDL_AudioPlaybackTracker_Init(&t,spans,16));
 assert(!SDL_AudioPlaybackTracker_Reset(&t,7,1));
 /* A positive short-write prefix, then its exact same-rate suffix. New desired
  * rates never rewrite the already accepted A samples. */
 assert(!SDL_AudioPlaybackTracker_AcceptEstimated(&t,2400,1,NAN,NAN,1./48000,1,0,.05));
 assert(!SDL_AudioPlaybackTracker_AcceptEstimated(&t,2400,1,NAN,NAN,1./48000,1,.05,.1));
 assert(!SDL_AudioPlaybackTracker_AcceptEstimated(&t,4800,1,NAN,NAN,3./48000,3,.1,.4));
 assert(!SDL_AudioPlaybackTracker_AcceptEstimated(&t,4800,1,NAN,NAN,1./48000,1,.4,.5));
 int serial=7; Clock c={.source_aout=&t,.source_soft=1,.serial=7,.queue_serial=&serial};
 const uint32_t heads[]={2400,4800,7200,9600,12000,14400};
 const double pts[]={.05,.1,.25,.4,.45,.5};
 const double rates[]={1,1,3,3,1,1};
 for (int i=0;i<6;i++) {
  assert(!SDL_AudioPlaybackTracker_Observe(&t,heads[i]));
  SDL_AudioPlaybackSnapshot p; SDL_AudioPlaybackTracker_GetSnapshot(&t,&p);
  assert(isnan(p.pts)); near(p.played_media,pts[i]); near(p.media_rate,rates[i]);
  near(get_clock(&c),pts[i]);
  if(i==0) { /* Mixed A+B backlog: .15 output sec but .35 media sec. */
   near(.4-(.4-p.played_media),.05);
   assert(fabs((.4-.15*3)-.05)>.09 && fabs((.4-.15)-.05)>.19);
   c.paused=1; c.pts=.05; near(get_clock(&c),.05); c.paused=0;
  }
 }
 /* Unknown absolute PTS stays unknown, despite known drain/rate coordinates. */
 assert(!SDL_AudioPlaybackTracker_AcceptEstimated(&t,480,1,NAN,NAN,1./48000,1,NAN,NAN));
 assert(!SDL_AudioPlaybackTracker_AcceptEstimated(&t,480,1,NAN,NAN,1./48000,1,10,10.01));
 near(get_clock(&c),.5); /* Queued unknown/gap cannot alter the current clock. */
 assert(!SDL_AudioPlaybackTracker_Observe(&t,14640));assert(isnan(get_clock(&c)));
 assert(!SDL_AudioPlaybackTracker_Observe(&t,15120));near(get_clock(&c),10.005);
 /* A successful seek resets both frame and progress coordinates; stale clock
  * must remain unavailable until the new generation has an acknowledged base. */
 serial=8; assert(isnan(get_clock(&c)));
 assert(!SDL_AudioPlaybackTracker_Reset(&t,8,2));
 assert(!SDL_AudioPlaybackTracker_AcceptEstimated(&t,480,1,NAN,NAN,3./48000,3,2,2.03));
 c.serial=8;
 assert(!SDL_AudioPlaybackTracker_Observe(&t,0)); near(get_clock(&c),2.0);
 assert(!SDL_AudioPlaybackTracker_Observe(&t,480));near(get_clock(&c),2.03);
 puts("PASS actual media ledger: 1x/3x/1x backlog, short prefix, pause, unknown PTS and seek");
}
