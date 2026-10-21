#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glue.h"
#include "cyc_core.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_png.h"
#include "cpu6502.h"
#include "widescreen.h"
#include "hud.h"
#include "wide_enemies.h"
static int warped,stage=4;
static uint16_t objects;
static int verify_native_scene(uint16_t address,uint8_t value){
 uint8_t old=cyc_mod_peek(address);cyc_mod_poke(address,value);smb3_wide_capture();
 const uint32_t *native=glue_get_framebuffer(),*out=smb3_wide_present(native);int valid=!smb3_wide_active();
 int bar_first,bar_anchor;smb3_hud_bounds(&bar_first,&bar_anchor);
 const uint32_t *hud=smb3_hud_present(native);
 for(int y=0;y<240;y++){
  /* The status bar is its own layer: only the rows it rebuilt differ from the
   * machine's picture, and the picture pastes that layer, not the raw frame. */
  int staged=bar_first>=0&&y>=bar_first&&y<=bar_anchor;
  if(!staged&&memcmp(hud+y*256,native+y*256,256*sizeof(uint32_t)))valid=0;
  if(memcmp(out+y*426+97,hud+y*256+12,232*sizeof(uint32_t)))valid=0;
  for(int x=0;x<97;x++)if(out[y*426+x]!=0xff000000u||out[y*426+329+x]!=0xff000000u)valid=0;
 }
 cyc_mod_poke(address,old);smb3_wide_capture();return valid;
}
static void warp(void){
 if(warped||cpu.pc!=0x892a)return;
 uint16_t layout=stage==4?0xb501:0xb3a8;
 cyc_mod_poke(0x70a,4);
 cyc_mod_poke(0x61,layout&255);cyc_mod_poke(0x62,layout>>8);
 cyc_mod_poke(0x65,objects&255);cyc_mod_poke(0x66,objects>>8);
 cyc_mod_poke(0x7eb9,layout&255);cyc_mod_poke(0x7eba,layout>>8);
 cyc_mod_poke(0x7ebb,objects&255);cyc_mod_poke(0x7ebc,objects>>8);
 warped=1;
}
static unsigned buttons(int f){
 if((f>=180&&f<190)||(f>=360&&f<370))return NES_PAD_START;
 if(f>=720&&f<730)return NES_PAD_RIGHT;
 if(f>=840&&f<850)return NES_PAD_UP;
 if(f>=960&&f<970)return NES_PAD_A;
 if(f>=1200)return NES_PAD_RIGHT|NES_PAD_B|((f%90<55)?NES_PAD_A:0);
 return 0;
}
int main(int argc,char **argv){
 if(argc<2)return 1;if(argc>2)stage=atoi(argv[2]);
 FILE *file=fopen(argv[1],"rb");if(!file)return 2;
 uint8_t *rom=malloc(393232);if(fread(rom,1,393232,file)!=393232)return 3;fclose(file);
 const uint8_t sig4[]={1,0xd3,0,0,0xd4,1,0x2c};
 const uint8_t sig6[]={1,0x6d,0x13,0x12,0x6d,0x1a,0x18};
 const uint8_t *sig=stage==4?sig4:sig6;
 for(unsigned i=0;i<8192-7;i++)if(!memcmp(rom+16+6*8192+i,sig,7))objects=0xc000+i;
 free(rom);if(!objects||!glue_load_rom(argv[1]))return 4;
 glue_set_widescreen(true);cyc_render_capture_background(true);cyc_mod_set_return_hook(warp);
 unsigned compared=0,bad=0,aligned=0;
 for(int f=0;f<2600;f++){
  /* Test-only camera traversal: keep the actor alive above the gaps so both
   * stages exercise moving platforms across the original viewport edges. */
  if(f>1100&&warped){
   const uint8_t *r=cyc_cpu_ram();int x=stage==4?r[0x12]*256+r[0xfd]+112:24+(f-1100)*2;
   cyc_mod_poke(0x75,x>>8);cyc_mod_poke(0x90,x&255);
   cyc_mod_poke(0x87,1);cyc_mod_poke(0xa2,64);cyc_mod_poke(0xcf,0);cyc_mod_poke(0xf1,0);cyc_mod_poke(0x7cff,1);
  }
  glue_set_pad1(buttons(f));glue_step_frame();int16_t audio[4096];glue_get_audio_samples(audio,4096,1);
  int w,h;const uint32_t *out=glue_get_present_framebuffer(&w,&h);
  if(f==1140||f==1440||f==1740||f==2040||f==2340){char path[80];snprintf(path,sizeof path,"scene-%d-%d.png",stage,f);cyc_write_png(path,out,w,h);
   static uint32_t bg[256*240];const uint8_t *r=cyc_cpu_ram();
   printf("stage %d frame %d warped %d mode=%02x raster=%02x set=%d camera=%d,%d\n",stage,f,warped,r[0x100],r[0x101],r[0x70a],r[0x12]*256+r[0xfd],r[0x13]*240+r[0xfc]);
   if(smb3_wide_active()&&cyc_render_background(bg)){
    unsigned frame_bad=0;static uint32_t map[256*240];int camera_x,camera_y;smb3_wide_frame_camera(r,&camera_x,&camera_y);
    for(int y=0;y<240;y++)for(int x=0;x<256;x++)map[y*256+x]=smb3_wide_terrain_argb(camera_x+x,camera_y+y);
    for(int y=8;y<192;y++)for(int x=16;x<240;x++){
     unsigned expected=smb3_wide_terrain_argb(camera_x+x,camera_y+y);
     compared++;if(expected!=bg[y*256+x]){bad++;frame_bad++;}
    }
    printf(" frame background mismatches=%u\n",frame_bad);
    snprintf(path,sizeof path,"scene-%d-%d-bg.png",stage,f);cyc_write_png(path,bg,256,240);
    snprintf(path,sizeof path,"scene-%d-%d-map.png",stage,f);cyc_write_png(path,map,256,240);
    const Smb3EnemyLayer *layers=smb3_enemies_layers();const uint8_t *oam=cyc_render_oam();
    for(int object=0;object<SMB3_OBJECT_SLOTS;object++)if(layers[object].valid){
     for(int k=0;k<layers[object].count;k++){
      const Smb3EnemyPiece *pc=&layers[object].pieces[k];
      int px=layers[object].obj_x+layers[object].off_x+pc->dx-camera_x,py=layers[object].obj_y+layers[object].off_y+pc->dy-camera_y+1;
      for(int s=0;s<64;s++){
       const uint8_t *p=oam+s*4;
       if(p[0]>=191||p[1]!=pc->tile||p[2]!=pc->attr)continue;
       if((int)p[3]==px&&(int)p[0]+1==py){aligned++;break;}
      }
     }
    }
   }
   if(f==1140){
    if(!verify_native_scene(0x559,1)||!verify_native_scene(0x7974,1)||
       !verify_native_scene(0x100,0x40)||!verify_native_scene(0x101,0x60)||
       !verify_native_scene(0x14,1))return 5;
    puts("Reward, card/bonus, raster and exit scenes: native pixels and clean black margins verified");
   }
  }
 }
 printf("Background mismatches: %u / %u; aligned native sprite pieces: %u\n",bad,compared,aligned);
 return !warped||bad||!compared;
}
