/* Ordinary applied-rate continuity helpers. No platform or SoundTouch code. */
#ifndef IJK_FF_RATE_CLOCK_H
#define IJK_FF_RATE_CLOCK_H
#include <math.h>
#include <stdint.h>
static inline double ffp_rate_anchor(double pts, double end, double updated,
                                     double old_rate, double boundary)
{
    if (!isfinite(pts) || !isfinite(end) || !isfinite(updated) ||
        !isfinite(old_rate) || old_rate <= 0 || !isfinite(boundary)) return pts;
    return fmin(pts + fmax(0.0, boundary - updated) * old_rate, end);
}
static inline int ffp_rate_fresh(uint64_t old_seq, int old_gen, uint64_t old_epoch,
                                  uint64_t seq, int gen, uint64_t epoch)
{
    return seq >= old_seq && (seq > old_seq || gen != old_gen || epoch != old_epoch);
}
static inline double ffp_rate_wall_duration(double media_duration, double rate)
{
    return media_duration / rate;
}
static inline double ffp_rate_timer_rebase(double timer, double old_rate,
                                          double new_rate, double boundary)
{
    if (!isfinite(timer) || !isfinite(boundary) || boundary < timer) return timer;
    return boundary - (boundary - timer) * old_rate / new_rate;
}
#endif
