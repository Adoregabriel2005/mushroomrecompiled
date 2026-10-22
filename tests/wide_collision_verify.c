#include <stdio.h>
#include <stdlib.h>
#include "glue.h"
#include "cyc_core.h"
#include "cyc_mod.h"
#include "cyc_run.h"
#include "wide_enemies.h"

/* The 1-1 entry script from widescreen_verify, stopped at the settled first
 * screen. The wide picture already shows a ? block in the right margin and
 * other ? blocks inside the native frame. */
static unsigned buttons(int f){
 if((f>=180&&f<190)||(f>=360&&f<370))return NES_PAD_START;
 if(f>=720&&f<730)return NES_PAD_RIGHT;
 if(f>=840&&f<850)return NES_PAD_UP;
 if(f>=960&&f<970)return NES_PAD_A;
 return 0;
}

typedef struct {
 int slot,x,y,off;
 int camera;
 uint8_t before;
} Hit;

/* Final data reads of the live tile map. The runner calls this at the exact
 * instruction PC, so it also proves the margin hit consults the world page. */
static uint16_t trace_addr,trace_pc[16];
static unsigned trace_count;
static uint8_t trace_map_read(uint16_t pc,uint16_t addr,uint8_t value){
 if(addr==trace_addr&&trace_count<16)trace_pc[trace_count++]=pc;
 return value;
}

static const uint8_t *state(void){return cyc_cpu_ram();}

static void cleanup_state(void){remove("wide-collision-state.bin");}

static int run_to_level(void){
 for(int f=0;f<=1140;f++){
  glue_set_pad1(buttons(f));glue_step_frame();
  int16_t audio[4096];glue_get_audio_samples(audio,4096,1);
 }
 const uint8_t *r=state();
 if(!smb3_enemies_scene(r)){fprintf(stderr,"1-1 did not reach a wide scene\n");return 1;}
 return 0;
}

static int find_hits(Hit *margin,Hit *native){
 const uint8_t *r=state();
 size_t len=0;const uint8_t *map=cyc_cart_ram(&len);
 if(len<8192){fprintf(stderr,"Cart work RAM is not the 8KB SMB3 map\n");return 1;}
 int camera=r[0x12]*256+r[0xfd],wide=smb3_enemies_camera(r);
 margin->slot=native->slot=-1;
 for(int row=0;row<27;row++)for(int col=0;col<240;col++){
  int off=(col/16)*432+row*16+(col&15);uint8_t tile=map[off];
  if(tile<0x60||tile>0x65)continue;
  int x=col*16,y=row*16;
  if(margin->slot<0&&x>=camera+256&&x<wide+426){
   margin->slot=0;margin->x=x-10;margin->y=y;margin->off=off;
   margin->camera=camera;margin->before=tile;
  }
  if(native->slot<0&&x>=camera+64&&x+16<=camera+192){
   native->slot=0;native->x=x-10;native->y=y;native->off=off;
   native->camera=camera;native->before=tile;
  }
 }
 if(margin->slot<0||native->slot<0){
  fprintf(stderr,"No margin/native ? block pair at camera %d\n",camera);return 1;
 }
 /* Level_DoBumpBlocks needs object slots 6/7 for the bump effect; using one
  * of them for the shell would suppress the map write and hide the bug. */
 if(r[0x661+margin->slot]||r[0x661+6]||r[0x661+7]){
  fprintf(stderr,"Shell or block-bump slot occupied at frame %llu\n",(unsigned long long)glue_frame_count());return 1;
 }
 return 0;
}

static void plant(const Hit *h){
 const uint8_t *r=state();
 int vscroll=r[0x13]*240+r[0xfc];
 int slot=h->slot;
 cyc_mod_poke((uint16_t)(0x661 + slot),5);          /* OBJSTATE_KICKED */
 cyc_mod_poke((uint16_t)(0x671 + slot),0x6c);       /* green Koopa shell */
 cyc_mod_poke((uint16_t)(0x91 + slot),(uint8_t)h->x);
 cyc_mod_poke((uint16_t)(0x76 + slot),(uint8_t)(h->x>>8));
 cyc_mod_poke((uint16_t)(0xa3 + slot),(uint8_t)h->y);
 cyc_mod_poke((uint16_t)(0x88 + slot),(uint8_t)(h->y>>8));
 cyc_mod_poke((uint16_t)(0xbe + slot),0x18);        /* real kick speed, right */
 cyc_mod_poke((uint16_t)(0xd0 + slot),0);
 cyc_mod_poke((uint16_t)(0xac + slot),(uint8_t)(h->x-h->camera));
 cyc_mod_poke((uint16_t)(0xb5 + slot),(uint8_t)(h->y-vscroll));
 cyc_mod_poke((uint16_t)(0x651 + slot),0);
 cyc_mod_poke((uint16_t)(0x681 + slot),0);
 cyc_mod_poke((uint16_t)(0xd9 + slot),0);
 cyc_mod_poke((uint16_t)(0x659 + slot),0xff);       /* manually placed */
 cyc_mod_poke((uint16_t)(0x669 + slot),0);
 cyc_mod_poke((uint16_t)(0x679 + slot),0);
}

static int run_hit(const Hit *h,uint64_t *hashes,int margin){
 const uint8_t *r=state();
 size_t len=0;const uint8_t *map=cyc_cart_ram(&len);
 if(len<8192||map[h->off]!=h->before){
  fprintf(stderr,"Fixture map tile changed before the hit\n");return 1;
 }
 if(margin?(h->x<h->camera+256):(h->x+16>h->camera+256)){
  fprintf(stderr,"Fixture shell starts on the wrong side of the native frame\n");return 1;
 }
 trace_addr=(uint16_t)(0x6000+h->off);trace_count=0;
 cyc_mod_set_cart_read_hook(trace_map_read);
 plant(h);
 int first_hvis=-1;
 for(int f=0;f<30;f++){
  glue_set_pad1(0);glue_step_frame();
  if(f==0)first_hvis=r[0x651+h->slot];
  if(hashes)hashes[f]=cyc_mem_state_hash();
  int16_t audio[4096];glue_get_audio_samples(audio,4096,1);
 }
 cyc_mod_set_cart_read_hook(NULL);
 if(map[h->off]!=0x5f){
  fprintf(stderr,"%s ? block at x=%d did not become empty (%02x)\n",margin?"Margin":"Native",h->x+10,map[h->off]);
  return 1;
 }
 if(!trace_count){
  fprintf(stderr,"%s hit never read map offset %d\n",margin?"Margin":"Native",h->off);
  return 1;
 }
 if(margin&&!first_hvis){
  fprintf(stderr,"Margin shell lost its native SprHVis (would paint a wrapped ghost)\n");
  return 1;
 }
 return 0;
}

static int compare_hashes(const uint64_t *a,const uint64_t *b){
 for(int f=0;f<30;f++)if(a[f]!=b[f]){
  fprintf(stderr,"Recompiled/interpreted scenario diverged at hit frame %d\n",f);
  return 1;
 }
 return 0;
}

int main(int argc,char **argv){
 if(argc<2||!glue_load_rom(argv[1]))return 1;
 atexit(cleanup_state);
 glue_set_widescreen(true);
 if(run_to_level())return 2;
 Hit margin,native;
 if(find_hits(&margin,&native))return 3;
 uint64_t compiled[30],pre_state=cyc_mem_state_hash();
 if(!glue_save_state_utf8("wide-collision-state.bin"))return 4;
 if(run_hit(&margin,compiled,1))return 5;
 unsigned compiled_reads=trace_count;
 uint16_t compiled_pc=trace_pc[0];
 if(!glue_load_state_utf8("wide-collision-state.bin"))return 6;
 if(run_hit(&native,NULL,0))return 7;

 /* Replay the same entry and the same margin hit with the interpreter. The
  * block gate reads guest state only, so the two engines must agree frame for
  * frame, including the map write. */
 glue_reset();glue_set_widescreen(true);cyc_run_native=false;
 if(run_to_level())return 8;
 Hit margin2,native2;
 if(find_hits(&margin2,&native2))return 9;
 if(cyc_mem_state_hash()!=pre_state){
  fprintf(stderr,"Recompiled/interpreted state differs before the scenario\n");return 10;
 }
 uint64_t interpreted[30];
 if(run_hit(&margin2,interpreted,1))return 11;
 if(compare_hashes(compiled,interpreted))return 12;
 printf("Wide block collision: margin ? block %02x->5f at x=%d (%u map reads, first pc %04x), native control %02x->5f, HVis preserved, 30 recompiled/interpreted hashes identical\n",
  margin.before,margin.x+10,compiled_reads,compiled_pc,native.before);
 return 0;
}
