#include "../src/theme.h"
static int launcher_paints;
static int full_invalidations;
static BOOL tracked_invalidate(HWND hwnd,const RECT *r,BOOL erase){
 if(!r)full_invalidations++;return InvalidateRect(hwnd,r,erase);
}
static void tracked_theme_render(HDC dc,int w,int h,int paused,const uint32_t *fb,int ready,int slot){
 launcher_paints++;theme_render(dc,w,h,paused,fb,ready,slot);
}
#define theme_render tracked_theme_render
#define InvalidateRect tracked_invalidate
#define wWinMain application_entry_unused
#include "../src/app.c"
#undef wWinMain
#undef theme_render
#undef InvalidateRect
#include "cyc_png.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <assert.h>
static void capture(const char *file,HWND page) {
 RECT client;GetClientRect(main_window,&client);int width=client.right,height=client.bottom;
 HDC dc=CreateCompatibleDC(NULL);BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=width;bi.bmiHeader.biHeight=-height;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;void *pixels;HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&pixels,NULL,0);HGDIOBJ old=SelectObject(dc,bitmap);
 if(page)SendMessageW(page,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_CHILDREN);else {theme_render(dc,width,height,0,NULL,0,1);for(int i=0;i<MENU_COUNT;i++){RECT r;GetWindowRect(menu_buttons[i],&r);MapWindowPoints(NULL,main_window,(POINT*)&r,2);SetViewportOrgEx(dc,r.left,r.top,NULL);DRAWITEMSTRUCT draw={0};draw.hDC=dc;draw.hwndItem=menu_buttons[i];draw.rcItem=(RECT){0,0,r.right-r.left,r.bottom-r.top};draw.itemState=IsWindowEnabled(menu_buttons[i])?0:ODS_DISABLED;theme_button(&draw,i==menu_selection,i);}SetViewportOrgEx(dc,0,0,NULL);}
 assert(cyc_write_png(file,pixels,width,height));SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
int main(int argc,char **argv){
 GetCurrentDirectoryW(32768,root_dir);swprintf(asset_dir,32768,L"%ls\\assets",root_dir);swprintf(ini_path,32768,L"%ls\\ui-test.ini",root_dir);
 DeleteFileW(ini_path);
 preferences_defaults(&cfg);assert(input_init());assert(theme_init(asset_dir));
 WNDCLASSW wc={0};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"Smb3UIVerification";assert(RegisterClassW(&wc));main_window=CreateWindowW(wc.lpszClassName,L"UI verification",WS_POPUP,0,0,1280,720,NULL,NULL,wc.hInstance,NULL);assert(main_window);update_menu_visibility();capture("ui-launcher.png",NULL);
 const int sizes[][2]={{900,650},{1280,950},{1920,1080},{1024,768},{1280,720}};
 for(int i=0;i<5;i++){
  int w=sizes[i][0],h=sizes[i][1];SetWindowPos(main_window,NULL,0,0,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
  int invalidations_before=full_invalidations;
  SendMessageW(main_window,WM_SIZE,SIZE_RESTORED,MAKELPARAM(w,h));
  assert(full_invalidations>invalidations_before);
  for(int b=0;b<MENU_COUNT;b++){RECT r;GetWindowRect(menu_buttons[b],&r);MapWindowPoints(NULL,main_window,(POINT*)&r,2);assert(r.left==MulDiv(60,w,1280)&&r.top==MulDiv(268+b*38,h,720));assert(r.bottom<=MulDiv(652,h,720));}
  char path[80];snprintf(path,sizeof path,"ui-resize-%dx%d.png",w,h);capture(path,NULL);
 }
 puts("Resize: complete repaint and menu bounds verified at five window sizes");
 for(int tab=0;tab<7;tab++){SetWindowPos(main_window,NULL,0,0,1280,720,SWP_NOZORDER);preferences_open(main_window,&cfg,tab,asset_dir);HWND page=FindWindowExW(main_window,NULL,L"Smb3SettingsPage",NULL);assert(page);char path[64];snprintf(path,sizeof path,"ui-page-%d.png",tab);capture(path,page);
  int paints_before=launcher_paints;
  for(int repaint=0;repaint<3;repaint++){InvalidateRect(main_window,NULL,FALSE);SendMessageW(main_window,WM_PAINT,0,0);}
  assert(launcher_paints==paints_before);
  if(tab==2){HWND choice=GetDlgItem(page,320);assert(choice);SendMessageW(choice,CB_SETCURSEL,0,0);SendMessageW(page,WM_COMMAND,900,0);assert(cfg.aspect==0);}else SendMessageW(page,WM_COMMAND,901,0);assert(!preferences_active());}
 int paints_before=launcher_paints;InvalidateRect(main_window,NULL,FALSE);SendMessageW(main_window,WM_PAINT,0,0);assert(launcher_paints>paints_before);
 puts("Painting: settings suppress launcher redraw on all seven pages; closing restores launcher");
 preferences_open(main_window,&cfg,2,asset_dir);HWND page=FindWindowExW(main_window,NULL,L"Smb3SettingsPage",NULL);SendMessageW(GetDlgItem(page,320),CB_SETCURSEL,2,0);SendMessageW(page,WM_COMMAND,901,0);assert(cfg.aspect==0);
 int virtual_id=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,2,12,1);assert(virtual_id>=0);input_select("");input_update();SDL_Joystick *virtual_pad=SDL_JoystickOpen(virtual_id);assert(virtual_pad);int bindings[11];for(int i=0;i<11;i++)bindings[i]=-1;bindings[1]=0;input_configure(bindings);SDL_JoystickSetVirtualButton(virtual_pad,0,1);input_update();assert(input_read().pad&INPUT_A);SDL_JoystickSetVirtualButton(virtual_pad,0,0);input_update();assert(!(input_read().pad&INPUT_A));SDL_JoystickClose(virtual_pad);SDL_JoystickDetachVirtual(virtual_id);input_update();
 restore_saved_rom();assert(!rom_ready&&!running&&!paused);
 if(argc>1){
  wchar_t test_rom[32768];assert(MultiByteToWideChar(CP_UTF8,0,argv[1],-1,test_rom,32768));
  try_load_rom(test_rom);assert(rom_ready&&running&&!paused);
  rom_ready=running=paused=0;restore_saved_rom();assert(rom_ready&&running&&!paused);
  SendMessageW(main_window,WM_KEYDOWN,VK_ESCAPE,0);assert(running&&paused);
  SendMessageW(main_window,WM_KEYDOWN,VK_ESCAPE,1L<<30);assert(paused);
  SendMessageW(main_window,WM_COMMAND,BTN_SETTINGS,0);assert(preferences_active()&&paused);
  HWND settings=FindWindowExW(main_window,NULL,L"Smb3SettingsPage",NULL);SendMessageW(settings,WM_COMMAND,901,0);assert(!preferences_active()&&paused);
  SendMessageW(main_window,WM_COMMAND,BTN_RESUME,0);assert(running&&!paused);
  WritePrivateProfileStringW(L"app",L"rom",L"missing-rom.nes",ini_path);rom_ready=running=paused=0;restore_saved_rom();assert(!rom_ready&&!running&&!paused);
  puts("Startup: first use, remembered ROM, missing ROM, Esc, settings and resume OK");
 }
 audio_close();DestroyWindow(main_window);input_close();theme_close();DeleteFileW(ini_path);puts("UI: seven pages, apply/discard, virtual gamepad and hotplug OK");return 0;
}
