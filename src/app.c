#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <wchar.h>

#include "glue.h"
#include "replay.h"
#include "theme.h"
#include "preferences.h"
#include "choice.h"
#include "widescreen.h"

#define MENU_COUNT 10

enum {
    BTN_RESUME = 100,
    BTN_RESTART,
    BTN_SAVE_SLOT,
    BTN_LOAD_SLOT,
    BTN_SETTINGS,
    BTN_FULLSCREEN,
    BTN_EXIT,
    BTN_LOAD_ROM = 120,
    BTN_CONTROLS,
    BTN_ABOUT
};

static HWND main_window = NULL;
static HWND menu_buttons[MENU_COUNT];
static const int menu_ids[MENU_COUNT]={BTN_RESUME,BTN_RESTART,BTN_CONTROLS,BTN_SETTINGS,BTN_LOAD_ROM,BTN_ABOUT,BTN_SAVE_SLOT,BTN_LOAD_SLOT,BTN_FULLSCREEN,BTN_EXIT};
static int menu_selection=0, previous_menu_pad=0;
static int hovered_btn = -1;
static int rom_ready = 0;
static Preferences cfg;
static wchar_t asset_dir[32768];
static wchar_t rom_path[32768], root_dir[32768], ini_path[32768];
static Replay movie;
static int running = 0, paused = 0, quitting = 0, slot = 1, is_fullscreen = 0;
static WINDOWPLACEMENT prev_placement = {sizeof(WINDOWPLACEMENT)};

/* Framebuffer & Audio */
static uint32_t active_framebuffer[256 * 240];
static uint32_t presented_framebuffer[SMB3_WIDE_WIDTH * 240];
static int presented_width=256;
static HWAVEOUT audio_device = NULL;
static HANDLE audio_event = NULL;
static WAVEHDR audio_headers[8];
static int16_t audio_buffers[8][4096];
static int audio_index = 0;
static unsigned audio_fill = 0, audio_chunk = 0, audio_prefill = 0;
static int audio_priming = 1, audio_fault = 0;
static uint64_t audio_submitted_frames = 0;

static HDC present_dc = NULL;
static HBITMAP present_bitmap = NULL, present_old = NULL;
static int present_w = 0, present_h = 0;

static wchar_t notification[600] = {0};
static ULONGLONG notification_until = 0;
static HFONT notify_font = NULL;

static void show_message(const wchar_t *text) {
    wcsncpy(notification, text, 599);
    notification[599] = 0;
    notification_until = GetTickCount64() + 6000;
    InvalidateRect(main_window, NULL, FALSE);
}

static void draw_notification(HDC dc, int w, int h) {
    if (!notification[0] || GetTickCount64() >= notification_until) return;
    RECT r = {(!running || paused) ? w * 42 / 100 : 32, h - 110, w - 32, h - 36};

    HBRUSH brush = CreateSolidBrush(RGB(18, 22, 34));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(252, 216, 0));
    HGDIOBJ ob = SelectObject(dc, brush);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 14, 14);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(brush);
    DeleteObject(pen);

    if (!notify_font) {
        notify_font = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    }
    HFONT old = SelectObject(dc, notify_font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 245, 230));
    InflateRect(&r, -16, -10);
    DrawTextW(dc, notification, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

static void save_settings(void) {
    WritePrivateProfileStringW(L"app", L"rom", rom_path, ini_path);
    cfg.fullscreen = is_fullscreen;
    preferences_save(&cfg, ini_path);
}

static void get_slot_path(wchar_t *out, int s) {
    swprintf(out, 32768, L"%ls\\smb3-slot%d.save", root_dir, s);
}

/* Audio subsystem */
static void audio_reset(void) {
    if (audio_device) { waveOutReset(audio_device); waveOutPause(audio_device); }
    audio_index = 0; audio_fill = 0; audio_prefill = 0; audio_priming = 1;
}

static void audio_close(void) {
    if (!audio_device) return;
    waveOutReset(audio_device);
    for (int i = 0; i < 8; i++) waveOutUnprepareHeader(audio_device, &audio_headers[i], sizeof(WAVEHDR));
    waveOutClose(audio_device);
    audio_device = NULL;
    if (audio_event) { CloseHandle(audio_event); audio_event = NULL; }
}

static void audio_open(void) {
    glue_set_audio_sample_rate((uint32_t)cfg.sample_rate);
    WAVEFORMATEX fmt = {
        WAVE_FORMAT_PCM, 2, (DWORD)cfg.sample_rate,
        (DWORD)cfg.sample_rate * 4, 4, 16, 0
    };
    UINT selected = WAVE_MAPPER;
    for (UINT i = 0; i < waveOutGetNumDevs(); i++) {
        WAVEOUTCAPSW caps;
        if (waveOutGetDevCapsW(i, &caps, sizeof caps) == MMSYSERR_NOERROR &&
            !wcscmp(caps.szPname, cfg.audio_device)) {
            selected = i;
            break;
        }
    }
    audio_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!audio_event || waveOutOpen(&audio_device, selected, &fmt, (DWORD_PTR)audio_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        audio_device = NULL;
        if (audio_event) { CloseHandle(audio_event); audio_event = NULL; }
        show_message(L"Aviso: Dispositivo de udio padro inicializado.");
        return;
    }
    audio_chunk = (unsigned)cfg.sample_rate / 100 * 2;
    audio_fault = 0;
    audio_submitted_frames = 0;
    memset(audio_headers, 0, sizeof audio_headers);
    for (int i = 0; i < 8; i++) {
        audio_headers[i].lpData = (LPSTR)audio_buffers[i];
        audio_headers[i].dwBufferLength = sizeof(audio_buffers[i]);
        if (waveOutPrepareHeader(audio_device, &audio_headers[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            audio_fault = 1;
            audio_close();
            return;
        }
    }
    audio_reset();
}

static void audio_submit_samples(const int16_t *samples, size_t count) {
    if (!audio_device || audio_fault || cfg.muted || paused) return;

    for (size_t i = 0; i < count; i++) {
        WAVEHDR *current = &audio_headers[audio_index];
        while ((current->dwFlags & WHDR_INQUEUE) && !(current->dwFlags & WHDR_DONE)) {
            WaitForSingleObject(audio_event, 5);
        }
        audio_buffers[audio_index][audio_fill++] = (int16_t)((int32_t)samples[i] * cfg.volume / 100);
        if (audio_fill >= audio_chunk) {
            WAVEHDR *h = &audio_headers[audio_index];
            h->dwBufferLength = (DWORD)(audio_fill * sizeof(int16_t));
            if (waveOutWrite(audio_device, h, sizeof *h) != MMSYSERR_NOERROR) {
                audio_fault = 1;
                return;
            }
            audio_submitted_frames += audio_fill / 2;
            audio_fill = 0;
            audio_index = (audio_index + 1) % 8;
            if (audio_priming && ++audio_prefill >= (unsigned)(cfg.latency == 0 ? 2 : cfg.latency == 1 ? 4 : 6)) {
                audio_priming = 0;
                waveOutRestart(audio_device);
            }
        }
    }
}

static void toggle_fullscreen(void) {
    DWORD style = GetWindowLongW(main_window, GWL_STYLE);
    if (!is_fullscreen) {
        GetWindowPlacement(main_window, &prev_placement);
        MONITORINFO mi = {sizeof(MONITORINFO)};
        if (GetMonitorInfoW(MonitorFromWindow(main_window, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongW(main_window, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(main_window, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            is_fullscreen = 1;
        }
    } else {
        SetWindowLongW(main_window, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(main_window, &prev_placement);
        SetWindowPos(main_window, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        is_fullscreen = 0;
    }
    save_settings();
}

static void update_menu_visibility(void) {
    int show = (!running || paused) && !preferences_active();
    for (int i = 0; i < MENU_COUNT; i++) {
        if (menu_buttons[i]) {
            ShowWindow(menu_buttons[i], show ? SW_SHOW : SW_HIDE);
        }
    }
    theme_layout(main_window, menu_buttons, MENU_COUNT);
    for(int i=0;i<MENU_COUNT;i++)if(i==0||i==1||i==6||i==7)EnableWindow(menu_buttons[i],rom_ready);
    if(!rom_ready&&(menu_selection==0||menu_selection==1||menu_selection==6||menu_selection==7))menu_selection=4;
}

static void try_load_rom(const wchar_t *path) {
    if (!path || !path[0]) return;
    if (glue_load_rom_wide(path)) {
        wcsncpy(rom_path, path, 32767);
        rom_path[32767] = 0;
        rom_ready = 1;
        running = 1;
        paused = 0;
        audio_reset();
        save_settings();
    } else {
        show_message(L"ROM incompatível. Selecione ROM compatível (USA Rev 1), arquivo .nes.");
    }
    update_menu_visibility();
    InvalidateRect(main_window, NULL, FALSE);
}

static void restore_saved_rom(void) {
    wchar_t saved_rom[32768];
    GetPrivateProfileStringW(L"app", L"rom", L"", saved_rom, 32768, ini_path);
    if (saved_rom[0]) try_load_rom(saved_rom);
}

static void prompt_rom_file(void) {
    wchar_t filter[] = L"ROMs de NES (*.nes)\0*.nes\0Todos os Arquivos (*.*)\0*.*\0";
    wchar_t file[32768] = {0};
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = main_window;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 32768;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) {
        try_load_rom(file);
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE inst = ((LPCREATESTRUCT)lp)->hInstance;
        choice_register(inst);

        const wchar_t *btn_labels[MENU_COUNT] = {
            L"Jogar / Retomar", L"Reiniciar aventura", L"Controles",
            L"Configurações", L"Selecionar ROM", L"Sobre o projeto",
            L"Salvar · slot 1", L"Carregar · slot 1", L"Tela cheia", L"Sair"
        };

        for (int i = 0; i < MENU_COUNT; i++) {
            menu_buttons[i] = CreateWindowW(L"BUTTON", btn_labels[i],
                                            WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
                                            0, 0, 0, 0, hwnd,
                                            (HMENU)(INT_PTR)menu_ids[i], inst, NULL);
        }
        return 0;
    }
    case WM_SIZE: {
        int w = LOWORD(lp);
        int h = HIWORD(lp);
        if (w > 0 && h > 0) {
            theme_layout(hwnd, menu_buttons, MENU_COUNT);
            preferences_resize();
            /* Every element uses client-relative coordinates. Windows' default
             * resize only invalidates exposed strips and retains old artwork. */
            RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT *)lp;
        if (dis->CtlType == ODT_BUTTON) {
            int index=0;for(int i=0;i<MENU_COUNT;i++)if(menu_ids[i]==dis->CtlID)index=i;
            theme_button(dis, menu_selection==index, index);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        for(int i=0;i<MENU_COUNT;i++)if(menu_ids[i]==id)menu_selection=i;
        InvalidateRect(hwnd,NULL,FALSE);
        if(id==BTN_LOAD_ROM){prompt_rom_file();return 0;}
        if(id==BTN_CONTROLS||id==BTN_ABOUT){preferences_open(hwnd,&cfg,id==BTN_ABOUT?4:1,asset_dir);update_menu_visibility();return 0;}
        if (id == BTN_RESUME) {
            if (rom_ready) {
                running = 1;
                paused = 0;
                audio_reset();
                update_menu_visibility();
                InvalidateRect(hwnd, NULL, FALSE);
            } else {
                prompt_rom_file();
            }
            return 0;
        }
        if (id == BTN_RESTART) {
            if (rom_ready) {
                glue_reset();
                running = 1;
                paused = 0;
                show_message(L"Partida reiniciada.");
                update_menu_visibility();
            }
            return 0;
        }
        if (id == BTN_SAVE_SLOT) {
            if (rom_ready) {
                wchar_t spath[32768];
                get_slot_path(spath, slot);
                if (glue_save_state(spath)) {
                    wchar_t msg[128];
                    swprintf(msg, 128, L"Progresso salvo com sucesso no Slot %d.", slot);
                    show_message(msg);
                } else {
                    show_message(L"Erro ao salvar estado.");
                }
            }
            return 0;
        }
        if (id == BTN_LOAD_SLOT) {
            if (rom_ready) {
                wchar_t spath[32768];
                get_slot_path(spath, slot);
                if (glue_load_state(spath)) {
                    wchar_t msg[128];
                    swprintf(msg, 128, L"Progresso restaurado do Slot %d.", slot);
                    show_message(msg);
                    paused = 0;
                    running = 1;
                    audio_reset();
                    update_menu_visibility();
                } else {
                    show_message(L"Nenhum arquivo de progresso encontrado para este slot.");
                }
            }
            return 0;
        }
        if (id == BTN_SETTINGS) {
            preferences_open(hwnd, &cfg, 2, asset_dir);
            update_menu_visibility();
            return 0;
        }
        if (id == BTN_FULLSCREEN) {
            toggle_fullscreen();
            return 0;
        }
        if (id == BTN_EXIT) {
            PostQuitMessage(0);
            return 0;
        }
        break;
    }
    case PREFS_APPLIED: {
        glue_set_widescreen(cfg.aspect==2);
        input_select(cfg.device);input_configure(cfg.buttons);input_set_deadzone(cfg.deadzone);
        if(cfg.fullscreen!=is_fullscreen)toggle_fullscreen();
        if(!is_fullscreen){int widths[]={960,1024,1280,1600,1920},heights[]={720,768,720,900,1080};RECT size={0,0,widths[cfg.resolution],heights[cfg.resolution]};AdjustWindowRect(&size,GetWindowLongW(hwnd,GWL_STYLE),FALSE);SetWindowPos(hwnd,NULL,0,0,size.right-size.left,size.bottom-size.top,SWP_NOMOVE|SWP_NOZORDER);}
        save_settings();
        audio_close();
        audio_open();
        show_message(L"Configuraes aplicadas com sucesso.");
        update_menu_visibility();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case PREFS_CLOSED: {
        input_select(cfg.device);input_configure(cfg.buttons);input_set_deadzone(cfg.deadzone);
        update_menu_visibility();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_KEYDOWN: {
        if((!running||paused)&&!preferences_active()){
            if(wp==VK_UP||wp==VK_DOWN){menu_selection=(menu_selection+(wp==VK_UP?MENU_COUNT-1:1))%MENU_COUNT;SetFocus(menu_buttons[menu_selection]);InvalidateRect(hwnd,NULL,FALSE);return 0;}
            if(wp==VK_RETURN){SendMessageW(hwnd,WM_COMMAND,menu_ids[menu_selection],0);return 0;}
        }
        if (wp == VK_ESCAPE || wp == (WPARAM)cfg.keys[10]) {
            if (rom_ready && !(lp & (1L << 30))) {
                paused ^= 1;
                audio_reset();
                update_menu_visibility();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        if (wp == VK_F11 || (wp == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000))) {
            toggle_fullscreen();
            return 0;
        }
        if (wp == '1' || wp == '2' || wp == '3' || wp == '4' || wp == '5') {
            slot = (int)(wp - '0');
            wchar_t label[128];
            swprintf(label, 128, L"Salvar Estado (Slot %d)", slot);
            SetWindowTextW(menu_buttons[6], label);
            swprintf(label, 128, L"Carregar Estado (Slot %d)", slot);
            SetWindowTextW(menu_buttons[7], label);
            swprintf(label, 128, L"Slot de salvamento ativo: %d", slot);
            show_message(label);
            return 0;
        }
        break;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits=(MINMAXINFO*)lp;limits->ptMinTrackSize.x=900;limits->ptMinTrackSize.y=650;return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        /* Settings own the client area; painting the launcher here can bleed
         * through the child labels when Windows repaints their background. */
        if (preferences_active()) {
            EndPaint(hwnd, &ps);
            return 0;
        }
        RECT r;
        GetClientRect(hwnd, &r);
        int w = r.right;
        int h = r.bottom;

        if (!running || paused || !rom_ready) {
            /* Render Menu / Museum theme */
            theme_render(hdc, w, h, paused, active_framebuffer, rom_ready, slot);
        } else {
            /* Render Active Game Framebuffer */
            RECT src_rect;
            RECT dst = video_destination(w, h, cfg.aspect, cfg.scale, &src_rect);
            if(cfg.aspect==2&&presented_width==SMB3_WIDE_WIDTH){
                int dw=w,dh=MulDiv(w,240,presented_width);
                if(dh>h){dh=h;dw=MulDiv(h,presented_width,240);}
                if(cfg.scale==1){int k=w/presented_width;if(h/240<k)k=h/240;if(k>0){dw=presented_width*k;dh=240*k;}}
                dst=(RECT){(w-dw)/2,(h-dh)/2,(w+dw)/2,(h+dh)/2};
                src_rect=(RECT){0,0,presented_width,240};
            }

            /* Fill letterbox / pillar bars */
            HBRUSH black = CreateSolidBrush(RGB(10, 12, 18));
            if (dst.left > 0) {
                RECT left_bar = {0, 0, dst.left, h};
                FillRect(hdc, &left_bar, black);
            }
            if (dst.right < w) {
                RECT right_bar = {dst.right, 0, w, h};
                FillRect(hdc, &right_bar, black);
            }
            if (dst.top > 0) {
                RECT top_bar = {0, 0, w, dst.top};
                FillRect(hdc, &top_bar, black);
            }
            if (dst.bottom < h) {
                RECT bottom_bar = {0, dst.bottom, w, h};
                FillRect(hdc, &bottom_bar, black);
            }
            DeleteObject(black);

            /* Blit NES Frame */
            BITMAPINFO info = {0};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = presented_width;
            info.bmiHeader.biHeight = -240;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;

            SetStretchBltMode(hdc, cfg.filter ? HALFTONE : COLORONCOLOR);
            StretchDIBits(hdc, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top,
                          src_rect.left, src_rect.top, src_rect.right - src_rect.left, src_rect.bottom - src_rect.top,
                          presented_framebuffer, &info, DIB_RGB_COLORS, SRCCOPY);
        }

        draw_notification(hdc, w, h);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd_line, int show_cmd) {
    (void)prev; (void)cmd_line;

    /* Get application directory */
    GetModuleFileNameW(NULL, root_dir, 32768);
    wchar_t *slash = wcsrchr(root_dir, L'\\');
    if (slash) *slash = 0;

    swprintf(ini_path, 32768, L"%ls\\config.ini", root_dir);
    swprintf(asset_dir, 32768, L"%ls\\assets", root_dir);

    preferences_load(&cfg, ini_path);
    glue_set_widescreen(cfg.aspect==2);
    input_init();
    input_select(cfg.device);input_configure(cfg.buttons);input_set_deadzone(cfg.deadzone);
    theme_init(asset_dir);

    /* Register Window Class */
    WNDCLASSW wc = {0};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"Smb3RecompWindow";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    if (!wc.hIcon) wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(18, 22, 34));
    RegisterClassW(&wc);

    /* Dimensions: 768x720 base (3x NES) */
    int win_w = 1280, win_h = 760;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - win_w) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - win_h) / 2;

    main_window = CreateWindowExW(0, L"Smb3RecompWindow",
                                 L"Mushroom Recompiled · v1.6.0",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
                                 sx, sy, win_w, win_h, NULL, NULL, inst, NULL);

    audio_open();

    int argument_count = 0;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments && argument_count > 1) try_load_rom(arguments[1]);
    else restore_saved_rom();
    if (arguments) LocalFree(arguments);

    update_menu_visibility();
    if(cfg.fullscreen)toggle_fullscreen();

    /* High Precision Frame Timer */
    LARGE_INTEGER freq, prev_time;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev_time);
    double target_frame_time = 1.0 / 60.0988;
    double next_frame_tick = (double)prev_time.QuadPart;
    timeBeginPeriod(1);

    MSG msg;
    int is_running = 1;

    while (is_running) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                is_running = 0;
                break;
            }
            if (preferences_route(&msg)) continue;
            if(msg.message==WM_KEYDOWN&&(!running||paused)&&(msg.wParam==VK_UP||msg.wParam==VK_DOWN||msg.wParam==VK_RETURN)){
                SendMessageW(main_window,WM_KEYDOWN,msg.wParam,msg.lParam);continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!is_running) break;

        /* Input Processing */
        input_update();
        InputState input = input_read();
        int menu_edge=input.pad&~previous_menu_pad;previous_menu_pad=input.pad;
        if((!running||paused)&&!preferences_active()){
            if(menu_edge&(INPUT_DOWN|INPUT_UP)){menu_selection=(menu_selection+((menu_edge&INPUT_UP)?MENU_COUNT-1:1))%MENU_COUNT;SetFocus(menu_buttons[menu_selection]);InvalidateRect(main_window,NULL,FALSE);}
            if(menu_edge&INPUT_A)SendMessageW(main_window,WM_COMMAND,menu_ids[menu_selection],0);
        }

        /* Map keys to NES bits */
        uint8_t nes_pad = 0;
        if (GetKeyState(cfg.keys[0]) & 0x8000) nes_pad |= NES_PAD_UP;
        if (GetKeyState(cfg.keys[1]) & 0x8000) nes_pad |= NES_PAD_DOWN;
        if (GetKeyState(cfg.keys[2]) & 0x8000) nes_pad |= NES_PAD_LEFT;
        if (GetKeyState(cfg.keys[3]) & 0x8000) nes_pad |= NES_PAD_RIGHT;
        if (GetKeyState(cfg.keys[4]) & 0x8000) nes_pad |= NES_PAD_B;
        if (GetKeyState(cfg.keys[5]) & 0x8000) nes_pad |= NES_PAD_A;
        if (GetKeyState(cfg.keys[6]) & 0x8000) nes_pad |= NES_PAD_SELECT;
        if (GetKeyState(cfg.keys[7]) & 0x8000) nes_pad |= NES_PAD_START;

        /* Merge Gamepad inputs */
        if (input.connected) {
            if (input.pad & INPUT_UP)     nes_pad |= NES_PAD_UP;
            if (input.pad & INPUT_DOWN)   nes_pad |= NES_PAD_DOWN;
            if (input.pad & INPUT_LEFT)   nes_pad |= NES_PAD_LEFT;
            if (input.pad & INPUT_RIGHT)  nes_pad |= NES_PAD_RIGHT;
            if (input.pad & INPUT_B)      nes_pad |= NES_PAD_B;
            if (input.pad & INPUT_A)      nes_pad |= NES_PAD_A;
            if (input.pad & INPUT_SELECT) nes_pad |= NES_PAD_SELECT;
            if (input.pad & INPUT_START)  nes_pad |= NES_PAD_START;

            /* Check Gamepad Menu toggle */
            if (input.menu & INPUT_MENU) {
                static int menu_btn_held = 0;
                if (!menu_btn_held && rom_ready) {
                    paused ^= 1;
                    update_menu_visibility();
                    InvalidateRect(main_window, NULL, FALSE);
                }
                menu_btn_held = 1;
            }
        }

        /* Turbo emulation (oscillates every other frame) */
        static int turbo_phase = 0;
        turbo_phase ^= 1;
        if ((GetKeyState(cfg.keys[8]) & 0x8000) || (input.pad & INPUT_TURBO_B)) {
            if (turbo_phase) nes_pad |= NES_PAD_B;
            else nes_pad &= ~NES_PAD_B;
        }
        if ((GetKeyState(cfg.keys[9]) & 0x8000) || (input.pad & INPUT_TURBO_A)) {
            if (turbo_phase) nes_pad |= NES_PAD_A;
            else nes_pad &= ~NES_PAD_A;
        }

        glue_set_pad1(nes_pad);

        /* Step Frame */
        if (running && !paused && rom_ready && !preferences_active()) {
            glue_step_frame();
            const uint32_t *fb = glue_get_framebuffer();
            if (fb) {
                memcpy(active_framebuffer, fb, sizeof(active_framebuffer));
                int image_height;
                const uint32_t *present=glue_get_present_framebuffer(&presented_width,&image_height);
                memcpy(presented_framebuffer,present,presented_width*240*sizeof(uint32_t));
                for(int y=0;y<240;y++)for(int x=0;x<presented_width;x++){
                    uint32_t c=presented_framebuffer[y*presented_width+x];int gain=cfg.brightness;
                    if(cfg.scanlines&&(y&1))gain=gain*(cfg.scanlines==1?80:55)/100;
                    int r=((c>>16)&255)*gain/100,g=((c>>8)&255)*gain/100,b=(c&255)*gain/100;
                    presented_framebuffer[y*presented_width+x]=0xff000000u|((r>255?255:r)<<16)|((g>255?255:g)<<8)|(b>255?255:b);
                }
            }

            /* Drain and send audio */
            int16_t audio_out[4096];
            size_t got = glue_get_audio_samples(audio_out, 4096, cfg.stereo);
            if (got > 0) {
                audio_submit_samples(audio_out, got);
            }

            InvalidateRect(main_window, NULL, FALSE);
        }

        /* Frame Rate Limiting (~60.0988 Hz) */
        LARGE_INTEGER cur_time;
        QueryPerformanceCounter(&cur_time);
        next_frame_tick += target_frame_time * (double)freq.QuadPart;
        while ((double)cur_time.QuadPart < next_frame_tick) {
            Sleep(1);
            QueryPerformanceCounter(&cur_time);
        }
        if ((double)cur_time.QuadPart - next_frame_tick > 3 * target_frame_time * freq.QuadPart)
            next_frame_tick = (double)cur_time.QuadPart;
    }

    audio_close();
    timeEndPeriod(1);
    theme_close();
    input_close();
    return 0;
}
