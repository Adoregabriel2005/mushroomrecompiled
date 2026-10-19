#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include "theme.h"
#include <wincodec.h>
#include <stdlib.h>
#include <math.h>
#include <wchar.h>


static HBITMAP logo_bitmap = NULL;
static RECT logo_bounds = {0};


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
    wchar_t path[32768];
    swprintf(path,32768,L"%ls\\..\\png.png",asset_path);
    logo_bitmap=load_wic_bitmap(path,&logo_bounds);
    return logo_bitmap!=NULL;
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

    title_font    = make_font(44, FW_HEAVY,   L"Segoe UI Black");
    subtitle_font = make_font(24, FW_BOLD,    L"Trebuchet MS");
    label_font    = make_font(18, FW_NORMAL,  L"Segoe UI");
    small_font    = make_font(13, FW_SEMIBOLD,L"Segoe UI");
    menu_font     = make_font(20, FW_BOLD,    L"Trebuchet MS");
}


void theme_gallery(HDC dc,RECT r){
 if(!logo_bitmap)return;
 BITMAP bm;GetObject(logo_bitmap,sizeof bm,&bm);
 double k=fmin((double)(r.right-r.left)/bm.bmWidth,(double)(r.bottom-r.top)/bm.bmHeight);
 int w=(int)(bm.bmWidth*k),h=(int)(bm.bmHeight*k);
 HDC art=CreateCompatibleDC(dc);HGDIOBJ old=SelectObject(art,logo_bitmap);
 BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
 SetStretchBltMode(dc,HALFTONE);
 AlphaBlend(dc,r.left+(r.right-r.left-w)/2,r.top+(r.bottom-r.top-h)/2,w,h,art,0,0,bm.bmWidth,bm.bmHeight,blend);
 SelectObject(art,old);DeleteDC(art);
}
void theme_render(HDC dc,int w,int h,int paused,const uint32_t *fb,int ready,int slot){
 if(w<=0||h<=0)return;prepare(dc,w,h);
 fill_rect(canvas_dc,(RECT){0,0,w,h},RGB(5,10,18));
 fill_rect(canvas_dc,(RECT){0,0,sx(510),h},RGB(12,24,43));
 fill_rect(canvas_dc,(RECT){sx(508),0,sx(512),h},RGB(0,165,235));
 draw_word(canvas_dc,60,35,410,65,L"MUSHROOM",title_font,smb_cream,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
 draw_word(canvas_dc,60,104,390,45,L"RECOMPILED",subtitle_font,RGB(0,181,244),DT_LEFT|DT_VCENTER|DT_SINGLELINE);
 draw_word(canvas_dc,60,224,410,30,paused?L"Partida pausada.":L"Sua ROM. Sua aventura.",label_font,RGB(222,229,241),DT_LEFT|DT_SINGLELINE);
 fill_rect(canvas_dc,(RECT){sx(550),sy(125),sx(1240),sy(390)},RGB(0,0,0));
 theme_gallery(canvas_dc,(RECT){sx(565),sy(145),sx(1225),sy(370)});
 draw_word(canvas_dc,560,440,650,40,L"ROM fornecida pelo jogador",subtitle_font,smb_cream,DT_LEFT|DT_SINGLELINE);
 draw_word(canvas_dc,560,495,650,35,L"Teclado e controles USB / wireless",label_font,RGB(174,190,211),DT_LEFT|DT_SINGLELINE);
 draw_word(canvas_dc,560,535,650,35,L"Configurações, save states e vídeo 16:9",label_font,RGB(174,190,211),DT_LEFT|DT_SINGLELINE);
 wchar_t status[120];swprintf(status,120,L"%ls   ·   SLOT %d",ready?L"ROM VALIDADA":L"SELECIONE SUA ROM",slot);
 draw_word(canvas_dc,60,652,420,22,status,small_font,smb_gold,DT_LEFT|DT_SINGLELINE);
 draw_word(canvas_dc,60,679,420,18,L"↑ ↓ / controle navegar   ·   A / Enter selecionar",small_font,RGB(174,190,211),DT_LEFT|DT_SINGLELINE);
 draw_word(canvas_dc,60,700,420,18,L"Esc / Home menu   ·   F11 tela cheia   ·   1–5 slot",small_font,RGB(174,190,211),DT_LEFT|DT_SINGLELINE);
 if(paused&&fb){RECT b={sx(1030),sy(575),sx(1250),sy(700)};fill_rect(canvas_dc,b,RGB(12,24,43));BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=256;info.bmiHeader.biHeight=-240;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;StretchDIBits(canvas_dc,b.left+4,b.top+4,b.right-b.left-8,b.bottom-b.top-8,0,0,256,240,fb,&info,DIB_RGB_COLORS,SRCCOPY);}
 BitBlt(dc,0,0,w,h,canvas_dc,0,0,SRCCOPY);
}void theme_button(const DRAWITEMSTRUCT *item,int selected,int index) {
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

