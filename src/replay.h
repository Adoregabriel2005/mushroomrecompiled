#pragma once
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>

#define SMB3_ROM_CRC32 0x2E6301EDu
#define SMB3_ROM_SIZE  393232u
#define REPLAY_BUILD   1u
#define REPLAY_MAX_FRAMES 10000000u

typedef struct {
    uint8_t *inputs;
    uint32_t count, capacity;
    uint64_t target, state_hash;
} Replay;

uint32_t smb3_crc32(const void *data, size_t size);
uint64_t smb3_hash(const void *data, size_t size, uint64_t hash);
int  replay_append(Replay *r, uint8_t input);
void replay_free(Replay *r);
int  replay_save(const wchar_t *path, const Replay *r, uint64_t frame, uint64_t hash);
int  replay_load(const wchar_t *path, Replay *out);
uint64_t glue_state_hash(void);
