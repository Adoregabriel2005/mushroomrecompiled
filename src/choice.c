#define WIN32_LEAN_AND_MEAN
#include "choice.h"
#include <windowsx.h>
#include <stdlib.h>
#include <wchar.h>

/* Custom inline value selector for Super Mario Bros. 3 Recomp.
 * Replaces OS dropdowns with an inline stepper with arrows and keyboard/mousewheel support. */
typedef struct {
    wchar_t **values;
    int count;
    int capacity;
    int index;
    int hover;
    HFONT font;
} Choice;

static void clear(Choice *c) {
    if (!c) return;
    for (int i = 0; i < c->count; i++) free(c->values[i]);
    free(c->values);
    c->values = NULL;
    c->count = 0;
    c->capacity = 0;
    c->index = -1;
}

static void adjust(HWND hwnd, Choice *c, int delta) {
    if (!c || !c->count) return;
    c->index = (c->index + delta + c->count) % c->count;
    InvalidateRect(hwnd, NULL, FALSE);
    SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), CBN_SELCHANGE), (LPARAM)hwnd);
}

static LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Choice *c = (Choice *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_CREATE) {
        c = (Choice *)calloc(1, sizeof *c);
        if (!c) return -1;
        c->index = -1;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)c);
        return 0;
    }
    if (!c) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case CB_ADDSTRING: {
        if (c->count == c->capacity) {
            int n = c->capacity ? c->capacity * 2 : 16;
            void *p = realloc(c->values, (size_t)n * sizeof *c->values);
            if (!p) return CB_ERRSPACE;
            c->values = (wchar_t **)p;
            c->capacity = n;
        }
        wchar_t *copy = _wcsdup((const wchar_t *)lp);
        if (!copy) return CB_ERRSPACE;
        c->values[c->count] = copy;
        return c->count++;
    }
    case CB_RESETCONTENT:
        clear(c);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case CB_GETCOUNT:
        return c->count;
    case CB_GETCURSEL:
        return c->index;
    case CB_SETCURSEL:
        c->index = ((int)wp >= 0 && (int)wp < c->count) ? (int)wp : -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return c->index;
    case CB_GETLBTEXT:
        if ((int)wp < 0 || (int)wp >= c->count) return CB_ERR;
        wcscpy((wchar_t *)lp, c->values[wp]);
        return (LRESULT)wcslen(c->values[wp]);
    case CB_GETLBTEXTLEN:
        return ((int)wp >= 0 && (int)wp < c->count) ? (LRESULT)wcslen(c->values[wp]) : CB_ERR;
    case WM_SETFONT:
        c->font = (HFONT)wp;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)c->font;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | (wp == VK_RETURN ? DLGC_WANTMESSAGE : 0);
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        if (!c->hover) {
            c->hover = 1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        c->hover = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        RECT r;
        GetClientRect(hwnd, &r);
        adjust(hwnd, c, GET_X_LPARAM(lp) < r.right / 3 ? -1 : 1);
        return 0;
    }
    case WM_MOUSEWHEEL:
        adjust(hwnd, c, GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_LEFT || wp == VK_UP) {
            adjust(hwnd, c, -1);
            return 0;
        }
        if (wp == VK_RIGHT || wp == VK_DOWN || wp == VK_RETURN || wp == VK_SPACE) {
            adjust(hwnd, c, 1);
            return 0;
        }
        break;
    case WM_PAINT:
    case WM_PRINTCLIENT: {
        PAINTSTRUCT ps;
        HDC target = (msg == WM_PAINT) ? BeginPaint(hwnd, &ps) : (HDC)wp;
        RECT r;
        GetClientRect(hwnd, &r);
        if (r.right < 1 || r.bottom < 1) {
            if (msg == WM_PAINT) EndPaint(hwnd, &ps);
            return 0;
        }
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, r.right, r.bottom);
        HGDIOBJ old = SelectObject(dc, bitmap);
        int focus = (GetFocus() == hwnd);

        /* SMB3 Theme Colors: Deep dark blue-black base, gold accents, crimson highlights */
        HBRUSH brush = CreateSolidBrush(RGB(18, 22, 34));
        FillRect(dc, &r, brush);
        DeleteObject(brush);

        brush = CreateSolidBrush(c->hover || focus ? RGB(38, 48, 72) : RGB(26, 32, 50));
        HPEN pen = CreatePen(PS_SOLID, 1, focus ? RGB(252, 216, 0) : RGB(64, 76, 110));
        HGDIOBJ ob = SelectObject(dc, brush);
        HGDIOBJ op = SelectObject(dc, pen);
        RoundRect(dc, 0, 0, r.right - 1, r.bottom - 1, 10, 10);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(pen);
        DeleteObject(brush);

        if (c->font) SelectObject(dc, c->font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(250, 246, 235));
        int arrow = r.bottom * 3 / 4;
        if (arrow < 18) arrow = 18;

        RECT text = {arrow, 0, r.right - arrow, r.bottom};
        if (c->index >= 0 && c->index < c->count) {
            DrawTextW(dc, c->values[c->index], -1, &text, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
        }

        SetTextColor(dc, focus || c->hover ? RGB(252, 216, 0) : RGB(160, 176, 210));
        RECT left = {0, 0, arrow, r.bottom};
        RECT right = {r.right - arrow, 0, r.right, r.bottom};
        DrawTextW(dc, L"◀", -1, &left, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(dc, L"▶", -1, &right, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        BitBlt(target, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        if (msg == WM_PAINT) EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        clear(c);
        free(c);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void choice_register(HINSTANCE instance) {
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = procedure;
    wc.hInstance = instance;
    wc.lpszClassName = L"Smb3Choice";
    wc.hCursor = LoadCursor(NULL, IDC_HAND);
    RegisterClassW(&wc);
}
