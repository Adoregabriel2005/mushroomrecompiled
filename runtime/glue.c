#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <stdint.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glue.h"
#include "cyc_core.h"
#include "cyc_run.h"
#include "cyc_state.h"
#include "widescreen.h"
#include "wide_enemies.h"
static bool widescreen;

static bool loaded;
static uint64_t frames;
static uint8_t pad1;
static int sample_rate = 48000;
static uint32_t rom_crc;
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffff;
    while (n--) { c ^= *p++; for (int i=0;i<8;i++) c=(c>>1)^(0xedb88320u & (0u-(c&1))); }
    return ~c;
}
static size_t save_host(uint8_t *out, size_t cap) {
    if (out && cap >= sizeof(frames)) memcpy(out, &frames, sizeof(frames));
    return sizeof(frames);
}
static bool load_host(const uint8_t *in, size_t n, bool apply) {
    if (n != sizeof(frames)) return false;
    if (apply) memcpy(&frames, in, n);
    return true;
}
static bool load_file(FILE *f) {
    if (!f) return false;
    if (fseek(f,0,SEEK_END)) { fclose(f); return false; }
    long n=ftell(f);
    if (n != 393232 || fseek(f,0,SEEK_SET)) { fclose(f); return false; }
    uint8_t *image=malloc((size_t)n);
    if (!image) { fclose(f); return false; }
    bool ok=fread(image,1,(size_t)n,f)==(size_t)n;
    fclose(f);
    uint32_t crc=crc32(image+16,(size_t)n-16);
    ok=ok && crc==0x2e6301ed && smb3_wide_load(image,(size_t)n) && cyc_load_ines(image,(size_t)n);
    free(image);
    if (!ok) return false;
    loaded=true; rom_crc=crc;
    static const CycStateHost host={save_host,load_host};
    cyc_state_set_host(&host);
    glue_reset();
    return true;
}
#ifdef _WIN32
static FILE *open_wide(const wchar_t *path, const char *mode) {
    return _wfopen(path, mode[0] == 'r' ? L"rb" : L"wb");
}
#else
/* wchar_t is UCS-4 on Linux. Encode to UTF-8 rather than relying on the
 * process locale, so paths from GTK/SDL file dialogs work unchanged. */
static bool wide_to_utf8(const wchar_t *in, char *out, size_t cap) {
    size_t o = 0;
    for (; in && *in; in++) {
        uint32_t c = (uint32_t)*in;
        if (c < 0x80) {
            if (o + 1 >= cap) return false;
            out[o++] = (char)c;
        } else if (c < 0x800) {
            if (o + 2 >= cap) return false;
            out[o++] = (char)(0xc0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            if (o + 3 >= cap) return false;
            out[o++] = (char)(0xe0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[o++] = (char)(0x80 | (c & 0x3f));
        } else if (c <= 0x10ffff) {
            if (o + 4 >= cap) return false;
            out[o++] = (char)(0xf0 | (c >> 18));
            out[o++] = (char)(0x80 | ((c >> 12) & 0x3f));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[o++] = (char)(0x80 | (c & 0x3f));
        } else {
            return false;
        }
    }
    if (o >= cap) return false;
    out[o] = 0;
    return true;
}
static FILE *open_wide(const wchar_t *path, const char *mode) {
    char buf[4096];
    if (!path || !wide_to_utf8(path, buf, sizeof buf)) return NULL;
    return fopen(buf, mode);
}
#endif
bool glue_load_rom(const char *p) { return p && load_file(fopen(p,"rb")); }
bool glue_load_rom_wide(const wchar_t *p) { return p && load_file(open_wide(p,"rb")); }
bool glue_is_rom_loaded(void) { return loaded; }
uint32_t glue_get_rom_crc32(void) { return rom_crc; }
void glue_init(void) { glue_reset(); }
void glue_reset(void) {
    if (!loaded) return;
    cyc_set_region(CYC_REGION_NTSC);
    cyc_power_on(0); cyc_run_power_on(); cyc_audio_enable(sample_rate);
    frames=0; pad1=0;
    smb3_wide_reset();
    smb3_enemies_reset();
    hw_observe_line=8;
    cyc_run_observer=smb3_wide_capture;
}
void glue_step_frame(void) { if (loaded) { smb3_enemies_tick();cyc_run_frame(); frames++; } }
void glue_set_pad1(uint8_t p) { pad1=p; cyc_set_controller(0,p); }
void glue_set_pad2(uint8_t p) { cyc_set_controller(1,p); }
uint8_t glue_get_pad1(void) { return pad1; }
const uint32_t *glue_get_framebuffer(void) { return cyc_frame_argb(); }
void glue_set_widescreen(bool enabled) { widescreen=enabled;smb3_enemies_enable(enabled); }
const uint32_t *glue_get_present_framebuffer(int *width,int *height) {
    *width=widescreen?SMB3_WIDE_WIDTH:256;*height=240;
    return widescreen?smb3_wide_present(cyc_frame_argb()):cyc_frame_argb();
}
size_t glue_get_audio_samples(int16_t *out,size_t capacity,int stereo) {
    (void)stereo;
    if (!loaded || !out) return 0;
    int16_t mono[2048];
    size_t n=cyc_audio_read(mono,capacity/2 < 2048 ? capacity/2 : 2048);
    for (size_t i=0;i<n;i++) out[i*2]=out[i*2+1]=mono[i];
    return n*2;
}
void glue_set_audio_sample_rate(uint32_t rate) {
    if (rate!=44100 && rate!=48000 && rate!=96000) return;
    sample_rate=(int)rate;
    if (loaded) cyc_audio_enable(sample_rate);
}
uint32_t glue_audio_sample_rate(void) { return (uint32_t)sample_rate; }
double glue_frame_rate(void) { return 1.0/cyc_frame_seconds(); }
uint64_t glue_frame_count(void) { return frames; }
bool glue_save_state(const wchar_t *path) {
    if (!loaded || !path) return false;
    uint8_t *data=NULL; size_t len=0; char error[256];
    if (!cyc_state_save(&data,&len,error,sizeof error)) return false;
    FILE *f=open_wide(path,"wb");
    bool ok=f && fwrite(data,1,len,f)==len;
    if (f && fclose(f)) ok=false;
    free(data); return ok;
}
bool glue_load_state(const wchar_t *path) {
    if (!loaded || !path) return false;
    FILE *f=open_wide(path,"rb"); if (!f) return false;
    if (fseek(f,0,SEEK_END)) { fclose(f); return false; }
    long n=ftell(f);
    if (n<=0 || n>64*1024*1024 || fseek(f,0,SEEK_SET)) { fclose(f); return false; }
    uint8_t *data=malloc((size_t)n);
    if (!data) { fclose(f); return false; }
    bool ok=fread(data,1,(size_t)n,f)==(size_t)n; fclose(f);
    char error[256];
    if (ok) ok=cyc_state_load(data,(size_t)n,error,sizeof error);
    if (ok) {smb3_wide_reset();smb3_enemies_reset();}
    free(data); return ok;
}
