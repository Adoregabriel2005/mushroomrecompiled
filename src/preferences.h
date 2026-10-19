#pragma once
#include <windows.h>
#include "input.h"
#define PREFS_APPLIED (WM_APP+20)
#define PREFS_CLOSED (WM_APP+21)
typedef struct {
 int keys[11]; char device[512]; int buttons[11],deadzone;
 int resolution,aspect,scale,filter,scanlines,brightness,fullscreen;
 int volume,muted,sample_rate,stereo,latency; wchar_t audio_device[128];
} Preferences;
void preferences_defaults(Preferences *p);
void preferences_load(Preferences *p,const wchar_t *path);
void preferences_save(const Preferences *p,const wchar_t *path);
void preferences_open(HWND owner,Preferences *p,int tab,const wchar_t *asset_dir);
int preferences_route(MSG *msg);
int preferences_active(void);
void preferences_resize(void);
RECT video_destination(int width,int height,int aspect,int scale,RECT *source);
