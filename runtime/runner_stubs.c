#include <stdint.h>
#include <stddef.h>

/* Global runtime variables referenced by nesrecomp runner */
char g_exe_dir[260] = ".";
int g_render_width = 256;
int g_widescreen_left = 0;
int g_widescreen_right = 0;
int g_ws_eff_left = -1;
int g_ws_eff_right = -1;

int g_ws_oam_sidecar = 0;
int16_t g_oam_x16[64];
int16_t g_ws_shadow_x16[64];
int16_t g_ws_obj_true_rel = 0;
uint8_t g_ws_obj_rel8 = 0;
uint8_t g_ws_obj_ctx_valid = 0;
int g_ws_obj_delta_min = -24;
int g_ws_obj_delta_max = 56;

uint16_t g_rts_target = 0;
int g_rti_bank = -1;
uint16_t g_rti_source = 0;
int recomp_audio_debug_enabled(void) { return 0; }

void nes_vblank_callback(void) {
    extern void glue_vblank_callback(void);
    glue_vblank_callback();
}

void nes_foreign_trace_init_dump(void) {
}

void save_ram_mark_dirty(void) {
}

int chr_override_active(void) {
    return 0;
}

void chr_override_on_ppuaddr(uint16_t addr) {
    (void)addr;
}

void chr_override_on_chr_write(uint16_t addr, uint8_t val) {
    (void)addr;
    (void)val;
}

void save_png(const char *path, int w, int h, const void *rgb, int stride) {
    (void)path;
    (void)w;
    (void)h;
    (void)rgb;
    (void)stride;
}

int hdpack_recording(void) {
    return 0;
}

void hdpack_frame_begin(void) {
}

void *hdpack_pixels(void) {
    return NULL;
}

