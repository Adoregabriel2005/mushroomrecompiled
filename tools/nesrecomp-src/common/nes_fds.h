/* Famicom Disk System media, shared by the compiler and the cycle runtime.
 * https://www.nesdev.org/wiki/FDS_disk_format and .../FDS_file_format
 *
 * This is file decoding only; the RAM Adapter and drive are modeled elsewhere.
 * Three image forms:
 *   fwNES   "FDS\x1a", side count at byte 4, 16-byte header, 65500-byte sides
 *   raw     the same sides with no header; begins with block 1 (01 *NINTENDO-HVC*)
 *   .qd     65536-byte sides whose blocks keep their 2-byte CRCs (no gaps)
 * fwNES and raw sides hold blocks back to back, without the gaps, gap marks
 * and CRCs the drive clocks out. nes_fds_side_stream() restores them.
 *
 * Oracle: nesref runs the Mesen 0.9.9 libretro core ("core: Mesen 0.9.9"),
 * source Core/FdsLoader.cpp. Its layout is the default profile:
 *   FdsLoader.cpp:16     lead-in 28300 bits of zero gap = 3537 bytes
 *   FdsLoader.cpp:18-27  block lengths 1:56 2:2 3:16 4:1+size, where size is
 *                        read from the two bytes 3 and 2 before the block-4 code
 *   FdsLoader.cpp:26     any other code (0 included) ends the side
 *   FdsLoader.cpp:32-40  per block: $80 mark, block bytes, CRC bytes $4D $62
 *                        (a constant, not a CRC), then 976 bits = 122 bytes gap
 *   FdsLoader.cpp:128-131 a side shorter than 65500 bytes is zero-padded to it
 *   FdsLoader.cpp:111-117 sides: header byte 4, else file size / 65500
 * Mesen2 (SourMesen/Mesen2 b9fa69d, Core/NES/Loaders/FdsLoader.cpp) differs,
 * selected by NES_FDS_PROFILE_MESEN2:
 *   :61-65 an unknown code emits $80 and the rest of the side verbatim
 *   :73    a block with j + length >= side size ends the side (so a block that
 *          ends exactly at the side end is dropped)
 *   :54-71 .qd: the size is 5 and 4 bytes back, blocks carry their own CRCs
 *   :168   a truncated headered image is zero-filled to its declared sides
 * Neither Mesen reads the file-amount block: every well-formed block on the
 * side is streamed, hidden files included. Where Mesen is undefined (it reads
 * outside its buffer) this file defines it: bytes before the first side or past
 * the end of the file read as zero, and a 0.9.9 block that would run past the
 * end of the file ends the side.
 *
 * CRC: CRC-16/KERMIT (reflected 0x8408, init 0, LSB first on disk) over the $80
 * mark and the block bytes. That is what both Mesen drives write
 * (0.9.9 FDS.cpp:299-310 augmented form, Mesen2 Fds.cpp:337-347 direct form)
 * and what nesdev specifies ("the '1' bit at the end of the gap is included").
 * Mesen 0.9.9 never raises the bad-CRC status bit (FDS.cpp:430, _badCrc is
 * never set), so its constant $4D $62 is never checked; NES_FDS_CRC_MESEN
 * reproduces it byte for byte, NES_FDS_CRC_COMPUTED writes the real CRC.
 *
 * File amount (block 2) versus hidden files: the BIOS LoadFiles ($E1F8)
 * reads the amount into $06 at $E484-$E48C and visits exactly that many
 * header/data pairs ($E224-$E231: DEC $06 / BNE); it never looks past them.
 * Files beyond the amount are loaded only by games with their own loader
 * (nesdev: copy protection by a hard-coded higher amount). The parser walks
 * every block until a code that is not a file header and marks files at
 * index >= amount hidden. */
#ifndef NES_FDS_H
#define NES_FDS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nes_cart.h"

#define NES_FDS_SIDE_BYTES      65500u
#define NES_FDS_QD_SIDE_BYTES   65536u
#define NES_FDS_HEADER_BYTES    16u
#define NES_FDS_LEAD_IN_BYTES   (28300u / 8u)
#define NES_FDS_BLOCK_GAP_BYTES (976u / 8u)
#define NES_FDS_GAP_MARK        0x80u
#define NES_FDS_INFO_BYTES      56u
#define NES_FDS_AMOUNT_BYTES    2u
#define NES_FDS_FILE_HDR_BYTES  16u

typedef enum { NES_FDS_NONE, NES_FDS_FWNES, NES_FDS_RAW, NES_FDS_QD } NesFdsFormat;
typedef enum { NES_FDS_OK, NES_FDS_ERR_NOT_FDS, NES_FDS_ERR_NO_SIDES } NesFdsError;
typedef enum { NES_FDS_PROFILE_MESEN099, NES_FDS_PROFILE_MESEN2 } NesFdsProfile;
typedef enum { NES_FDS_CRC_COMPUTED, NES_FDS_CRC_MESEN } NesFdsCrc;

/* NesFdsImage.flags */
#define NES_FDS_IMG_TRUNCATED 1u  /* fewer bytes than the declared sides; missing bytes read as zero */
#define NES_FDS_IMG_TRAILING  2u  /* bytes after the last whole side, ignored */

typedef struct {
    const uint8_t *image;
    size_t size, data_offset;     /* data_offset: 16 for fwNES, else 0 */
    uint32_t side_bytes;          /* 65500, or 65536 for .qd */
    unsigned sides, flags;
    NesFdsFormat format;
    NesFdsError error;
} NesFdsImage;

/* NesFdsSide.flags. The low byte is defects; the last two are information.
 * TAIL_DATA: the walk ended at a nonzero byte that is not a file header. In
 * the owner library (No-Intro verified) 43 of 198 sides end that way, all with
 * 05 DB B6 6D DB B6 ... (18 bytes) or a lone FF after the last file: bytes on
 * the disk that no file claims, not damage. Both Mesens stop there as at a
 * zero (0.9.9 drops them, Mesen2 streams $80 and the rest verbatim). When such
 * a byte cuts the file table short, MISSING_FILES reports it. */
#define NES_FDS_SIDE_NO_INFO       0x001u /* side does not begin with block 1 */
#define NES_FDS_SIDE_BAD_SIGNATURE 0x002u /* block 1 lacks *NINTENDO-HVC* (the BIOS rejects it) */
#define NES_FDS_SIDE_NO_AMOUNT     0x004u /* block 1 is not followed by block 2 */
#define NES_FDS_SIDE_NO_DATA       0x008u /* a file header is not followed by block 4 */
#define NES_FDS_SIDE_OVERRUN       0x010u /* a block extends past the end of the side */
#define NES_FDS_SIDE_TRUNCATED     0x020u /* a block extends past the end of the image */
#define NES_FDS_SIDE_MISSING_FILES 0x040u /* fewer files on the side than the file amount */
#define NES_FDS_SIDE_BAD_CRC       0x080u /* .qd: a stored block CRC is wrong */
#define NES_FDS_SIDE_HIDDEN_FILES  0x100u /* files past the file amount */
#define NES_FDS_SIDE_TAIL_DATA     0x200u /* nonzero, unclaimed bytes after the file table */
#define NES_FDS_SIDE_DEFECTS       0x0FFu

typedef struct {
    uint8_t raw[NES_FDS_INFO_BYTES];  /* block 1 including its code */
    char game_name[4];
    uint8_t licensee, game_type, revision, side_number, disk_number, disk_type;
    uint8_t boot_file_id, country, rewrite_count, actual_side, disk_type_other, disk_version;
    uint8_t mfg_date[3], rewrite_date[3];  /* BCD, as stored */
    uint8_t writer_serial[2];         /* as stored; byte order undocumented */
} NesFdsDiskInfo;

typedef struct {
    unsigned index;               /* position in the walk */
    uint8_t number, id, type;     /* type 0 PRG-RAM, 1 CHR-RAM, 2 nametable (VRAM) */
    char name[9];                 /* 8 bytes as stored, NUL-terminated */
    uint16_t load_addr, size;
    uint32_t header_pos, data_pos;  /* side offsets of the block 3 and block 4 codes */
    const uint8_t *data;          /* size bytes, NULL unless wholly inside the side and image */
    bool hidden, has_data;
} NesFdsFile;

typedef struct {
    NesFdsDiskInfo info;
    unsigned file_amount, file_count, flags;
    uint32_t end_pos;             /* side offset where the walk stopped */
    uint8_t end_code;             /* byte found there (0 on a clean side) */
} NesFdsSide;

typedef struct {
    const NesFdsImage *img;
    unsigned side, index, file_amount, flags;
    uint32_t pos;
    bool done;
} NesFdsWalk;

/* CRC-16/KERMIT, one byte at a time. */
static inline uint16_t nes_fds_crc16(uint16_t crc, const uint8_t *data, size_t len)
{
    while (len--) {
        crc ^= *data++;
        for (int k = 0; k < 8; ++k) crc = (uint16_t)((crc >> 1) ^ (0x8408u & (0u - (crc & 1u))));
    }
    return crc;
}

static inline bool nes_fds_is_raw_side(const uint8_t *p, size_t size)
{
    return size >= 15 && !memcmp(p, "\x01*NINTENDO-HVC*", 15);
}

/* .qd: block 1, its CRC, then block 2. The CRC must match, which a raw side
 * (block 2 at offset 56) cannot satisfy by accident except by chance 1/65536. */
static inline bool nes_fds_looks_qd(const uint8_t *p, size_t size)
{
    uint8_t mark = NES_FDS_GAP_MARK;
    if (size < NES_FDS_INFO_BYTES + 3 || !nes_fds_is_raw_side(p, size) || p[NES_FDS_INFO_BYTES + 2] != 2) return false;
    uint16_t crc = nes_fds_crc16(nes_fds_crc16(0, &mark, 1), p, NES_FDS_INFO_BYTES);
    return (p[NES_FDS_INFO_BYTES] | p[NES_FDS_INFO_BYTES + 1] << 8) == crc;
}

/* Detect and frame an image. hint NES_FDS_NONE detects; NES_FDS_QD forces
 * the .qd layout (Mesen2 chooses it by the .qd extension alone). */
static inline bool nes_fds_image(const uint8_t *image, size_t size, NesFdsFormat hint, NesFdsImage *img)
{
    memset(img, 0, sizeof(*img));
    img->image = image; img->size = size; img->error = NES_FDS_ERR_NOT_FDS;
    if (!image) return false;
    uint64_t need;
    if (size >= NES_FDS_HEADER_BYTES && !memcmp(image, "FDS\x1a", 4) && hint != NES_FDS_QD) {
        img->format = NES_FDS_FWNES; img->data_offset = NES_FDS_HEADER_BYTES;
        img->side_bytes = NES_FDS_SIDE_BYTES; img->sides = image[4];
        need = NES_FDS_HEADER_BYTES + (uint64_t)img->sides * img->side_bytes;
    } else if (nes_fds_is_raw_side(image, size) || (hint == NES_FDS_QD && size)) {
        bool qd = hint == NES_FDS_QD || (hint == NES_FDS_NONE && nes_fds_looks_qd(image, size));
        img->format = qd ? NES_FDS_QD : NES_FDS_RAW;
        img->side_bytes = qd ? NES_FDS_QD_SIDE_BYTES : NES_FDS_SIDE_BYTES;
        img->sides = (unsigned)(size / img->side_bytes);
        need = (uint64_t)img->sides * img->side_bytes;
    } else return false;
    if (size < need) img->flags |= NES_FDS_IMG_TRUNCATED;
    if (size > need) img->flags |= NES_FDS_IMG_TRAILING;
    if (!img->sides) { img->error = NES_FDS_ERR_NO_SIDES; return false; }
    img->error = NES_FDS_OK;
    return true;
}

static inline const char *nes_fds_format_name(NesFdsFormat f)
{
    return f == NES_FDS_FWNES ? "fwnes" : f == NES_FDS_RAW ? "raw" : f == NES_FDS_QD ? "qd" : "none";
}

/* Image offset of a side position, or -1 outside the image's side data. */
static inline int64_t nes_fds_offset(const NesFdsImage *img, unsigned side, int64_t pos)
{
    int64_t off = (int64_t)img->data_offset + (int64_t)side * img->side_bytes + pos;
    return off < (int64_t)img->data_offset || off >= (int64_t)img->size ? -1 : off;
}

/* A side byte; zero outside the image. bounded: also zero outside this side
 * (Mesen2's read()); unbounded reads run on into the next side (Mesen 0.9.9
 * reads one contiguous file buffer). */
static inline uint8_t nes_fds_byte(const NesFdsImage *img, unsigned side, int64_t pos, bool bounded)
{
    if (bounded && (pos < 0 || pos >= (int64_t)img->side_bytes)) return 0;
    int64_t off = nes_fds_offset(img, side, pos);
    return off < 0 ? 0 : img->image[off];
}

/* The byte stream the drive clocks out for one side (see the header comment).
 * Writes at most cap bytes and returns the full length, so a NULL/0 call sizes
 * the buffer. The length is at least the side size (zero padding). .qd blocks
 * keep their stored CRCs whatever crc_mode says. */
static inline size_t nes_fds_side_stream(const NesFdsImage *img, unsigned side, NesFdsProfile profile,
                                         NesFdsCrc crc_mode, uint8_t *out, size_t cap)
{
    size_t n = 0;
#define NES_FDS_PUT(b) do { if (n < cap) out[n] = (uint8_t)(b); ++n; } while (0)
    if (side >= img->sides) return 0;
    const bool qd = img->format == NES_FDS_QD, m2 = profile == NES_FDS_PROFILE_MESEN2;
    const int64_t side_bytes = img->side_bytes;
    for (unsigned i = 0; i < NES_FDS_LEAD_IN_BYTES; ++i) NES_FDS_PUT(0);
    for (int64_t j = 0; j < side_bytes;) {
        uint32_t len;
        switch (nes_fds_byte(img, side, j, m2)) {
        case 1: len = NES_FDS_INFO_BYTES; break;
        case 2: len = NES_FDS_AMOUNT_BYTES; break;
        case 3: len = NES_FDS_FILE_HDR_BYTES; break;
        case 4: {
            int back = qd ? 5 : 3;
            len = 1u + (nes_fds_byte(img, side, j - back, m2) | nes_fds_byte(img, side, j - back + 1, m2) << 8);
            break;
        }
        default:
            if (m2) {
                NES_FDS_PUT(NES_FDS_GAP_MARK);
                for (int64_t k = j; k < side_bytes; ++k) NES_FDS_PUT(nes_fds_byte(img, side, k, true));
            }
            goto done;
        }
        if (qd) len += 2;
        if (m2) { if (j + len >= side_bytes) goto done; }
        /* Mesen 0.9.9 would read past the end of the file here (undefined). */
        else if (nes_fds_offset(img, side, j + len - 1) < 0) goto done;
        uint8_t mark = NES_FDS_GAP_MARK;
        uint16_t crc = nes_fds_crc16(0, &mark, 1);
        NES_FDS_PUT(mark);
        for (uint32_t k = 0; k < len; ++k) {
            uint8_t b = nes_fds_byte(img, side, j + k, m2);
            crc = nes_fds_crc16(crc, &b, 1);
            NES_FDS_PUT(b);
        }
        if (!qd) {
            if (crc_mode == NES_FDS_CRC_MESEN) { NES_FDS_PUT(0x4D); NES_FDS_PUT(0x62); }
            else { NES_FDS_PUT(crc & 255); NES_FDS_PUT(crc >> 8); }
        }
        for (unsigned i = 0; i < NES_FDS_BLOCK_GAP_BYTES; ++i) NES_FDS_PUT(0);
        j += len;
    }
done:
    while (n < (size_t)side_bytes) NES_FDS_PUT(0);
#undef NES_FDS_PUT
    return n;
}

/* ---- File table ----------------------------------------------------------
 * The walk never leaves its side. Positions are side offsets. */

/* A block of len bytes (plus its CRC on .qd) at pos; false with flags set when
 * it is not wholly on the side or in the image. */
static inline bool nes_fds_block_fits(NesFdsWalk *w, uint32_t pos, uint32_t len)
{
    uint32_t total = len + (w->img->format == NES_FDS_QD ? 2u : 0u);
    if ((uint64_t)pos + total > w->img->side_bytes) { w->flags |= NES_FDS_SIDE_OVERRUN; return false; }
    if (nes_fds_offset(w->img, w->side, (int64_t)pos + total - 1) < 0) { w->flags |= NES_FDS_SIDE_TRUNCATED; return false; }
    return true;
}

/* Checks a .qd block's stored CRC; returns the side offset after the block. */
static inline uint32_t nes_fds_block_end(NesFdsWalk *w, uint32_t pos, uint32_t len)
{
    if (w->img->format != NES_FDS_QD) return pos + len;
    const uint8_t *p = w->img->image + nes_fds_offset(w->img, w->side, pos);
    uint8_t mark = NES_FDS_GAP_MARK;
    uint16_t crc = nes_fds_crc16(nes_fds_crc16(0, &mark, 1), p, len);
    if ((p[len] | p[len + 1] << 8) != crc) w->flags |= NES_FDS_SIDE_BAD_CRC;
    return pos + len + 2;
}

static inline uint8_t nes_fds_walk_byte(const NesFdsWalk *w, uint32_t pos)
{
    return nes_fds_byte(w->img, w->side, pos, true);
}

/* Parses blocks 1 and 2 of a side and positions w at its first file. */
static inline bool nes_fds_side_begin(const NesFdsImage *img, unsigned side, NesFdsSide *s, NesFdsWalk *w)
{
    memset(s, 0, sizeof(*s));
    memset(w, 0, sizeof(*w));
    w->img = img; w->side = side; w->done = true;
    if (side >= img->sides) return false;
    if (nes_fds_walk_byte(w, 0) != 1) { w->flags |= NES_FDS_SIDE_NO_INFO; s->end_code = nes_fds_walk_byte(w, 0); goto out; }
    if (!nes_fds_block_fits(w, 0, NES_FDS_INFO_BYTES)) goto out;
    {
        const uint8_t *b = img->image + nes_fds_offset(img, side, 0);
        NesFdsDiskInfo *d = &s->info;
        memcpy(d->raw, b, NES_FDS_INFO_BYTES);
        if (memcmp(b + 1, "*NINTENDO-HVC*", 14)) w->flags |= NES_FDS_SIDE_BAD_SIGNATURE;
        d->licensee = b[0x0F]; memcpy(d->game_name, b + 0x10, 3); d->game_name[3] = 0;
        d->game_type = b[0x13]; d->revision = b[0x14]; d->side_number = b[0x15];
        d->disk_number = b[0x16]; d->disk_type = b[0x17]; d->boot_file_id = b[0x19];
        memcpy(d->mfg_date, b + 0x1F, 3); d->country = b[0x22];
        memcpy(d->rewrite_date, b + 0x2C, 3); memcpy(d->writer_serial, b + 0x31, 2);
        d->rewrite_count = b[0x34]; d->actual_side = b[0x35];
        d->disk_type_other = b[0x36]; d->disk_version = b[0x37];
    }
    w->pos = nes_fds_block_end(w, 0, NES_FDS_INFO_BYTES);
    if (nes_fds_walk_byte(w, w->pos) != 2) { w->flags |= NES_FDS_SIDE_NO_AMOUNT; s->end_code = nes_fds_walk_byte(w, w->pos); goto out; }
    if (!nes_fds_block_fits(w, w->pos, NES_FDS_AMOUNT_BYTES)) goto out;
    w->file_amount = s->file_amount = nes_fds_walk_byte(w, w->pos + 1);
    w->pos = nes_fds_block_end(w, w->pos, NES_FDS_AMOUNT_BYTES);
    w->done = false;
out:
    s->flags = w->flags; s->end_pos = w->pos;
    return !w->done;
}

/* Ends a walk: a side that stops before its file amount is missing files. */
static inline void nes_fds_walk_end(NesFdsWalk *w)
{
    if (w->index < w->file_amount) w->flags |= NES_FDS_SIDE_MISSING_FILES;
    w->done = true;
}

/* Next file header (and its data block) on the side. At the end, w->flags
 * holds the side's defects and w->pos where the walk stopped. A header whose
 * data block is missing or cut short is still returned, with has_data false. */
static inline bool nes_fds_next_file(NesFdsWalk *w, NesFdsFile *f)
{
    memset(f, 0, sizeof(*f));
    if (w->done) return false;
    uint8_t code = nes_fds_walk_byte(w, w->pos);
    if (code != 3) {
        if (code) w->flags |= NES_FDS_SIDE_TAIL_DATA;
        nes_fds_walk_end(w);
        return false;
    }
    if (!nes_fds_block_fits(w, w->pos, NES_FDS_FILE_HDR_BYTES)) { nes_fds_walk_end(w); return false; }
    const uint8_t *h = w->img->image + nes_fds_offset(w->img, w->side, w->pos);
    f->index = w->index; f->number = h[1]; f->id = h[2];
    memcpy(f->name, h + 3, 8); f->name[8] = 0;
    f->load_addr = (uint16_t)(h[11] | h[12] << 8);
    f->size = (uint16_t)(h[13] | h[14] << 8);
    f->type = h[15];
    f->header_pos = w->pos;
    f->hidden = w->index >= w->file_amount;
    if (f->hidden) w->flags |= NES_FDS_SIDE_HIDDEN_FILES;
    w->pos = nes_fds_block_end(w, w->pos, NES_FDS_FILE_HDR_BYTES);
    w->index++;
    if (nes_fds_walk_byte(w, w->pos) != 4) {
        w->flags |= NES_FDS_SIDE_NO_DATA;
        nes_fds_walk_end(w);
        return true;
    }
    f->data_pos = w->pos;
    if (!nes_fds_block_fits(w, w->pos, 1u + f->size)) { nes_fds_walk_end(w); return true; }
    f->has_data = true;
    f->data = w->img->image + nes_fds_offset(w->img, w->side, w->pos + 1);
    w->pos = nes_fds_block_end(w, w->pos, 1u + f->size);
    return true;
}

/* Walks the whole side: disk info, file amount, file count and defects. */
static inline bool nes_fds_side(const NesFdsImage *img, unsigned side, NesFdsSide *s)
{
    NesFdsWalk w;
    NesFdsFile f;
    if (nes_fds_side_begin(img, side, s, &w))
        while (nes_fds_next_file(&w, &f)) s->file_count++;
    s->flags = w.flags;
    s->end_pos = w.pos;
    if (!s->end_code) s->end_code = w.pos < img->side_bytes ? nes_fds_byte(img, side, w.pos, true) : 0;
    return !(s->flags & NES_FDS_SIDE_DEFECTS);
}

/* ---- The RAM Adapter as a cartridge ---------------------------------------
 * The RAM Adapter is iNES mapper 20: 32 KiB of PRG RAM at $6000-$DFFF, the
 * 8 KiB BIOS ROM at $E000-$FFFF and 8 KiB of CHR RAM, nametable arrangement
 * from $4025 (Mesen FdsLoader.cpp:141 powers it on vertical). The compiler
 * (which compiles the BIOS) and the runtime describe it with the same
 * NesCartInfo, so nes_cart_identity() ties a generated program to the board.
 * The only BIOS with a recorded identity is the Japanese RAM Adapter's
 * disksys.rom (bios/disksys.toml in the FDS game repositories). */
#define NES_FDS_MAPPER        20u
#define NES_FDS_BIOS_BYTES    8192u
#define NES_FDS_BIOS_CRC32    0x5E607DCFu
#define NES_FDS_PRG_RAM_BYTES 32768u
#define NES_FDS_CHR_RAM_BYTES 8192u

static inline void nes_fds_cart_info(NesCartInfo *c)
{
    memset(c, 0, sizeof(*c));
    c->mapper = NES_FDS_MAPPER;
    c->prg_size = NES_FDS_BIOS_BYTES;
    c->prg_ram = NES_FDS_PRG_RAM_BYTES;
    c->chr_ram = NES_FDS_CHR_RAM_BYTES;
    c->vertical = 1;
}

/* A BIOS image is accepted only with the expected size and CRC32. */
static inline bool nes_fds_bios_matches(const uint8_t *bios, size_t size, uint32_t expected_crc32)
{
    return bios && size == NES_FDS_BIOS_BYTES && nes_crc32(0, bios, size) == expected_crc32;
}

static inline const char *nes_fds_file_type_name(uint8_t type)
{
    return type == 0 ? "PRG" : type == 1 ? "CHR" : type == 2 ? "NT" : "?";
}
#endif
