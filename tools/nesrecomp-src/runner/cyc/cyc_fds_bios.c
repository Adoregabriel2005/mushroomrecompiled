/*
 * cyc_fds_bios.c - the FDS BIOS lookup and identity check (cyc_fds_bios.h).
 */
#include "cyc_fds_bios.h"
#include "../../common/nes_cart.h"
#include "../../common/nes_fds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t cyc_fds_bios_expected(const char *path, uint32_t compiled_crc, uint32_t *size)
{
    *size = NES_FDS_BIOS_BYTES;
    if (compiled_crc) return compiled_crc;
    char toml[1024];
    snprintf(toml, sizeof(toml), "%s", path ? path : "");
    char *dot = strrchr(toml, '.'), *slash = strrchr(toml, '/'), *bslash = strrchr(toml, '\\');
    if (dot && dot > slash && dot > bslash) *dot = 0;
    strncat(toml, ".toml", sizeof(toml) - strlen(toml) - 1);
    FILE *f = fopen(toml, "r");
    uint32_t crc = NES_FDS_BIOS_CRC32;
    if (f) {
        char line[256];
        unsigned v;
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, " crc32 = \"0x%x\"", &v) == 1 || sscanf(line, " crc32 = 0x%x", &v) == 1) crc = v;
            else if (sscanf(line, " size = %u", &v) == 1) *size = v;
        }
        fclose(f);
    }
    return crc;
}

CycFdsBiosStatus cyc_fds_bios_check(const char *path, uint32_t compiled_crc, bool want_data, CycFdsBiosResult *r)
{
    memset(r, 0, sizeof(*r));
    snprintf(r->path, sizeof(r->path), "%s", path ? path : "");
    r->want_crc = cyc_fds_bios_expected(path, compiled_crc, &r->want_size);
    FILE *f = path && *path ? fopen(path, "rb") : NULL;
    if (!f) {
        r->status = CYC_FDS_BIOS_MISSING;
        snprintf(r->detail, sizeof(r->detail), "Cannot read %s", path && *path ? path : "(no file)");
        return r->status;
    }
    /* The CRC over the whole file, streamed: a wrong pick may be any size. */
    uint8_t chunk[8192];
    uint8_t *data = (uint8_t *)malloc(NES_FDS_BIOS_BYTES);
    size_t n, total = 0;
    uint32_t crc = 0;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (data && total + n <= NES_FDS_BIOS_BYTES) memcpy(data + total, chunk, n);
        crc = nes_crc32(crc, chunk, n);
        total += n;
    }
    fclose(f);
    r->size = total;
    r->crc = crc;
    if (total != r->want_size || total != NES_FDS_BIOS_BYTES || crc != r->want_crc) {
        free(data);
        r->status = CYC_FDS_BIOS_WRONG;
        snprintf(r->detail, sizeof(r->detail),
                 "Not the FDS BIOS: %zu bytes, CRC %08X (expected %u bytes, CRC %08X)",
                 total, (unsigned)crc, (unsigned)r->want_size, (unsigned)r->want_crc);
        return r->status;
    }
    if (want_data) r->data = data;
    else free(data);
    r->status = CYC_FDS_BIOS_OK;
    snprintf(r->detail, sizeof(r->detail), "FDS BIOS OK (CRC %08X)", (unsigned)crc);
    return r->status;
}

CycFdsBiosStatus cyc_fds_bios_locate(const CycFdsBiosLookup *in, bool want_data, CycFdsBiosResult *r)
{
    struct { const char *path, *source; } c[3];
    int count = 0;
    if (in->explicit_path && *in->explicit_path) {
        c[count].path = in->explicit_path, c[count++].source = "--fds-bios";
    } else {
        if (in->saved_path && *in->saved_path) c[count].path = in->saved_path, c[count++].source = "config.ini [FDS] Bios";
        if (in->compiled_path && *in->compiled_path) c[count].path = in->compiled_path, c[count++].source = "game.toml [fds] bios";
    }
    bool saved_gone = false;
    for (int i = 0; i < count; ++i) {
        FILE *f = fopen(c[i].path, "rb");
        if (!f) {
            saved_gone |= c[i].path == in->saved_path;
            continue;
        }
        fclose(f);
        CycFdsBiosStatus st = cyc_fds_bios_check(c[i].path, in->compiled_crc, want_data, r);
        if (st == CYC_FDS_BIOS_MISSING) continue;
        r->source = c[i].source;
        if (st == CYC_FDS_BIOS_OK && c[i].path != in->saved_path)
            snprintf(r->detail, sizeof(r->detail), "%sFDS BIOS OK (CRC %08X), from %s",
                     saved_gone ? "The selected file is gone; " : "", (unsigned)r->crc, c[i].source);
        return st;
    }
    const char *source = in->explicit_path && *in->explicit_path ? "--fds-bios" : NULL;
    memset(r, 0, sizeof(*r));
    r->source = source;
    r->status = CYC_FDS_BIOS_MISSING;
    if (source) {
        snprintf(r->path, sizeof(r->path), "%s", in->explicit_path);
        snprintf(r->detail, sizeof(r->detail), "Cannot read --fds-bios %s", in->explicit_path);
    } else {
        snprintf(r->detail, sizeof(r->detail), "%sNo FDS BIOS selected",
                 saved_gone ? "The selected file is gone. " : "");
    }
    return r->status;
}

bool cyc_fds_image_file(const char *path)
{
    if (!path || !*path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool fds = false;
    uint8_t *buf = n > 0 ? (uint8_t *)malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
        NesCartInfo cart;
        NesFdsImage img;
        const char *ext = strrchr(path, '.');
        bool qd = ext && (!strcmp(ext, ".qd") || !strcmp(ext, ".QD"));
        fds = !nes_cart_image(buf, (size_t)n, &cart) && nes_fds_image(buf, (size_t)n, qd ? NES_FDS_QD : NES_FDS_NONE, &img);
    }
    free(buf);
    fclose(f);
    return fds;
}
