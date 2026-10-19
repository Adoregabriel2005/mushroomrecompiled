#define WIN32_LEAN_AND_MEAN
#include "replay.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes_runtime.h"

uint32_t smb3_crc32(const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int)(crc & 1)));
        }
    }
    return ~crc;
}

uint64_t smb3_hash(const void *data, size_t size, uint64_t hash) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ p[i]) * 1099511628211ull;
    }
    return hash;
}

uint64_t glue_state_hash(void) {
    uint64_t h = 14695981039346656037ull;
    h = smb3_hash(&g_cpu, sizeof(g_cpu), h);
    h = smb3_hash(g_ram, sizeof(g_ram), h);
    h = smb3_hash(g_sram, sizeof(g_sram), h);
    h = smb3_hash(g_ppu_oam, sizeof(g_ppu_oam), h);
    h = smb3_hash(g_ppu_pal, sizeof(g_ppu_pal), h);
    h = smb3_hash(g_ppu_nt, sizeof(g_ppu_nt), h);
    return h;
}

int replay_append(Replay *r, uint8_t input) {
    if (r->count == r->capacity) {
        uint32_t next = r->capacity ? r->capacity * 2 : 1024;
        if (next > REPLAY_MAX_FRAMES) return 0;
        uint8_t *p = (uint8_t *)realloc(r->inputs, next);
        if (!p) return 0;
        r->inputs = p;
        r->capacity = next;
    }
    r->inputs[r->count++] = input;
    return 1;
}

void replay_free(Replay *r) {
    if (r->inputs) free(r->inputs);
    r->inputs = NULL;
    r->count = r->capacity = 0;
    r->target = r->state_hash = 0;
}

int replay_save(const wchar_t *path, const Replay *r, uint64_t frame, uint64_t hash) {
    FILE *f = _wfopen(path, L"wb");
    if (!f) return 0;

    const char tag[8] = "SMB3REC";
    fwrite(tag, 1, 8, f);
    uint32_t build = REPLAY_BUILD;
    uint32_t rom_crc = SMB3_ROM_CRC32;
    uint32_t count = r->count;
    fwrite(&build, 4, 1, f);
    fwrite(&rom_crc, 4, 1, f);
    fwrite(&frame, 8, 1, f);
    fwrite(&hash, 8, 1, f);
    fwrite(&count, 4, 1, f);
    if (count > 0 && r->inputs) {
        fwrite(r->inputs, 1, count, f);
    }
    fclose(f);
    return 1;
}

int replay_load(const wchar_t *path, Replay *out) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return 0;

    char tag[8];
    if (fread(tag, 1, 8, f) != 8 || memcmp(tag, "SMB3REC", 7) != 0) {
        fclose(f);
        return 0;
    }

    uint32_t build = 0, rom_crc = 0, count = 0;
    uint64_t frame = 0, hash = 0;
    if (fread(&build, 4, 1, f) != 1 ||
        fread(&rom_crc, 4, 1, f) != 1 ||
        fread(&frame, 8, 1, f) != 1 ||
        fread(&hash, 8, 1, f) != 1 ||
        fread(&count, 4, 1, f) != 1) {
        fclose(f);
        return 0;
    }

    if (count > REPLAY_MAX_FRAMES) {
        fclose(f);
        return 0;
    }

    uint8_t *inputs = NULL;
    if (count > 0) {
        inputs = (uint8_t *)malloc(count);
        if (!inputs || fread(inputs, 1, count, f) != count) {
            free(inputs);
            fclose(f);
            return 0;
        }
    }
    fclose(f);

    out->inputs = inputs;
    out->count = count;
    out->capacity = count;
    out->target = frame;
    out->state_hash = hash;
    return 1;
}
