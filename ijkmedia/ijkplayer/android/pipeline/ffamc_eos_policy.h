/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef IJKPLAYER_ANDROID_FFAMC_EOS_POLICY_H
#define IJKPLAYER_ANDROID_FFAMC_EOS_POLICY_H

static bool codec_input_is_eof(Decoder *d)
{
    return d->packet_pending && !d->pkt.data && !d->pkt.side_data_elems &&
           d->pkt_serial == d->queue->serial;
}

static void record_codec_output(IJKFF_Pipenode *node, const SDL_AMediaCodecBufferInfo *info)
{
    IJKFF_Pipenode_Opaque *opaque = node->opaque;
    if (info->flags & (AMEDIACODEC__BUFFER_FLAG_FAKE_FRAME |
                       AMEDIACODEC__BUFFER_FLAG_CODEC_CONFIG))
        return;
    if ((info->flags & AMEDIACODEC__BUFFER_FLAG_END_OF_STREAM) && !info->size)
        return;
    // A Surface picture can have zero bytes. Keep this flag across seek/recreate.
    opaque->real_output_seen = true;
    opaque->hevc_drain_idle_since_us = 0; // genuine output is drain progress
}

static bool should_fallback_to_ffplay(IJKFF_Pipenode *node)
{
    IJKFF_Pipenode_Opaque *opaque = node->opaque;
    FFPlayer *ffp = opaque->ffp;
    if (!ffp || !ffp->is)
        return false;
    if (!ffp->mediacodec_auto_fallback || ffp->mediacodec_auto_fallback_triggered)
        return false;
    // Audio startup can set first_frame_decoded without a video picture.
    // After compressed EOF there is no input to replay in software.
    return !ffp->is->viddec.queue->abort_request &&
           !opaque->real_output_seen && !codec_input_is_eof(&ffp->is->viddec);
}

#endif
