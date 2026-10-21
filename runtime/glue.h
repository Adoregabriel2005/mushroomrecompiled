#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NES Controller Button Flags */
#define NES_PAD_A       0x80
#define NES_PAD_B       0x40
#define NES_PAD_SELECT  0x20
#define NES_PAD_START   0x10
#define NES_PAD_UP      0x08
#define NES_PAD_DOWN    0x04
#define NES_PAD_LEFT    0x02
#define NES_PAD_RIGHT   0x01

/* Lifecycle */
bool     glue_load_rom(const char *path);
bool     glue_load_rom_utf8(const char *path);
bool     glue_load_rom_wide(const wchar_t *path);
bool     glue_is_rom_loaded(void);
uint32_t glue_get_rom_crc32(void);
void     glue_init(void);
void     glue_reset(void);

/* Frame Execution */
void     glue_step_frame(void);

/* Input */
void     glue_set_pad1(uint8_t pressed);
void     glue_set_pad2(uint8_t pressed);
uint8_t  glue_get_pad1(void);

/* Audio & Video output */
const uint32_t *glue_get_framebuffer(void); /* 256x240 ARGB8888 */
void glue_set_widescreen(bool enabled);
const uint32_t *glue_get_present_framebuffer(int *width,int *height);
size_t   glue_get_audio_samples(int16_t *stereo_buffer, size_t max_samples, int stereo);
uint32_t glue_audio_sample_rate(void);
void glue_set_audio_rate_scale(double scale);
const uint8_t *glue_get_chr_rom(size_t *len);
void     glue_set_audio_sample_rate(uint32_t rate);
double   glue_frame_rate(void);
uint64_t glue_frame_count(void);

/* Save State */
bool     glue_save_state(const wchar_t *path);
bool     glue_load_state(const wchar_t *path);
bool     glue_save_state_utf8(const char *path);
bool     glue_load_state_utf8(const char *path);

/* Synthetic battery save: the ROM header sets no battery bit, but the game
 * keeps its progress slots in work RAM ($6000-$7FFF), so persist that window
 * in saves/smb3.srm like smb3pc does. Power-on clears it; the frontend loads
 * the file after ROM load and saves when it changes. */
size_t   glue_sram_size(void);
bool     glue_sram_export(uint8_t *out, size_t len);
bool     glue_sram_import(const uint8_t *in, size_t len);
int      glue_sram_load(const char *path); /* 1 loaded, 0 missing, -1 invalid */
bool     glue_sram_save(const char *path); /* writes only when changed */

#ifdef __cplusplus
}
#endif
