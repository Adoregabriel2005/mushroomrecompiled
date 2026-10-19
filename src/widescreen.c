/* Presentation-only SMB3 world renderer. No CPU, VRAM or gameplay writes.
 * Tile layout and scroll interpretation researched with Southbird's SMB3
 * disassembly: https://github.com/captainsouthbird/smb3 . */
#include "widescreen.h"
#include "cyc_core.h"
#include "cyc_render.h"
#include <string.h>

static uint8_t layouts[15][1024];
static uint8_t chr[8192],ram[2048],blocks[8192],oam[256];
static uint32_t colors[32],canvas[SMB3_WIDE_WIDTH*240];
static uint8_t opaque[SMB3_WIDE_WIDTH*192];
static uint8_t sprite_claimed[SMB3_WIDE_WIDTH*192];
static bool tables_ok,captured,active;
static int origin;

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
/* Repair the two native edge metatiles from the full level map, rather than
 * the PPU's partially updated nametable/attribute columns. Restore their
 * sprites from captured OAM; do not change gameplay or the PPU itself. */
static bool repaired_column(int x,int x0) {
 return x<x0+16 || x>=x0+240;
}
static void sprite_edges(int x0) {
 memset(sprite_claimed,0,sizeof sprite_claimed);
 for(int slot=0;slot<64;slot++){
  const uint8_t *s=oam+slot*4;int y=s[0]+1,x=x0+s[3];
  if(y>=192||(s[3]>=16&&s[3]<=232))continue;
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
 bool playable=tables_ok&&captured&&set>=1&&set<=14&&set!=7&&
  !ram[0x3ef]&&screens>1&&screens<=15&&ram[0x100]!=0x80&&
  (cyc_render_line_mask(8)&0x08);
 if(playable){
  int camera_x=ram[0x12]*256+ram[0xfd];
  int camera_y=ram[0x13]*240+ram[0xfc];
  int level_width=screens*256;
  int wide_camera=camera_x-85;if(wide_camera<0)wide_camera=0;
  if(wide_camera>level_width-426)wide_camera=level_width-426;
  origin=camera_x-wide_camera;
  if(origin>=0&&origin<=170){
   active=true;
   for(int y=0;y<192;y++)for(int x=0;x<426;x++){
    unsigned pixel=terrain_pixel(wide_camera+x,camera_y+y,set);
    opaque[y*426+x]=(pixel&3)!=0;
    canvas[y*426+x]=colors[pixel];
   }
   /* Keep the native center, but leave both 16-pixel edge bands supplied
    * by the complete map. They may contain stale PPU tiles or attributes. */
   for(int y=0;y<192;y++)memcpy(canvas+y*426+origin+16,native+y*256+16,224*sizeof(uint32_t));
   sprite_edges(origin);
  }
 }
 if(!active)for(int y=0;y<192;y++)memcpy(canvas+y*426+85,native+y*256,256*sizeof(uint32_t));
 /* Fixed HUD: original 256 pixels, centered, black on both sides. */
 for(int y=192;y<240;y++)memcpy(canvas+y*426+85,native+y*256,256*sizeof(uint32_t));
 return canvas;
}
bool smb3_wide_active(void){return active;}
int smb3_wide_origin(void){return origin;}
