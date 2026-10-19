/* Famicom Disk System saves: what a game writes to its disk, kept beside the
 * image. Shared by the cycle runtime's host and its tests.
 *
 * A save ("sidecar") holds the WHOLE drive stream of every side that differs
 * from the stream the loader rebuilds from the image, exactly as the drive
 * left it: gaps, $80 marks, blocks and CRC bytes as the BIOS wrote them. The
 * image file itself is never written.
 *
 * Why whole sides and not a diff: the side stream is rebuilt from the image
 * by a loader with options (nes_fds_side_stream: Mesen 0.9.9 or Mesen2
 * layout, computed or constant CRC bytes). A diff against that stream is only
 * meaningful against the exact base it was taken from, so a loader fix or a
 * different option would apply it to the wrong bytes or have to reject it.
 * A whole side is self-contained: it reloads byte for byte whatever the
 * options, and a side the game never wrote is still rebuilt from the image.
 * A side is 65500-70000 bytes, so a two-sided game's save is ~130 KiB.
 *
 * Mesen's format (<stem>.ips, Core/FDS.cpp SaveBattery / CreateIpsPatch) is
 * an IPS patch of the image FILE: Mesen strips gaps, marks and CRCs from its
 * sides (FdsLoader::RebuildFdsFile) and diffs the result against the file.
 * That loses the stream-level bytes this format keeps, so it is supported as
 * an import and export path only (nes_fds_rebuild_side, nes_fds_ips_*).
 *
 * Layout (little endian):
 *   0   8  magic "NRFDSAV\x1a"
 *   8   4  version (1)
 *   12  4  disk identity: CRC-32 of the side data (nes_fds_identity)
 *   16  8  disk identity: FNV-1a 64 of the same bytes
 *   24  4  side data bytes (sides * side size)
 *   28  2  sides      30 2 side size (65500, or 65536 for .qd)
 *   32  1  stream layout it was written under (NesFdsProfile), informational
 *   33  1  CRC mode (NesFdsCrc), informational
 *   34  1  write position (0 under the head, 2 = Mesen's two behind), informational
 *   35  1  stored sides N
 *   36  4  reserved, 0
 *   40     N records: u8 side, 3 zero bytes, u32 length, u32 CRC-32 of the
 *          stream, the stream bytes; sides ascending, each at most once
 *   end 4  CRC-32 of every byte before it
 * A save is accepted only whole: bad magic, version, record or CRC is
 * "corrupt"; a valid save made for another disk is "foreign". Either way it
 * is not applied (the host refuses to start rather than overwrite it).
 *
 * The identity is computed over the side data, not the file: a headered
 * image and its raw twin are the same disk and share saves. */
#ifndef NES_FDS_SAVE_H
#define NES_FDS_SAVE_H
#include "nes_fds.h"

#define NES_FDS_SAVE_MAGIC      "NRFDSAV\x1a"
#define NES_FDS_SAVE_VERSION    1u
#define NES_FDS_SAVE_HEADER     40u
#define NES_FDS_SAVE_MAX_SIDES  255u
#define NES_FDS_SAVE_MAX_STREAM (1u << 20)   /* no loader builds a side stream anywhere near this */

typedef struct {
    uint32_t crc32, bytes;
    uint64_t fnv;
    uint16_t sides, side_bytes;
} NesFdsIdentity;

typedef struct {
    unsigned side;
    const uint8_t *bytes;
    uint32_t len;
} NesFdsSavedSide;

typedef struct {
    NesFdsIdentity id;
    uint8_t layout, crc_mode, write_at;
    unsigned count;
    NesFdsSavedSide sides[NES_FDS_SAVE_MAX_SIDES];
} NesFdsSave;

typedef enum {
    NES_FDS_SAVE_OK,
    NES_FDS_SAVE_CORRUPT,     /* not a whole, intact save file */
    NES_FDS_SAVE_VERSION_NEWER,
    NES_FDS_SAVE_FOREIGN,     /* intact, but for a different disk */
} NesFdsSaveStatus;

static inline const char *nes_fds_save_status_name(NesFdsSaveStatus s)
{
    return s == NES_FDS_SAVE_OK ? "ok" : s == NES_FDS_SAVE_CORRUPT ? "corrupt or truncated" :
           s == NES_FDS_SAVE_VERSION_NEWER ? "written by a newer version" : "made for a different disk image";
}

static inline uint32_t nes_fds_rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline void nes_fds_wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* The disk the image holds: every side-data byte (zero where a truncated
 * image stops), independent of the fwNES header. */
static inline void nes_fds_identity(const NesFdsImage *img, NesFdsIdentity *id)
{
    memset(id, 0, sizeof(*id));
    id->sides = (uint16_t)img->sides;
    id->side_bytes = (uint16_t)(img->side_bytes & 0xFFFF);
    id->bytes = img->sides * img->side_bytes;
    uint32_t crc = 0;
    uint64_t fnv = 0xCBF29CE484222325ull;
    size_t have = img->size > img->data_offset ? img->size - img->data_offset : 0;
    if (have > id->bytes) have = id->bytes;
    crc = nes_crc32(crc, img->image + img->data_offset, have);
    for (size_t i = 0; i < have; ++i) fnv = (fnv ^ img->image[img->data_offset + i]) * 0x100000001B3ull;
    static const uint8_t zero[256] = { 0 };
    for (size_t left = id->bytes - have; left;) {
        size_t n = left < sizeof(zero) ? left : sizeof(zero);
        crc = nes_crc32(crc, zero, n);
        for (size_t i = 0; i < n; ++i) fnv *= 0x100000001B3ull;
        left -= n;
    }
    id->crc32 = crc;
    id->fnv = fnv;
}

static inline bool nes_fds_identity_equal(const NesFdsIdentity *a, const NesFdsIdentity *b)
{
    return a->crc32 == b->crc32 && a->fnv == b->fnv && a->bytes == b->bytes && a->sides == b->sides &&
           a->side_bytes == b->side_bytes;
}

/* Serialize. Returns the file size; writes only when it fits in cap (a NULL/0
 * call sizes the buffer). */
static inline size_t nes_fds_save_write(const NesFdsSave *s, uint8_t *out, size_t cap)
{
    size_t n = NES_FDS_SAVE_HEADER + 4;
    for (unsigned i = 0; i < s->count; ++i) n += 12 + s->sides[i].len;
    if (!out || cap < n) return n;
    memset(out, 0, NES_FDS_SAVE_HEADER);
    memcpy(out, NES_FDS_SAVE_MAGIC, 8);
    nes_fds_wr32(out + 8, NES_FDS_SAVE_VERSION);
    nes_fds_wr32(out + 12, s->id.crc32);
    nes_fds_wr32(out + 16, (uint32_t)s->id.fnv);
    nes_fds_wr32(out + 20, (uint32_t)(s->id.fnv >> 32));
    nes_fds_wr32(out + 24, s->id.bytes);
    out[28] = (uint8_t)s->id.sides; out[29] = (uint8_t)(s->id.sides >> 8);
    out[30] = (uint8_t)s->id.side_bytes; out[31] = (uint8_t)(s->id.side_bytes >> 8);
    out[32] = s->layout; out[33] = s->crc_mode; out[34] = s->write_at; out[35] = (uint8_t)s->count;
    size_t p = NES_FDS_SAVE_HEADER;
    for (unsigned i = 0; i < s->count; ++i) {
        const NesFdsSavedSide *r = &s->sides[i];
        memset(out + p, 0, 4);
        out[p] = (uint8_t)r->side;
        nes_fds_wr32(out + p + 4, r->len);
        nes_fds_wr32(out + p + 8, nes_crc32(0, r->bytes, r->len));
        memcpy(out + p + 12, r->bytes, r->len);
        p += 12 + r->len;
    }
    nes_fds_wr32(out + p, nes_crc32(0, out, p));
    return n;
}

/* Parse and check a save for the disk `expect` (NULL: any disk). On OK the
 * records point into buf. */
static inline NesFdsSaveStatus nes_fds_save_parse(const uint8_t *buf, size_t n, const NesFdsIdentity *expect,
                                                  NesFdsSave *s)
{
    memset(s, 0, sizeof(*s));
    if (!buf || n < NES_FDS_SAVE_HEADER + 4 || memcmp(buf, NES_FDS_SAVE_MAGIC, 8)) return NES_FDS_SAVE_CORRUPT;
    if (nes_fds_rd32(buf + n - 4) != nes_crc32(0, buf, n - 4)) return NES_FDS_SAVE_CORRUPT;
    uint32_t version = nes_fds_rd32(buf + 8);
    if (version > NES_FDS_SAVE_VERSION) return NES_FDS_SAVE_VERSION_NEWER;
    if (version != NES_FDS_SAVE_VERSION) return NES_FDS_SAVE_CORRUPT;
    s->id.crc32 = nes_fds_rd32(buf + 12);
    s->id.fnv = (uint64_t)nes_fds_rd32(buf + 16) | (uint64_t)nes_fds_rd32(buf + 20) << 32;
    s->id.bytes = nes_fds_rd32(buf + 24);
    s->id.sides = (uint16_t)(buf[28] | buf[29] << 8);
    s->id.side_bytes = (uint16_t)(buf[30] | buf[31] << 8);
    s->layout = buf[32]; s->crc_mode = buf[33]; s->write_at = buf[34];
    unsigned count = buf[35];
    if (nes_fds_rd32(buf + 36) || count > s->id.sides) return NES_FDS_SAVE_CORRUPT;
    size_t p = NES_FDS_SAVE_HEADER, end = n - 4;
    for (unsigned i = 0; i < count; ++i) {
        if (end - p < 12) return NES_FDS_SAVE_CORRUPT;
        unsigned side = buf[p];
        uint32_t len = nes_fds_rd32(buf + p + 4), crc = nes_fds_rd32(buf + p + 8);
        if (buf[p + 1] || buf[p + 2] || buf[p + 3] || side >= s->id.sides || (i && side <= s->sides[i - 1].side) ||
            !len || len > NES_FDS_SAVE_MAX_STREAM || end - p - 12 < len || nes_crc32(0, buf + p + 12, len) != crc)
            return NES_FDS_SAVE_CORRUPT;
        s->sides[i].side = side;
        s->sides[i].bytes = buf + p + 12;
        s->sides[i].len = len;
        p += 12 + len;
    }
    if (p != end) return NES_FDS_SAVE_CORRUPT;
    s->count = count;
    if (expect && !nes_fds_identity_equal(expect, &s->id)) return NES_FDS_SAVE_FOREIGN;
    return NES_FDS_SAVE_OK;
}

/* ---- Mesen's .ips saves ---------------------------------------------------
 * libretro/Mesen 0102910 (nesref's core) == Mesen 0.9.9 here:
 *   FdsLoader::RebuildFdsFile (FdsLoader.cpp:64-100): per side, skip gap bytes
 *   up to and including an $80, copy one block (1:56 2:2 3:16 4:1+size, size
 *   from the last file header's bytes 13/14, anything else 1 byte), skip its
 *   2 CRC bytes, repeat; then zero-fill to 65500. A headered image gets a
 *   fresh header ("FDS\x1a", sides, zeros).
 *   IpsPatcher::CreatePatch / PatchBuffer (IpsPatcher.cpp:74-190).
 * Where Mesen reads past its buffer (a block cut off by the end of the
 * stream) the rebuild stops; a side whose blocks exceed 65500 bytes (Mesen's
 * gapNeeded underflows) is cut at 65500 and reported. */
static inline bool nes_fds_rebuild_side(const uint8_t *stream, size_t len, uint8_t *out /* 65500 */)
{
    memset(out, 0, NES_FDS_SIDE_BYTES);
    size_t i = 0, n = 0;
    uint32_t file_size = 0;
    bool in_gap = true, fits = true;
    while (i < len) {
        if (in_gap) {
            if (stream[i] == NES_FDS_GAP_MARK) in_gap = false;
            i++;
            continue;
        }
        uint32_t block = 1;
        switch (stream[i]) {
        case 1: block = NES_FDS_INFO_BYTES; break;
        case 2: block = NES_FDS_AMOUNT_BYTES; break;
        case 3:
            block = NES_FDS_FILE_HDR_BYTES;
            if (i + 14 < len) file_size = stream[i + 13] | (uint32_t)stream[i + 14] << 8;
            break;
        case 4: block = 1 + file_size; break;
        default: break;
        }
        if (i + block > len) break;
        for (uint32_t k = 0; k < block; ++k, ++n) {
            if (n < NES_FDS_SIDE_BYTES) out[n] = stream[i + k];
            else fits = false;
        }
        i += block + 2;
        in_gap = true;
    }
    return fits;
}

/* IPS as Mesen writes it (IpsPatcher::CreatePatch). a and b have n bytes.
 * Returns the patch size; writes only when it fits in cap. */
static inline size_t nes_fds_ips_create(const uint8_t *a, const uint8_t *b, size_t n, uint8_t *out, size_t cap)
{
    size_t o = 0;
#define NES_FDS_IPS_PUT(x) do { if (out && o < cap) out[o] = (uint8_t)(x); ++o; } while (0)
    NES_FDS_IPS_PUT('P'); NES_FDS_IPS_PUT('A'); NES_FDS_IPS_PUT('T'); NES_FDS_IPS_PUT('C'); NES_FDS_IPS_PUT('H');
    size_t i = 0;
    while (i < n) {
        while (i < n && a[i] == b[i]) i++;
        if (i >= n) break;
        size_t addr = i;
        unsigned length = 0;
        uint8_t rle_byte = b[i], rle_count = 0;
        bool rle = false;
        while (i < n && length < 65535 && a[i] != b[i]) {
            if (b[i] == rle_byte) rle_count++;
            else if (rle) break;
            else { rle_byte = b[i]; rle_count = 1; }
            length++;
            i++;
            if ((length == rle_count && rle_count > 3) || rle_count > 13) {
                if (length == rle_count) rle = true;
                else { length -= rle_count; i -= rle_count; break; }
            }
        }
        NES_FDS_IPS_PUT(addr >> 16); NES_FDS_IPS_PUT(addr >> 8); NES_FDS_IPS_PUT(addr);
        if (rle) {
            NES_FDS_IPS_PUT(0); NES_FDS_IPS_PUT(0);
            NES_FDS_IPS_PUT(0); NES_FDS_IPS_PUT(rle_count); NES_FDS_IPS_PUT(rle_byte);
        } else {
            NES_FDS_IPS_PUT(length >> 8); NES_FDS_IPS_PUT(length);
            for (unsigned k = 0; k < length; ++k) NES_FDS_IPS_PUT(b[addr + k]);
        }
    }
    NES_FDS_IPS_PUT('E'); NES_FDS_IPS_PUT('O'); NES_FDS_IPS_PUT('F');
#undef NES_FDS_IPS_PUT
    return o;
}

/* Apply an IPS patch to a copy of `in` (Mesen PatchBuffer: records may grow
 * the output, an optional 3-byte truncation offset follows EOF). out_size
 * receives the result size; out (cap bytes, may be NULL to size) receives the
 * bytes. False for a malformed patch. */
static inline bool nes_fds_ips_apply(const uint8_t *ips, size_t ips_size, const uint8_t *in, size_t in_size,
                                     uint8_t *out, size_t cap, size_t *out_size)
{
    if (ips_size < 8 || memcmp(ips, "PATCH", 5)) return false;
    size_t size = in_size, p = 5;
    long truncate = -1;
    for (int pass = 0; pass < 2; ++pass) {
        p = 5;
        if (pass && out) { if (cap < size) return false; memcpy(out, in, in_size); memset(out + in_size, 0, size - in_size); }
        for (;;) {
            if (ips_size - p < 3) return false;
            if (!memcmp(ips + p, "EOF", 3)) {
                p += 3;
                if (ips_size - p >= 3) truncate = (long)ips[p] << 16 | (long)ips[p + 1] << 8 | ips[p + 2];
                break;
            }
            if (ips_size - p < 5) return false;
            size_t addr = (size_t)ips[p] << 16 | (size_t)ips[p + 1] << 8 | ips[p + 2];
            unsigned length = (unsigned)ips[p + 3] << 8 | ips[p + 4];
            p += 5;
            if (!length) {
                if (ips_size - p < 3) return false;
                unsigned count = (unsigned)ips[p] << 8 | ips[p + 1];
                uint8_t value = ips[p + 2];
                p += 3;
                if (!pass) { if (addr + count > size) size = addr + count; }
                else if (out) memset(out + addr, value, count);
            } else {
                if (ips_size - p < length) return false;
                if (!pass) { if (addr + length > size) size = addr + length; }
                else if (out) memcpy(out + addr, ips + p, length);
                p += length;
            }
        }
    }
    if (truncate >= 0 && (size_t)truncate < size) size = (size_t)truncate;
    *out_size = size;
    return true;
}
#endif
