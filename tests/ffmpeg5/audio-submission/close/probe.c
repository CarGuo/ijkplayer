/* Real pthread condition-wakeup smoke test around two exact production functions.
 * JNI and core submission policy are stubs; no Android/audio/device/network I/O.
 * This proves condition-wait cancellation, not interruption of a blocked JNI write. */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
typedef pthread_mutex_t SDL_mutex;
typedef pthread_cond_t SDL_cond;
typedef pthread_t SDL_Thread;
typedef void JNIEnv;
typedef int SDL_Android_AudioTrack;
typedef uint8_t Uint8;
#include "submission.h"
typedef struct {SDL_AudioSubmissionCallback submission;void*userdata;} SDL_AudioSpec;
typedef struct {SDL_cond*wakeup_cond;SDL_mutex*wakeup_mutex;SDL_AudioSpec spec;SDL_Android_AudioTrack*atrack;volatile bool abort_request,pause_on,need_flush;SDL_Thread*audio_tid;} SDL_Aout_Opaque;
typedef struct {SDL_Aout_Opaque*opaque;} SDL_Aout;
static pthread_mutex_t entered_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t entered_cond=PTHREAD_COND_INITIALIZER;
static int entered,wait_returned,result,cancelled,writes,failed;
static void SDL_LockMutex(SDL_mutex*m){assert(!pthread_mutex_lock(m));}
static void SDL_UnlockMutex(SDL_mutex*m){assert(!pthread_mutex_unlock(m));}
static void SDL_CondSignal(SDL_cond*c){assert(!pthread_cond_signal(c));}
static void SDL_CondWaitTimeout(SDL_cond*c,SDL_mutex*m,int ms){
 pthread_mutex_lock(&entered_mutex);entered=1;pthread_cond_signal(&entered_cond);pthread_mutex_unlock(&entered_mutex);
 struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_nsec+=ms*1000000L;if(t.tv_nsec>=1000000000L){t.tv_sec++;t.tv_nsec-=1000000000L;}
 pthread_cond_timedwait(c,m,&t);wait_returned++;
}
static void SDL_WaitThread(SDL_Thread*t,int*status){assert(!pthread_join(*t,NULL));}
static void SDL_Android_AudioTrack_play(JNIEnv*e,SDL_Android_AudioTrack*t){}
static void SDL_Android_AudioTrack_pause(JNIEnv*e,SDL_Android_AudioTrack*t){}
static void SDL_Android_AudioTrack_flush(JNIEnv*e,SDL_Android_AudioTrack*t){}
static int SDL_Android_AudioTrack_write(JNIEnv*e,SDL_Android_AudioTrack*t,uint8_t*d,int n){writes++;return -1;}
static int notify(void*u,const SDL_AudioSubmission*s,int action,int error){if(action==SDL_AUDIO_SUBMIT_FAIL){failed=1;return SDL_AUDIO_SUBMIT_DRAINED;}if(action==SDL_AUDIO_SUBMIT_CANCEL)cancelled++;if(action==SDL_AUDIO_SUBMIT_CHECK && failed)return SDL_AUDIO_SUBMIT_DRAINED;return SDL_AUDIO_SUBMIT_READY;}
#include "extracted.h"
static void*run(void*p){uint8_t buffer[256]={0};SDL_AudioSubmission s={0,1};int track_paused=0;result=aout_write_submitted_buffer(NULL,p,buffer,256,&s,&track_paused);return NULL;}
int main(void){
 for(int i=0;i<100;i++){
  pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER;pthread_cond_t c=PTHREAD_COND_INITIALIZER;pthread_t t;SDL_Android_AudioTrack track=0;
  SDL_Aout_Opaque o={.wakeup_cond=&c,.wakeup_mutex=&m,.spec={notify,NULL},.atrack=&track,.audio_tid=&t};SDL_Aout a={&o};
  entered=wait_returned=cancelled=writes=failed=0;result=9;
  assert(!pthread_create(&t,NULL,run,&a));
  pthread_mutex_lock(&entered_mutex);while(!entered)pthread_cond_wait(&entered_cond,&entered_mutex);pthread_mutex_unlock(&entered_mutex);
  aout_close_audio(&a);
  assert(result==-1 && cancelled==1 && writes==1 && wait_returned>=1 && o.audio_tid==NULL);
  assert(!pthread_mutex_destroy(&m));assert(!pthread_cond_destroy(&c));
 }
 puts("PASS 100 real-pthread quiet-idle close/wakeup/join iterations; one synthetic failed write, cancellation, no repeated writes; core/JNI mocked");
}
