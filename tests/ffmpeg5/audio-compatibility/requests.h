#define __ANDROID__ 1
#define AV_LOG_WARNING 0
#define AV_LOG_INFO 0
#define av_log(...) ((void)0)
/* PRODUCTION_RATE_FLAGS */
typedef struct {char *value;} AVDictionaryEntry;
typedef struct {int software_rate_mode,platform_rate_mode,requested_software_rate_mode;void *audio_st,*play_mutex;} VideoState;
typedef struct {VideoState *is;void *player_opts,*aout;int soundtouch_enable,pf_playback_rate_changed;float pf_playback_rate;} FFPlayer;
static void SDL_LockMutex(void*m){}
static void SDL_UnlockMutex(void*m){}
static AVDictionaryEntry option;static int requests,last_fields;static float last_requested;
static AVDictionaryEntry *av_dict_get(void *d,const char*k,void*p,int flags){return d?&option:NULL;}
static void SDL_AoutRequestPlatformRate(void*a,float r,int p,int f){requests++;last_fields=f;last_requested=r;}
/* PRODUCTION_SETTER */
static void test_rate_requests(void){
 FFPlayer f={.pf_playback_rate=1,.soundtouch_enable=1};
 const float bad[]={NAN,INFINITY,-INFINITY,0,-1,1e-30f,FLT_MAX,.24999f,4.00001f};
 for(int stage=0;stage<3;stage++){
  VideoState state={.software_rate_mode=1,.audio_st=&f};
  f.is=stage==1?&state:NULL;f.soundtouch_enable=stage==2?0:1;
  option.value="1";f.player_opts=stage==2?&option:NULL;
  for(int i=0;i<9;i++){f.pf_playback_rate=1;f.pf_playback_rate_changed=0;ffp_set_playback_rate(&f,bad[i]);assert(f.pf_playback_rate==1&&!f.pf_playback_rate_changed);}
  for(float r=.25f;r<=4;r+=.25f){ffp_set_playback_rate(&f,r);assert(f.pf_playback_rate==r&&f.pf_playback_rate_changed);}
 }
 VideoState state={.platform_rate_mode=1,.audio_st=&f};f.is=&state;f.soundtouch_enable=0;f.player_opts=NULL;
 requests=0;ffp_set_playback_rate(&f,3);assert(requests==1&&f.pf_playback_rate==3);
 ffp_set_playback_rate(&f,NAN);assert(requests==1&&f.pf_playback_rate==3);
 if (last_fields & SDL_AUDIO_RATE_LOGICAL_ONLY) abort();
 state.software_rate_mode=1;state.platform_rate_mode=0;
 ffp_set_playback_rate(&f,4);assert(f.pf_playback_rate==4&&last_requested==4);
 assert((last_fields & (SDL_AUDIO_RATE_LOGICAL_ONLY|SDL_AUDIO_RATE_ENGINE_WAKE)) == (SDL_AUDIO_RATE_LOGICAL_ONLY|SDL_AUDIO_RATE_ENGINE_WAKE));
 puts("PASS production rate setter: software boundaries/extremes before prepare and during playback; platform tuple remains runtime-checked");
}
