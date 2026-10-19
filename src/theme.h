#pragma once
#include <windows.h>
#include <stdint.h>

int  theme_init(const wchar_t *asset_path);
void theme_close(void);
void theme_render(HDC dc, int width, int height, int paused, const uint32_t *framebuffer, int rom_ready, int slot);
void theme_button(const DRAWITEMSTRUCT *item, int selected, int index);
void theme_layout(HWND parent, HWND *buttons, int count);
void theme_gallery(HDC dc, RECT bounds);
