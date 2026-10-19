#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
 bool valid;
 int camera;
 int offset_x,offset_y;
 uint8_t oam[256];
} Smb3EnemyLayer;
enum {SMB3_OBJECT_SLOTS=8};
void smb3_enemies_enable(bool enabled);
void smb3_enemies_reset(void);
void smb3_enemies_tick(void);
void smb3_enemies_capture(void);
const Smb3EnemyLayer *smb3_enemies_layers(void);
int smb3_enemies_camera(const uint8_t *ram);
bool smb3_enemies_scene(const uint8_t *ram);
