/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef IJKPLAYER_ANDROID_FFAMC_ERROR_POLICY_H
#define IJKPLAYER_ANDROID_FFAMC_ERROR_POLICY_H

/* Called only by the output-owner loop, after codec cleanup and the existing
 * pre-output software-fallback decision. The input thread reports through its
 * enqueue_error mailbox; it never notifies Java independently. */
static void notify_codec_terminal_error(IJKFF_Pipenode *node, int ret)
{
    IJKFF_Pipenode_Opaque *opaque = node->opaque;
    FFPlayer *ffp = opaque->ffp;
    if (ret >= 0 || ret == AVERROR_EXIT || opaque->terminal_error_notified ||
        !ffp || !ffp->is || ffp->is->abort_request ||
        ffp->is->viddec.queue->abort_request)
        return;

    /* A pending seek/flush/reconfigure is not by itself a successful recovery:
     * this decoder thread has terminated. Real fatal errors must remain visible.
     * Normal control-flow retries do not reach this terminal notification. */
    opaque->terminal_error_notified = true;
    ffp_notify_msg2(ffp, FFP_MSG_ERROR, ret);
}
#endif
