/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef IJKPLAYER_ANDROID_FFAMC_HEVC_BOUNDARY_H
#define IJKPLAYER_ANDROID_FFAMC_HEVC_BOUNDARY_H
/* Classified from filtered Annex B bytes, never AV_PKT_FLAG_KEY. IDR/BLA
 * establishes a closed reference boundary. CRA/open-GOP recovery is not proven
 * safe for this restart path. 0: no VCL, 1: closed boundary, -1: unsafe. */
static int ijk_amc_hevc_closed_boundary(const uint8_t *data, int size)
{
    int found = 0;
    for (int i = 0; data && i + 3 < size; ++i) {
        int start = -1;
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
            start = i + 3;
        else if (i + 4 < size && data[i] == 0 && data[i + 1] == 0 &&
                 data[i + 2] == 0 && data[i + 3] == 1)
            start = i + 4;
        if (start < 0)
            continue;
        if (start + 1 >= size || (data[start] & 0x80) || !(data[start + 1] & 7))
            return -1;
        int type = (data[start] >> 1) & 63;
        if (type < 32) {
            if (type < 16 || type > 20)
                return -1;
            found = 1;
        }
        i = start + 1;
    }
    return found;
}
#endif
