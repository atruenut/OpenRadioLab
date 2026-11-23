// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "AtlasWindow.h"

#include "LearnWindow.h"
#include "../Bands.h"
#include "../Util.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace orl {
namespace {

const char* kAtlasClass = "ORLAtlasWindow";
enum { A_FILTER = 1, A_LIST, A_TITLE, A_TEXT, A_TRY, A_LFILTER };

struct State {
    HWND self = nullptr, owner = nullptr;
    HFONT font = nullptr, big = nullptr;
    int dpi = 96;
    std::vector<const Band*> shown;
} G;

int S(int v) { return MulDiv(v, G.dpi, 96); }
HWND H(int id) { return GetDlgItem(G.self, id); }

std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

std::string range(const Band& b) { return formatFrequency(b.lo) + " - " + formatFrequency(b.hi); }

void fillList() {
    char buf[200] = {0};
    GetWindowTextA(H(A_FILTER), buf, sizeof buf);
    const std::string f = lower(buf);
    size_t n; const Band* t = bandTable(n);
    G.shown.clear();
    for (size_t i = 0; i < n; ++i) {
        const std::string hay = lower(std::string(t[i].name) + " " + t[i].category + " " + t[i].note);
        if (f.empty() || hay.find(f) != std::string::npos) G.shown.push_back(&t[i]);
    }
    std::stable_sort(G.shown.begin(), G.shown.end(), [](const Band* a, const Band* b) { return a->lo < b->lo; });
    HWND l = H(A_LIST);
    SendMessageA(l, LB_RESETCONTENT, 0, 0);
    for (const Band* b : G.shown) {
        const std::string row = formatFrequency(b->lo) + "  " + b->name;
        SendMessageA(l, LB_ADDSTRING, 0, (LPARAM)row.c_str());
    }
    if (!G.shown.empty()) SendMessageA(l, LB_SETCURSEL, 0, 0);
}

void showSel() {
    const int i = (int)SendMessageA(H(A_LIST), LB_GETCURSEL, 0, 0);
    if (i < 0 || i >= (int)G.shown.size()) {
        SetWindowTextA(H(A_TITLE), "Frequency Atlas");
        SetWindowTextA(H(A_TEXT), "Nothing matches that search.");
        ShowWindow(H(A_TRY), SW_HIDE);
        return;
    }
    const Band& b = *G.shown[i];
    static const char* modes[] = {"WFM (wide FM)", "NFM (narrow FM)", "AM", "USB (upper sideband)", "LSB (lower sideband)", "CW (Morse)"};
    std::string t = "Range: " + range(b) + "\r\nGroup: " + b.category + "\r\nUsually listened to as: " + modes[b.mode] + "\r\n\r\n" + b.note +
                    "\r\n\r\nWhere this information comes from:\r\n" + b.source +
                    "\r\n\r\nThis atlas is a plain-language summary of well-known bands, not the official allocation table. "
                    "Private services (cell phones, police and fire, business radio) are deliberately not described here.";
    SetWindowTextA(H(A_TITLE), b.name);
    SetWindowTextA(H(A_TEXT), t.c_str());
    if (b.tryHz > 0) {
        const std::string label = "Tune to " + formatFrequency(b.tryHz);
        SetWindowTextA(H(A_TRY), label.c_str());
        ShowWindow(H(A_TRY), SW_SHOW);
    } else ShowWindow(H(A_TRY), SW_HIDE);
}

void layout() {
    RECT rc; GetClientRect(G.self, &rc);
    const int W = (int)rc.right, Hh = (int)rc.bottom, m = S(8), lw = S(360);
    SetWindowPos(H(A_LFILTER), nullptr, m, m + S(4), S(50), S(20), SWP_NOZORDER);
    SetWindowPos(H(A_FILTER), nullptr, m + S(54), m, lw - S(54), S(24), SWP_NOZORDER);
    SetWindowPos(H(A_LIST), nullptr, m, m + S(30), lw, Hh - 2 * m - S(30), SWP_NOZORDER);
    const int x = 2 * m + lw, w = W - x - m;
    SetWindowPos(H(A_TITLE), nullptr, x, m, w, S(30), SWP_NOZORDER);
    SetWindowPos(H(A_TEXT), nullptr, x, m + S(34), w, Hh - S(34) - S(40) - 2 * m, SWP_NOZORDER);
    SetWindowPos(H(A_TRY), nullptr, x, Hh - S(32) - m + S(4), S(260), S(28), SWP_NOZORDER);
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            G.self = h;
            HDC dc = GetDC(nullptr); G.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
            LOGFONTA lf{}; GetObjectA(G.font, sizeof lf, &lf);
            lf.lfHeight = lf.lfHeight * 14 / 10; lf.lfWeight = FW_BOLD;
            G.big = CreateFontIndirectA(&lf);
            auto mk = [&](const char* cls, const char* text, DWORD style, DWORD ex, int id, HFONT f) {
                HWND c = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, nullptr, nullptr);
                SendMessageA(c, WM_SETFONT, (WPARAM)f, TRUE);
                return c;
            };
            mk("STATIC", "Search:", 0, 0, A_LFILTER, G.font);
            mk("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, A_FILTER, G.font);
            mk("LISTBOX", "", WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP, WS_EX_CLIENTEDGE, A_LIST, G.font);
            mk("STATIC", "", 0, 0, A_TITLE, G.big);
            mk("EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE, A_TEXT, G.font);
            mk("BUTTON", "Tune", BS_PUSHBUTTON | WS_TABSTOP, 0, A_TRY, G.font);
            layout(); fillList(); showSel();
            return 0;
        }
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = S(760); mm->ptMinTrackSize.y = S(440); return 0; }
        case WM_COMMAND:
            if (LOWORD(w) == A_FILTER && HIWORD(w) == EN_CHANGE) { fillList(); showSel(); }
            else if (LOWORD(w) == A_LIST && HIWORD(w) == LBN_SELCHANGE) showSel();
            else if (LOWORD(w) == A_TRY) {
                const int i = (int)SendMessageA(H(A_LIST), LB_GETCURSEL, 0, 0);
                if (i >= 0 && i < (int)G.shown.size() && G.shown[i]->tryHz > 0)
                    PostMessageA(G.owner, WM_ORL_TRYIT, (WPARAM)G.shown[i]->mode, (LPARAM)(unsigned long)G.shown[i]->tryHz);
            }
            return 0;
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: if (G.big) { DeleteObject(G.big); G.big = nullptr; } G.self = nullptr; return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

} // namespace

bool registerAtlasClass(HINSTANCE hi) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = proc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = kAtlasClass;
    return RegisterClassA(&wc) != 0;
}

void showAtlasWindow(HWND owner, HINSTANCE hi, HFONT font) {
    if (G.self) { ShowWindow(G.self, SW_RESTORE); SetForegroundWindow(G.self); return; }
    G.owner = owner; G.font = font;
    HDC dc = GetDC(nullptr); G.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    CreateWindowExA(0, kAtlasClass, "OpenRadio Lab - Frequency Atlas", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                    S(1000), S(660), owner, nullptr, hi, nullptr);
}

} // namespace orl
