/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef IJKPLAYER_ANDROID_FFAMC_GENERATION_H
#define IJKPLAYER_ANDROID_FFAMC_GENERATION_H
#include "ffamc_hevc_boundary.h"

/* A new hvcC description must not enter MediaCodec while earlier outputs are
 * retained for presentation. Some OMX implementations time out output-port
 * disable after three seconds, including legitimate pauses/low-frame-rate
 * queues. Drain privately, preserve every old picture, then recreate. */
static void cancel_hevc_generation_l(IJKFF_Pipenode_Opaque *opaque)
{
    opaque->h264_crop_replay_pending = false;
    opaque->h264_baseline_restore_pending = false;
    opaque->hevc_boundary_pending = false;
    opaque->hevc_boundary_drained = false;
    opaque->hevc_drain_idle_since_us = 0;
    opaque->hevc_packet_checked = false;
    av_bsf_free(&opaque->h264_next_bsfc);
    avcodec_parameters_free(&opaque->hevc_next_codecpar);
    avcodec_parameters_free(&opaque->hevc_next_csd);
}

/* A real flush starts a baseline epoch, not a guessed seek-target epoch.
 * Initial container parameters remain authoritative only for baseline behavior;
 * a later headerless B IDR is deliberately not claimed to identify B.
 * Stage before filtering the first new-serial AU, commit via the existing
 * no-old-input generation path, then inspect that same source AU normally. */
static int stage_h264_baseline_restore_l(IJKFF_Pipenode_Opaque *opaque)
{
    AVCodecParameters *next = NULL, *csd = NULL;
    AVBSFContext *bsf = NULL;
    int ret;
    if (!h264_crop_enabled(opaque) || !opaque->h264_crop_committed)
        return 0;
    next = avcodec_parameters_alloc(); csd = avcodec_parameters_alloc();
    if (!next || !csd) { ret = AVERROR(ENOMEM); goto fail; }
    if ((ret = avcodec_parameters_copy(next, opaque->h264_baseline_codecpar)) < 0 ||
        (ret = ijk_amc_create_bsf(&bsf, next, opaque->ffp->is->video_st->time_base)) < 0 ||
        (ret = avcodec_parameters_copy(csd, bsf->par_out)) < 0)
        goto fail;
    opaque->hevc_next_codecpar = next;
    opaque->hevc_next_csd = csd;
    opaque->h264_next_bsfc = bsf;
    opaque->hevc_boundary_serial = opaque->decoder->pkt_serial;
    opaque->hevc_boundary_pending = true;
    opaque->h264_baseline_restore_pending = true;
    return 0;
fail:
    avcodec_parameters_free(&next); avcodec_parameters_free(&csd);
    av_bsf_free(&bsf); return ret;
}

/* Feeder only. Returns 1 while the current filtered AU must stay pending, 0
 * when it can be submitted, or a genuine error. In threaded mode all shared
 * codec/generation state is protected by acodec_mutex. Never wait while holding
 * the vout mutex or a reference to a new-generation input index. */
static int advance_hevc_generation(JNIEnv *env, IJKFF_Pipenode *node,
                                   bool threaded, int64_t time_us,
                                   int *enqueue_count)
{
    IJKFF_Pipenode_Opaque *opaque = node->opaque;
    Decoder *d = opaque->decoder;
    int ret = 0, owned = 0;
    bool surface_locked = false;
    bool crop = h264_crop_enabled(opaque);
    bool baseline_restore = opaque->h264_baseline_restore_pending;
    AVCodecParameters *previous = NULL;
    if (opaque->abort || opaque->ffp->is->abort_request ||
        d->queue->abort_request || d->queue->serial != d->pkt_serial)
        return 1;
    if (!crop && (!opaque->ffp->mediacodec_handle_resolution_change ||
        opaque->codecpar->codec_id != AV_CODEC_ID_HEVC))
        return 0;

    if (crop && !baseline_restore && !opaque->jsurface &&
        (opaque->h264_next_bsfc || opaque->h264_crop_replay_pending)) goto wait_surface;
    if (crop && baseline_restore)
        opaque->hevc_boundary_pending = true; // Surface recreation retains this staged epoch
    if (opaque->hevc_packet_checked && !opaque->hevc_boundary_pending)
        return 0;
    if (crop && opaque->h264_crop_disabled && !baseline_restore) return 0;
    if (crop && !opaque->hevc_boundary_pending) {
        AVCodecParameters *next = NULL, *csd = NULL;
        AVBSFContext *bsf = NULL;
        const AVCodecParameters *active = opaque->hevc_active_csd ? opaque->hevc_active_csd
            : opaque->bsfc ? opaque->bsfc->par_out : opaque->codecpar;
        if (d->pkt_temp.size <= 0) {
            opaque->hevc_packet_checked = true;
            return 0;
        }
        ret = ijk_amc_h264_crop_parameters(opaque->codecpar, active, &d->pkt,
                &d->pkt_temp, opaque->ffp->is->video_st->time_base, &next, &csd, &bsf);
        if (ret == 2) {
            opaque->h264_crop_disabled = true;
            avcodec_parameters_free(&opaque->hevc_active_csd);
            ALOGI("H264 crop workaround: unsupported parameters, baseline playback retained\n");
            ret = 0;
        }
        if (ret <= 0) {
            opaque->hevc_packet_checked = ret == 0;
            return ret;
        }
        // The held packet is already filtered. Prove there are no old filter
        // outputs left before replacing that cache; never re-filter this AU.
        if (opaque->bsf_pending) {
            AVPacket tail = {0};
            ret = av_bsf_receive_packet(opaque->bsfc, &tail);
            av_packet_unref(&tail);
            if (ret != AVERROR(EAGAIN)) {
                avcodec_parameters_free(&next); avcodec_parameters_free(&csd);
                av_bsf_free(&bsf);
                return ret < 0 ? ret : AVERROR_INVALIDDATA;
            }
            opaque->bsf_pending = false;
        }
        opaque->acodec_reconfigure_request = true;
        SDL_LockMutex(opaque->acodec_mutex);
        opaque->acodec_reconfigure_request = false;
        SDL_CondSignal(opaque->acodec_cond);
        cancel_hevc_generation_l(opaque);
        opaque->hevc_next_codecpar = next;
        opaque->hevc_next_csd = csd;
        opaque->h264_next_bsfc = bsf;
        opaque->hevc_boundary_serial = d->pkt_serial;
        opaque->hevc_boundary_pending = true;
        ALOGI("H264 crop boundary: serial=%d size=%dx%d -> %dx%d held=%d pts=%lld dts=%lld\n",
              d->pkt_serial, opaque->codecpar->width, opaque->codecpar->height,
              next->width, next->height, d->pkt_temp.size,
              (long long)d->pkt_temp.pts, (long long)d->pkt_temp.dts);
        SDL_UnlockMutex(opaque->acodec_mutex);
    }
    if (!crop && !opaque->hevc_boundary_pending) {
        size_t size = 0;
        const uint8_t *extra = av_packet_get_side_data(&d->pkt,
                                  AV_PKT_DATA_NEW_EXTRADATA, &size);
        if (!extra && !opaque->hevc_next_codecpar) {
            opaque->hevc_packet_checked = true;
            return 0;
        }
        AVPacket probe_packet = {0};
        AVCodecParameters *next = NULL, *csd = NULL;
        ret = av_packet_ref(&probe_packet, &d->pkt);
        if (ret < 0)
            return ret;
        if (!extra) {
            const AVCodecParameters *staged = opaque->hevc_next_codecpar;
            uint8_t *side = av_packet_new_side_data(&probe_packet,
                              AV_PKT_DATA_NEW_EXTRADATA, staged->extradata_size);
            if (!side) { av_packet_unref(&probe_packet); return AVERROR(ENOMEM); }
            memcpy(side, staged->extradata, staged->extradata_size);
        }
        ret = ijk_amc_hevc_generation_parameters(opaque->codecpar,
                 &probe_packet, &d->pkt_temp, &next, &csd);
        av_packet_unref(&probe_packet);
        if (ret < 0)
            return ret;
        if (!opaque->hevc_active_csd)
            ret = ijk_amc_hevc_effective_csd(opaque->codecpar, NULL, NULL,
                                             &opaque->hevc_active_csd);
        if (ret < 0) {
            avcodec_parameters_free(&next); avcodec_parameters_free(&csd);
            return ret;
        }
        const AVCodecParameters *active = opaque->hevc_active_csd;
        bool equivalent = active->extradata_size == csd->extradata_size &&
            !memcmp(active->extradata, csd->extradata, csd->extradata_size) &&
            opaque->codecpar->width == next->width &&
            opaque->codecpar->height == next->height &&
            opaque->codecpar->format == next->format &&
            opaque->codecpar->profile == next->profile;
        if (threaded) {
            opaque->acodec_reconfigure_request = true;
            SDL_LockMutex(opaque->acodec_mutex);
            opaque->acodec_reconfigure_request = false;
            SDL_CondSignal(opaque->acodec_cond);
        }
        cancel_hevc_generation_l(opaque);
        if (equivalent) {
            ret = avcodec_parameters_copy(opaque->codecpar, next);
            avcodec_parameters_free(&next); avcodec_parameters_free(&csd);
            opaque->hevc_packet_checked = ret >= 0;
            if (threaded) SDL_UnlockMutex(opaque->acodec_mutex);
            return ret;
        }
        opaque->hevc_next_codecpar = next;
        opaque->hevc_next_csd = csd;
        opaque->hevc_boundary_serial = d->pkt_serial;
        if (threaded) SDL_UnlockMutex(opaque->acodec_mutex);
    }

    if (!opaque->hevc_boundary_pending) {
        int closed = ijk_amc_hevc_closed_boundary(d->pkt_temp.data, d->pkt_temp.size);
        if (closed == 0) {
            opaque->hevc_packet_checked = true;
            return 0; // configuration-only: wait for next AU, no reset/EOS
        }
        if (closed < 0)
            return AVERROR_INVALIDDATA; // never destroy live references blindly
        if (threaded) {
            opaque->acodec_reconfigure_request = true;
            SDL_LockMutex(opaque->acodec_mutex);
            opaque->acodec_reconfigure_request = false;
            SDL_CondSignal(opaque->acodec_cond);
        }
        opaque->hevc_boundary_pending = true;
        if (threaded)
            SDL_UnlockMutex(opaque->acodec_mutex);
    }
    // Ordinary Surface recreation cancels old indices, not this admitted AU.
    // Do not consume it in the dummy codec and strand its staged B cache.
    if (crop && !baseline_restore && !opaque->jsurface && opaque->h264_next_bsfc) goto wait_surface;
    if (opaque->input_packet_count > 0) {
        ret = feed_codec_eos(node, time_us, threaded, enqueue_count);
        if (ret < 0)
            return ret;
    }
    if (threaded) {
        opaque->acodec_reconfigure_request = true;
        SDL_LockMutex(opaque->acodec_mutex);
        opaque->acodec_reconfigure_request = false;
        SDL_CondSignal(opaque->acodec_cond);
    }
    if (opaque->abort || opaque->ffp->is->abort_request ||
        d->queue->abort_request || opaque->acodec_flush_request ||
        d->queue->serial != opaque->hevc_boundary_serial ||
        d->pkt_serial != opaque->hevc_boundary_serial) {
        ret = 1; // ordinary seek/stop path owns cancellation and flush
        goto done;
    }
    // Serialize with set_surface's surface->vout detach. Surface replacement
    // is an ordinary lifecycle interruption, not a failed ownership return.
    ffpipeline_lock_surface(opaque->pipeline);
    surface_locked = true;
    if (ffpipeline_is_surface_need_reconfigure_l(opaque->pipeline)) {
        opaque->hevc_drain_idle_since_us = 0;
        ret = 1; // next feeder iteration runs the ordinary surface handler
        goto done;
    }
    owned = SDL_VoutAndroid_countOwnedBuffers(opaque->weak_vout,
                                      SDL_AMediaCodec_getSerial(opaque->acodec));
    if (owned < 0) {
        ret = AVERROR_EXTERNAL;
        goto done;
    }
    if (owned > 0 || opaque->off_buf_out > 0 || opaque->ffp->is->paused) {
        // Legitimate presentation/paused ownership is not a codec timeout.
        opaque->hevc_drain_idle_since_us = 0;
        ret = 1;
        goto done;
    }
    if (opaque->input_packet_count > 0 && !opaque->hevc_boundary_drained) {
        int64_t now = av_gettime_relative();
        if (!opaque->hevc_drain_idle_since_us)
            opaque->hevc_drain_idle_since_us = now;
        if (now - opaque->hevc_drain_idle_since_us >= 10 * AV_TIME_BASE) {
            ret = AVERROR(ETIMEDOUT);
            goto done;
        }
        ret = 1;
        goto done;
    }
    // No old codec-owned/in-flight/reordered/presentation index survives here.
    // The held filtered_packet owns pkt_temp and remains byte-exact throughout.
    if (crop) {
        ALOGI("H264 crop drained: serial=%d input_eos=%d output_eos=%d reorder=%d owned=%d\n",
              d->pkt_serial, opaque->input_eos_queued, opaque->output_eos_seen,
              opaque->off_buf_out, owned);
        previous = opaque->codecpar;
        opaque->codecpar = opaque->hevc_next_codecpar;
        opaque->hevc_next_codecpar = NULL;
    } else {
        ret = avcodec_parameters_copy(opaque->codecpar, opaque->hevc_next_codecpar);
        if (ret < 0) goto done;
    }
    ret = recreate_format_l(env, node);
    if (ret < 0)
        goto done;
    // Callee retains the new reference BEFORE releasing its old reference,
    // so passing the existing Surface requires no second outer JNI allocation.
    ret = crop ? reconfigure_codec_l_checked(env, node, opaque->jsurface, true)
               : reconfigure_codec_l(env, node, opaque->jsurface);
    if (ret < 0)
        goto done;
    if (crop) {
        av_bsf_free(&opaque->bsfc);
        opaque->bsfc = opaque->h264_next_bsfc;
        opaque->h264_next_bsfc = NULL;
        if (!opaque->h264_baseline_codecpar) {
            opaque->h264_baseline_codecpar = previous;
            previous = NULL;
        }
        avcodec_parameters_free(&previous);
        opaque->h264_crop_committed = !baseline_restore;
    }
    avcodec_parameters_free(&opaque->hevc_active_csd);
    opaque->hevc_active_csd = opaque->hevc_next_csd;
    opaque->hevc_next_csd = NULL;
    opaque->input_packet_count = 0;
    d->finished = 0;
    ALOGI("%s generation commit: serial=%d codec=%d size=%dx%d owned=%d\n",
          crop ? (baseline_restore ? "H264 baseline restore" : "H264 crop") : "HEVC",
          d->pkt_serial, SDL_AMediaCodec_getSerial(opaque->acodec),
          opaque->codecpar->width, opaque->codecpar->height, owned);
    cancel_hevc_generation_l(opaque);
    opaque->hevc_packet_checked = !baseline_restore;
    opaque->h264_crop_replay_pending = crop && !baseline_restore;
    ret = baseline_restore ? 1 : 0; // retain and inspect the same source AU on the next iteration
    if (crop) {
        // The new codec now owns the new CSD, so publish its matching metadata
        // even if a seek arrived during configure. Never submit the stale AU;
        // the normal serial/flush path cancels it on the next feeder iteration.
        SDL_LockMutex(d->queue->mutex);
        if (!h264_input_valid_l(opaque) ||
            d->pkt_serial != opaque->hevc_boundary_serial) ret = 1;
        SDL_UnlockMutex(d->queue->mutex);
    }
done:
    if (previous) {
        // Failed teardown/configure is terminal, but active metadata/BSF must
        // not claim that the staged CSD was successfully configured.
        opaque->hevc_next_codecpar = opaque->codecpar;
        opaque->codecpar = previous;
    }
    if (surface_locked)
        ffpipeline_unlock_surface(opaque->pipeline);
    if (threaded) {
        SDL_CondSignal(opaque->acodec_cond);
        SDL_UnlockMutex(opaque->acodec_mutex);
    }
    return ret;
wait_surface:
    SDL_LockMutex(d->queue->mutex);
    if (!opaque->abort && !d->queue->abort_request && d->pkt_serial == d->queue->serial)
        SDL_CondWaitTimeout(d->queue->cond, d->queue->mutex, 10);
    SDL_UnlockMutex(d->queue->mutex);
    return 1;
}
#endif
