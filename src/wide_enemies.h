#pragma once
#include <stdbool.h>
#include <stdint.h>
/* One drawer piece, positioned relative to its object's world origin:
 * drawer output is scroll-independent, so the margin
 * copy lands exactly where the frame's own draw put it - no per-frame
 * offset search to jitter. slot keeps the frame's OAM order so overlapping
 * margin sprites keep hardware priority. */
typedef struct {
 int8_t dx,dy;
 uint8_t tile,attr;
 int16_t slot;
} Smb3EnemyPiece;
typedef struct {
 bool valid;
 int obj_x,obj_y;
 /* Rigid per-object correction voted from the frame's own OAM, so margin
  * pieces sit exactly where the frame drew them (no doubled/thickened
  * lines, no seam step at the frame's edge). */
 int8_t off_x,off_y;
 uint8_t count;
 Smb3EnemyPiece pieces[12];
} Smb3EnemyLayer;
enum {SMB3_OBJECT_SLOTS=8};
void smb3_enemies_enable(bool enabled);
void smb3_enemies_reset(void);
/* Genuine level load (same site as the sfx-voice reset): the extended spawn
 * window restarts, holding off a few frames so the load sweep claims its
 * opening objects first. */
void smb3_wide_level_load(void);
void smb3_enemies_tick(void);
void smb3_enemies_capture(void);
const Smb3EnemyLayer *smb3_enemies_layers(void);
/* Bit mask of frame-OAM slots owned by managed layers this frame (one bit
 * per OAM slot). The edge-band repair must skip these: the margin pass
 * draws them, and painting both sources side-by-side is what thickened
 * straddling sprites. */
uint64_t smb3_enemies_claimed_oam(void);
int smb3_enemies_camera(const uint8_t *ram);
bool smb3_enemies_scene(const uint8_t *ram);
