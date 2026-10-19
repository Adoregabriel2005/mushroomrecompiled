#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
