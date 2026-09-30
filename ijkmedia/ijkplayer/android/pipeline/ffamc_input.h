/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef IJKPLAYER_ANDROID_FFAMC_INPUT_H
#define IJKPLAYER_ANDROID_FFAMC_INPUT_H

/* Included after the pipeline's private state, so the same input state machine
 * is used by both Android feed modes and exercised by the host regressions. */
/* The compressed packet in Decoder remains untouched for software fallback.
 * pkt_temp is only a cursor into filtered_packet, never a second owner. Both
 * MediaCodec feeding modes share the same send/drain and seek lifecycle. */
static int prepare_input_packet(IJKFF_Pipenode *node, bool threaded)
{
    IJKFF_Pipenode_Opaque *opaque = node->opaque;
    FFPlayer *ffp = opaque->ffp;
    Decoder *d = opaque->decoder;
    int ret;

    if (d->packet_pending && d->queue->serial == d->pkt_serial)
        return 0;
    av_packet_unref(&opaque->filtered_packet);
    memset(&d->pkt_temp, 0, sizeof(d->pkt_temp));
    if (d->queue->serial != d->pkt_serial) {
        if (opaque->bsfc)
            av_bsf_flush(opaque->bsfc);
        opaque->bsf_pending = false;
        d->packet_pending = 0;
    }

    for (;;) {
        AVPacket pkt = {0};
        if (opaque->bsf_pending) {
            ret = av_bsf_receive_packet(opaque->bsfc, &opaque->filtered_packet);
            if (ret >= 0) {
                d->pkt_temp = opaque->filtered_packet;
                d->packet_pending = 1;
                return 0;
            }
            opaque->bsf_pending = false;
            if (ret == AVERROR_EOF) {
                // Only report completion after all buffered filter output.
                d->pkt_temp = (AVPacket){ .pts = AV_NOPTS_VALUE, .dts = AV_NOPTS_VALUE };
                d->packet_pending = 1;
                return 0;
            }
            if (ret == AVERROR_INVALIDDATA) {
                // A damaged access unit must not disable an otherwise working
                // hardware decoder. The Annex B filters consumed this packet;
                // continue at the next packet/keyframe, as the old feeder did.
                ALOGW("MediaCodec: dropping malformed access unit\n");
                av_packet_unref(&opaque->filtered_packet);
                av_packet_unref(&d->pkt);
                d->packet_pending = 0;
                continue;
            }
            if (ret != AVERROR(EAGAIN))
                return ret;
        }

        if (d->queue->nb_packets == 0)
            SDL_CondSignal(d->empty_queue_cond);
        ret = ijk_amc_get_packet(ffp, d, &opaque->abort, &pkt);
        if (ret < 0)
            return ret;
        if (ffp_is_flush_packet(&pkt) || opaque->acodec_flush_request) {
            opaque->acodec_flush_request = true;
            if (threaded)
                SDL_LockMutex(opaque->acodec_mutex);
            if (SDL_AMediaCodec_isStarted(opaque->acodec) && opaque->input_packet_count > 0) {
                // Flushing an empty codec fails on OMX.SEC.AVC.Decoder.
                SDL_VoutAndroid_invalidateAllBuffers(opaque->weak_vout);
                SDL_AMediaCodec_flush(opaque->acodec);
                opaque->input_packet_count = 0;
            }
            if (opaque->bsfc)
                av_bsf_flush(opaque->bsfc);
            opaque->bsf_pending = false;
            opaque->acodec_flush_request = false;
            if (threaded) {
                SDL_CondSignal(opaque->acodec_cond);
                SDL_UnlockMutex(opaque->acodec_mutex);
            }
            d->finished = 0;
            d->next_pts = d->start_pts;
            d->next_pts_tb = d->start_pts_tb;
        }
        if (ffp_is_flush_packet(&pkt) || d->queue->serial != d->pkt_serial) {
            av_packet_unref(&pkt);
            continue;
        }

        av_packet_unref(&d->pkt);
        av_packet_move_ref(&d->pkt, &pkt);
        // Leave the original available even if probing or filtering fails.
        d->packet_pending = 1;
        if (ffp->mediacodec_handle_resolution_change &&
            opaque->codecpar->codec_id == AV_CODEC_ID_H264) {
            ret = ijk_amc_update_h264_extradata(opaque->codecpar, &d->pkt);
            if (ret < 0)
                return ret;
            if (ret > 0) {
                ALOGW("AV_PKT_DATA_NEW_EXTRADATA: %d x %d\n",
                      opaque->codecpar->width, opaque->codecpar->height);
                // Refresh the filter before converting this access unit. The
                // format recreated below will use its updated Annex B CSD.
                ret = ijk_amc_create_bsf(&opaque->bsfc, opaque->codecpar,
                                         ffp->is->video_st->time_base);
                if (ret < 0)
                    return ret;
                opaque->aformat_need_recreate = true;
                ffpipeline_set_surface_need_reconfigure_l(opaque->pipeline, true);
            }
        }

        if (opaque->bsfc) {
            // send_packet consumes its argument, so send a reference rather
            // than Decoder's packet. Drain fully before submitting more input.
            // Referencing a null packet allocates non-NULL data at size zero,
            // which would turn the queue's EOF marker into a malformed AU.
            if (!d->pkt.data && !d->pkt.side_data_elems) {
                ret = av_bsf_send_packet(opaque->bsfc, NULL);
            } else {
                ret = av_packet_ref(&pkt, &d->pkt);
                if (ret < 0)
                    return ret;
                ret = av_bsf_send_packet(opaque->bsfc, &pkt);
            }
            av_packet_unref(&pkt);
            if (ret < 0)
                return ret;
            opaque->bsf_pending = true;
        } else {
            if (!d->pkt.data && !d->pkt.side_data_elems) {
                d->pkt_temp = d->pkt;
                return 0;
            }
            ret = av_packet_ref(&opaque->filtered_packet, &d->pkt);
            if (ret < 0)
                return ret;
            d->pkt_temp = opaque->filtered_packet;
            return 0;
        }
    }
}

#endif
