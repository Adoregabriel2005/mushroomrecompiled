#define wWinMain application_entry_unused
#include "../src/app.c"
#undef wWinMain
#include "cyc_png.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <assert.h>
static void capture(const char *file,HWND page) {
 HDC dc=CreateCompatibleDC(NULL);BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=1280;bi.bmiHeader.biHeight=-720;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;void *pixels;HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&pixels,NULL,0);HGDIOBJ old=SelectObject(dc,bitmap);
 if(page)SendMessageW(page,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_CHILDREN);else {theme_render(dc,1280,720,0,NULL,0,1);for(int i=0;i<MENU_COUNT;i++){RECT r;GetWindowRect(menu_buttons[i],&r);MapWindowPoints(NULL,main_window,(POINT*)&r,2);SetViewportOrgEx(dc,r.left,r.top,NULL);DRAWITEMSTRUCT draw={0};draw.hDC=dc;draw.hwndItem=menu_buttons[i];draw.rcItem=(RECT){0,0,r.right-r.left,r.bottom-r.top};draw.itemState=IsWindowEnabled(menu_buttons[i])?0:ODS_DISABLED;theme_button(&draw,i==menu_selection,i);}SetViewportOrgEx(dc,0,0,NULL);}
 assert(cyc_write_png(file,pixels,1280,720));SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
int main(void){
 GetCurrentDirectoryW(32768,root_dir);swprintf(asset_dir,32768,L"%ls\\assets",root_dir);swprintf(ini_path,32768,L"%ls\\ui-test.ini",root_dir);
 preferences_defaults(&cfg);assert(input_init());assert(theme_init(asset_dir));
 WNDCLASSW wc={0};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"Smb3UIVerification";assert(RegisterClassW(&wc));main_window=CreateWindowW(wc.lpszClassName,L"UI verification",WS_POPUP,0,0,1280,720,NULL,NULL,wc.hInstance,NULL);assert(main_window);update_menu_visibility();capture("ui-launcher.png",NULL);
 for(int tab=0;tab<7;tab++){SetWindowPos(main_window,NULL,0,0,1280,720,SWP_NOZORDER);preferences_open(main_window,&cfg,tab,asset_dir);HWND page=FindWindowExW(main_window,NULL,L"Smb3SettingsPage",NULL);assert(page);char path[64];snprintf(path,sizeof path,"ui-page-%d.png",tab);capture(path,page);
  if(tab==2){HWND choice=GetDlgItem(page,320);assert(choice);SendMessageW(choice,CB_SETCURSEL,2,0);SendMessageW(page,WM_COMMAND,900,0);assert(cfg.aspect==2);}else SendMessageW(page,WM_COMMAND,901,0);assert(!preferences_active());}
 preferences_open(main_window,&cfg,2,asset_dir);HWND page=FindWindowExW(main_window,NULL,L"Smb3SettingsPage",NULL);SendMessageW(GetDlgItem(page,320),CB_SETCURSEL,0,0);SendMessageW(page,WM_COMMAND,901,0);assert(cfg.aspect==2);
 int virtual_id=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,2,12,1);assert(virtual_id>=0);input_select("");input_update();SDL_Joystick *virtual_pad=SDL_JoystickOpen(virtual_id);assert(virtual_pad);int bindings[11];for(int i=0;i<11;i++)bindings[i]=-1;bindings[1]=0;input_configure(bindings);SDL_JoystickSetVirtualButton(virtual_pad,0,1);input_update();assert(input_read().pad&INPUT_A);SDL_JoystickSetVirtualButton(virtual_pad,0,0);input_update();assert(!(input_read().pad&INPUT_A));SDL_JoystickClose(virtual_pad);SDL_JoystickDetachVirtual(virtual_id);input_update();
 audio_close();DestroyWindow(main_window);input_close();theme_close();DeleteFileW(ini_path);puts("UI: seven pages, apply/discard, virtual gamepad and hotplug OK");return 0;
}
