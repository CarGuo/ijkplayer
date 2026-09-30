/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef IJKPLAYER_ANDROID_FFAMC_PACKET_QUEUE_H
#define IJKPLAYER_ANDROID_FFAMC_PACKET_QUEUE_H

/* Requires the player's Decoder/PacketQueue and SDL declarations. */
/* Unlike the generic blocking queue reader, this reader can stop just the
 * hardware feeder without aborting the queue needed by software fallback. */
static int ijk_amc_get_packet(FFPlayer *ffp, Decoder *d, volatile bool *abort, AVPacket *pkt)
{
    PacketQueue *q = d->queue;

    for (;;) {
        int ret;
        if (*abort || q->abort_request)
            return AVERROR_EXIT;
        ret = ffp_packet_queue_get(q, pkt, 0, &d->pkt_serial);
        if (ret < 0)
            return ret;
        if (!ret) {
            SDL_CondSignal(d->empty_queue_cond);
            if (ffp->packet_buffering && q->is_buffer_indicator && !d->finished)
                ffp_toggle_buffering(ffp, 1);
            SDL_LockMutex(q->mutex);
            if (!q->nb_packets && !q->abort_request && !*abort)
                SDL_CondWaitTimeout(q->cond, q->mutex, 100);
            SDL_UnlockMutex(q->mutex);
            continue;
        }
        if (ffp->packet_buffering && d->finished == d->pkt_serial) {
            av_packet_unref(pkt);
            continue;
        }
        return ret;
    }
}

#endif
