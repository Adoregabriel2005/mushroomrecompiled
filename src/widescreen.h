#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SMB3_WIDE_WIDTH 426
#define SMB3_WIDE_HEIGHT 240
#define SMB3_HUD_Y 192
bool smb3_wide_load(const uint8_t *rom,size_t size);
void smb3_wide_reset(void);
void smb3_wide_capture(void);
const uint32_t *smb3_wide_present(const uint32_t *native);
bool smb3_wide_active(void);
int smb3_wide_origin(void);
