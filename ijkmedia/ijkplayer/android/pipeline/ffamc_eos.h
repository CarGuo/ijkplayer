/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef IJKPLAYER_ANDROID_FFAMC_EOS_H
#define IJKPLAYER_ANDROID_FFAMC_EOS_H

/* Shared by both feed modes. Keep the null compressed packet pending until a
 * new serial arrives: it is both the EOF latch and the fallback replay guard. */
static int feed_codec_eos(IJKFF_Pipenode *node, int64_t timeout_us,
                          bool threaded, int *enqueue_count)
{
    IJKFF_Pipenode_Opaque *opaque = node->opaque;
    Decoder *d = opaque->decoder;
    ssize_t index;
    int ret = 0;
    if (opaque->input_eos_queued) {
        // Bound the wait so stop, seek and an output error can interrupt it.
        SDL_LockMutex(d->queue->mutex);
        if (!opaque->abort && !d->queue->abort_request &&
            d->pkt_serial == d->queue->serial)
            SDL_CondWaitTimeout(d->queue->cond, d->queue->mutex, 10);
        SDL_UnlockMutex(d->queue->mutex);
        return 0;
    }
    if (threaded) {
        // As with surface reconfigure, announce before acquiring this mutex so
        // a hot dequeue loop yields rather than starving EOS submission.
        opaque->acodec_reconfigure_request = true;
        SDL_LockMutex(opaque->acodec_mutex);
        opaque->acodec_reconfigure_request = false;
        SDL_CondSignal(opaque->acodec_cond);
    }
    if (opaque->abort || d->queue->abort_request ||
        d->pkt_serial != d->queue->serial || opaque->acodec_flush_request)
        goto done;
    index = SDL_AMediaCodec_dequeueInputBuffer(opaque->acodec, timeout_us);
    if (index == AMEDIACODEC__INFO_TRY_AGAIN_LATER)
        goto done;
    if (index < 0) {
        ret = AVERROR_EXTERNAL;
        goto done;
    }
    if (SDL_AMediaCodec_queueInputBuffer(opaque->acodec, index, 0, 0, 0,
            AMEDIACODEC__BUFFER_FLAG_END_OF_STREAM) != SDL_AMEDIA_OK) {
        ret = AVERROR_EXTERNAL;
        goto done;
    }
    opaque->input_eos_queued = true;
    ++opaque->input_packet_count;
    if (enqueue_count)
        ++*enqueue_count;
done:
    if (threaded)
        SDL_UnlockMutex(opaque->acodec_mutex);
    return ret;
}

/* Called under acodec_mutex (or in the single-threaded mode). Return one for
 * consumed metadata/empty EOS, zero for a picture, negative for codec failure. */
static int receive_codec_eos(IJKFF_Pipenode *node, int index,
                             const SDL_AMediaCodecBufferInfo *info)
{
    IJKFF_Pipenode_Opaque *opaque = node->opaque;
    if (info->flags & AMEDIACODEC__BUFFER_FLAG_END_OF_STREAM) {
        opaque->output_eos_seen = true;
        opaque->output_eos_serial = opaque->decoder->pkt_serial;
    }
    if ((info->flags & AMEDIACODEC__BUFFER_FLAG_CODEC_CONFIG) ||
        ((info->flags & AMEDIACODEC__BUFFER_FLAG_END_OF_STREAM) && !info->size)) {
        if (!(info->flags & AMEDIACODEC__BUFFER_FLAG_FAKE_FRAME) &&
            SDL_AMediaCodec_releaseOutputBuffer(opaque->acodec, index, false) != SDL_AMEDIA_OK)
            return AVERROR_EXTERNAL;
        return 1;
    }
    return 0;
}

/* Called on the iteration AFTER the EOS-bearing picture was queued or dropped.
 * Drain the decoder reorder slots too, then publish finished. ffplay additionally
 * waits for its picture queue to empty before sending playback completion. */
static int drain_codec_eos(IJKFF_Pipenode *node, AVFrame *frame, int *got_frame, bool threaded)
{
    IJKFF_Pipenode_Opaque *opaque = node->opaque;
    Decoder *d = opaque->decoder;
    if (d->queue->serial != opaque->output_eos_serial ||
        d->pkt_serial != opaque->output_eos_serial)
        return 0;
    if (opaque->off_buf_out > 0) {
        AMC_Buf_Out *buf = &opaque->amc_buf_out[--opaque->off_buf_out];
        int ret = amc_fill_frame(node, frame, got_frame, buf->port,
                                 buf->acodec_serial, &buf->info);
        return ret < 0 ? AVERROR_EXTERNAL : ret;
    }
    if (opaque->hevc_boundary_pending &&
        opaque->hevc_boundary_serial == opaque->output_eos_serial) {
        // Private generation drain is NOT stream completion. All EOS-bearing
        // pictures/reorder slots have now passed through the normal queue.
        opaque->hevc_boundary_drained = true;
    } else {
        d->finished = opaque->output_eos_serial;
    }
    // No Java dequeue after output EOS, but don't spin while paused at EOF.
    if (threaded) {
        // The caller holds acodec_mutex. Release it during the EOS wait so the
        // feeder can commit a drained generation, seek, or surface change.
        if (!opaque->abort && !d->queue->abort_request)
            SDL_CondWaitTimeout(opaque->acodec_cond, opaque->acodec_mutex, 10);
    } else {
        SDL_LockMutex(d->queue->mutex);
        if (!opaque->abort && !d->queue->abort_request &&
            d->queue->serial == opaque->output_eos_serial)
            SDL_CondWaitTimeout(d->queue->cond, d->queue->mutex, 10);
        SDL_UnlockMutex(d->queue->mutex);
    }
    return 0;
}
#endif
