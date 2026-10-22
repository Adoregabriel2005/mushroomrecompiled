/* Presentation-only SMB3 world renderer. No CPU, VRAM or gameplay writes.
 * Tile layout and scroll interpretation researched with Southbird's SMB3
 * disassembly: https://github.com/captainsouthbird/smb3 . */
#include "widescreen.h"
#include "hud.h"
#include "cyc_core.h"
#include "cyc_render.h"
#include "wide_enemies.h"
#include <string.h>

static uint8_t layouts[15][1024];
static uint8_t chr[8192],ram[2048],blocks[8192],oam[256];
static uint32_t colors[32],canvas[SMB3_WIDE_WIDTH*240];
static uint8_t opaque[SMB3_WIDE_WIDTH*SMB3_WIDE_HEIGHT];
static uint8_t sprite_claimed[SMB3_WIDE_WIDTH*SMB3_WIDE_HEIGHT];
static bool tables_ok,captured,active;
static int origin;
/* Camera the last composed frame actually used (from the line-8 snapshot),
 * exposed so diagnostics can compare it against live RAM. */
static int used_cam_x,used_cam_y;
void smb3_wide_camera_used(int *x,int *y){if(x)*x=used_cam_x;if(y)*y=used_cam_y;}
void smb3_wide_frame_camera(const uint8_t *state,int *x,int *y){
 int game_x=state[0x12]*256+state[0xfd],game_y=state[0x13]*240+state[0xfc];
 /* Gameplay RAM may already describe the next frame. Reconstruct the world
  * position of the scroll actually latched by the PPU for this picture. */
 int dx=((cyc_render_line_scroll_x(8)-(game_x&255)+128)&255)-128;
 int ppu_y=(cyc_render_line_scroll_y(8)-8+480)%480;
 int dy=(ppu_y-game_y%480+720)%480-240;
 *x=game_x+dx;*y=game_y+dy;
}

bool smb3_wide_load(const uint8_t *rom,size_t n) {
 tables_ok=false;
 if(n!=393232)return false;
 const size_t fixed=16+30*8192;
 /* The supported USA Rev 1's Scroll_DoColumn table references. */
 static const uint8_t signature[]={0xad,0x0a,0x07,0x0a,0xa8,0xb9,0xf7,0x99};
 if(memcmp(rom+fixed+0x1c06,signature,sizeof signature))return false;
 for(int set=0;set<15;set++){
  unsigned bank=rom[fixed+0x1aa7+set];
  unsigned ptr=rom[fixed+0x19f7+set*2]|(rom[fixed+0x19f8+set*2]<<8);
  size_t start=16+bank*8192+(ptr&8191);
  if(bank>=32 || (ptr&8191)+1024>8192 || start+1024>16+256*1024)return false;
  memcpy(layouts[set],rom+start,1024);
 }
 tables_ok=true;return true;
}
void smb3_wide_reset(void){captured=false;active=false;origin=85;}
void smb3_wide_capture(void){
 memcpy(ram,cyc_cpu_ram(),sizeof ram);
 size_t bytes=0;const uint8_t *data=cyc_cart_ram(&bytes);
 if(!data||bytes<sizeof blocks){captured=false;return;}
 memcpy(blocks,data,sizeof blocks);
 for(unsigned a=0;a<sizeof chr;a++)chr[a]=cyc_render_chr((uint16_t)a);
 for(int i=0;i<32;i++)colors[i]=cyc_render_color(i);
 memcpy(oam,cyc_render_oam(),sizeof oam);
 captured=true;
}
static unsigned terrain_pixel(int wx,int wy,int set) {
 if(wx<0||wy<0||wy>=27*16)return 0;
 int column=wx/16,row=wy/16;
 unsigned off=(column/16)*432+row*16+(column&15);
 if(off>=0x1950)return 0;
 unsigned tile=blocks[off];
 unsigned quarter=((wx&8)?2:0)+((wy&8)?1:0);
 unsigned pattern=layouts[set][quarter*256+tile]*16+(wy&7);
 unsigned bit=7-(wx&7);
 unsigned pixel=((chr[pattern]>>bit)&1)|(((chr[pattern+8]>>bit)&1)<<1);
 return pixel?((tile>>6)*4+pixel):0;
}
/* smb3pc overscan: screen columns 0-11 and 244-255 are left to the extra-
 * column pass, four columns further in than the NES's own 8-pixel blanking,
 * so the half-tile smear at the frame's edge and stale tile fetches never
 * show. Repair those two 12-column strips from the full level map, rather
 * than the PPU's partially updated nametable/attribute columns. Restore
 * their sprites from captured OAM; do not change gameplay or the PPU itself. */
uint32_t smb3_wide_terrain_argb(int x,int y){return colors[terrain_pixel(x,y,ram[0x70a])];}
#define WIDE_CROP 12
#define WIDE_VISIBLE (256-2*WIDE_CROP)
#define WIDE_HUD_X ((SMB3_WIDE_WIDTH-WIDE_VISIBLE)/2)
static bool repaired_column(int x,int x0) {
 return x<x0+WIDE_CROP || x>=x0+WIDE_CROP+WIDE_VISIBLE;
}
static void sprite_edges(int x0) {
 memset(sprite_claimed,0,sizeof sprite_claimed);
 /* Slots owned by managed enemy layers are skipped: the margin pass draws
  * them from the re-run pieces. Painting both sources side-by-side is what
  * thickened straddling sprites. Priority counts below still include the
  * skipped slots, which is the hardware's own selection for this frame. */
 uint64_t owned=smb3_enemies_claimed_oam();
 for(int slot=0;slot<64;slot++){
  const uint8_t *s=oam+slot*4;int y=s[0]+1,x=x0+s[3];
  if(y>=192||(s[3]>=WIDE_CROP&&s[3]+8<=256-WIDE_CROP))continue;
  if((owned>>slot)&1u)continue;
  unsigned tile=s[1],attr=s[2],base=(tile&1)*4096+(tile&0xfe)*16;
  for(int dy=0;dy<16;dy++)for(int dx=0;dx<8;dx++){
   int px=x+dx,py=y+dy;
   if(px<0||px>=426||py>=192||!repaired_column(px,x0)||!(cyc_render_line_mask(py)&0x10))continue;
   /* Preserve the hardware's first-eight-sprites selection on each row. */
   int prior=0;for(int i=0;i<slot;i++)if(py>=oam[i*4]+1&&py<oam[i*4]+17)prior++;
   if(prior>=8)continue;
   unsigned row=(attr&0x80)?15-dy:dy,col=(attr&0x40)?dx:7-dx;
   unsigned address=base+(row/8)*16+(row&7);
   unsigned v=((chr[address]>>col)&1)|(((chr[address+8]>>col)&1)<<1);
   if(!v||sprite_claimed[py*426+px])continue;
   sprite_claimed[py*426+px]=1;
   if((attr&0x20)&&opaque[py*426+px])continue;
   canvas[py*426+px]=colors[16+(attr&3)*4+v];
  }
 }
}
const uint32_t *smb3_wide_present(const uint32_t *native) {
 for(unsigned i=0;i<426*240;i++)canvas[i]=0xff000000u;
 active=false;origin=85;
 int set=ram[0x70a],screens=ram[0x372]+1;
 /* Menus, map, bonus games, vertical rooms and raster effects stay stock.
  * Horizontal multi-screen levels have a fully expanded block map in SRAM. */
 bool playable=tables_ok&&captured&&smb3_enemies_scene(ram)&&
  (cyc_render_line_mask(8)&0x08);
 /* The status bar as its own layer (hud.c, after smb3pc's hud.cpp): the frame
  * is the layer's picture, the rows from the split down rebuilt from the
  * frame's own captures, so no stripe of playfield backdrop or card colour
  * shows above the bar. The level's rows between the split and the bar's first
  * row keep their own capture and are treated as playfield - the ground runs
  * on beside the bar, up to the bar's own first row (smb3pc's bar_top). */
 static uint32_t bar[SMB3_WIDE_HEIGHT*256];
 smb3_hud_compose(native,bar);
 int bar_top=smb3_hud_top();if(bar_top>240)bar_top=240;
 if(playable){
  int camera_x,camera_y;smb3_wide_frame_camera(ram,&camera_x,&camera_y);
  used_cam_x=camera_x;used_cam_y=camera_y;
  int level_width=screens*256;
  int wide_camera=camera_x-85;if(wide_camera<0)wide_camera=0;
  if(wide_camera>level_width-426)wide_camera=level_width-426;
  origin=camera_x-wide_camera;
  if(origin>=0&&origin<=170){
   active=true;
   for(int y=0;y<bar_top;y++)for(int x=0;x<426;x++){
    unsigned pixel=terrain_pixel(wide_camera+x,camera_y+y,set);
    opaque[y*426+x]=(pixel&3)!=0;
    canvas[y*426+x]=colors[pixel];
   }
   /* The layer's picture with its overscan columns cropped, pasted at the
    * frame's own position; only the columns outside it are composed here (as
    * smb3pc pastes framebuf after hud_compose). Keep the native center, but
    * leave both 12-column edge bands supplied by the complete map. They may
    * contain stale PPU tiles or attributes. */
   for(int y=0;y<bar_top;y++)memcpy(canvas+y*426+origin+WIDE_CROP,bar+y*256+WIDE_CROP,WIDE_VISIBLE*sizeof(uint32_t));
   /* Capture first: the edge repair needs the claimed-slot mask so no
    * sprite is painted from both sources. */
   smb3_enemies_capture();
   sprite_edges(origin);
   /* Enemy re-runs happen here, on settled end-of-frame RAM: at the
    * scanline-8 snapshot the game logic is still mid-update, so a re-run
    * there sees a mix of old and new positions while the DMA'd OAM is a
    * frame ahead - the placement votes then chase 2-pixel phantoms and
    * margin sprites visibly jump. End-of-frame RAM and OAM are mutually
    * consistent, so the votes agree instead of flipping.
    * Margin sprites: the re-run pieces of objects reaching outside the
    * pasted frame, at their true world positions. The pasted game frame
    * always wins (it already shows everything inside it); only columns
    * outside it are composed here, so margin enemies walk in instead of
    * popping onto the screen. Lowest OAM slot first: it has the sprite
    * priority, claims its pixels and closes them to the pieces behind it.
    * Managed objects own their edge-band pixels too (the repair skips
    * their slots), so a straddling sprite has exactly one source. */
   const Smb3EnemyLayer *layers=smb3_enemies_layers();
   {
    struct {const Smb3EnemyPiece *pc;uint8_t object;}order[SMB3_OBJECT_SLOTS*12];int norder=0;
    for(int object=0;object<SMB3_OBJECT_SLOTS;object++){
     if(!layers[object].valid)continue;
     for(int k=0;k<layers[object].count&&norder<SMB3_OBJECT_SLOTS*12;k++){
      order[norder].pc=&layers[object].pieces[k];order[norder].object=(uint8_t)object;norder++;
     }
    }
    for(int i=1;i<norder;i++){const Smb3EnemyPiece *t=order[i].pc;uint8_t to=order[i].object;int j=i-1;
     while(j>=0&&order[j].pc->slot>t->slot){order[j+1]=order[j];j--;}
     order[j+1].pc=t;order[j+1].object=to;}
    for(int i=0;i<norder;i++){
     const Smb3EnemyPiece *pc=order[i].pc;
     int object=order[i].object;
     /* World row to screen row, matching the frame's own draw: the sprite
      * for a piece lands one row below the piece's world row. Dropping this
      * +1 put every margin sprite a row high - the piranha's pipe mask then
      * painted its black cap edge on the row above the pipe's cap line, and
      * its head's dark base row stopped merging with that line, so the pipe's
      * mouth showed a black bar that the rest of the picture does not have. */
     int y=layers[object].obj_y+layers[object].off_y-camera_y+pc->dy+1;
     int x=layers[object].obj_x+layers[object].off_x-wide_camera+pc->dx;
     unsigned tile=pc->tile,attr=pc->attr,base=(tile&1)*4096+(tile&0xfe)*16;
     for(int dy=0;dy<16;dy++)for(int dx=0;dx<8;dx++){
      int px=x+dx,py=y+dy;
      if(px<0||px>=426||py<0||py>=192)continue;
      /* Pasted frame wins: only the columns outside it are composed.
       * Edge bands belong to managed objects now (their frame slots are
       * skipped by the repair), so straddling sprites continue seamlessly
       * instead of doubling one pixel apart - thicker lines. */
      if(px>=origin+WIDE_CROP&&px<origin+WIDE_CROP+WIDE_VISIBLE)continue;
      if(!(cyc_render_line_mask(py)&0x10))continue;
      unsigned row=(attr&0x80)?15-dy:dy,col=(attr&0x40)?dx:7-dx;
      unsigned address=base+(row/8)*16+(row&7);
      unsigned v=((chr[address]>>col)&1)|(((chr[address+8]>>col)&1)<<1);
      if(!v||sprite_claimed[py*426+px])continue;
      sprite_claimed[py*426+px]=1;
      if((attr&0x20)&&opaque[py*426+px])continue;
      canvas[py*426+px]=colors[16+(attr&3)*4+v];
     }
    }
   }
  }
 }
 /* No expansion: the cropped frame stays centred on black. */
 if(!active){
  for(int y=0;y<240;y++)memcpy(canvas+y*426+WIDE_HUD_X,bar+y*256+WIDE_CROP,WIDE_VISIBLE*sizeof(uint32_t));
  return canvas;
 }
 /* Fixed HUD: original pixels with the overscan cropped, centered, black on
  * both sides, so it stays centred even while the frame has slid toward a
  * level boundary. */
 for(int y=bar_top;y<240;y++)memcpy(canvas+y*426+WIDE_HUD_X,bar+y*256+WIDE_CROP,WIDE_VISIBLE*sizeof(uint32_t));
 return canvas;
}
bool smb3_wide_active(void){return active;}
int smb3_wide_origin(void){return origin;}
