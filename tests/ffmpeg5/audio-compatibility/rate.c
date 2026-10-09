/* Executes the production JNI transaction with deterministic JNI method mocks. */
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include <limits.h>
#include "ff_soundtouch.h"
typedef float jfloat;typedef int jint;typedef void *jclass;typedef int jmethodID;
typedef struct Tuple {float speed,pitch;int fallback;} Tuple;
typedef Tuple *jobject;
typedef const struct JNITable *JNIEnv;
struct JNITable {jclass (*GetObjectClass)(JNIEnv*,jobject);jmethodID(*GetMethodID)(JNIEnv*,jclass,const char*,const char*);jfloat(*CallFloatMethod)(JNIEnv*,jobject,jmethodID);jint(*CallIntMethod)(JNIEnv*,jobject,jmethodID,...);jobject(*CallObjectMethod)(JNIEnv*,jobject,jmethodID,...);void(*DeleteLocalRef)(JNIEnv*,jclass);};
enum {SPEED=1,PITCH,FALLBACK,SET_PITCH,SET_FALLBACK,SET_RATE,GET_RATE};
enum {SUCCESS,REJECT_BEFORE,REJECT_AFTER,WRONG_SPEED,WRONG_PITCH,WRONG_FALLBACK,READ_FAIL,ROLLBACK_FAIL,ROLLBACK_READ_FAIL,INITIAL_READ_FAIL};
static int mode,exception,set_calls,get_calls,pool_count;
static Tuple track,pool[16];static int legacy_rate;
static jclass cls(JNIEnv*e,jobject o){return (jclass)o;}
static jmethodID method(JNIEnv*e,jclass c,const char*n,const char*s){if(!strcmp(n,"getPlaybackRate"))return GET_RATE;if(!strcmp(n,"setPlaybackRate"))return SET_RATE;if(!strcmp(n,"setPitch"))return SET_PITCH;if(!strcmp(n,"setAudioFallbackMode"))return SET_FALLBACK;if(!strcmp(n,"getSpeed"))return SPEED;if(!strcmp(n,"getPitch"))return PITCH;return FALLBACK;}
static jfloat getf(JNIEnv*e,jobject o,jmethodID m){return m==SPEED?o->speed:o->pitch;}
static jint geti(JNIEnv*e,jobject o,jmethodID m,...){
 if(m==GET_RATE){get_calls++;if((mode==READ_FAIL&&get_calls==2)||(mode==ROLLBACK_READ_FAIL&&get_calls==2)){exception=1;return 0;}return legacy_rate;}
 if(m==SET_RATE){va_list a;va_start(a,m);int n=va_arg(a,int);va_end(a);set_calls++;
  if(set_calls==1&&mode==REJECT_BEFORE)return -2;
  if(set_calls==2&&mode==ROLLBACK_FAIL){exception=1;return -1;}
  legacy_rate=n;
  if(set_calls==1&&(mode==REJECT_AFTER||mode==ROLLBACK_FAIL||mode==ROLLBACK_READ_FAIL)){exception=1;return -1;}
  if(set_calls==1&&mode==WRONG_SPEED)legacy_rate=24000;
  return 0;}
 return o->fallback;
}
static jobject call(JNIEnv*e,jobject o,jmethodID m,...){va_list a;va_start(a,m);if(m==SET_PITCH)o->pitch=(float)va_arg(a,double);else o->fallback=va_arg(a,int);va_end(a);return o;}
static void del(JNIEnv*e,jclass c){}
static int J4A_GetSystemAndroidApiLevel(JNIEnv*e){return 23;}
static int J4A_ExceptionCheck__catchAll(JNIEnv*e){int x=exception;exception=0;return x;}
static void J4A_DeleteLocalRef__p(JNIEnv*e,jobject*o){*o=NULL;}
static jobject J4AC_android_media_AudioTrack__getPlaybackParams(JNIEnv*e,jobject o){
 ++get_calls;if((mode==INITIAL_READ_FAIL&&get_calls==1)||(mode==READ_FAIL&&get_calls==2)||(mode==ROLLBACK_READ_FAIL&&get_calls==2)){exception=1;return NULL;}
 pool[pool_count]=track;return &pool[pool_count++];}
static jobject J4AC_android_media_PlaybackParams__setSpeed(JNIEnv*e,jobject o,float speed){o->speed=speed;return o;}
static void J4AC_android_media_AudioTrack__setPlaybackParams(JNIEnv*e,jobject o,jobject params){
 ++set_calls;
 if(set_calls==1&&mode==REJECT_BEFORE){exception=1;return;}
 if(set_calls==2&&mode==ROLLBACK_FAIL){exception=1;return;}
 track=*params;
 if(set_calls==1&&(mode==REJECT_AFTER||mode==ROLLBACK_FAIL||mode==ROLLBACK_READ_FAIL)){exception=1;return;}
 if(set_calls==1&&mode==WRONG_SPEED)track.speed=.75f;
 if(set_calls==1&&mode==WRONG_PITCH)track.pitch=2.f;
 if(set_calls==1&&mode==WRONG_FALLBACK)track.fallback=1;
}
#include "rate_extracted.h"
static void test_transaction(JNIEnv *env,int failure,float requested){
 mode=failure;exception=set_calls=get_calls=pool_count=0;track=(Tuple){1.25f,.9f,0};
 float speed=-1,pitch=-1;
 int result=J4AC_android_media_AudioTrack__setParamsChecked(env,NULL,requested,1,&speed,&pitch);
 if(failure==SUCCESS){assert(result==0&&track.speed==requested&&track.pitch==1&&track.fallback==2);assert(speed==requested&&pitch==1);}
 else if(failure==ROLLBACK_FAIL||failure==ROLLBACK_READ_FAIL||failure==INITIAL_READ_FAIL)assert(result<0);
 else{assert(result==1);assert(speed==1.25f&&pitch==.9f);assert(track.speed==1.25f&&track.pitch==.9f&&track.fallback==0);assert(set_calls==2);}
 assert(!exception);
}
int main(){const struct JNITable table={cls,method,getf,geti,call,del};JNIEnv e=&table;
 for(int m=SUCCESS;m<=INITIAL_READ_FAIL;m++)for(float r=2.5f;r<=3;r+=.5f)test_transaction(&e,m,r);
 puts("PASS actual JNI transaction: rejected 2.5x/3x, partial mutation, mismatched tuple, getter failure, verified rollback and fatal unverified state");
#if FFP_TEST_HAVE_LEGACY
 for(int m=SUCCESS;m<=ROLLBACK_READ_FAIL;m++) {
  if(m==WRONG_PITCH||m==WRONG_FALLBACK)continue;
  mode=m;set_calls=get_calls=exception=0;legacy_rate=60000;float speed=-1,pitch=-1;
  int r=J4AC_android_media_AudioTrack__setLegacyRateChecked(&e,&track,48000,3,0,&speed,&pitch);
  if(m==SUCCESS)assert(r==0&&legacy_rate==144000&&speed==3&&pitch==3);
  else if(m==ROLLBACK_FAIL||m==ROLLBACK_READ_FAIL)assert(r<0);
  else assert(r==1&&legacy_rate==60000&&speed==1.25f&&pitch==1.25f);
 }
 mode=SUCCESS;set_calls=get_calls=0;legacy_rate=144000;float speed,pitch;
 assert(!J4AC_android_media_AudioTrack__setLegacyRateChecked(&e,&track,48000,1,1,&speed,&pitch));
 assert(legacy_rate==48000&&speed==1&&pitch==1);
 puts("PASS API16-22 checked sample-rate control, rollback and verified 1x reset before software");
#endif
 test_rate_requests();
}
