/* USA Rev 1 object RAM and routine research: Southbird SMB3 disassembly,
 * PRG000 Object_DoStateAction/Object_DeleteOffScreen and PRG005 spawner.
 * Controllers/events keep their original triggers. Real objects use their
 * existing slots, state machines, world coordinates and collision routines. */
#include "wide_enemies.h"
#include "cyc_core.h"
#include "cyc_mod.h"
#include "cpu6502.h"
#include <string.h>
static bool enabled;
static Smb3EnemyLayer layers[5];
bool smb3_enemies_scene(const uint8_t *r){
 int set=r[0x70a],screens=r[0x372]+1;
 return set>=1&&set<=14&&set!=7&&!r[0x3ef]&&screens>1&&screens<=15&&r[0x100]!=0x80&&r[0x561]&&!r[0x55f]&&!r[0x3de];
}
int smb3_enemies_camera(const uint8_t *r){
 int camera=r[0x12]*256+r[0xfd]-85,max=(r[0x372]+1)*256-426;
 if(camera<0)camera=0;if(camera>max)camera=max;return camera;
}
static bool object_bank(void){
 return cyc_mod_peek(0xca46)==0xbd&&cyc_mod_peek(0xca47)==0x61&&cyc_mod_peek(0xca48)==6;
}
static uint8_t read_policy(uint16_t pc,uint16_t addr,uint8_t value){
 if(!enabled||(pc!=0xd40f&&pc!=0xd41a)||cyc_mod_isolated())return value;
 const uint8_t *r=cyc_cpu_ram();
 if(!smb3_enemies_scene(r)||!object_bank())return value;
 /* Original deletion chooses alternating right/left tests in Y. Move each
  * boundary to the matching edge of the expanded, level-clamped viewport. */
 int camera=smb3_enemies_camera(r)+((cpu.y&1)?0:170);
 if(pc==0xd40f&&addr==0xfd)return (uint8_t)camera;
 if(pc==0xd41a&&addr==0x12)return (uint8_t)(camera>>8);
 return value;
}
void smb3_enemies_enable(bool on){enabled=on;cyc_mod_set_ram_read_hook(on?read_policy:NULL);if(!on)memset(layers,0,sizeof layers);}
void smb3_enemies_reset(void){memset(layers,0,sizeof layers);}
const Smb3EnemyLayer *smb3_enemies_layers(void){return layers;}
void smb3_enemies_tick(void){
 const uint8_t *r=cyc_cpu_ram();
 if(!enabled||!smb3_enemies_scene(r))return;
 int camera=smb3_enemies_camera(r);
 int count=0;while(count<48&&cyc_mod_peek((uint16_t)(0x7b41+count*3))!=255)count++;
 if(count==48)return; /* A new scene has not finished loading its records. */
 /* Same level records and spawned bits as Level_ObjectsSpawnByScroll.
  * Keep the five major slots: never overwrite a live object or trigger an
  * event/controller early. Init runs normally on the next game update. */
 for(int i=0;i<count;i++){
  uint16_t rec=(uint16_t)(0x7b41+i*3);int id=cyc_mod_peek(rec);
  if(id==255)break;
  if(id>=0xb4||id==0x41||(cyc_mod_peek((uint16_t)(0x7b10+i))&0x80))continue;
  int x=cyc_mod_peek(rec+1)*16,y=cyc_mod_peek(rec+2)*16;
  if(x<camera-64||x>camera+426+16)continue;
  int slot=4;while(slot>=0&&r[0x661+slot])slot--;
  if(slot<0)break;
  cyc_mod_poke((uint16_t)(0x91+slot),(uint8_t)x);
  cyc_mod_poke((uint16_t)(0x76+slot),(uint8_t)(x>>8));
  cyc_mod_poke((uint16_t)(0xa3+slot),(uint8_t)y);
  cyc_mod_poke((uint16_t)(0x88+slot),(uint8_t)(y>>8));
  cyc_mod_poke((uint16_t)(0x671+slot),(uint8_t)id);
  cyc_mod_poke((uint16_t)(0x659+slot),(uint8_t)i);
  cyc_mod_poke((uint16_t)(0x7b10+i),cyc_mod_peek((uint16_t)(0x7b10+i))|0x80);
  cyc_mod_poke((uint16_t)(0x661+slot),1);
 }
}
void smb3_enemies_capture(void){
 memset(layers,0,sizeof layers);
 const uint8_t *r=cyc_cpu_ram();
 if(!enabled||!smb3_enemies_scene(r))return;
 int native=r[0x12]*256+r[0xfd],wide=smb3_enemies_camera(r);
 for(int slot=0;slot<5;slot++){
  int x=r[0x76+slot]*256+r[0x91+slot],state=r[0x661+slot];
  if(state<2||x+64<wide||x>=wide+426||(x>=native&&x+64<native+256))continue;
  int camera=x>64?x-64:0;
  if(!cyc_mod_isolate_begin())continue;
  /* Draw the actual object's current state in a scratch machine with a
   * shifted camera. All guest RAM, mapper, registers and clocks are restored.
   * The host only retains the produced OAM; no speculative movement commits. */
  cyc_mod_poke(0xfd,(uint8_t)camera);cyc_mod_poke(0x12,(uint8_t)(camera>>8));
  cyc_mod_poke(0xce,1);cyc_mod_poke(0xcd,(uint8_t)slot);
  for(int i=0;i<64;i++)cyc_mod_poke((uint16_t)(0x200+i*4),0xf8);
  CycModRegs regs;cyc_mod_regs(&regs);
  cyc_mod_poke(0x71f,0);
  bool mapped=cyc_mod_call(0xffd1,&regs)&&object_bank();
  regs.x=(uint8_t)slot;
  if(mapped&&cyc_mod_call(0xca46,&regs)){
   for(int i=0;i<256;i++)layers[slot].oam[i]=cyc_mod_peek((uint16_t)(0x200+i));
   layers[slot].camera=camera;layers[slot].valid=true;
  }
  cyc_mod_isolate_end();
  r=cyc_cpu_ram();
 }
}
