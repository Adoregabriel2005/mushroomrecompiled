/* Famicom Disk System media contracts (common/nes_fds.h) over the synthetic
 * images written by tools/cyc/fds_fixtures.py. Usage: cyc_fds_disk_test <dir> */
#include "../../common/nes_fds.h"
#include "../../common/nes_fds_save.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)

static const char *dir;

static uint8_t *load(const char *name, size_t *size)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "missing fixture %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
    CHECK(buf && fread(buf, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !strcmp(s + a - b, suffix);
}

/* Opens a fixture the way the tools do: .qd by extension, else detection. */
static uint8_t *open_image(const char *name, NesFdsImage *img)
{
    size_t size;
    uint8_t *data = load(name, &size);
    CHECK(nes_fds_image(data, size, ends_with(name, ".qd") ? NES_FDS_QD : NES_FDS_NONE, img));
    return data;
}

static uint8_t *stream(const NesFdsImage *img, unsigned side, NesFdsProfile p, NesFdsCrc c, size_t *len)
{
    *len = nes_fds_side_stream(img, side, p, c, NULL, 0);
    uint8_t *out = (uint8_t *)malloc(*len);
    CHECK(out && nes_fds_side_stream(img, side, p, c, out, *len) == *len);
    return out;
}

static bool same_stream(const NesFdsImage *a, unsigned sa, const NesFdsImage *b, unsigned sb, NesFdsProfile p, NesFdsCrc c)
{
    size_t la, lb;
    uint8_t *x = stream(a, sa, p, c, &la), *y = stream(b, sb, p, c, &lb);
    bool same = la == lb && !memcmp(x, y, la);
    free(x); free(y);
    return same;
}

/* Every file on a side, in walk order; returns the side summary. */
static unsigned walk(const NesFdsImage *img, unsigned side, NesFdsSide *s, NesFdsFile *files, unsigned max)
{
    NesFdsWalk w;
    NesFdsFile f;
    unsigned n = 0;
    if (nes_fds_side_begin(img, side, s, &w))
        while (nes_fds_next_file(&w, &f)) { CHECK(n < max); files[n++] = f; }
    NesFdsSide full;
    nes_fds_side(img, side, &full);
    CHECK(full.flags == w.flags && full.file_count == n && full.end_pos == w.pos);
    s->flags = w.flags; s->file_count = n; s->end_pos = w.pos;
    return n;
}

/* Mesen 0.9.9's drive CRC (FDS.cpp:330-342): bits enter at the top, so two
 * zero bytes flush the register (FDS.cpp:303-306). */
static uint16_t mesen099_crc(uint16_t acc, uint8_t value)
{
    for (unsigned n = 1; n <= 0x80; n <<= 1) {
        uint8_t carry = acc & 1;
        acc >>= 1;
        if (carry) acc ^= 0x8408;
        if (value & n) acc ^= 0x8000;
    }
    return acc;
}

static void crc_contracts(void)
{
    CHECK(nes_fds_crc16(0, (const uint8_t *)"123456789", 9) == 0x2189);   /* CRC-16/KERMIT check value */
    uint8_t block[300];
    for (unsigned len = 0; len < sizeof(block); ++len) {
        for (unsigned i = 0; i < len; ++i) block[i] = (uint8_t)(i * 37 + len);
        uint16_t direct = nes_fds_crc16(0, block, len), aug = 0;
        for (unsigned i = 0; i < len; ++i) aug = mesen099_crc(aug, block[i]);
        aug = mesen099_crc(mesen099_crc(aug, 0), 0);
        CHECK(aug == direct);                    /* both Mesen drives write the same CRC */
        uint8_t tail[2] = { (uint8_t)direct, (uint8_t)(direct >> 8) };
        CHECK(nes_fds_crc16(direct, tail, 2) == 0);   /* the residue a reading drive checks */
    }
    /* Zero gap bytes leave a zero register: the gap is transparent. */
    uint8_t zeros[16] = {0};
    CHECK(nes_fds_crc16(0, zeros, sizeof(zeros)) == 0);
}

/* The computed-CRC stream parses back into gap, $80, block, CRC runs whose
 * residue is zero, starting after the 3537-byte lead-in. */
static unsigned check_stream_blocks(const uint8_t *s, size_t len)
{
    size_t i = NES_FDS_LEAD_IN_BYTES;
    unsigned blocks = 0;
    for (size_t k = 0; k < i; ++k) CHECK(s[k] == 0);
    uint16_t size = 0;
    while (i < len && s[i] == NES_FDS_GAP_MARK) {
        uint8_t code = s[i + 1];
        size_t n = code == 1 ? 56 : code == 2 ? 2 : code == 3 ? 16 : code == 4 ? 1u + size : 0;
        CHECK(n && i + 1 + n + 2 <= len);
        if (code == 3) size = (uint16_t)(s[i + 1 + 13] | s[i + 1 + 14] << 8);
        CHECK(nes_fds_crc16(0, s + i, 1 + n + 2) == 0);
        i += 1 + n + 2;
        for (size_t k = 0; k < NES_FDS_BLOCK_GAP_BYTES; ++k) CHECK(s[i + k] == 0);
        i += NES_FDS_BLOCK_GAP_BYTES;
        blocks++;
    }
    for (; i < len; ++i) CHECK(s[i] == 0);
    return blocks;
}

static unsigned manifest_streams(void)
{
    size_t size;
    uint8_t *text = load("manifest.txt", &size);
    char *line = (char *)realloc(text, size + 1);
    CHECK(line);
    line[size] = 0;
    unsigned count = 0;
    for (char *p = line; *p;) {
        char *next = strchr(p, '\n');
        if (next) *next++ = 0; else next = p + strlen(p);
        char image[256], profile[16], crc[16], expected[512];
        unsigned side;
        if (*p) {
            CHECK(sscanf(p, "stream %255s %u %15s %15s %511s", image, &side, profile, crc, expected) == 5);
            NesFdsImage img;
            uint8_t *data = open_image(image, &img);
            size_t want_len, got_len;
            uint8_t *want = load(expected, &want_len);
            uint8_t *got = stream(&img, side, !strcmp(profile, "mesen2") ? NES_FDS_PROFILE_MESEN2 : NES_FDS_PROFILE_MESEN099,
                                  !strcmp(crc, "mesen") ? NES_FDS_CRC_MESEN : NES_FDS_CRC_COMPUTED, &got_len);
            if (got_len != want_len || memcmp(got, want, got_len)) {
                size_t at = 0;
                while (at < got_len && at < want_len && got[at] == want[at]) ++at;
                fprintf(stderr, "%s: stream length %zu vs %zu, first difference at %zu\n", p, got_len, want_len, at);
                exit(1);
            }
            free(got); free(want); free(data);
            count++;
        }
        p = next;
    }
    free(line);
    return count;
}

/* Disk saves (common/nes_fds_save.h): identity, the save file, Mesen's .ips. */
static unsigned save_contracts(void)
{
    unsigned n = 0;
    NesFdsImage hdr, raw, other;
    uint8_t *h = open_image("two_side.fds", &hdr), *r = open_image("two_side_raw.fds", &raw);
    uint8_t *o = open_image("basic.fds", &other);
    NesFdsIdentity ih, ir, io;
    nes_fds_identity(&hdr, &ih); nes_fds_identity(&raw, &ir); nes_fds_identity(&other, &io);
    CHECK(nes_fds_identity_equal(&ih, &ir) && !nes_fds_identity_equal(&ih, &io)); n++;   /* the header is not the disk */
    CHECK(ih.sides == 2 && ih.side_bytes == NES_FDS_SIDE_BYTES && ih.bytes == 2 * NES_FDS_SIDE_BYTES); n++;
    /* a truncated image's identity counts the missing bytes as zero */
    NesFdsImage t;
    uint8_t *tr = open_image("truncated.fds", &t);
    NesFdsIdentity it;
    nes_fds_identity(&t, &it);
    CHECK(it.bytes == 2 * NES_FDS_SIDE_BYTES); n++;
    /* write -> parse round trip */
    size_t l0, l1;
    uint8_t *s0 = stream(&hdr, 0, NES_FDS_PROFILE_MESEN099, NES_FDS_CRC_COMPUTED, &l0);
    uint8_t *s1 = stream(&hdr, 1, NES_FDS_PROFILE_MESEN2, NES_FDS_CRC_MESEN, &l1);
    static NesFdsSave sv, back;
    memset(&sv, 0, sizeof(sv));
    sv.id = ih; sv.layout = 1; sv.crc_mode = 1; sv.write_at = 2; sv.count = 2;
    sv.sides[0].side = 0; sv.sides[0].bytes = s0; sv.sides[0].len = (uint32_t)l0;
    sv.sides[1].side = 1; sv.sides[1].bytes = s1; sv.sides[1].len = (uint32_t)l1;
    size_t size = nes_fds_save_write(&sv, NULL, 0);
    CHECK(size == NES_FDS_SAVE_HEADER + 4 + 24 + l0 + l1); n++;
    uint8_t *file = (uint8_t *)malloc(size);
    CHECK(nes_fds_save_write(&sv, file, size) == size); n++;
    CHECK(nes_fds_save_parse(file, size, &ir, &back) == NES_FDS_SAVE_OK); n++;   /* the raw twin accepts it */
    CHECK(back.count == 2 && back.sides[0].len == l0 && !memcmp(back.sides[0].bytes, s0, l0) &&
          back.sides[1].side == 1 && back.sides[1].len == l1 && !memcmp(back.sides[1].bytes, s1, l1) &&
          back.layout == 1 && back.crc_mode == 1 && back.write_at == 2); n++;
    CHECK(nes_fds_save_parse(file, size, &io, &back) == NES_FDS_SAVE_FOREIGN); n++;
    CHECK(nes_fds_save_parse(file, size, NULL, &back) == NES_FDS_SAVE_OK); n++;
    /* single-bit errors (every bit near both ends, a spread in between) and
     * truncations are caught: the file CRC-32 */
    for (size_t i = 0; i < size; i += (i < 64 || size - i < 64) ? 1 : 97) {
        for (int bit = 0; bit < 8; bit += (i < 64 ? 1 : 3)) {
            file[i] ^= (uint8_t)(1u << bit);
            CHECK(nes_fds_save_parse(file, size, &ih, &back) != NES_FDS_SAVE_OK); n++;
            file[i] ^= (uint8_t)(1u << bit);
        }
    }
    for (size_t cut = 0; cut < size; cut += cut < 128 ? 1 : 4099) {
        CHECK(nes_fds_save_parse(file, cut, &ih, &back) == NES_FDS_SAVE_CORRUPT); n++;
    }
    /* malformed records under a valid file CRC: side 0 twice, a side past the
     * disk, a reserved word set */
    for (unsigned k = 0; k < 3; ++k) {
        uint8_t *copy = (uint8_t *)malloc(size);
        memcpy(copy, file, size);
        if (k == 0) copy[NES_FDS_SAVE_HEADER + 12 + l0] = 0;
        else if (k == 1) copy[NES_FDS_SAVE_HEADER] = 7;
        else copy[36] = 1;
        nes_fds_wr32(copy + size - 4, nes_crc32(0, copy, size - 4));
        CHECK(nes_fds_save_parse(copy, size, &ih, &back) == NES_FDS_SAVE_CORRUPT); n++;
        free(copy);
    }
    nes_fds_wr32(file + 8, 2);
    nes_fds_wr32(file + size - 4, nes_crc32(0, file, size - 4));
    CHECK(nes_fds_save_parse(file, size, &ih, &back) == NES_FDS_SAVE_VERSION_NEWER); n++;
    sv.count = 0;
    uint8_t empty[64];
    CHECK(nes_fds_save_write(&sv, empty, sizeof(empty)) == NES_FDS_SAVE_HEADER + 4 &&
          nes_fds_save_parse(empty, NES_FDS_SAVE_HEADER + 4, &ih, &back) == NES_FDS_SAVE_OK && back.count == 0); n++;
    /* Mesen's rebuild undoes the 0.9.9 loader on a clean side (both CRC modes) */
    static const char *const CLEAN[] = { "basic.fds", "two_side.fds", "hidden.fds", "full.fds", "missing.fds" };
    static uint8_t side[NES_FDS_SIDE_BYTES];
    for (unsigned k = 0; k < 5; ++k) {
        NesFdsImage img;
        uint8_t *d = open_image(CLEAN[k], &img);
        for (unsigned sd = 0; sd < img.sides; ++sd)
            for (int c = 0; c < 2; ++c) {
                size_t len;
                uint8_t *st = stream(&img, sd, NES_FDS_PROFILE_MESEN099, (NesFdsCrc)c, &len);
                CHECK(nes_fds_rebuild_side(st, len, side));
                CHECK(!memcmp(side, d + img.data_offset + (size_t)sd * NES_FDS_SIDE_BYTES, NES_FDS_SIDE_BYTES)); n++;
                free(st);
            }
        free(d);
    }
    /* IPS: create then apply gives the new bytes; identical inputs give an empty patch */
    size_t len = hdr.size;
    uint8_t *mod = (uint8_t *)malloc(len);
    memcpy(mod, h, len);
    for (size_t i = 100; i < 140; ++i) mod[i] = 0x55;             /* a run: RLE */
    mod[5000] ^= 1; mod[5002] ^= 2; mod[len - 1] ^= 0x80;          /* scattered bytes, the last one */
    for (size_t i = 70000; i < 70300; ++i) mod[i] = (uint8_t)i;    /* literal bytes */
    size_t ips_n = nes_fds_ips_create(h, mod, len, NULL, 0);
    uint8_t *ips = (uint8_t *)malloc(ips_n), *patched = (uint8_t *)malloc(len);
    CHECK(nes_fds_ips_create(h, mod, len, ips, ips_n) == ips_n && !memcmp(ips, "PATCH", 5) &&
          !memcmp(ips + ips_n - 3, "EOF", 3)); n++;
    size_t out_n = 0;
    CHECK(nes_fds_ips_apply(ips, ips_n, h, len, patched, len, &out_n) && out_n == len && !memcmp(patched, mod, len)); n++;
    CHECK(nes_fds_ips_create(h, h, len, NULL, 0) == 8); n++;
    CHECK(!nes_fds_ips_apply(ips, ips_n - 1, h, len, NULL, 0, &out_n)); n++;   /* no EOF */
    CHECK(!nes_fds_ips_apply((const uint8_t *)"PATCX\0\0\0EOF", 11, h, len, NULL, 0, &out_n)); n++;
    free(ips); free(patched); free(mod);
    /* Mesen's record splitting, byte for byte: a run of 4 from a record's
     * start is RLE, a run of 3 stays literal, 14 equal bytes after another
     * split off into RLE, a run of 4 after other bytes stays literal
     * (expected bytes from a line-by-line Python transliteration of
     * IpsPatcher::CreatePatch, uint8_t rleCount and uint16_t Length). */
    {
        uint8_t a[64] = { 0 }, b[64] = { 0 }, got[64];
        memset(b + 10, 5, 4); memset(b + 20, 7, 3); b[30] = 1; memset(b + 31, 9, 15);
        b[50] = 2; b[51] = 3; memset(b + 52, 4, 4);
        static const uint8_t WANT[] = {
            0x50, 0x41, 0x54, 0x43, 0x48, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x04, 0x05, 0x00, 0x00, 0x14, 0x00,
            0x03, 0x07, 0x07, 0x07, 0x00, 0x00, 0x1E, 0x00, 0x01, 0x01, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x0F,
            0x09, 0x00, 0x00, 0x32, 0x00, 0x06, 0x02, 0x03, 0x04, 0x04, 0x04, 0x04, 0x45, 0x4F, 0x46,
        };
        CHECK(nes_fds_ips_create(a, b, 64, got, sizeof(got)) == sizeof(WANT) && !memcmp(got, WANT, sizeof(WANT))); n++;
        uint8_t back_b[64];
        CHECK(nes_fds_ips_apply(got, sizeof(WANT), a, 64, back_b, 64, &out_n) && out_n == 64 && !memcmp(back_b, b, 64)); n++;
    }
    /* a stream byte changed under a recomputed file CRC: the record CRC catches it;
     * and every byte of a stream reaches the file, the last one included */
    {
        s0[l0 - 1] = 0x5A;
        sv.id = ih; sv.count = 1;
        sv.sides[0].side = 0; sv.sides[0].bytes = s0; sv.sides[0].len = (uint32_t)l0;
        size_t fs = nes_fds_save_write(&sv, NULL, 0);
        uint8_t *f2 = (uint8_t *)malloc(fs);
        CHECK(nes_fds_save_write(&sv, f2, fs) == fs && nes_fds_save_parse(f2, fs, &ih, &back) == NES_FDS_SAVE_OK &&
              back.sides[0].bytes[l0 - 1] == 0x5A); n++;
        f2[NES_FDS_SAVE_HEADER + 12 + 1000] ^= 1;
        nes_fds_wr32(f2 + fs - 4, nes_crc32(0, f2, fs - 4));
        CHECK(nes_fds_save_parse(f2, fs, &ih, &back) == NES_FDS_SAVE_CORRUPT); n++;
        free(f2);
    }
    free(file); free(s0); free(s1); free(h); free(r); free(o); free(tr);
    return n;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    dir = argv[1];
    crc_contracts();

    NesFdsImage hdr, raw, qd, auto_qd;
    NesFdsSide s;
    NesFdsFile f[8];
    size_t n;
    uint8_t *d_hdr = open_image("basic.fds", &hdr), *d_raw = open_image("basic_raw.fds", &raw);
    uint8_t *d_qd = open_image("basic.qd", &qd);
    CHECK(hdr.format == NES_FDS_FWNES && hdr.sides == 1 && hdr.data_offset == 16 && !hdr.flags);
    CHECK(raw.format == NES_FDS_RAW && raw.sides == 1 && !raw.data_offset && !raw.flags);
    CHECK(qd.format == NES_FDS_QD && qd.sides == 1 && qd.side_bytes == 65536 && !qd.flags);
    CHECK(nes_fds_image(d_qd, qd.size, NES_FDS_NONE, &auto_qd) && auto_qd.format == NES_FDS_QD); /* detected by content */
    /* Disk info and file table, identically from all three forms. */
    const NesFdsImage *forms[3] = { &hdr, &raw, &qd };
    for (unsigned k = 0; k < 3; ++k) {
        CHECK(walk(forms[k], 0, &s, f, 8) == 3);
        CHECK(!s.flags && s.file_amount == 3 && !strcmp(s.info.game_name, "BAS"));
        CHECK(s.info.licensee == 1 && s.info.game_type == 0x20 && s.info.boot_file_id == 0x0F && s.info.country == 0x49);
        CHECK(s.info.mfg_date[0] == 0x61 && s.info.rewrite_count == 2 && s.info.writer_serial[0] == 0x12);
        CHECK(!strcmp(f[0].name, "KYODAKU-") && f[0].load_addr == 0x2800 && f[0].size == 0xE0 && f[0].type == 2);
        CHECK(!strcmp(f[1].name, "CHARDATA") && f[1].load_addr == 0 && f[1].size == 0x2000 && f[1].type == 1 && f[1].id == 1);
        CHECK(!strcmp(f[2].name, "MAINPRG ") && f[2].load_addr == 0x6000 && f[2].size == 0x100 && f[2].type == 0 && f[2].id == 0x0F);
        for (unsigned i = 0; i < 3; ++i) CHECK(f[i].has_data && f[i].data && !f[i].hidden && f[i].number == i);
    }
    CHECK(f[0].header_pos == 56 + 2 + 2 + 2 && f[0].data_pos == 62 + 18);          /* .qd positions count CRCs */
    { NesFdsFile g[8]; walk(&hdr, 0, &s, g, 8);
      CHECK(g[0].header_pos == 58 && g[0].data_pos == 74 && g[1].header_pos == 74 + 1 + 0xE0);
      CHECK(!memcmp(g[2].data, f[2].data, 0x100) && !memcmp(g[1].data, f[1].data, 0x2000)); }
    /* Headered and raw forms stream identically, in every profile and CRC mode. */
    for (int p = 0; p < 2; ++p)
        for (int c = 0; c < 2; ++c) CHECK(same_stream(&hdr, 0, &raw, 0, (NesFdsProfile)p, (NesFdsCrc)c));
    { NesFdsImage h2, r2; uint8_t *a = open_image("two_side.fds", &h2), *b = open_image("two_side_raw.fds", &r2);
      CHECK(h2.sides == 2 && r2.sides == 2 && !h2.flags && !r2.flags);
      for (unsigned side = 0; side < 2; ++side)
          for (int p = 0; p < 2; ++p)
              for (int c = 0; c < 2; ++c) CHECK(same_stream(&h2, side, &r2, side, (NesFdsProfile)p, (NesFdsCrc)c));
      CHECK(walk(&h2, 1, &s, f, 8) == 3 && s.info.side_number == 1 && s.info.actual_side == 1 && !s.flags);
      CHECK(f[2].type == 2 && f[2].size == 0x400 && !strcmp(f[2].name, "SIDEB-NT"));
      CHECK(!same_stream(&h2, 0, &h2, 1, NES_FDS_PROFILE_MESEN099, NES_FDS_CRC_COMPUTED));
      free(a); free(b); }
    /* The .qd's stored CRCs (from the fixture's own CRC code) equal the ones
     * computed for the fwNES form: same stream up to 65500, then padding. */
    { size_t lq, lf; uint8_t *sq = stream(&qd, 0, NES_FDS_PROFILE_MESEN099, NES_FDS_CRC_COMPUTED, &lq);
      uint8_t *sf = stream(&hdr, 0, NES_FDS_PROFILE_MESEN099, NES_FDS_CRC_COMPUTED, &lf);
      CHECK(lf == 65500 && lq == 65536 && !memcmp(sq, sf, lf));
      for (size_t i = lf; i < lq; ++i) CHECK(sq[i] == 0);
      CHECK(check_stream_blocks(sf, lf) == 2 + 2 * 3);
      free(sq);
      /* Mesen's constant differs from the CRC, in the same place. */
      uint8_t *sm = stream(&hdr, 0, NES_FDS_PROFILE_MESEN099, NES_FDS_CRC_MESEN, &n);
      CHECK(n == lf && sm[NES_FDS_LEAD_IN_BYTES] == 0x80 && sm[NES_FDS_LEAD_IN_BYTES + 1] == 1);
      CHECK(sm[NES_FDS_LEAD_IN_BYTES + 57] == 0x4D && sm[NES_FDS_LEAD_IN_BYTES + 58] == 0x62);
      CHECK(!memcmp(sm, sf, NES_FDS_LEAD_IN_BYTES + 57) && sf[NES_FDS_LEAD_IN_BYTES + 57] != 0x4D);
      /* Mesen2 marks the end of data with $80 and streams the zero rest. */
      uint8_t *s2 = stream(&hdr, 0, NES_FDS_PROFILE_MESEN2, NES_FDS_CRC_MESEN, &n);
      size_t used = NES_FDS_LEAD_IN_BYTES + 8 * (1 + 2 + NES_FDS_BLOCK_GAP_BYTES) + 56 + 2 + 3 * 16 + 3 + 0xE0 + 0x2000 + 0x100;
      CHECK(!memcmp(sm, s2, used) && sm[used] == 0 && s2[used] == 0x80);
      free(sm); free(s2); free(sf); }
    /* Hidden files: walked, marked, and not a defect. */
    { NesFdsImage img; uint8_t *d = open_image("hidden.fds", &img);
      CHECK(walk(&img, 0, &s, f, 8) == 4 && s.file_amount == 2 && s.flags == NES_FDS_SIDE_HIDDEN_FILES);
      CHECK(!f[1].hidden && f[2].hidden && f[3].hidden && !strcmp(f[3].name, "HIDDEN  "));
      CHECK(nes_fds_side(&img, 0, &s));
      size_t len; uint8_t *st = stream(&img, 0, NES_FDS_PROFILE_MESEN099, NES_FDS_CRC_COMPUTED, &len);
      CHECK(check_stream_blocks(st, len) == 2 + 2 * 4);   /* Mesen streams hidden files too */
      free(st); free(d); }
    { NesFdsImage img; uint8_t *d = open_image("missing.fds", &img);
      CHECK(walk(&img, 0, &s, f, 8) == 3 && s.flags == NES_FDS_SIDE_MISSING_FILES && !nes_fds_side(&img, 0, &s));
      free(d); }
    /* Unclaimed bytes after the table are information; a stray code that cuts
     * the table short is a defect through MISSING_FILES. */
    { NesFdsImage img; uint8_t *d = open_image("tail_data.fds", &img);
      CHECK(walk(&img, 0, &s, f, 8) == 2 && s.flags == NES_FDS_SIDE_TAIL_DATA);
      CHECK(nes_fds_side(&img, 0, &s) && s.end_code == 0x05);
      size_t a, b; uint8_t *s099 = stream(&img, 0, NES_FDS_PROFILE_MESEN099, NES_FDS_CRC_COMPUTED, &a);
      uint8_t *s2 = stream(&img, 0, NES_FDS_PROFILE_MESEN2, NES_FDS_CRC_COMPUTED, &b);
      CHECK(check_stream_blocks(s099, a) == 2 + 2 * 2 && a == 65500 && b > 65500);   /* 0.9.9 drops the bytes */
      size_t at = b - (65500 - s.end_pos) - 1;                                         /* Mesen2: $80 + side rest */
      CHECK(s2[at] == 0x80 && s2[at + 1] == 0x05 && s2[at + 2] == 0xDB);
      free(s099); free(s2); free(d);
      d = open_image("cut_short.fds", &img);
      CHECK(walk(&img, 0, &s, f, 8) == 2 && s.flags == (NES_FDS_SIDE_TAIL_DATA | NES_FDS_SIDE_MISSING_FILES));
      CHECK(!nes_fds_side(&img, 0, &s) && s.end_code == 0x07);
      free(d); }
    { NesFdsImage img; uint8_t *d = open_image("no_data.fds", &img);
      CHECK(walk(&img, 0, &s, f, 8) == 1 && s.flags == (NES_FDS_SIDE_NO_DATA | NES_FDS_SIDE_MISSING_FILES));
      CHECK(!f[0].has_data && !f[0].data && !strcmp(f[0].name, "ORPHAN  "));
      free(d); }
    { NesFdsImage img; uint8_t *d = open_image("overrun.fds", &img);
      CHECK(walk(&img, 0, &s, f, 8) == 1 && s.flags == NES_FDS_SIDE_OVERRUN && !f[0].has_data && f[0].data_pos == 74);
      free(d); d = open_image("overrun2.fds", &img);
      CHECK(walk(&img, 0, &s, f, 8) == 1 && s.flags == NES_FDS_SIDE_OVERRUN);   /* the parser never leaves the side */
      CHECK(walk(&img, 1, &s, f, 8) == 3 && !s.flags);
      free(d); }
    { NesFdsImage img; uint8_t *d = open_image("full.fds", &img);
      CHECK(walk(&img, 0, &s, f, 8) == 1 && !s.flags && f[0].has_data && s.end_pos == 65500);
      size_t a, b; uint8_t *s099 = stream(&img, 0, NES_FDS_PROFILE_MESEN099, NES_FDS_CRC_COMPUTED, &a);
      uint8_t *s2 = stream(&img, 0, NES_FDS_PROFILE_MESEN2, NES_FDS_CRC_COMPUTED, &b);
      CHECK(check_stream_blocks(s099, a) == 4 && check_stream_blocks(s2, b) == 3);   /* Mesen2's >= drops it */
      free(s099); free(s2); free(d); }
    { NesFdsImage img; size_t size; uint8_t *d = load("truncated.fds", &size);
      CHECK(nes_fds_image(d, size, NES_FDS_NONE, &img) && img.sides == 2 && img.flags == NES_FDS_IMG_TRUNCATED);
      CHECK(walk(&img, 0, &s, f, 8) == 3 && !s.flags);
      CHECK(walk(&img, 1, &s, f, 8) == 1 && s.flags == NES_FDS_SIDE_TRUNCATED && !f[0].has_data && !strcmp(f[0].name, "CUTSHORT"));
      free(d); }
    { NesFdsImage img; size_t size; uint8_t *d = load("trailing.fds", &size);
      CHECK(nes_fds_image(d, size, NES_FDS_NONE, &img) && img.format == NES_FDS_RAW && img.sides == 1 && img.flags == NES_FDS_IMG_TRAILING);
      free(d); }
    { NesFdsImage img; uint8_t *d = open_image("no_info.fds", &img);
      CHECK(walk(&img, 1, &s, f, 8) == 0 && s.flags == NES_FDS_SIDE_NO_INFO);
      size_t len; uint8_t *st = stream(&img, 1, NES_FDS_PROFILE_MESEN099, NES_FDS_CRC_COMPUTED, &len);
      CHECK(len == 65500 && check_stream_blocks(st, len) == 0);
      free(st); free(d); }
    { NesFdsImage img; uint8_t *d = open_image("bad_sig.fds", &img);
      CHECK(walk(&img, 0, &s, f, 8) == 3 && s.flags == NES_FDS_SIDE_BAD_SIGNATURE);
      free(d); }
    { NesFdsImage img; uint8_t *d = open_image("bad_crc.qd", &img);
      CHECK(walk(&img, 0, &s, f, 8) == 3 && s.flags == NES_FDS_SIDE_BAD_CRC);
      free(d); }
    { NesFdsImage img; size_t size; uint8_t *d = load("zero_sides.fds", &size);
      CHECK(!nes_fds_image(d, size, NES_FDS_NONE, &img) && img.error == NES_FDS_ERR_NO_SIDES);
      free(d); d = load("short_raw.fds", &size);
      CHECK(!nes_fds_image(d, size, NES_FDS_NONE, &img) && img.error == NES_FDS_ERR_NO_SIDES);
      free(d); d = load("not_fds.nes", &size);
      CHECK(!nes_fds_image(d, size, NES_FDS_NONE, &img) && img.error == NES_FDS_ERR_NOT_FDS);
      CHECK(!nes_fds_image(d, 3, NES_FDS_NONE, &img) && !nes_fds_image(NULL, 0, NES_FDS_NONE, &img));
      free(d); }
    /* Every truncation of a disk frames or fails cleanly and never reads out of
     * bounds (the loop runs under the fixture's exact allocation). */
    for (size_t cut = 0; cut <= hdr.size; cut += cut < 200 ? 1 : 997) {
        uint8_t *copy = (uint8_t *)malloc(cut ? cut : 1);
        memcpy(copy, d_hdr, cut);
        NesFdsImage img;
        if (nes_fds_image(copy, cut, NES_FDS_NONE, &img)) {
            CHECK(img.sides == 1 && (cut == hdr.size) == !(img.flags & NES_FDS_IMG_TRUNCATED));
            walk(&img, 0, &s, f, 8);
            size_t len; uint8_t *st = stream(&img, 0, NES_FDS_PROFILE_MESEN2, NES_FDS_CRC_COMPUTED, &len); free(st);
            st = stream(&img, 0, NES_FDS_PROFILE_MESEN099, NES_FDS_CRC_COMPUTED, &len); free(st);
        } else CHECK(cut < 16);
        free(copy);
    }
    /* Corrupt block codes, lengths and sizes at random (deterministically):
     * every API must stay inside the image (run this test under ASan). */
    { const char *names[] = { "basic.fds", "two_side_raw.fds", "basic.qd", "hidden.fds" };
      static NesFdsFile many[4096];   /* 17-byte minimum per file on a 65536-byte side */
      uint32_t x = 12345;
      for (unsigned k = 0; k < 4; ++k) {
          size_t size; uint8_t *orig = load(names[k], &size);
          for (unsigned iter = 0; iter < 300; ++iter) {
              size_t cut = size - (iter % 3 == 0 ? (x % 70000) % size : 0);
              uint8_t *copy = (uint8_t *)malloc(cut ? cut : 1);
              memcpy(copy, orig, cut);
              for (unsigned m = 0; m < 1 + iter % 8; ++m) {
                  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                  size_t at = (x >> 8) % (iter & 1 ? 400 : cut ? cut : 1);
                  if (at < cut) copy[at] = (uint8_t)(m & 1 ? x : (x & 7));   /* small values hit block codes 0-7 */
              }
              NesFdsImage img;
              if (nes_fds_image(copy, cut, ends_with(names[k], ".qd") ? NES_FDS_QD : NES_FDS_NONE, &img))
                  for (unsigned side = 0; side < img.sides; ++side) {
                      walk(&img, side, &s, many, 4096);
                      for (int p = 0; p < 2; ++p) {
                          size_t len; uint8_t *st = stream(&img, side, (NesFdsProfile)p, NES_FDS_CRC_COMPUTED, &len);
                          CHECK(len >= img.side_bytes); free(st);
                      }
                  }
              free(copy);
          }
          free(orig);
      } }
    unsigned streams = manifest_streams();
    CHECK(streams == 92);
    free(d_hdr); free(d_raw); free(d_qd);
    unsigned saves = save_contracts();
    printf("fds media contracts passed (%u Mesen-transliterated streams, %u disk save checks)\n", streams, saves);
    return 0;
}
