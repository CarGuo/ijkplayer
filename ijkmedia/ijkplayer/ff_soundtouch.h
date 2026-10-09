/* Bounded streaming adapter for the pinned LGPL SoundTouch dependency.
 * Frames always include all channels. Input and output never alias. */
#ifndef IJK_FF_SOUNDTOUCH_H
#define IJK_FF_SOUNDTOUCH_H
#ifdef __cplusplus
extern "C" {
#endif
#define FFP_SOUNDTOUCH_MIN_RATE 0.25
#define FFP_SOUNDTOUCH_MAX_RATE 4.0
int ffp_soundtouch_needs_reset(void *handle, int next_frames);
void *ffp_soundtouch_create(int sample_rate, int channels, double tempo);
void ffp_soundtouch_destroy(void *handle);
int ffp_soundtouch_put(void *handle, const short *pcm, int frames);
int ffp_soundtouch_receive(void *handle, short *pcm, int capacity_frames);
int ffp_soundtouch_flush(void *handle);
#ifdef __cplusplus
}
#endif
#endif
