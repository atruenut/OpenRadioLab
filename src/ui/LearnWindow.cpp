// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "LearnWindow.h"

#include "../learn/Content.h"

#include <commctrl.h>
#include <string>

namespace orl {
namespace {

const char* kLearnClass = "ORLLearnWindow";
enum { ID_LIST = 1, ID_TEXT, ID_TRY, ID_TITLE };

struct State {
    HWND self = nullptr, owner = nullptr;
    HFONT font = nullptr, big = nullptr;
    int dpi = 96;
} G;

int S(int v) { return MulDiv(v, G.dpi, 96); }

std::string crlf(const char* s) {
    std::string o;
    for (; *s; ++s) { if (*s == '\n') o += "\r\n\r\n"; else o += *s; }    // blank line between paragraphs
    return o;
}

void showItem(int i) {
    size_t nl, nt;
    const Lesson* L = lessons(nl);
    const Term* T = dictionary(nt);
    std::string title, text;
    bool tryBtn = false;
    if (i >= 0 && i < (int)nl) {
        title = L[i].title;
        text = crlf(L[i].body);
        tryBtn = L[i].tryHz > 0;
        if (tryBtn) SetWindowTextA(GetDlgItem(G.self, ID_TRY), L[i].tryLabel);
    } else {
        title = "Radio dictionary";
        for (size_t k = 0; k < nt; ++k) text += std::string(T[k].term) + "\r\n    " + T[k].meaning + "\r\n\r\n";
    }
    SetWindowTextA(GetDlgItem(G.self, ID_TITLE), title.c_str());
    SetWindowTextA(GetDlgItem(G.self, ID_TEXT), text.c_str());
    ShowWindow(GetDlgItem(G.self, ID_TRY), tryBtn ? SW_SHOW : SW_HIDE);
}

void layout() {
    RECT rc; GetClientRect(G.self, &rc);
    const int m = S(8), lw = S(250);
    SetWindowPos(GetDlgItem(G.self, ID_LIST), nullptr, m, m, lw, rc.bottom - 2 * m, SWP_NOZORDER);
    const int x = m * 2 + lw, w = rc.right - x - m;
    SetWindowPos(GetDlgItem(G.self, ID_TITLE), nullptr, x, m, w, S(30), SWP_NOZORDER);
    SetWindowPos(GetDlgItem(G.self, ID_TEXT), nullptr, x, m + S(34), w, rc.bottom - S(34) - S(40) - 2 * m, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(G.self, ID_TRY), nullptr, x, rc.bottom - S(32) - m + S(4), S(320), S(28), SWP_NOZORDER);
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            G.self = h;
            HDC dc = GetDC(nullptr); G.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
            LOGFONTA lf{};
            GetObjectA(G.font, sizeof lf, &lf);
            lf.lfHeight = lf.lfHeight * 14 / 10; lf.lfWeight = FW_BOLD;
            G.big = CreateFontIndirectA(&lf);
            HWND list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP,
                                        0, 0, 10, 10, h, (HMENU)(INT_PTR)ID_LIST, nullptr, nullptr);
            HWND title = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, (HMENU)(INT_PTR)ID_TITLE, nullptr, nullptr);
            HWND text = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                        0, 0, 10, 10, h, (HMENU)(INT_PTR)ID_TEXT, nullptr, nullptr);
            HWND tryb = CreateWindowExA(0, "BUTTON", "Try it", WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 10, 10, h, (HMENU)(INT_PTR)ID_TRY, nullptr, nullptr);
            SendMessageA(list, WM_SETFONT, (WPARAM)G.font, TRUE);
            SendMessageA(title, WM_SETFONT, (WPARAM)G.big, TRUE);
            SendMessageA(text, WM_SETFONT, (WPARAM)G.font, TRUE);
            SendMessageA(tryb, WM_SETFONT, (WPARAM)G.font, TRUE);
            SendMessageA(text, EM_SETLIMITTEXT, 200000, 0);
            size_t nl; const Lesson* L = lessons(nl);
            for (size_t i = 0; i < nl; ++i) SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)L[i].title);
            SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)"Radio dictionary (A-Z)");
            SendMessageA(list, LB_SETCURSEL, 0, 0);
            layout();
            showItem(0);
            return 0;
        }
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = (MINMAXINFO*)l;
            mm->ptMinTrackSize.x = S(640); mm->ptMinTrackSize.y = S(400);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(w) == ID_LIST && HIWORD(w) == LBN_SELCHANGE) {
                showItem((int)SendMessageA(GetDlgItem(h, ID_LIST), LB_GETCURSEL, 0, 0));
            } else if (LOWORD(w) == ID_TRY) {
                size_t nl; const Lesson* L = lessons(nl);
                const int i = (int)SendMessageA(GetDlgItem(h, ID_LIST), LB_GETCURSEL, 0, 0);
                if (i >= 0 && i < (int)nl && L[i].tryHz > 0)
                    PostMessageA(G.owner, WM_ORL_TRYIT, (WPARAM)L[i].tryMode, (LPARAM)(unsigned long)L[i].tryHz);
            }
            return 0;
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY:
            if (G.big) { DeleteObject(G.big); G.big = nullptr; }
            G.self = nullptr;
            return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

} // namespace

bool registerLearnClass(HINSTANCE hi) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kLearnClass;
    return RegisterClassA(&wc) != 0;
}

void showLearnWindow(HWND owner, HINSTANCE hi, HFONT font) {
    if (G.self) { ShowWindow(G.self, SW_RESTORE); SetForegroundWindow(G.self); return; }
    G.owner = owner; G.font = font;
    G.dpi = 96;
    HDC dc = GetDC(nullptr); G.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    CreateWindowExA(0, kLearnClass, "OpenRadio Lab - Learn", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                    S(940), S(640), owner, nullptr, hi, nullptr);
}

} // namespace orl
