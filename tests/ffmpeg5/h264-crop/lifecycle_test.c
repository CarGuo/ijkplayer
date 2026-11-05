#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#define ALOGE(...) ((void)0)
#define ALOGI(...) ((void)0)
#define SDL_AMEDIA_OK 0
#define SDL_AMEDIA_ERROR_UNKNOWN -1
#define AVERROR_EXTERNAL -2
#define AV_NOPTS_VALUE INT64_MIN
#define JNI_OK 0
typedef int sdl_amedia_status_t;
typedef int jint;
typedef int64_t jlong;
typedef void *jobject;
typedef void *jstring;
typedef struct JNIAPI *JNIEnv;
struct JNIAPI {jobject(*NewGlobalRef)(JNIEnv*,jobject);void*(*GetDirectBufferAddress)(JNIEnv*,jobject);jlong(*GetDirectBufferCapacity)(JNIEnv*,jobject);jstring(*NewStringUTF)(JNIEnv*,const char*);};
typedef struct {jobject android_media_codec,output_buffer_info;bool release_attempted;} SDL_AMediaCodec_Opaque;
typedef struct {void *opaque_class;SDL_AMediaCodec_Opaque *opaque;bool started,configured;} SDL_AMediaCodec;
typedef struct {int unused;} SDL_AMediaFormat;
typedef struct IJKFF_Pipenode_Opaque {
    jobject jsurface;SDL_AMediaCodec *acodec;void *weak_vout,*input_aformat,*any_input_mutex;
    bool quirk_reconfigure_with_new_codec,h264_input_abandoned,hevc_boundary_pending,hevc_boundary_drained,input_eos_queued,output_eos_seen,acodec_first_dequeue_output_request;
    int hevc_drain_idle_since_us,input_packet_count,off_buf_out,enqueue_error;int64_t last_queued_pts;
} IJKFF_Pipenode_Opaque;
typedef struct {IJKFF_Pipenode_Opaque *opaque;} IJKFF_Pipenode;
static int g_amediacodec_class,fault,exception,stops,releases,creates,configures,starts,deleted,set_buffers;
static int buffer_fault;static uint8_t buffer[64];static SDL_AMediaCodec replacement;static SDL_AMediaCodec_Opaque replacement_o;
static jobject global_ref(JNIEnv *e,jobject o){(void)e;if(fault==1){exception=1;return NULL;}return o;}
static void *address(JNIEnv *e,jobject o){(void)e;(void)o;return buffer_fault==2?NULL:buffer;}
static jlong capacity(JNIEnv *e,jobject o){(void)e;(void)o;return buffer_fault==3?0:sizeof(buffer);}
static jstring key(JNIEnv *e,const char *s){(void)e;return buffer_fault==4?NULL:(void*)s;}
static struct JNIAPI api={global_ref,address,capacity,key};static JNIEnv envp=&api;
static int J4A_ExceptionCheck__catchAll(JNIEnv *e){(void)e;int x=exception;exception=0;return x;}
static void J4A_DeleteGlobalRef__p(JNIEnv *e,jobject *p){(void)e;*p=NULL;}
static void SDL_JNI_DeleteGlobalRefP(JNIEnv *e,jobject *p){J4A_DeleteGlobalRef__p(e,p);}
static void SDL_JNI_DeleteLocalRefP(JNIEnv *e,jobject *p){J4A_DeleteGlobalRef__p(e,p);}
static int SDL_JNI_SetupThreadEnv(JNIEnv **e){*e=&envp;return JNI_OK;}
static void J4AC_MediaCodec__release(JNIEnv *e,jobject o){(void)e;assert(o);++releases;if(fault==3)exception=1;}
static void J4AC_MediaCodec__release__catchAll(JNIEnv *e,jobject o){J4AC_MediaCodec__release(e,o);exception=0;}
static void SDL_AMediaCodec_FreeInternal(SDL_AMediaCodec *c){(void)c;++deleted;}
static jobject SDL_AMediaFormatJava_getObject(JNIEnv *e,SDL_AMediaFormat *f){(void)e;return f;}
static jobject J4AC_ByteBuffer__allocateDirect(JNIEnv *e,jint n){(void)e;assert(n<=64);return buffer_fault==1?NULL:buffer;}
static void J4AC_MediaFormat__setByteBuffer(JNIEnv *e,jobject f,jstring k,jobject b){(void)e;assert(f&&k&&b);++set_buffers;if(buffer_fault==5)exception=1;}
static bool SDL_AMediaCodec_isConfigured(SDL_AMediaCodec *c){assert(c);return c->configured;}
static bool SDL_AMediaCodec_isStarted(SDL_AMediaCodec *c){assert(c);return c->started;}
static int SDL_AMediaCodec_stop(SDL_AMediaCodec *c){assert(c);++stops;c->started=false;return fault==2?-1:0;}
static int SDL_AMediaCodec_start(SDL_AMediaCodec *c){assert(c);++starts;if(fault==6)return -1;c->started=true;return 0;}
static void SDL_VoutAndroid_invalidateAllBuffers(void *v){assert(v);}
static void SDL_VoutAndroid_setAMediaCodec(void *v,SDL_AMediaCodec *c){assert(v);(void)c;}
static void SDL_LockMutex(void *p){(void)p;}
static void SDL_UnlockMutex(void *p){(void)p;}
static SDL_AMediaCodec *create_codec_l(JNIEnv *e,IJKFF_Pipenode *n){(void)e;(void)n;++creates;if(fault==4)return NULL;replacement_o=(SDL_AMediaCodec_Opaque){.android_media_codec=(void*)2};replacement=(SDL_AMediaCodec){.opaque_class=&g_amediacodec_class,.opaque=&replacement_o};return &replacement;}
static int SDL_AMediaCodec_configure_surface(JNIEnv *e,SDL_AMediaCodec *c,void *f,jobject s,void *crypto,int flags){(void)e;(void)f;(void)s;(void)crypto;(void)flags;assert(c);++configures;if(fault==5)return -1;c->configured=true;return 0;}
static void SDL_AMediaCodec_decreaseReferenceP(SDL_AMediaCodec **c);
#include "lifecycle_functions.h"
static void SDL_AMediaCodec_decreaseReferenceP(SDL_AMediaCodec **c){if(!*c)return;if((*c)->started)SDL_AMediaCodec_stop(*c);SDL_AMediaCodecJava_delete(*c);*c=NULL;}
int main(void){
    for(fault=0;fault<=6;++fault){stops=releases=creates=configures=starts=deleted=exception=0;
        SDL_AMediaCodec_Opaque jo={.android_media_codec=(void*)1};SDL_AMediaCodec old={.opaque_class=&g_amediacodec_class,.opaque=&jo,.started=true,.configured=true};
        IJKFF_Pipenode_Opaque o={.jsurface=(void*)1,.acodec=&old,.weak_vout=(void*)1,.quirk_reconfigure_with_new_codec=true};IJKFF_Pipenode n={&o};
        int ret=reconfigure_codec_l_checked(&envp,&n,(void*)1,true);
        if(fault){assert(ret<0&&o.enqueue_error==ret);}else assert(!ret&&!o.enqueue_error&&o.acodec==&replacement);
        if(fault==2){assert(stops==1&&!releases&&!creates&&!configures&&!starts);}
        if(fault==3){assert(stops==1&&releases==1&&!creates&&!configures&&!starts&&jo.release_attempted);}
        if(fault==4){assert(!o.acodec&&!configures&&!starts);}
        // Same started-only cleanup condition as production crop path.
        if(o.acodec&&SDL_AMediaCodec_isStarted(o.acodec))SDL_AMediaCodec_stop(o.acodec);
        SDL_AMediaCodec_decreaseReferenceP(&o.acodec);
        if(fault==2)assert(stops==1);
        if(fault==3)assert(stops==1&&releases==1);
        printf("PASS extracted checked lifecycle fault=%d\n",fault);
    }
    fault=0;SDL_AMediaFormat format;uint8_t data[16];memset(data,7,sizeof(data));
    for(buffer_fault=0;buffer_fault<=5;++buffer_fault){set_buffers=0;int ret=SDL_AMediaFormatJava_setBufferChecked(&envp,&format,"csd-0",data,sizeof(data));if(buffer_fault)assert(ret<0);else assert(!ret&&set_buffers==1&&!memcmp(buffer,data,sizeof(data)));printf("PASS extracted checked setBuffer fault=%d\n",buffer_fault);}
    return 0;
}
