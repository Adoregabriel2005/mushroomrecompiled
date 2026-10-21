#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "glue.h"
#include "cyc_core.h"
#include "cyc_run.h"
#include "cyc_png.h"
#include "cpu6502.h"

static uint8_t input(unsigned f) {
    if ((f>=180 && f<190) || (f>=360 && f<370)) return NES_PAD_START;
    if (f>=500 && f<510) return NES_PAD_DOWN;
    if (f>=550 && f<560) return NES_PAD_RIGHT;
    if (f>=620 && f<630) return NES_PAD_A;
    if (f>=900) return NES_PAD_RIGHT | NES_PAD_B | ((f%90<30)?NES_PAD_A:0);
    return 0;
}
int main(int argc,char **argv) {
    const char *rom=argc>1?argv[1]:"game.nes";
    if (!glue_load_rom(rom)) { fprintf(stderr,"ROM load failed\n"); return 1; }
    cyc_run_miss=calloc(cyc_run_miss_slots(),sizeof(uint32_t));
    if (!cyc_run_miss) return 2;
    uint64_t changed=0,previous=0,audio_count=0,nonzero_audio=0;
    uint64_t reference[3000];
    for (unsigned f=0;f<3000;f++) {
        glue_set_pad1(input(f)); glue_step_frame();
        if (glue_frame_count()!=f+1) return 3;
        reference[f]=cyc_mem_state_hash();
        if (cpu.jammed) { fprintf(stderr,"CPU jammed at frame %u\n",f); return 9; }
        const uint32_t *fb=glue_get_framebuffer();
        uint64_t hash=14695981039346656037ULL;
        for (unsigned i=0;i<256*240;i++) { hash^=fb[i]; hash*=1099511628211ULL; }
        if (f && hash!=previous) changed++;
        previous=hash;
        int16_t audio[4096]; size_t n=glue_get_audio_samples(audio,4096,1);
        audio_count+=n/2;
        for (size_t i=0;i<n;i++) if (audio[i]) nonzero_audio++;
        if (f==179 || f==359 || f==599 || f==899 || f==1199 || f==2999) {
            char name[64]; snprintf(name,sizeof name,"verify-frame-%u.png",f+1);
            cyc_write_png(name,fb,256,240);
        }
    }
    FILE *seeds=fopen("cycle-misses.txt","w");
    if (!seeds) return 4;
    for (unsigned i=0;i<cyc_run_miss_slots();i++) if (cyc_run_miss[i]) {
        unsigned bank; uint16_t addr; cyc_run_miss_decode(i,&bank,&addr);
        fprintf(seeds,"4k:%02X:%04X\n",bank,addr);
    }
    fclose(seeds);
    printf("3000 frames; %llu picture changes; %llu audio samples; %llu nonzero samples\n",
        (unsigned long long)changed,(unsigned long long)audio_count,(unsigned long long)nonzero_audio);
    printf("Native cycles: %llu; interpreted ROM: %llu; RAM: %llu; other: %llu\n",
        (unsigned long long)cyc_run_native_cycles,(unsigned long long)cyc_run_interp_rom_cycles,
        (unsigned long long)cyc_run_interp_ram_cycles,(unsigned long long)cyc_run_interp_other_cycles);
    if (changed<10 || !nonzero_audio || cyc_run_interp_other_cycles) return 5;
    if (cyc_run_native_cycles < 999 * (cyc_run_interp_rom_cycles + cyc_run_interp_ram_cycles)) {
        fprintf(stderr,"Native coverage below 99.9 percent\n"); return 12;
    }
    if (!glue_save_state(L"verify-state.bin")) return 6;
    glue_set_pad1(0);
    for (int i=0;i<30;i++) glue_step_frame();
    uint64_t expected=cyc_mem_state_hash();
    if (!glue_load_state(L"verify-state.bin")) return 7;
    glue_set_pad1(0);
    for (int i=0;i<30;i++) glue_step_frame();
    if (expected!=cyc_mem_state_hash()) { fprintf(stderr,"Save-state divergence\n"); return 8; }
    puts("Save/load: deterministic continuation OK");
    /* Corrupted images are rejected and leave the machine untouched. The
     * format has no payload checksum, so this covers what it validates:
     * magic/version/identity header, chunk structure and truncation. */
    {
        uint64_t live_before = cyc_mem_state_hash();
        FILE *sf = fopen("verify-state.bin", "rb");
        if (!sf) return 13;
        if (fseek(sf, 0, SEEK_END)) { fclose(sf); return 13; }
        long n = ftell(sf);
        if (n <= 0 || fseek(sf, 0, SEEK_SET)) { fclose(sf); return 13; }
        uint8_t *image = malloc((size_t)n);
        if (!image) { fclose(sf); return 13; }
        bool ok = fread(image, 1, (size_t)n, sf) == (size_t)n;
        fclose(sf);
        if (!ok) { free(image); return 13; }
        /* Case 1: bad magic. Case 2: truncation. */
        for (int cas = 0; cas < 2; cas++) {
            uint8_t *bad = malloc((size_t)n);
            size_t bad_len = (size_t)n;
            if (!bad) { free(image); return 13; }
            memcpy(bad, image, (size_t)n);
            if (cas == 0) bad[0] ^= 0x5A;
            else bad_len -= 64;
            FILE *bf = fopen("verify-state-bad.bin", "wb");
            if (!bf || fwrite(bad, 1, bad_len, bf) != bad_len) {
                if (bf) fclose(bf);
                free(bad);
                free(image);
                return 13;
            }
            fclose(bf);
            free(bad);
            if (glue_load_state_utf8("verify-state-bad.bin")) {
                fprintf(stderr, "Corrupted save state (case %d) was accepted\n", cas);
                remove("verify-state-bad.bin");
                free(image);
                return 13;
            }
            if (cyc_mem_state_hash() != live_before) {
                fprintf(stderr, "Rejected save state (case %d) changed the machine\n", cas);
                remove("verify-state-bad.bin");
                free(image);
                return 13;
            }
        }
        free(image);
        remove("verify-state-bad.bin");
        puts("Save/load: corrupted images rejected, machine unchanged");
    }
    /* Battery SRAM: file round trip, then rejection of a wrong-size file. */
    {
        size_t sram_len = glue_sram_size();
        if (!sram_len || sram_len > 0x2000) return 14;
        if (!glue_sram_save("verify-sram.bin")) return 14;
        uint8_t before[0x2000], pattern[0x2000], after[0x2000];
        if (!glue_sram_export(before, sram_len)) return 14;
        for (size_t i = 0; i < sram_len; i++) pattern[i] = (uint8_t)(i * 7 + 3);
        if (!glue_sram_import(pattern, sram_len)) return 14;
        if (glue_sram_load("verify-sram.bin") != 1) return 14;
        if (!glue_sram_export(after, sram_len) || memcmp(before, after, sram_len)) {
            fprintf(stderr, "SRAM round trip mismatch\n");
            return 14;
        }
        remove("verify-sram.bin");
        uint64_t live_before = cyc_mem_state_hash();
        FILE *bf = fopen("verify-sram-bad.bin", "wb");
        if (!bf) return 14;
        uint8_t junk[100];
        memset(junk, 0x42, sizeof junk);
        bool ok = fwrite(junk, 1, sizeof junk, bf) == sizeof junk;
        fclose(bf);
        if (!ok) { remove("verify-sram-bad.bin"); return 14; }
        if (glue_sram_load("verify-sram-bad.bin") != -1) {
            fprintf(stderr, "Wrong-size SRAM file was accepted\n");
            remove("verify-sram-bad.bin");
            return 14;
        }
        if (cyc_mem_state_hash() != live_before) {
            fprintf(stderr, "Rejected SRAM file changed the machine\n");
            remove("verify-sram-bad.bin");
            return 14;
        }
        remove("verify-sram-bad.bin");
        puts("SRAM: file round trip and invalid rejection OK");
    }
    glue_reset();
    cyc_run_native=false;
    for (unsigned f=0;f<3000;f++) {
        glue_set_pad1(input(f)); glue_step_frame();
        if (reference[f]!=cyc_mem_state_hash()) { fprintf(stderr,"Native/interpreter divergence at frame %u\n",f); return 10; }
        int16_t discard[4096]; glue_get_audio_samples(discard,4096,1);
    }
    puts("Native/interpreter: 3000 frame hashes match");
    for (unsigned rate=44100;rate<=96000;rate=(rate==44100?48000:rate==48000?96000:96001)) {
        glue_reset(); glue_set_audio_sample_rate(rate);
        size_t total=0;
        for (unsigned f=0;f<60;f++) {
            glue_step_frame(); int16_t audio[4096]; total+=glue_get_audio_samples(audio,4096,1)/2;
        }
        double expected=rate*60.0/glue_frame_rate();
        if (total<expected-150 || total>expected+150) return 11;
        printf("Audio %u Hz: %zu samples in 60 frames\n",rate,total);
    }
    free(cyc_run_miss); cyc_run_miss=NULL;
    return 0;
}
