/* USA Rev 1 object RAM and routine research: Southbird SMB3 disassembly,
 * PRG000 Object_DoStateAction/Object_DeleteOffScreen and PRG005 spawner.
 * Controllers/events keep their original triggers. Real objects use their
 * existing slots, state machines, world coordinates and collision routines. */
#include "wide_enemies.h"
#include "glue.h"
#include "cyc_core.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "widescreen.h"
#include "cpu6502.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool enabled;
static Smb3EnemyLayer layers[SMB3_OBJECT_SLOTS];
/* Frame this capture describes (see smb3_enemies_capture). */
static uint64_t captured_frame;
static bool captured_frame_valid;
/* Frames left in the post-load grace. The loader builds the level then
 * sweeps its first screen calling the spawner, which creates the opening
 * objects; the extended window stays off until that sweep is done, so far
 * records cannot eat the five slots out from under it. */
static int load_grace;
static bool scene_prev;
void smb3_wide_level_load(void){load_grace=4;}
/* One-frame hold for skipped draws: piranha plants skip their drawer every
 * fourth frame, so a fresh isolated capture can be legitimately empty. Like
 * smb3pc's held pieces, the last drawn OAM is repeated once; a second empty
 * frame shows nothing, as the game does. */
static int8_t prev_offx[SMB3_OBJECT_SLOTS],prev_offy[SMB3_OBJECT_SLOTS];
static uint8_t prev_id[SMB3_OBJECT_SLOTS],prev_rec[SMB3_OBJECT_SLOTS];
static bool prev_have[SMB3_OBJECT_SLOTS];
/* Frame-OAM slots owned by managed layers (see header). Rebuilt every
 * capture; the edge repair consults it so no sprite is painted twice. */
static uint64_t claimed_oam;
uint64_t smb3_enemies_claimed_oam(void){return claimed_oam;}
/* Pre-update snapshot: the full dynamic slot state from the end of the
 * previous frame. The re-run executes the object's state action in scratch
 * (with gameplay halted, so it draws without advancing), and every input it
 * reads must match what the frame's own update saw: positions (drawn
 * before movement is applied), velocities, the Var1-Var5 phase variables
 * (Var3>>3 selects the piranha's animation frame - live Var3 is already one
 * tick ahead, which showed the wrong head/stem tiles every 8th tick),
 * timers, frame/flip/sprite scratch and visibility flags. Starting from
 * live RAM drew everything one tick ahead of the picture, and no placement
 * vote can repair phase-shifted pieces. Tenant- and state-checked (id +
 * level-record + state); anything else is drawn live for one frame (the
 * anchor vote covers the seam) and seeds the snapshot for the next. */
typedef struct {
 bool have;
 uint8_t id,rec,state;
 uint8_t x,xhi,y,yhi,xvel,yvel,var1,var2,var4,var5,frame,flip;
 uint8_t sprx,spry,varbss,sprhvis,sprvvis,detstat,timer,timer2,var3;
} SnapSlot;
static SnapSlot snap[SMB3_OBJECT_SLOTS];
/* The live state observed at the end of the last capture. It is only moved
 * into snap[] when it differs from the live state now, i.e. when a new frame
 * has begun. Two captures of the same frame (the presenter is called again
 * with a different source image, as the edge-repair test does) must produce
 * the same pieces, so the second call may not overwrite the rewind target
 * with its own post-update state - that drew a different animation tick. */
static SnapSlot seen[SMB3_OBJECT_SLOTS];
static bool seen_valid;
static int snap_vscroll;
static void read_slot(const uint8_t *r,int slot,SnapSlot *sn){
 memset(sn,0,sizeof *sn);
 if(!r[0x661+slot])return;
 sn->have=true;
 sn->id=r[0x671+slot];sn->rec=r[0x659+slot];sn->state=r[0x661+slot];
 sn->x=r[0x91+slot];sn->xhi=r[0x76+slot];
 sn->y=r[0xa3+slot];sn->yhi=r[0x88+slot];
 sn->xvel=r[0xbe + slot];sn->yvel=r[0xd0 + slot];
 sn->var1=r[0x689+slot];sn->var2=r[0x691+slot];
 sn->var5=r[0x9a+slot];
 sn->frame=r[0x669+slot];sn->flip=r[0x679+slot];
 sn->sprx=r[0xac+slot];sn->spry=r[0xb5+slot];
 sn->sprhvis=r[0x651+slot];sn->sprvvis=r[0x681+slot];
 sn->detstat=r[0xd9+slot];
 sn->timer=r[0x518+slot];sn->timer2=r[0x520+slot];
 if(slot<5){
  sn->var4=r[0x7f+slot];
  sn->var3=cyc_mod_peek((uint16_t)(0x7fd0 + slot));
 }
 if(slot<7)sn->varbss=r[0xc6+slot];
}
/* Rotate the snapshot only when the machine has actually advanced. Treating
 * a repeat call as "same frame" is safe even if it is not: identical live
 * state means the rewind target holds exactly the values a rewind would
 * write, so the draw is the same either way. */
static void advance_snapshot(const uint8_t *r){
 SnapSlot cur[SMB3_OBJECT_SLOTS];
 for(int s=0;s<SMB3_OBJECT_SLOTS;s++)read_slot(r,s,&cur[s]);
 if(seen_valid&&memcmp(cur,seen,sizeof cur)==0)return;
 if(seen_valid)memcpy(snap,seen,sizeof snap);
 memcpy(seen,cur,sizeof seen);
 seen_valid=true;
}
static Smb3EnemyPiece held_pieces[SMB3_OBJECT_SLOTS][12];
static uint8_t held_count[SMB3_OBJECT_SLOTS];
static bool held_valid[SMB3_OBJECT_SLOTS];
static uint8_t held_id[SMB3_OBJECT_SLOTS],held_rec[SMB3_OBJECT_SLOTS];
bool smb3_enemies_scene(const uint8_t *r){
 int set=r[0x70a],screens=r[0x372]+1;
 /* The hit end card ($7974) and Player_EndLevel ($559) are still the level:
  * smb3pc widens through both and lets the window follow the run-off. Only
  * Level_ExitToMap ($14), the junction reloads, vertical rooms and non-level
  * scenes return to native framing. */
 return set>=1&&set<=14&&set!=7&&!r[0x3ef]&&screens>1&&screens<=15&&
  r[0x100]==0xc0&&r[0x101]==0&&r[0x561]&&!r[0x55f]&&!r[0x3de]&&!r[0x14];
}
int smb3_enemies_camera(const uint8_t *r){
 int camera=r[0x12]*256+r[0xfd]-85,max=(r[0x372]+1)*256-426;
 if(camera<0)camera=0;
 if(camera>max)camera=max;
 return camera;
}
static bool object_bank(void){
 return cyc_mod_peek(0xca46)==0xbd&&cyc_mod_peek(0xca47)==0x61&&cyc_mod_peek(0xca48)==6;
}
/* The native 256-pixel frame is a presentation crop, not a collision
 * boundary. Object_DetermineHorzVis sets every SprHVis bit for a margin
 * object, and the block-hit paths use Object_AnySprOffscreen as a proxy for
 * relevance, so they discard a wall hit before Object_BumpOffBlocks. The map
 * itself is already addressed by world pages (ObjTile_DetXHi/YHi), so only
 * these native gates need to know the expanded viewport. SprHVis is left
 * untouched: the native OAM draw must keep hiding its wrapped copy. */
static bool margin_block_hit(const uint8_t *r,int slot){
 int x=r[0x76+slot]*256+r[0x91+slot];
 int camera=r[0x12]*256+r[0xfd],wide=smb3_enemies_camera(r);
 return (x<camera||x+16>camera+256)&&x+16>wide&&x<wide+426;
}
/* Return address of the JSR that entered Object_AnySprOffscreen. The routine
 * is two LDAs and an RTS, so the pushed address is still on top when its
 * first operand read runs. */
static uint16_t any_return_address(void){
 uint8_t lo,hi;
 if(!cyc_mod_peek_ok((uint16_t)(0x100|(uint8_t)(cpu.s+1)),&lo)||
    !cyc_mod_peek_ok((uint16_t)(0x100|(uint8_t)(cpu.s+2)),&hi))return 0;
 return (uint16_t)((lo|(hi<<8))+1);
}
static uint8_t read_policy(uint16_t pc,uint16_t addr,uint8_t value){
 if(!enabled||cyc_mod_isolated())return value;
 if(pc!=0xd40f&&pc!=0xd41a&&pc!=0xcc9d&&pc!=0xdd54)return value;
 const uint8_t *r=cyc_cpu_ram();
 if(!smb3_enemies_scene(r)||!object_bank())return value;
 int slot=cpu.x;
 /* ObjState_Kicked's wall response first rejects the native frame's 6/228
  * SpriteX edges, then Object_AnySprOffscreen. A shell in the margin is
  * beyond both, even though Object_Move has already reported the wall. */
 if(pc==0xcc9d&&slot<SMB3_OBJECT_SLOTS&&addr==0xac+slot&&margin_block_hit(r,slot))return 128;
 /* Only the two block-hit callers are widened: the kicked shell's wall
  * response ($CCAE, return $CCB1) and Object_HandleBumpUnderneath ($D16D,
  * return $D170). Other Object_AnySprOffscreen users (poofs, splashes,
  * deletion) must keep their native answer or their wrapped OAM would leak
  * into the middle of the wide picture. */
 if(pc==0xdd54&&slot<SMB3_OBJECT_SLOTS&&addr==0x651+slot&&margin_block_hit(r,slot)){
  uint16_t ret=any_return_address();
  if(ret==0xccb1||ret==0xd170)return 0;
 }
 /* The delete table alternates right/left distances by Y parity (even rows
  * carry high byte $01, odd rows $FF). Like smb3pc's delete slack, move each
  * boundary 176 pixels past the matching edge of the expanded viewport, so
  * an object is only removed once it has left the wide picture instead of
  * popping while still on screen. */
 int camera=smb3_enemies_camera(r)+((cpu.y&1)?-176:176);
 if(pc==0xd40f&&addr==0xfd)return (uint8_t)camera;
 if(pc==0xd41a&&addr==0x12)return (uint8_t)(camera>>8);
 return value;
}
void smb3_enemies_enable(bool on){enabled=on;cyc_mod_set_ram_read_hook(on?read_policy:NULL);captured_frame_valid=false;if(!on){memset(layers,0,sizeof layers);claimed_oam=0;memset(snap,0,sizeof snap);memset(seen,0,sizeof seen);seen_valid=false;}}
void smb3_enemies_reset(void){memset(layers,0,sizeof layers);memset(held_pieces,0,sizeof held_pieces);memset(held_valid,0,sizeof held_valid);memset(prev_have,0,sizeof prev_have);claimed_oam=0;memset(snap,0,sizeof snap);memset(seen,0,sizeof seen);seen_valid=false;load_grace=0;scene_prev=false;captured_frame_valid=false;}
const Smb3EnemyLayer *smb3_enemies_layers(void){return layers;}
void smb3_enemies_tick(void){
 const uint8_t *r=cyc_cpu_ram();
 bool scene=smb3_enemies_scene(r);
 if(!enabled||!scene){scene_prev=false;return;}
 /* The end card and run-off keep the wide picture (smb3_enemies_scene), but
  * the game no longer advances its own scrolling spawner there. Scanning the
  * records here would create late Goombas in the margins, where they can
  * overlap enemies already on screen; keep only what the level spawned. */
 if(r[0x559]||cyc_mod_peek(0x7974))return;
 /* A level load is only signalled from generated code at the sound-reset
  * site; the interpreter never runs that hook, so the object window would
  * start in a different state per execution engine. Entering a gameplay
  * scene is visible in RAM either way, so arm the walk-out window here
  * too: identical behaviour whether the frame ran compiled or interpreted. */
 if(!scene_prev){scene_prev=true;if(load_grace<4)load_grace=4;}
 int camera=smb3_enemies_camera(r);
 int count=0;while(count<48&&cyc_mod_peek((uint16_t)(0x7b41+count*3))!=255)count++;
 if(count==48)return; /* A new scene has not finished loading its records. */
 /* Walk-out right after a load: claim the records already sitting in the
  * margins (like the first pipe's plant) while the reveal still masks it.
  * Steady state stays strictly outside the picture, so nothing ever pops
  * onto the screen mid-game. */
 bool walkout=load_grace>0;
 if(load_grace>0)load_grace--;
 /* Same level records and spawned bits as Level_ObjectsSpawnByScroll.
  * Keep the five major slots: never overwrite a live object or trigger an
  * event/controller early. Init runs normally on the next game update.
  * Only records strictly outside the visible picture are created here; they
  * walk in like smb3pc's widened spawn window. Everything inside is the
  * game's own spawner, so nothing ever pops onto the screen. */
 /* Spawn columns: the first 16-pixel column fully past each picture edge.
  * pic_right always starts past the last visible pixel, so x >= pic_right
  * can never be seen; pic_left straddles the edge when wide is unaligned,
  * so the strict x < pic_left (one whole column clear for 16-aligned
  * records) is the pop-free side. Distance caps keep far records dormant
  * until the camera nears, so opening slots are never eaten by the far end
  * of the level (which would vanish them for good). */
 int pic_left=camera&~15,pic_right=((camera+426+15)&~15);
 /* Reserve slots for the game's own spawner: margin objects must never eat
  * the last free slots, or visible enemies would fail to spawn (a worse
  * pop-in than a late margin walk-in). */
 int free=0;for(int s=0;s<5;s++)if(!r[0x661+s])free++;
 for(int i=0;i<count;i++){
  if(free<2)break;
  uint16_t rec=(uint16_t)(0x7b41 + i*3);int id=cyc_mod_peek(rec);
  if(id==255)break;
  if(id>=0xb4||id==0x41||(cyc_mod_peek((uint16_t)(0x7b10+i))&0x80))continue;
  int x=cyc_mod_peek(rec+1)*16,y=cyc_mod_peek(rec+2)*16;
  /* Never create anything inside the picture the player can see: walk-out
   * merely reaches further out than steady state, it does not relax the
   * visibility rule (a record at screen x=224 popped in on top of Mario
   * while the guard was wider than the frame). */
  int native,frame_y;smb3_wide_frame_camera(r,&native,&frame_y);
  bool off_screen=x+16<=native||x>=native+256;
  bool want;
  if(walkout)want=off_screen&&x>=camera-64&&x<=camera+426+16;
  else want=(x>=pic_right&&x-camera<=480)||(x<pic_left&&camera-x<=64);
  if(!want)continue;
  int slot=4;while(slot>=0&&r[0x661+slot])slot--;
  if(slot<0)break;
  free--;
  cyc_mod_poke((uint16_t)(0x91 + slot),(uint8_t)x);
  cyc_mod_poke((uint16_t)(0x76 + slot),(uint8_t)(x>>8));
  cyc_mod_poke((uint16_t)(0xa3 + slot),(uint8_t)y);
  cyc_mod_poke((uint16_t)(0x88 + slot),(uint8_t)(y>>8));
  cyc_mod_poke((uint16_t)(0x671 + slot),(uint8_t)id);
  cyc_mod_poke((uint16_t)(0x659 + slot),(uint8_t)i);
  cyc_mod_poke((uint16_t)(0x7b10+i),cyc_mod_peek((uint16_t)(0x7b10+i))|0x80);
  cyc_mod_poke((uint16_t)(0x661 + slot),1);
 }
}
/* One capture per frame. The compositor can be asked for the same picture
 * more than once (a resize or a fullscreen switch presents again), and a
 * second capture would advance the rewind window a second time: the layers
 * would then be drawn one update ahead of the picture they belong to, so
 * margin sprites would sit a pixel off - the piranha's stem and pipe mask
 * visibly jump between two presentations of one frame. */
void smb3_enemies_capture(void){
 uint64_t frame=glue_frame_count();
 if(captured_frame_valid&&captured_frame==frame)return;
 captured_frame=frame;captured_frame_valid=true;
 memset(layers,0,sizeof layers);
 claimed_oam=0;
 const uint8_t *r=cyc_cpu_ram();
 if(!enabled||!smb3_enemies_scene(r)){memset(snap,0,sizeof snap);memset(seen,0,sizeof seen);seen_valid=false;return;}
 advance_snapshot(r);
 int native,frame_y;smb3_wide_frame_camera(r,&native,&frame_y);
 int wide=smb3_enemies_camera(r);
 for(int slot=0;slot<SMB3_OBJECT_SLOTS;slot++){
  int x=r[0x76+slot]*256+r[0x91+slot],y=r[0x88+slot]*256+r[0xa3+slot],state=r[0x661+slot];
  if(state<2){held_valid[slot]=false;prev_have[slot]=false;continue;}
  /* Wholly inside the pasted frame: the game's own pixels already show it.
   * The box is the widest object, so a piece straddling the frame's edge
   * is never skipped. Outside the wide picture: nothing to draw. */
  int sx=x-native,sy=y-frame_y;
  if((sx>=14&&sx+64<=242)||x+64<wide||x>=wide+426||sy+32<0||sy-32>=192){held_valid[slot]=false;prev_have[slot]=false;continue;}
  /* Rewind to the pre-update slot state when tenant and state match: the
   * halted state-action re-run then draws exactly what the frame's own
   * update drew (same positions, same Var3 animation tick, same Var4
   * state, same timers) instead of one tick ahead. Pieces and layer origin
   * below use the rewound origin, so they describe the picture on screen. */
  SnapSlot *sn=&snap[slot];
  bool rewound=sn->have&&sn->id==r[0x671+slot]&&sn->rec==r[0x659+slot]&&sn->state==state;
  int ox0=rewound?(sn->xhi*256+sn->x):x,oy0=rewound?(sn->yhi*256+sn->y):y;
  int vscroll=rewound?snap_vscroll:((int)r[0x13]*240+r[0xfc]);
  int camera=ox0>64?ox0-64:0;
  if(!cyc_mod_isolate_begin())continue;
  /* Re-run the object's own state action with the object mid-screen, so no
   * piece is culled at the frame's edge (culled-then-mixed pieces are what
   * made margin sprites flicker). Gameplay is halted ($CE) so the action
   * draws without advancing (no movement, no timer ticks, no spawns in
   * scratch). All guest RAM, mapper, registers and clocks are restored;
   * only relative pieces survive. No movement commits. */
  if(rewound){
   /* Every one of these is a per-slot array: the index MUST be added, or
    * the rewind lands on slot 0 and the object under test keeps its live
    * (already-advanced) values - one animation tick and one frame of motion
    * ahead of the picture. That mismatch is the visible flicker: the plant
    * drew with next-tick FlipBits (wrong palette/flip) and next-frame
    * position, against a placement anchored on the current frame. */
   cyc_mod_poke((uint16_t)(0x91 + slot),sn->x);
   cyc_mod_poke((uint16_t)(0x76 + slot),sn->xhi);
   cyc_mod_poke((uint16_t)(0xa3 + slot),sn->y);
   cyc_mod_poke((uint16_t)(0x88 + slot),sn->yhi);
   cyc_mod_poke((uint16_t)(0xbe + slot),sn->xvel);
   cyc_mod_poke((uint16_t)(0xd0 + slot),sn->yvel);
   cyc_mod_poke((uint16_t)(0x689 + slot),sn->var1);
   cyc_mod_poke((uint16_t)(0x691 + slot),sn->var2);
   cyc_mod_poke((uint16_t)(0x9a + slot),sn->var5);
   cyc_mod_poke((uint16_t)(0x669 + slot),sn->frame);
   cyc_mod_poke((uint16_t)(0x679 + slot),sn->flip);
   cyc_mod_poke((uint16_t)(0xac + slot),sn->sprx);
   cyc_mod_poke((uint16_t)(0xb5 + slot),sn->spry);
   cyc_mod_poke((uint16_t)(0x651 + slot),sn->sprhvis);
   cyc_mod_poke((uint16_t)(0x681 + slot),sn->sprvvis);
   cyc_mod_poke((uint16_t)(0xd9 + slot),sn->detstat);
   cyc_mod_poke((uint16_t)(0x518 + slot),sn->timer);
   cyc_mod_poke((uint16_t)(0x520 + slot),sn->timer2);
   if(slot<5){
    cyc_mod_poke((uint16_t)(0x7f + slot),sn->var4);
    cyc_mod_poke((uint16_t)(0x7fd0 + slot),sn->var3);
   }
   if(slot<7)cyc_mod_poke((uint16_t)(0xc6 + slot),sn->varbss);
  }
  cyc_mod_poke(0xfd,(uint8_t)camera);cyc_mod_poke(0x12,(uint8_t)(camera>>8));
  cyc_mod_poke(0xce,1);cyc_mod_poke(0xcd,(uint8_t)slot);
  /* Give the re-run a sprite base that cannot straddle the OAM page. The
   * allocator rotates Object_SprRAM every frame ($058F = SprRamOffsets[slot
   * + Counter_7to0]), and a drawer advances it with an 8-bit add: at base
   * $E8 the piranha's base becomes $F8 and its 16x32 block runs past $02FF.
   * The stem then wraps to OAM offset 0, and the handler's palette fix-up
   * (STA Sprite_RAM+$0A,Y) lands at $0302 - outside the page the PPU DMAs -
   * so the stem keeps the head's palette for that one frame and flashes.
   * The offset inside OAM is irrelevant to us (pieces are relative), so a
   * page-safe base draws the same sprite with no overflow. It still depends
   * on the slot, and stays ordered by it: the margin pass sorts pieces by
   * this offset for hardware priority, and a wrapped stem would sort ahead
   * of the black pipe mask and paint over it, leaving the stem too long.
   * slot*16 keeps every object's block (mask -8, body +0x20) inside the
   * page: the highest base is 0x80, ending at 0xA7. */
  cyc_mod_poke((uint16_t)(0x58f + slot),(uint8_t)(0x40 + slot*16));
  for(int i=0;i<64;i++)cyc_mod_poke((uint16_t)(0x200+i*4),0xf8);
  CycModRegs regs;cyc_mod_regs(&regs);
  cyc_mod_poke(0x71f,0);
  bool mapped=cyc_mod_call(0xffd1,&regs)&&object_bank();
  regs.x=(uint8_t)slot;

  /* Only the same object may reuse its hold: slots are recycled, and a
   * new arrival must never wear the previous tenant's sprites. */
  bool same=held_valid[slot]&&held_id[slot]==r[0x671+slot]&&held_rec[slot]==r[0x659+slot];
  Smb3EnemyPiece pieces[12];int n=0;
  if(mapped&&cyc_mod_call(0xca46,&regs)){
   /* Pieces relative to the object's screen origin (64: the shifted
    * camera put it there). Scroll-independent, so the margin copy lands
    * exactly where the frame's own draw put it. Anything farther than
    * the widest drawer box is a stale byte from another OAM writer. */
   for(int off=0;off<256&&n<12;off+=4){
    uint8_t ry=cyc_mod_peek((uint16_t)(0x200+off));
    if(ry==0xf8||ry>=239)continue;
    int dx=(int8_t)(cyc_mod_peek((uint16_t)(0x200+off+3))-64);
    /* ry is screen-relative; the object's screen row at draw time is its
     * (rewound) world row minus the pre-update vertical scroll. */
    int obj_sy=oy0-vscroll;
    int dy=(int8_t)(ry-(obj_sy&0xff));
    if(dx<-64||dx>64||dy<-64||dy>64)continue;
    pieces[n].dx=(int8_t)dx;pieces[n].dy=(int8_t)dy;
    pieces[n].tile=cyc_mod_peek((uint16_t)(0x200+off+1));
    pieces[n].attr=cyc_mod_peek((uint16_t)(0x200+off+2));
    pieces[n].slot=(int16_t)off;
    n++;
   }
  }
  if(n==0&&same){
   /* Skipped draw (piranha plants skip one frame in four): repeat last
    * frame's pieces once, as the game does. */
   for(int k=0;k<held_count[slot]&&k<12;k++)pieces[k]=held_pieces[slot][k];
   n=held_count[slot];held_valid[slot]=false;
  }else if(n>0){
   for(int k=0;k<n;k++)held_pieces[slot][k]=pieces[k];
   held_count[slot]=(uint8_t)n;
   held_valid[slot]=true;
   held_id[slot]=r[0x671+slot];held_rec[slot]=r[0x659+slot];
  }
  /* Anchor the pieces to the frame's own sprites by CONSENSUS, not by a
   * first/closest match. Every (piece, sprite) pair with the same tile and
   * attributes within 8px votes for its (dx,dy); the anchor is the winning
   * offset. A single stale copy of the same tile (the rotating OAM region
   * leaves them one region shift away) or a piece whose sprite the hardware
   * dropped can no longer swing the whole object by itself - on an
   * animation frame change that used to flip the anchor by 2px for one
   * frame and jump the plant's stem. A correct consensus has almost every
   * piece agreeing, so the anchor is rock-steady across animation frames.
   * All pairs near the winner are then claimed: the edge repair must not
   * repaint a sprite we draw, or the two copies sit 1-2px apart and the
   * stem reads as thicker. Claiming by proximity to the consensus (not by
   * first match) keeps that suppression exact. With no votes at all (the
   * object is entirely outside the game's own view) the previous anchor is
   * kept - there is no seam to honour, so any consistent placement is
   * invisible. */
  int8_t ox=0,oy=0;
  if(n>0){
   const uint8_t *oam=cyc_render_oam();
   /* votes[dx+8][dy+8] */
   static uint8_t votes[17][17];
   memset(votes,0,sizeof votes);
   int px0[12],py0[12];
   for(int k=0;k<n;k++){
    px0[k]=ox0+pieces[k].dx-native;
    py0[k]=oy0+pieces[k].dy+1-frame_y;
   }
   for(int k=0;k<n;k++){
    if(px0[k]<-8||px0[k]>263||py0[k]<-8||py0[k]>199)continue;
    for(int s=0;s<64;s++){
     const uint8_t *p=oam+s*4;
     if(p[0]>=191)continue;
     /* tile + flip bits identify the sprite; the palette bits do not. The
      * game can disagree with itself about the palette for one frame (see
      * the OAM-page note above), and that must never move the sprite. */
     if(p[1]!=pieces[k].tile||(p[2]&0xc0)!=(pieces[k].attr&0xc0))continue;
     int ddx=(int)p[3]-px0[k],ddy=(int)p[0]+1-py0[k];
     if(ddx<-8||ddx>8||ddy<-8||ddy>8)continue;
     if(votes[ddx+8][ddy+8]<255)votes[ddx+8][ddy+8]++;
    }
   }
   int bestv=0,bx=0,by=0;
   int total=0;
   for(int q=0;q<17;q++)for(int w=0;w<17;w++)total+=votes[q][w];
   for(int ddx=-8;ddx<=8;ddx++)for(int ddy=-8;ddy<=8;ddy++){
    int v=votes[ddx+8][ddy+8];
    if(!v)continue;
    /* Prefer the most agreement; ties go to the smallest correction. */
    if(v>bestv||(v==bestv&&(abs(ddx)+abs(ddy))<(abs(bx)+abs(by)))){
     bestv=v;bx=ddx;by=ddy;
    }
   }
   if(getenv("WIDE_DBG"))
    fprintf(stderr,"DBG s%d id=%02x n=%d total=%d best=%d at %d,%d prev=%d,%d\n",
            slot,r[0x671+slot],n,total,bestv,bx,by,
            prev_have[slot]?prev_offx[slot]:-99,prev_have[slot]?prev_offy[slot]:-99);
   if(bestv>0){
    ox=(int8_t)bx;oy=(int8_t)by;
    /* Claim every sprite sitting at the consensus position, so the edge
     * repair never draws a second copy of what we paint. */
    for(int k=0;k<n;k++){
     for(int s=0;s<64;s++){
      const uint8_t *p=oam+s*4;
      if(p[0]>=191)continue;
      /* Exact attributes here, not just the flip bits: a claim turns the
       * edge repair off for that slot, so it must be the same sprite, not
       * merely the same tile in another palette - an over-claim leaves the
       * native band unpainted and its garbage on screen. */
      if(p[1]!=pieces[k].tile||p[2]!=pieces[k].attr)continue;
      int ex=(int)p[3]-px0[k]-bx,ey=(int)p[0]+1-py0[k]-by;
      if(ex<0)ex=-ex;if(ey<0)ey=-ey;
      if(ex<=2&&ey<=2)claimed_oam|=(uint64_t)1<<(uint64_t)s;
     }
    }
    prev_offx[slot]=ox;prev_offy[slot]=oy;
    prev_id[slot]=r[0x671+slot];prev_rec[slot]=r[0x659+slot];
    prev_have[slot]=true;
   }else if(prev_have[slot]&&prev_id[slot]==r[0x671+slot]&&prev_rec[slot]==r[0x659+slot]){
    ox=prev_offx[slot];oy=prev_offy[slot];
   }
  }
  /* Live and in range: the pieces (even none at all) are the whole truth,
   * so the layer is valid either way - a skipped draw still counts as
   * drawn-beyond-the-viewport, and the hold covers its pixels. The origin
   * is the rewound (draw-time) position, matching the frame's sprites. */
  layers[slot].valid=true;layers[slot].obj_x=ox0;layers[slot].obj_y=oy0;
  layers[slot].off_x=ox;layers[slot].off_y=oy;
  layers[slot].count=(uint8_t)n;
  for(int k=0;k<n;k++)layers[slot].pieces[k]=pieces[k];
  cyc_mod_isolate_end();
  r=cyc_cpu_ram();
 }
 snap_vscroll=(int)r[0x13]*240+r[0xfc];
}
