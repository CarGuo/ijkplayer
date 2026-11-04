/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef IJKPLAYER_ANDROID_FFAMC_OUTPUT_SIZE_H
#define IJKPLAYER_ANDROID_FFAMC_OUTPUT_SIZE_H
#include <strings.h>
#include "ffamc_visible_size.h"

/* Included after the pipeline state; shared by both output dequeue modes.
 * A crop-only H.264 change need not carry NEW_EXTRADATA or recreate the codec.
 * Only update frame geometry: normal per-frame notifications publish it. */
static int update_output_frame_size(IJKFF_Pipenode_Opaque *opaque,
                                    int width, int height, int crop_mask,
                                    int left, int top, int right, int bottom)
{
    int codec_id = opaque->codecpar->codec_id;
    if (codec_id != AV_CODEC_ID_HEVC && codec_id != AV_CODEC_ID_H264)
        return 0;
    /* Keep the historical TI H.264 workaround until that path is qualified. */
    if (codec_id == AV_CODEC_ID_H264 &&
        !strncasecmp(opaque->acodec_name, "OMX.TI.DUCATI1.", 15))
        return 0;

    int ret = ijk_amc_visible_size(width, height, crop_mask,
        left, top, right, bottom, opaque->frame_rotate_degrees,
        &opaque->frame_width, &opaque->frame_height);
    /* HEVC retains its strict validation. For newly handled H.264 output,
     * incomplete/invalid vendor geometry must not break existing playback;
     * the resolver leaves the last known frame size untouched on failure. */
    return codec_id == AV_CODEC_ID_HEVC ? ret : 0;
}
#endif
