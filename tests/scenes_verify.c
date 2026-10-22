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
 int split=cyc_frame_split_line();int layer_top=split>=0&&split<240?split:240;
 /* The bar can never start above the split that creates it. */
 if(bar_first>=0&&bar_first<layer_top)valid=0;
 for(int y=0;y<240;y++){
  /* The status bar is its own layer: the rows from the split down are rebuilt
   * from the frame's own captures (hud.cpp's hud_build), so only they may
   * differ from the machine's picture, and the picture pastes that layer, not
   * the raw frame. */
  int staged=y>=layer_top&&y<=bar_anchor;
  if(!staged&&memcmp(hud+y*256,native+y*256,256*sizeof(uint32_t)))valid=0;
  if(memcmp(out+y*426+97,hud+y*256+12,232*sizeof(uint32_t)))valid=0;
  for(int x=0;x<97;x++)if(out[y*426+x]!=0xff000000u||out[y*426+329+x]!=0xff000000u)valid=0;
 }
 cyc_mod_poke(address,old);smb3_wide_capture();return valid;
}
static int verify_wide_end_scene(uint16_t address,uint8_t value){
 uint8_t old=cyc_mod_peek(address);cyc_mod_poke(address,value);smb3_wide_capture();
 uint64_t before=cyc_mem_state_hash();
 const uint32_t *native=glue_get_framebuffer(),*out=smb3_wide_present(native);
 const uint32_t *hud=smb3_hud_present(native);
 int valid=smb3_wide_active(),origin=smb3_wide_origin();
 int bar_top=smb3_hud_top();if(bar_top>240)bar_top=240;
 if(origin<0||origin>170)valid=0;
 for(int y=0;y<240;y++){
  int x=y<bar_top?origin+12:97;
  if(memcmp(out+y*426+x,hud+y*256+12,232*sizeof(uint32_t)))valid=0;
  if(y>=bar_top)for(int i=0;i<97;i++)
   if(out[y*426+i]!=0xff000000u||out[y*426+329+i]!=0xff000000u)valid=0;
 }
 int colored=0;
 for(int y=0;y<bar_top&&!colored;y++)for(int x=0;x<426;x++){
  if(x>=origin+12&&x<origin+244)continue;
  if(out[y*426+x]!=0xff000000u){colored=1;break;}
 }
 if(!colored)valid=0;
 if(cyc_mem_state_hash()!=before)valid=0;
 cyc_mod_poke(address,old);smb3_wide_capture();return valid;
}
static int verify_end_spawn_blocked(uint16_t address,uint8_t value){
 const uint8_t *r=cyc_cpu_ram();int free=0;
 for(int s=0;s<5;s++)if(!r[0x661+s])free++;
 int count=0;while(count<48&&cyc_mod_peek((uint16_t)(0x7b41+count*3))!=255)count++;
 if(free<2||count<1)return 0;
 int i=count-1;uint16_t rec=(uint16_t)(0x7b41+i*3),bit=(uint16_t)(0x7b10+i);
 int camera=smb3_enemies_camera(r),x=((camera+426+15)&~15);
 if(x<0||x>4080)return 0;
 uint8_t old_flag=cyc_mod_peek(address),old_id=cyc_mod_peek(rec);
 uint8_t old_col=cyc_mod_peek((uint16_t)(rec+1)),old_row=cyc_mod_peek((uint16_t)(rec+2));
 uint8_t old_bit=cyc_mod_peek(bit);
 /* Put one Goomba record on the first clear column beyond the wide picture,
  * with its spawned bit clear. The end-card/run-off tick must not claim it. */
 cyc_mod_poke(rec,0x72);cyc_mod_poke((uint16_t)(rec+1),(uint8_t)(x>>4));cyc_mod_poke((uint16_t)(rec+2),25);
 cyc_mod_poke(bit,0);cyc_mod_poke(address,value);
 uint64_t before=cyc_mem_state_hash();
 smb3_enemies_tick();
 int valid=cyc_mem_state_hash()==before;
 cyc_mod_poke(rec,old_id);cyc_mod_poke((uint16_t)(rec+1),old_col);cyc_mod_poke((uint16_t)(rec+2),old_row);
 cyc_mod_poke(bit,old_bit);cyc_mod_poke(address,old_flag);smb3_wide_capture();
 return valid;
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
    if(!verify_wide_end_scene(0x559,1)||!verify_wide_end_scene(0x7974,1)||
       !verify_end_spawn_blocked(0x559,1)||!verify_end_spawn_blocked(0x7974,1)||
       !verify_native_scene(0x100,0x40)||!verify_native_scene(0x101,0x60)||
       !verify_native_scene(0x14,1))return 5;
    puts("Level-end card/run-off: wide frame, composed margins and no late spawns; raster and exit scenes: native pixels and clean black margins verified");
   }
  }
 }
 printf("Background mismatches: %u / %u; aligned native sprite pieces: %u\n",bad,compared,aligned);
 return !warped||bad||!compared;
}
