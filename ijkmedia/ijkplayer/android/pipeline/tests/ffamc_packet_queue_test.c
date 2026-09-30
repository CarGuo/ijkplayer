/* Exercise the exact hardware-only queue reader against a bounded mock queue. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "libavformat/avformat.h"
#include "libavutil/error.h"

typedef pthread_mutex_t SDL_mutex;
typedef pthread_cond_t SDL_cond;
typedef struct PacketQueue {
    SDL_mutex *mutex;
    SDL_cond *cond;
    int abort_request, nb_packets, is_buffer_indicator, waits, serial;
    int serials[8];
    AVPacket packets[8];
} PacketQueue;
typedef struct Decoder {
    PacketQueue *queue;
    int pkt_serial, finished;
    SDL_cond *empty_queue_cond;
    AVPacket pkt, pkt_temp;
    int packet_pending;
    int64_t next_pts, start_pts;
    AVRational next_pts_tb, start_pts_tb;
} Decoder;
typedef struct VideoState { AVStream *video_st; } VideoState;
typedef struct FFPlayer {
    int packet_buffering, buffering_calls, mediacodec_handle_resolution_change;
    VideoState *is;
} FFPlayer;
static int SDL_LockMutex(SDL_mutex *m) { return pthread_mutex_lock(m); }
static int SDL_UnlockMutex(SDL_mutex *m) { return pthread_mutex_unlock(m); }
static int SDL_CondSignal(SDL_cond *c) { return pthread_cond_signal(c); }
static int SDL_CondWaitTimeout(SDL_cond *c, SDL_mutex *m, int ms)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_nsec += (long)ms * 1000000;
    t.tv_sec += t.tv_nsec / 1000000000;
    t.tv_nsec %= 1000000000;
    return pthread_cond_timedwait(c, m, &t);
}
static void ffp_toggle_buffering(FFPlayer *ffp, int on)
{
    assert(on);
    ++ffp->buffering_calls;
}
static int ffp_packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial)
{
    int ret = 0;
    assert(!block);
    pthread_mutex_lock(q->mutex);
    ++q->waits;
    pthread_cond_signal(q->cond);
    if (q->abort_request)
        ret = -1;
    else if (q->nb_packets) {
        av_packet_move_ref(pkt, &q->packets[0]);
        *serial = q->serials[0];
        for (int i = 1; i < q->nb_packets; ++i) {
            av_packet_move_ref(&q->packets[i - 1], &q->packets[i]);
            q->serials[i - 1] = q->serials[i];
        }
        --q->nb_packets;
        ret = 1;
    }
    pthread_mutex_unlock(q->mutex);
    return ret;
}
#include "../ffamc_packet_queue.h"

typedef struct Test {
    SDL_mutex mutex;
    SDL_cond cond;
    PacketQueue queue;
    Decoder decoder;
    FFPlayer player;
    volatile bool abort;
    int result;
    AVPacket pkt;
} Test;
static void init(Test *t)
{
    *t = (Test){0};
    pthread_mutex_init(&t->mutex, NULL);
    pthread_cond_init(&t->cond, NULL);
    t->queue.mutex = &t->mutex;
    t->queue.cond = &t->cond;
    t->queue.is_buffer_indicator = 1;
    t->decoder.queue = &t->queue;
    t->decoder.empty_queue_cond = &t->cond;
    t->player.packet_buffering = 1;
}
static void destroy(Test *t)
{
    av_packet_unref(&t->pkt);
    av_packet_unref(&t->decoder.pkt);
    for (int i = 0; i < t->queue.nb_packets; ++i)
        av_packet_unref(&t->queue.packets[i]);
    pthread_cond_destroy(&t->cond);
    pthread_mutex_destroy(&t->mutex);
}
static void enqueue(Test *t, int pts, int serial)
{
    pthread_mutex_lock(&t->mutex);
    AVPacket *p = &t->queue.packets[t->queue.nb_packets];
    assert(av_new_packet(p, 4) == 0);
    p->pts = pts;
    t->queue.serials[t->queue.nb_packets++] = serial;
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->mutex);
}
static void *reader(void *arg)
{
    Test *t = arg;
    t->result = ijk_amc_get_packet(&t->player, &t->decoder, &t->abort, &t->pkt);
    return NULL;
}
static int64_t milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * INT64_C(1000) + t.tv_nsec / 1000000;
}
static void test_abort(int abort_queue)
{
    Test t;
    pthread_t thread;
    init(&t);
    pthread_create(&thread, NULL, reader, &t);
    pthread_mutex_lock(&t.mutex);
    while (!t.queue.waits)
        pthread_cond_wait(&t.cond, &t.mutex);
    if (abort_queue)
        t.queue.abort_request = 1;
    else
        t.abort = true;
    /* Deliberately do not signal: the timed wait must allow hardware shutdown. */
    pthread_mutex_unlock(&t.mutex);
    int64_t before = milliseconds();
    pthread_join(thread, NULL);
    assert(milliseconds() - before < 1000);
    assert(t.result == AVERROR_EXIT);
    assert(t.queue.abort_request == abort_queue);
    if (!abort_queue) {
        // The same un-aborted queue remains usable by the software decoder.
        enqueue(&t, 99, 2);
        assert(ffp_packet_queue_get(&t.queue, &t.pkt, 0, &t.decoder.pkt_serial) == 1);
        assert(t.pkt.pts == 99);
    }
    destroy(&t);
}
#ifndef FFAMC_QUEUE_TEST_LIBRARY
int main(void)
{
    Test t;
    init(&t);
    for (int i = 1; i <= 4; ++i)
        enqueue(&t, i * 10, 1);
    for (int i = 1; i <= 4; ++i) {
        assert(ijk_amc_get_packet(&t.player, &t.decoder, &t.abort, &t.pkt) == 1);
        assert(t.pkt.pts == i * 10 && t.decoder.pkt_serial == 1);
        av_packet_unref(&t.pkt);
    }
    assert(!t.player.buffering_calls);
    t.decoder.finished = 1;
    enqueue(&t, 50, 1); // Suppress packets from a completed serial, as core does.
    enqueue(&t, 60, 2); // Preserve a new seek's packet and serial.
    assert(ijk_amc_get_packet(&t.player, &t.decoder, &t.abort, &t.pkt) == 1);
    assert(t.pkt.pts == 60 && t.decoder.pkt_serial == 2);
    destroy(&t);
    test_abort(0);
    test_abort(1);
    puts("PASS packet ordering, serial transition, hardware-only interruption, queue abort and software reuse");
    return 0;
}

#endif
