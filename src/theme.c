#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include "theme.h"
#include <wincodec.h>
#include <stdlib.h>
#include <math.h>
#include <wchar.h>

static uint32_t *bg_art = NULL;
static HBITMAP logo_bitmap = NULL;
static RECT logo_bounds = {0};
static UINT bg_w = 0, bg_h = 0;
static BITMAPINFO bg_info;
static HDC canvas_dc = NULL;
static HBITMAP canvas_bitmap = NULL, canvas_old = NULL;
static HFONT title_font = NULL, subtitle_font = NULL, label_font = NULL, small_font = NULL, menu_font = NULL;
static int canvas_w = 0, canvas_h = 0;

static const COLORREF smb_gold    = RGB(252, 216, 0);
static const COLORREF smb_red     = RGB(228, 52, 40);
static const COLORREF smb_cream   = RGB(255, 245, 230);
static const COLORREF smb_blue    = RGB(18, 22, 36);

static int sx(int x) { return MulDiv(x, canvas_w, 1280); }
static int sy(int y) { return MulDiv(y, canvas_h, 720); }

static void fill_rect(HDC dc, RECT r, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void draw_word(HDC dc, int x, int y, int w, int h, const wchar_t *s, HFONT f, COLORREF color, UINT flags) {
    RECT r = {sx(x), sy(y), sx(x + w), sy(y + h)};
    HFONT old = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, s, -1, &r, flags);
    SelectObject(dc, old);
}

static HFONT make_font(int size, int weight, const wchar_t *family) {
    return CreateFontW(-sy(size), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, family);
}

static HBITMAP load_wic_bitmap(const wchar_t *path, RECT *bounds) {
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    UINT w = 0, h = 0;
    HBITMAP bitmap = NULL;

    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void **)&factory);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateDecoderFromFilename(factory, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    if (SUCCEEDED(hr)) hr = IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) hr = IWICFormatConverter_GetSize(converter, &w, &h);
    if (SUCCEEDED(hr) && w && h && w <= 8192 && h <= 8192) {
        BITMAPINFO info = {0};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -(LONG)h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        uint32_t *pixels = NULL;
        bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
        if (bitmap) {
            hr = IWICFormatConverter_CopyPixels(converter, NULL, w * 4, w * h * 4, (BYTE *)pixels);
            if (bounds) {
                *bounds = (RECT){(LONG)w, (LONG)h, 0, 0};
                if (SUCCEEDED(hr)) {
                    for (UINT y = 0; y < h; y++) {
                        for (UINT x = 0; x < w; x++) {
                            if ((pixels[y * w + x] >> 24) > 10) {
                                if ((LONG)x < bounds->left) bounds->left = x;
                                if ((LONG)y < bounds->top) bounds->top = y;
                                if ((LONG)x >= bounds->right) bounds->right = x + 1;
                                if ((LONG)y >= bounds->bottom) bounds->bottom = y + 1;
                            }
                        }
                    }
                }
                if (FAILED(hr) || bounds->right <= bounds->left) {
                    *bounds = (RECT){0, 0, (LONG)w, (LONG)h};
                }
            }
        }
    }
    if (converter) IWICFormatConverter_Release(converter);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (factory) IWICImagingFactory_Release(factory);
    return bitmap;
}

int theme_init(const wchar_t *asset_path) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    wchar_t logo_file[32768], bg_file[32768];
    swprintf(logo_file, 32768, L"%ls\\official-logo.png", asset_path);
    swprintf(bg_file, 32768, L"%ls\\official-key-art.png", asset_path);

    logo_bitmap = load_wic_bitmap(logo_file, &logo_bounds);

    /* Load background art pixels */
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;

    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void **)&factory);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateDecoderFromFilename(factory, bg_file, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    if (SUCCEEDED(hr)) hr = IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) hr = IWICFormatConverter_GetSize(converter, &bg_w, &bg_h);
    if (SUCCEEDED(hr) && bg_w && bg_h && bg_w <= 8192 && bg_h <= 8192) {
        bg_art = (uint32_t *)malloc((size_t)bg_w * bg_h * 4);
        if (bg_art) {
            hr = IWICFormatConverter_CopyPixels(converter, NULL, bg_w * 4, bg_w * bg_h * 4, (BYTE *)bg_art);
            bg_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bg_info.bmiHeader.biWidth = bg_w;
            bg_info.bmiHeader.biHeight = -(LONG)bg_h;
            bg_info.bmiHeader.biPlanes = 1;
            bg_info.bmiHeader.biBitCount = 32;
        }
    }
    if (converter) IWICFormatConverter_Release(converter);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (factory) IWICImagingFactory_Release(factory);

    return 1;
}

static void free_canvas(void) {
    if (canvas_dc) {
        SelectObject(canvas_dc, canvas_old);
        DeleteObject(canvas_bitmap);
        DeleteDC(canvas_dc);
        canvas_dc = NULL;
    }
    if (title_font)    { DeleteObject(title_font);    title_font = NULL; }
    if (subtitle_font) { DeleteObject(subtitle_font); subtitle_font = NULL; }
    if (label_font)    { DeleteObject(label_font);    label_font = NULL; }
    if (small_font)    { DeleteObject(small_font);    small_font = NULL; }
    if (menu_font)     { DeleteObject(menu_font);     menu_font = NULL; }
}

void theme_close(void) {
    if (logo_bitmap) { DeleteObject(logo_bitmap); logo_bitmap = NULL; }
    free_canvas();
    if (bg_art) { free(bg_art); bg_art = NULL; }
    CoUninitialize();
}

static void prepare(HDC dc, int w, int h) {
    if (w == canvas_w && h == canvas_h && canvas_dc) return;
    free_canvas();
    canvas_w = w;
    canvas_h = h;
    canvas_dc = CreateCompatibleDC(dc);
    canvas_bitmap = CreateCompatibleBitmap(dc, w, h);
    canvas_old = SelectObject(canvas_dc, canvas_bitmap);

    title_font    = make_font(72, FW_HEAVY,   L"Segoe UI Black");
    subtitle_font = make_font(24, FW_BOLD,    L"Trebuchet MS");
    label_font    = make_font(18, FW_NORMAL,  L"Segoe UI");
    small_font    = make_font(13, FW_SEMIBOLD,L"Segoe UI");
    menu_font     = make_font(20, FW_BOLD,    L"Trebuchet MS");
}


void theme_gallery(HDC dc,RECT bounds){if(!bg_art)return;double k=fmin((double)(bounds.right-bounds.left)/bg_w,(double)(bounds.bottom-bounds.top)/bg_h);int w=(int)(bg_w*k),h=(int)(bg_h*k);SetStretchBltMode(dc,HALFTONE);StretchDIBits(dc,bounds.left+(bounds.right-bounds.left-w)/2,bounds.top+(bounds.bottom-bounds.top-h)/2,w,h,0,0,bg_w,bg_h,bg_art,&bg_info,DIB_RGB_COLORS,SRCCOPY);}
void theme_render(HDC dc,int w,int h,int paused,const uint32_t *fb,int ready,int slot) {
 if(w<=0||h<=0)return;prepare(dc,w,h);
 fill_rect(canvas_dc,(RECT){0,0,w,h},RGB(12,24,43));
 if(bg_art){int left=sx(510),rw=w-left;double scale=fmax((double)rw/bg_w,(double)h/bg_h);int dw=(int)(bg_w*scale),dh=(int)(bg_h*scale);SetStretchBltMode(canvas_dc,HALFTONE);StretchDIBits(canvas_dc,left+(rw-dw)/2,(h-dh)/2,dw,dh,0,0,bg_w,bg_h,bg_art,&bg_info,DIB_RGB_COLORS,SRCCOPY);}
 fill_rect(canvas_dc,(RECT){0,0,sx(510),h},RGB(12,24,43));
 fill_rect(canvas_dc,(RECT){sx(508),0,sx(512),h},smb_gold);
 fill_rect(canvas_dc,(RECT){sx(45),sy(18),sx(466),sy(215)},RGB(255,213,62));
 if(logo_bitmap){HDC art=CreateCompatibleDC(canvas_dc);HGDIOBJ old=SelectObject(art,logo_bitmap);int lw=logo_bounds.right-logo_bounds.left,lh=logo_bounds.bottom-logo_bounds.top;double k=fmin((double)sx(365)/lw,(double)sy(150)/lh);int dw=(int)(lw*k),dh=(int)(lh*k);BLENDFUNCTION b={AC_SRC_OVER,0,255,AC_SRC_ALPHA};AlphaBlend(canvas_dc,sx(66)+(sx(365)-dw)/2,sy(27)+(sy(150)-dh)/2,dw,dh,art,logo_bounds.left,logo_bounds.top,lw,lh,b);SelectObject(art,old);DeleteDC(art);}
 draw_word(canvas_dc,60,181,390,30,L"RECOMPILED",subtitle_font,RGB(12,24,43),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
 draw_word(canvas_dc,60,224,410,30,paused?L"Sua aventura espera por você.":L"Oito mundos. Uma grande aventura.",label_font,RGB(222,229,241),DT_LEFT|DT_SINGLELINE);
 wchar_t status[120];swprintf(status,120,L"%ls   ·   NES   ·   SLOT %d",ready?L"ROM VALIDADA":L"SELECIONE SUA ROM",slot);
 draw_word(canvas_dc,60,652,420,22,status,small_font,smb_gold,DT_LEFT|DT_SINGLELINE);
 draw_word(canvas_dc,60,679,420,18,L"↑ ↓ / controle navegar   ·   A / Enter selecionar",small_font,RGB(174,190,211),DT_LEFT|DT_SINGLELINE);
 draw_word(canvas_dc,60,700,420,18,L"Esc / Home menu   ·   F11 tela cheia   ·   1–5 slot",small_font,RGB(174,190,211),DT_LEFT|DT_SINGLELINE);
 if(paused&&fb){RECT b={sx(1030),sy(515),sx(1250),sy(700)};fill_rect(canvas_dc,b,RGB(12,24,43));BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=256;info.bmiHeader.biHeight=-240;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;StretchDIBits(canvas_dc,b.left+4,b.top+4,b.right-b.left-8,b.bottom-b.top-8,0,0,256,240,fb,&info,DIB_RGB_COLORS,SRCCOPY);}
 BitBlt(dc,0,0,w,h,canvas_dc,0,0,SRCCOPY);
}
void theme_button(const DRAWITEMSTRUCT *item,int selected,int index) {
 HDC dc=item->hDC;RECT r=item->rcItem;int active=selected||(item->itemState&ODS_FOCUS);int disabled=item->itemState&ODS_DISABLED;
 fill_rect(dc,r,active?RGB(185,36,42):RGB(12,24,43));
 if(active){HPEN pen=CreatePen(PS_SOLID,2,smb_gold);HGDIOBJ old=SelectObject(dc,pen);HGDIOBJ brush=SelectObject(dc,GetStockObject(NULL_BRUSH));Rectangle(dc,0,0,r.right,r.bottom);SelectObject(dc,old);SelectObject(dc,brush);DeleteObject(pen);}
 wchar_t text[128],number[8];GetWindowTextW(item->hwndItem,text,128);swprintf(number,8,L"%02d",index+1);
 SetBkMode(dc,TRANSPARENT);HFONT old=SelectObject(dc,small_font);SetTextColor(dc,active?smb_gold:RGB(126,158,186));RECT n=r;n.left+=12;DrawTextW(dc,number,-1,&n,DT_LEFT|DT_SINGLELINE|DT_VCENTER);
 SelectObject(dc,menu_font);SetTextColor(dc,disabled?RGB(104,119,135):smb_cream);r.left+=44;DrawTextW(dc,text,-1,&r,DT_LEFT|DT_SINGLELINE|DT_VCENTER);SelectObject(dc,old);
}
void theme_layout(HWND parent,HWND *buttons,int count){
 RECT r;GetClientRect(parent,&r);
 if(r.right<=0||r.bottom<=0)return;
 /* Child buttons may paint before the parent after a resize. Prepare their
  * fonts at the new size before moving or invalidating them. */
 HDC dc=GetDC(parent);prepare(dc,r.right,r.bottom);ReleaseDC(parent,dc);
 for(int i=0;i<count;i++)if(buttons[i])SetWindowPos(buttons[i],NULL,
  MulDiv(60,r.right,1280),MulDiv(268+i*38,r.bottom,720),
  MulDiv(390,r.right,1280),MulDiv(34,r.bottom,720),
  SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);
}

