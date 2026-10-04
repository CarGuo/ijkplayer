/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef IJKPLAYER_ANDROID_FFAMC_VISIBLE_SIZE_H
#define IJKPLAYER_ANDROID_FFAMC_VISIBLE_SIZE_H
/* Pure visible-size validation; stride/slice-height are allocation geometry. */
static int ijk_amc_visible_size(int width, int height, int crop_mask,
                               int left, int top, int right, int bottom,
                               int rotation, int *out_width, int *out_height)
{
    if (width <= 0 || height <= 0 || (crop_mask != 0 && crop_mask != 15))
        return -1;
    if (crop_mask) {
        if (left < 0 || top < 0 || right < left || bottom < top ||
            right >= width || bottom >= height)
            return -1;
        width = right - left + 1;
        height = bottom - top + 1;
    }
    if (rotation == 90 || rotation == 270) {
        int swap = width; width = height; height = swap;
    }
    *out_width = width;
    *out_height = height;
    return 0;
}
#endif
