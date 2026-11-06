/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef IJK_FFAMC_H264_CROP_PARSE_H
#define IJK_FFAMC_H264_CROP_PARSE_H
/* Bounded header parser, NOT an entropy decoder. Input must be one complete
 * demux/BSF access unit. This contract cannot be established from slice headers.
 * Unsupported syntax returns -1 before any codec mutation. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#define HC_SPS_BYTES 1024
#define HC_PPS_BYTES 512
#define HC_MAX_AU (8 * 1024 * 1024)

typedef struct HCBit { const uint8_t *p; size_t pos, bits; int bad; } HCBit;
static uint32_t hc_bits(HCBit *b, unsigned n)
{
    uint32_t v = 0;
    if (n > 32 || b->bad || b->pos > b->bits || n > b->bits - b->pos) { b->bad = 1; return 0; }
    while (n--) { v = (v << 1) | ((b->p[b->pos / 8] >> (7 - b->pos % 8)) & 1); ++b->pos; }
    return v;
}
static uint32_t hc_ue(HCBit *b)
{
    unsigned n = 0;
    while (!b->bad && !hc_bits(b, 1)) if (++n > 30) { b->bad = 1; return 0; }
    if (b->bad) return 0;
    return ((1U << n) - 1) + hc_bits(b, n);
}
static int32_t hc_se(HCBit *b)
{
    uint32_t u = hc_ue(b);
    return (u & 1) ? (int32_t)(u / 2 + 1) : -(int32_t)(u / 2);
}
/* EBSP validation is also applied to slice data without copying it. */
static int hc_unescape(const uint8_t *p, size_t n, uint8_t *out, size_t cap, size_t *len)
{
    size_t j = 0; unsigned zeros = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t v = p[i];
        if (zeros >= 2) {
            if (v == 3) {
                if (i + 1 >= n || p[i + 1] > 3) return -1;
                zeros = 0; continue;
            }
            if (v <= 2) return -1;
        }
        if (out && j < cap) out[j] = v;
        ++j; zeros = v == 0 ? zeros + 1 : 0;
    }
    *len = j;
    return 0;
}
static size_t hc_payload_bits(const uint8_t *p, size_t n)
{
    if (!n || !p[n - 1]) return 0;
    unsigned z = 0; uint8_t v = p[n - 1];
    while (!(v & 1)) { ++z; v >>= 1; }
    return n * 8 - z - 1;
}
static void hc_scaling(HCBit *b, unsigned count)
{
    int last = 8, next = 8;
    for (unsigned i = 0; i < count && !b->bad; ++i) {
        if (next) { int d = hc_se(b); if (d < -128 || d > 127) { b->bad = 1; return; } next = (last + d + 256) % 256; }
        last = next ? next : last;
    }
}
static void hc_hrd(HCBit *b)
{
    uint32_t count = hc_ue(b);
    if (count > 31) { b->bad = 1; return; }
    hc_bits(b, 8);
    for (uint32_t i = 0; i <= count; ++i) { hc_ue(b); hc_ue(b); hc_bits(b, 1); }
    hc_bits(b, 20);
}
static void hc_vui(HCBit *b)
{
    if (hc_bits(b, 1)) { unsigned ar = hc_bits(b, 8); if (ar == 255) hc_bits(b, 32); }
    if (hc_bits(b, 1)) hc_bits(b, 1);
    if (hc_bits(b, 1)) { hc_bits(b, 4); if (hc_bits(b, 1)) hc_bits(b, 24); }
    if (hc_bits(b, 1)) { hc_ue(b); hc_ue(b); }
    if (hc_bits(b, 1)) { hc_bits(b, 32); hc_bits(b, 32); hc_bits(b, 1); }
    unsigned nal = hc_bits(b, 1); if (nal) hc_hrd(b);
    unsigned vcl = hc_bits(b, 1); if (vcl) hc_hrd(b);
    if (nal || vcl) hc_bits(b, 1);
    hc_bits(b, 1);
    if (hc_bits(b, 1)) { hc_bits(b, 1); for (int i = 0; i < 6; ++i) hc_ue(b); }
}
typedef struct HCSPS {
    int valid, id, width, height, visible_w, visible_h;
    int frame_num_bits, poc_type, poc_bits, delta_always_zero;
    uint64_t version;
    uint32_t crop[4];
    size_t raw_size, rbsp_size, payload_bits, crop_begin, crop_end;
    uint8_t raw[HC_SPS_BYTES], rbsp[HC_SPS_BYTES];
} HCSPS;
typedef struct HCPPS {
    int valid, id, sps_id, bottom_poc, redundant, cabac, init_qp, deblocking;
    uint64_t sps_version;
    size_t raw_size;
    uint8_t raw[HC_PPS_BYTES];
} HCPPS;
typedef struct HCPair { HCSPS sps; HCPPS pps; } HCPair;
typedef struct HCMap { HCSPS sps[32]; HCPPS pps[256]; } HCMap;
typedef struct HCAU { int idr, no_output_prior, pps_id; HCPair pair; } HCAU;
static int hc_parse_sps(const uint8_t *p, size_t n, HCSPS *s)
{
    memset(s, 0, sizeof(*s));
    if (n < 4 || n > sizeof(s->raw) || (p[0] & 0x9f) != 7 || !(p[0] & 0x60)) return -1;
    memcpy(s->raw, p, n); s->raw_size = n;
    if (hc_unescape(p + 1, n - 1, s->rbsp, sizeof(s->rbsp), &s->rbsp_size)) return -1;
    s->payload_bits = hc_payload_bits(s->rbsp, s->rbsp_size);
    HCBit b = { s->rbsp, 0, s->payload_bits, 0 };
    unsigned profile = hc_bits(&b, 8), constraints = hc_bits(&b, 8);
    hc_bits(&b, 8); unsigned id = hc_ue(&b);
    if (id > 31 || (constraints & 3)) return -1;
    s->id = (int)id;
    if (profile == 100) {
        if (hc_ue(&b) != 1 || hc_ue(&b) != 0 || hc_ue(&b) != 0 || hc_bits(&b, 1)) return -1;
        if (hc_bits(&b, 1)) for (int i = 0; i < 8; ++i) if (hc_bits(&b, 1)) hc_scaling(&b, i < 6 ? 16 : 64);
    } else if (profile != 66 && profile != 77 && profile != 88) return -1;
    unsigned fn = hc_ue(&b); if (fn > 12) return -1; s->frame_num_bits = (int)fn + 4;
    unsigned poc = hc_ue(&b); if (poc > 2) return -1; s->poc_type = (int)poc;
    if (!poc) { unsigned v = hc_ue(&b); if (v > 12) return -1; s->poc_bits = (int)v + 4; }
    else if (poc == 1) {
        s->delta_always_zero = hc_bits(&b, 1); hc_se(&b); hc_se(&b);
        unsigned cycle = hc_ue(&b); if (cycle > 255) return -1;
        for (unsigned i = 0; i < cycle; ++i) hc_se(&b);
    }
    if (hc_ue(&b) > 16) return -1; // max_num_ref_frames
    if (hc_bits(&b, 1)) return -1; // gaps_in_frame_num_value_allowed_flag
    unsigned w = hc_ue(&b), h = hc_ue(&b);
    if (w > 1023 || h > 1023 || !hc_bits(&b, 1)) return -1; // progressive only
    s->width = (int)(w + 1) * 16; s->height = (int)(h + 1) * 16;
    hc_bits(&b, 1); s->crop_begin = b.pos;
    if (hc_bits(&b, 1)) for (int i = 0; i < 4; ++i) s->crop[i] = hc_ue(&b);
    s->crop_end = b.pos;
    uint64_t cw = ((uint64_t)s->crop[0] + s->crop[1]) * 2;
    uint64_t ch = ((uint64_t)s->crop[2] + s->crop[3]) * 2;
    if (cw >= (unsigned)s->width || ch >= (unsigned)s->height) return -1;
    s->visible_w = s->width - (int)cw; s->visible_h = s->height - (int)ch;
    if (hc_bits(&b, 1)) hc_vui(&b);
    if (b.bad || b.pos != b.bits) return -1;
    s->valid = 1; return 0;
}
static int hc_parse_pps(const uint8_t *p, size_t n, HCPPS *s)
{
    uint8_t rbsp[HC_PPS_BYTES]; size_t len;
    memset(s, 0, sizeof(*s));
    if (n < 2 || n > sizeof(s->raw) || (p[0] & 0x9f) != 8 || !(p[0] & 0x60) ||
        hc_unescape(p + 1, n - 1, rbsp, sizeof(rbsp), &len)) return -1;
    HCBit b = { rbsp, 0, hc_payload_bits(rbsp, len), 0 };
    unsigned id = hc_ue(&b), sid = hc_ue(&b);
    if (id > 255 || sid > 31) return -1;
    s->id = id; s->sps_id = sid;
    s->cabac = hc_bits(&b, 1); s->bottom_poc = hc_bits(&b, 1);
    if (hc_ue(&b)) return -1; // FMO
    if (hc_ue(&b) > 31 || hc_ue(&b) > 31) return -1;
    hc_bits(&b, 1); if (hc_bits(&b, 2) > 2) return -1;
    s->init_qp = hc_se(&b); int init_qs = hc_se(&b), chroma = hc_se(&b);
    if (s->init_qp < -26 || s->init_qp > 25 || init_qs < -26 || init_qs > 25 || chroma < -12 || chroma > 12) return -1;
    s->deblocking = hc_bits(&b, 1); hc_bits(&b, 1);
    s->redundant = hc_bits(&b, 1);
    if (s->redundant) return -1;
    if (b.pos < b.bits) {
        unsigned transform = hc_bits(&b, 1);
        if (hc_bits(&b, 1)) for (unsigned i = 0; i < 6 + 2 * transform; ++i) if (hc_bits(&b, 1)) hc_scaling(&b, i < 6 ? 16 : 64);
        hc_se(&b);
    }
    if (b.bad || b.pos != b.bits) return -1;
    memcpy(s->raw, p, n); s->raw_size = n; s->valid = 1; return 0;
}
/* Annex-B reader accepts 3/4-byte start codes and trailing_zero_8bits. */
static int hc_nal(const uint8_t *p, size_t n, size_t *cursor, const uint8_t **nal, size_t *len)
{
    size_t i = *cursor, zeros = 0;
    if (i == n) return 0;
    while (i < n && !p[i]) { ++zeros; ++i; }
    if (zeros < 2 || i == n || p[i++] != 1) return -1;
    size_t start = i, end = n, next = n;
    for (zeros = 0; i < n; ++i) {
        if (!p[i]) { ++zeros; continue; }
        if (p[i] == 1 && zeros >= 2) { end = i - zeros; next = end; break; }
        zeros = 0;
    }
    while (end > start && !p[end - 1]) --end;
    if (end <= start || (p[start] & 0x80)) return -1;
    *nal = p + start; *len = end - start; *cursor = next; return 1;
}
static int hc_store(HCMap *m, const uint8_t *p, size_t n)
{
    int type = p[0] & 31;
    if (type == 7) {
        HCSPS s; if (hc_parse_sps(p, n, &s)) return -1;
        HCSPS *old = &m->sps[s.id];
        s.version = old->version;
        if (!old->valid || old->raw_size != n || memcmp(old->raw, p, n)) {
            if (s.version == UINT64_MAX) return -1;
            ++s.version;
        }
        m->sps[s.id] = s;
    }
    else if (type == 8) {
        HCPPS s; if (hc_parse_pps(p, n, &s) || !m->sps[s.sps_id].valid) return -1;
        s.sps_version = m->sps[s.sps_id].version; m->pps[s.id] = s;
    }
    else return -1;
    return 0;
}
static int hc_extra(HCMap *m, const uint8_t *p, size_t n)
{
    if (!n) return 0;
    if (!p) return -1;
    if (p[0] == 1) {
        if (n < 7 || (p[4] & 0xfc) != 0xfc || (p[4] & 3) == 2 || (p[5] & 0xe0) != 0xe0) return -1;
        size_t cur = 6; unsigned count = p[5] & 31;
        for (int group = 0; group < 2; ++group) {
            for (unsigned i = 0; i < count; ++i) {
                if (cur + 2 > n) return -1;
                size_t size = (size_t)p[cur] * 256 + p[cur + 1]; cur += 2;
                if (!size || size > n - cur || (p[cur] & 31) != (group ? 8 : 7) || hc_store(m, p + cur, size)) return -1;
                cur += size;
            }
            if (!group) { if (cur >= n) return -1; count = p[cur++]; }
        }
        // High-profile AVC configuration extensions: 8-bit 4:2:0, no SPS ext.
        if (cur != n) {
            if (p[1] != 100 || n - cur != 4 || p[cur] != 0xfd || p[cur + 1] != 0xf8 || p[cur + 2] != 0xf8 || p[cur + 3]) return -1;
        }
        return 0;
    }
    size_t cur = 0, len; const uint8_t *nal; int ret;
    while ((ret = hc_nal(p, n, &cur, &nal, &len)) > 0) if (hc_store(m, nal, len)) return -1;
    return ret;
}
static int hc_pair(const HCMap *m, unsigned pps, HCPair *pair)
{
    if (pps > 255 || !m->pps[pps].valid || !m->sps[m->pps[pps].sps_id].valid ||
        m->pps[pps].sps_version != m->sps[m->pps[pps].sps_id].version) return -1;
    pair->pps = m->pps[pps]; pair->sps = m->sps[pair->pps.sps_id]; return 0;
}
static int hc_access_unit(HCMap *m, const uint8_t *p, size_t n, HCAU *au)
{
    size_t cur = 0, len; const uint8_t *nal; int ret, vcl = 0;
    memset(au, 0, sizeof(*au));
    if (!p || !n || n > HC_MAX_AU) return -1;
    while ((ret = hc_nal(p, n, &cur, &nal, &len)) > 0) {
        unsigned type = nal[0] & 31;
        if (type == 7 || type == 8) { if (vcl || hc_store(m, nal, len)) return -1; continue; }
        if (type == 9) { if (vcl) return -1; continue; }
        if (type == 6 || type == 12) continue;
        if (type != 1 && type != 5) return -1;
        if (++vcl > 1 || len < 3) return -1; // one non-partitioned slice / AU
        uint8_t head[128]; size_t full;
        /* Validate full EBSP, but retain only its header prefix. The tail is
         * deliberately not claimed to be valid CAVLC/CABAC data. */
        if (hc_unescape(nal + 1, len - 1, head, sizeof(head), &full)) return -1;
        size_t bytes = full < sizeof(head) ? full : sizeof(head);
        HCBit b = { head, 0, bytes * 8, 0 };
        if (hc_ue(&b)) return -1;
        unsigned slice = hc_ue(&b), pps = hc_ue(&b);
        if (slice > 9 || hc_pair(m, pps, &au->pair)) return -1;
        au->pps_id = (int)pps; au->idr = type == 5;
        HCSPS *s = &au->pair.sps; HCPPS *q = &au->pair.pps;
        unsigned frame_num = hc_bits(&b, s->frame_num_bits);
        if (au->idr) {
            if (slice % 5 != 2 || !(nal[0] & 0x60) || frame_num || hc_ue(&b) > 65535) return -1;
        }
        if (s->poc_type == 0) { hc_bits(&b, s->poc_bits); if (q->bottom_poc) hc_se(&b); }
        else if (s->poc_type == 1 && !s->delta_always_zero) { hc_se(&b); if (q->bottom_poc) hc_se(&b); }
        if (au->idr) {
            au->no_output_prior = hc_bits(&b, 1); hc_bits(&b, 1);
            // I-slice: no reference lists, weight table or cabac_init_idc.
            int qp = 26 + q->init_qp + hc_se(&b);
            if (qp < 0 || qp > 51) return -1;
            if (q->deblocking) {
                unsigned disable = hc_ue(&b); if (disable > 2) return -1;
                if (disable != 1) {
                    int alpha = hc_se(&b), beta = hc_se(&b);
                    if (alpha < -6 || alpha > 6 || beta < -6 || beta > 6) return -1;
                }
            }
            // No FMO or redundant pictures in the supported PPS subset.
            if (q->cabac) while ((b.pos & 7) && !b.bad) if (hc_bits(&b, 1) != 1) return -1;
        }
        unsigned tail_zeros = 0; uint8_t tail = nal[len - 1];
        while (tail && !(tail & 1)) { ++tail_zeros; tail >>= 1; }
        size_t payload_bits = full * 8 - tail_zeros - 1;
        if (b.bad || !tail || b.pos >= payload_bits) return -1;
    }
    return ret < 0 || vcl != 1 ? -1 : 0;
}
static int hc_bit_equal(const uint8_t *a, size_t ap, const uint8_t *b, size_t bp, size_t n)
{
    for (size_t i = 0; i < n; ++i) if (((a[(ap+i)/8] >> (7-(ap+i)%8)) & 1) != ((b[(bp+i)/8] >> (7-(bp+i)%8)) & 1)) return 0;
    return 1;
}
/* 0 unchanged, 1 exactly crop-only, -1 other semantic/parameter change. */
static int hc_change(const HCPair *a, const HCPair *b)
{
    if (a->pps.raw_size != b->pps.raw_size || memcmp(a->pps.raw, b->pps.raw, a->pps.raw_size)) return -1;
    const HCSPS *x = &a->sps, *y = &b->sps;
    if (x->crop_begin != y->crop_begin || x->payload_bits - x->crop_end != y->payload_bits - y->crop_end ||
        !hc_bit_equal(x->rbsp, 0, y->rbsp, 0, x->crop_begin) ||
        !hc_bit_equal(x->rbsp, x->crop_end, y->rbsp, y->crop_end, x->payload_bits - x->crop_end)) return -1;
    return memcmp(x->crop, y->crop, sizeof(x->crop)) != 0;
}
static size_t hc_csd(const HCPair *p, uint8_t *out)
{
    static const uint8_t sc[4] = {0,0,0,1}; size_t n = 0;
    memcpy(out+n, sc, 4); n += 4; memcpy(out+n, p->sps.raw, p->sps.raw_size); n += p->sps.raw_size;
    memcpy(out+n, sc, 4); n += 4; memcpy(out+n, p->pps.raw, p->pps.raw_size); return n + p->pps.raw_size;
}
#endif
