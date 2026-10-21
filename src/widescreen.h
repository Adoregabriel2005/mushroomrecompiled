#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SMB3_WIDE_WIDTH 426
#define SMB3_WIDE_HEIGHT 240
bool smb3_wide_load(const uint8_t *rom,size_t size);
void smb3_wide_reset(void);
void smb3_wide_capture(void);
const uint32_t *smb3_wide_present(const uint32_t *native);
bool smb3_wide_active(void);
int smb3_wide_origin(void);
uint32_t smb3_wide_terrain_argb(int world_x,int world_y);
void smb3_wide_frame_camera(const uint8_t *state,int *x,int *y);
/* Camera the last composed frame used (line-8 snapshot), for diagnostics. */
void smb3_wide_camera_used(int *x,int *y);
