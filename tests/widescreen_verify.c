#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glue.h"
#include "cyc_core.h"
#include "cyc_png.h"
#include "widescreen.h"
#include "hud.h"
#include "wide_enemies.h"
#include "cyc_run.h"
#include "cyc_mod.h"
static unsigned buttons(int f){
 if((f>=180&&f<190)||(f>=360&&f<370))return NES_PAD_START;
 if(f>=720&&f<730)return NES_PAD_RIGHT;
 if(f>=840&&f<850)return NES_PAD_UP;
 if(f>=960&&f<970)return NES_PAD_A;
 if(f>=1200)return NES_PAD_RIGHT|NES_PAD_B|((f>=1230&&(f-1230)%90<55)?NES_PAD_A:0);
 return 0;
}
int main(int argc,char **argv){
 if(argc<2||!glue_load_rom(argv[1]))return 1;
 uint64_t hashes[1800];unsigned active_frames=0,early_frames=0,layer_frames=0,retained_frames=0;int max_camera=0;
 glue_set_widescreen(true);
 for(int f=0;f<1800;f++){
  glue_set_pad1(buttons(f));glue_step_frame();hashes[f]=cyc_mem_state_hash();
  int16_t audio[4096];glue_get_audio_samples(audio,4096,1);
  int w,h;const uint32_t *out=glue_get_present_framebuffer(&w,&h),*native=glue_get_framebuffer();
  if(w!=426||h!=240)return 2;
  int active=smb3_wide_active(),origin=smb3_wide_origin();if(active){active_frames++;const uint8_t *state=cyc_cpu_ram();int camera=state[0x12]*256+state[0xfd];if(camera>max_camera)max_camera=camera;}
  if(active){const uint8_t *r=cyc_cpu_ram();int camera=r[0x12]*256+r[0xfd];
   for(int i=0;i<5;i++)if(r[0x661+i]>=2){
    int x=r[0x76+i]*256+r[0x91+i];
    if(x>=camera+256)early_frames++;
    if(x<camera-48&&x+16>smb3_enemies_camera(r))retained_frames++;
    for(int j=i+1;j<5;j++)if(r[0x661+j]&&r[0x659+i]<48&&r[0x659+i]==r[0x659+j])return 10;
   }
   const Smb3EnemyLayer *layers=smb3_enemies_layers();
   for(int i=0;i<5;i++)if(layers[i].valid&&layers[i].count){layer_frames++;goto layer_counted;}
   layer_counted:;
  }
  if(f==1140){
   const uint8_t *r=cyc_cpu_ram();int plant=0;
   for(int i=0;i<5;i++)if(r[0x661+i]>=2&&r[0x671+i]==0xa6&&r[0x76+i]*256+r[0x91+i]==360&&smb3_enemies_layers()[i].valid)plant=1;
   if(!plant){fprintf(stderr,"First pipe plant not active/drawn beyond original viewport\n");return 15;}
   uint64_t before=cyc_mem_state_hash();smb3_enemies_capture();
   if(cyc_mem_state_hash()!=before){fprintf(stderr,"Sprite drawing changed live machine\n");return 11;}
   if(!glue_save_state(L"verify-wide-enemies.bin"))return 12;
  }
  /* The status bar is its own layer (hud.c): the rows
   * the split stages are drawn from the bar's settled row, and every other
   * HUD row is the machine's own, pixel for pixel. A staged row must continue
   * the bar - the same background its settled row shows - not the playfield
   * that was still on the wire when the split began. */
  int bar_first,bar_anchor;smb3_hud_bounds(&bar_first,&bar_anchor);
  const uint32_t *hud=smb3_hud_present(native);
  /* Presenting the same frame again is what the cache is for: the layer is
   * rebuilt only when the state it was built from changes (hud_matches). */
  uint64_t builds=smb3_hud_builds();
  smb3_hud_present(native);
  if(smb3_hud_builds()!=builds){fprintf(stderr,"HUD layer rebuilt for an unchanged frame at %d\n",f);return 21;}
  int bar_top=smb3_hud_top();if(bar_top>240)bar_top=240;
  int split=cyc_frame_split_line();int layer_top=split>=0&&split<240?split:240;
  /* Nothing above the split may change: those rows are the machine's own.
   * From the split down hud_build() rebuilds every row, the level's rows
   * between the split and the bar included. */
  for(int y=0;y<layer_top;y++)if(memcmp(hud+y*256,native+y*256,256*sizeof(uint32_t))){
   fprintf(stderr,"HUD layer touched row %d above the split at %d\n",y,f);return 20;
  }
  /* The split IRQ is the only thing that can start a bar (hud.cpp's hud_top):
   * a frame without one gets no layer at all. */
  if(bar_first>=0&&cyc_frame_split_line()>=240){fprintf(stderr,"Bar without a split IRQ at %d\n",f);return 18;}
  /* Every row below the anchor is the bar's own captured state, so the layer
   * rebuilding it has to reproduce the machine's pixels exactly - that is the
   * port reproducing the PPU (palette, banking, scroll, sprites, greyscale). */
  for(int y=bar_anchor+1;y>0&&y<240;y++)if(memcmp(hud+y*256,native+y*256,256*sizeof(uint32_t))){
   fprintf(stderr,"Bar row %d is not the PPU's at %d\n",y,f);return 19;}
  uint32_t bar_bg=0;int bar_best=-1;
  for(int x=0;bar_anchor>=0&&x<256;x++){
   uint32_t c=hud[bar_anchor*256+x];int n=0;
   for(int k=0;k<256;k++)if(hud[bar_anchor*256+k]==c)n++;
   if(n>bar_best){bar_best=n;bar_bg=c;}
  }
  int play_x=active?origin+12:97;
  for(int y=0;y<240;y++){
   if(y<bar_top){
    /* The level still owns these rows (between the split and the bar they are
     * the playfield's own capture), so the wide picture pastes the layer with
     * the frame and fills their margins with level content. */
    if(memcmp(out+y*w+play_x,hud+y*256+12,232*sizeof(uint32_t)))return 5;
    continue;
   }
   for(int x=0;x<232;x++)if(out[y*w+97+x]!=hud[y*256+12+x]){fprintf(stderr,"Bar row %d is not the bar layer's at %d\n",y,f);return 3;}
   int staged=bar_first>=0&&y>=bar_first&&y<=bar_anchor;
   if(staged&&memcmp(hud+y*256,native+y*256,256*sizeof(uint32_t))){
    int n=0;for(int x=0;x<256;x++)if(hud[y*256+x]==bar_bg)n++;
    if(n<192){fprintf(stderr,"Repaired bar row %d does not continue the bar at %d (%d/256)\n",y,f,n);return 17;}
   }
   for(int x=0;x<97;x++)if(out[y*w+x]!=0xff000000u||out[y*w+329+x]!=0xff000000u)return 4;
  }
  if(active){
   static uint32_t clean[426*240],corrupted[256*240];
   memcpy(clean,out,sizeof clean);memcpy(corrupted,native,sizeof corrupted);
   for(int y=0;y<192;y++)for(int x=0;x<12;x++){
    corrupted[y*256+x]=0xffff00ffu;corrupted[y*256+244+x]=0xff00ff00u;
   }
   out=smb3_wide_present(corrupted);
   if(memcmp(clean,out,sizeof clean)){fprintf(stderr,"Native garbage leaked through repaired edges at %d\n",f);return 8;}
   out=smb3_wide_present(native);
  }
  if(f==1140||f==1320||f==1500||f==1680){char name[80];snprintf(name,sizeof name,"wide-output-%d.png",f);cyc_write_png(name,out,w,h);snprintf(name,sizeof name,"wide-reference-%d.png",f);cyc_write_png(name,native,256,240);const uint8_t *r=cyc_cpu_ram();printf("Frame %d: active=%d origin=%d camera=%d,y=%d tileset=%d\n",f,active,origin,r[0x12]*256+r[0xfd],r[0x13]*240+r[0xfc],r[0x70a]);for(int i=0;i<5;i++)printf(" slot%d state=%d id=%02x x=%d y=%d layer=%d\n",i,r[0x661+i],r[0x671+i],r[0x76+i]*256+r[0x91+i],r[0x88+i]*256+r[0xa3+i],smb3_enemies_layers()[i].valid);}
 }
 if(active_frames<100||max_camera<256){fprintf(stderr,"No gameplay extension\n");return 6;}
 if(!early_frames||!layer_frames||!retained_frames){fprintf(stderr,"No extended activation/drawing/retention: %u/%u/%u\n",early_frames,layer_frames,retained_frames);return 9;}
 printf("Enemies: %u active samples beyond native right edge; %u with sprite layers; %u retained beyond old left deletion boundary\n",early_frames,layer_frames,retained_frames);
 if(!glue_load_state(L"verify-wide-enemies.bin"))return 13;
 for(int f=1141;f<=1170;f++){
  glue_set_pad1(buttons(f));glue_step_frame();if(cyc_mem_state_hash()!=hashes[f])return 14;
  int16_t audio[4096];glue_get_audio_samples(audio,4096,1);
 }
 puts("Wide enemy save/load: deterministic continuation; isolated sprite drawing leaves live machine unchanged");
 remove("verify-wide-enemies.bin");
 glue_reset();glue_set_widescreen(true);cyc_run_native=false;
 for(int f=0;f<1800;f++){
  glue_set_pad1(buttons(f));glue_step_frame();
  if(cyc_mem_state_hash()!=hashes[f]){fprintf(stderr,"Machine changed at %d\n",f);return 7;}
  int16_t audio[4096];glue_get_audio_samples(audio,4096,1);
 }
 printf("Wide phase: %u frames; max camera %d; 1800 native/interpreter hashes identical with extended enemies; native center/HUD, black HUD margins and poisoned-edge rejection verified\n",active_frames,max_camera);
 glue_set_widescreen(false);glue_reset();cyc_run_native=true;
 for(int f=0;f<=1140;f++){
  glue_set_pad1(buttons(f));glue_step_frame();int16_t audio[4096];glue_get_audio_samples(audio,4096,1);
 }
 const uint8_t *r=cyc_cpu_ram();
 for(int i=0;i<5;i++)if(r[0x661+i]&&r[0x671+i]==0xa6&&r[0x76+i]*256+r[0x91+i]==360){fprintf(stderr,"Native-mode control unexpectedly spawned early plant\n");return 16;}
 puts("Native-mode control: first pipe plant remains unspawned at same camera/frame; widescreen activates and draws it");
 return 0;
}
