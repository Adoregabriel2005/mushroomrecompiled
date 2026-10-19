/*
 * extras.c — Game-specific hooks for Super Mario Bros. 3 (NESRecomp)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "game_extras.h"
#include "nes_runtime.h"

/* Forward declarations of generated entry points */
void func_FF40(void); /* RESET vector: 0xFF40 */
void func_F486(void); /* NMI vector:   0xF486 */
void func_F795(void); /* IRQ vector:   0xF795 */
void func_NMI(void);

const char *game_get_name(void) {
    return "Super Mario Bros. 3 Recomp";
}

/* Expected CRC32 of ROM data without 16-byte iNES header:
 * Super Mario Bros. 3 (USA) (Rev 1): 0x2E6301ED */
uint32_t game_get_expected_crc32(void) {
    return 0x2E6301ED;
}

void game_on_init(void) {
    printf("[SMB3] Game initialized. MMC3 mapper active. Entry at $FF40.\n");
}

void game_on_frame(uint64_t frame_count) {
    (void)frame_count;
}

void game_post_nmi(uint64_t frame_count) {
    (void)frame_count;
}

int game_handle_arg(const char *key, const char *val) {
    (void)key;
    (void)val;
    return 0;
}

const char *game_arg_usage(void) {
    return "  --turbo             Enable turbo button emulation\n"
           "  --widescreen        Enable 16:9 widescreen presentation mode\n";
}

void game_run_main(void) {
    /* RESET drives the main loop in NES games */
    func_FF40();
}

void game_run_nmi(void) {
    /* NMI fires every VBlank */
    func_NMI();
}

int game_dispatch_override(uint16_t addr) {
    (void)addr;
    return 0;
}

uint8_t game_ram_read_hook(uint16_t pc, uint16_t addr, uint8_t val) {
    (void)pc;
    (void)addr;
    return val;
}

void game_post_render(uint32_t *framebuf) {
    (void)framebuf;
}

void game_fill_frame_record(void *record) {
    (void)record;
}

int game_handle_debug_cmd(const char *cmd, int id, const char *json) {
    (void)cmd;
    (void)id;
    (void)json;
    return 0;
}
