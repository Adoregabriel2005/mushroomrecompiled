#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glue.h"
#include "cyc_core.h"
#include "cyc_png.h"
#include "widescreen.h"
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
 uint64_t hashes[1800];unsigned active_frames=0;int max_camera=0;
 glue_set_widescreen(true);
 for(int f=0;f<1800;f++){
  glue_set_pad1(buttons(f));glue_step_frame();hashes[f]=cyc_mem_state_hash();
  int16_t audio[4096];glue_get_audio_samples(audio,4096,1);
  int w,h;const uint32_t *out=glue_get_present_framebuffer(&w,&h),*native=glue_get_framebuffer();
  if(w!=426||h!=240)return 2;
  int active=smb3_wide_active(),origin=smb3_wide_origin();if(active){active_frames++;const uint8_t *state=cyc_cpu_ram();int camera=state[0x12]*256+state[0xfd];if(camera>max_camera)max_camera=camera;}
  for(int y=192;y<240;y++){
   if(memcmp(out+y*w+85,native+y*256,256*sizeof(uint32_t)))return 3;
   for(int x=0;x<85;x++)if(out[y*w+x]!=0xff000000u||out[y*w+341+x]!=0xff000000u)return 4;
  }
  for(int y=0;y<192;y++)if(memcmp(out+y*w+origin+8,native+y*256+8,248*sizeof(uint32_t)))return 5;
  if(f==1140||f==1320||f==1500||f==1680){char name[80];snprintf(name,sizeof name,"wide-output-%d.png",f);cyc_write_png(name,out,w,h);snprintf(name,sizeof name,"wide-reference-%d.png",f);cyc_write_png(name,native,256,240);const uint8_t *r=cyc_cpu_ram();printf("Frame %d: active=%d origin=%d camera=%d,y=%d tileset=%d\n",f,active,origin,r[0x12]*256+r[0xfd],r[0x13]*240+r[0xfc],r[0x70a]);}
 }
 if(active_frames<100||max_camera<256){fprintf(stderr,"No gameplay extension\n");return 6;}
 glue_reset();glue_set_widescreen(false);
 for(int f=0;f<1800;f++){
  glue_set_pad1(buttons(f));glue_step_frame();
  if(cyc_mem_state_hash()!=hashes[f]){fprintf(stderr,"Machine changed at %d\n",f);return 7;}
  int16_t audio[4096];glue_get_audio_samples(audio,4096,1);
 }
 printf("Wide phase: %u frames; max camera %d; 1800 machine hashes identical; native core/HUD and black HUD margins verified\n",active_frames,max_camera);
 return 0;
}





