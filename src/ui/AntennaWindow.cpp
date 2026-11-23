// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "AntennaWindow.h"

#include "../learn/Antenna.h"

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <string>

namespace orl {
namespace {

const char* kClass = "ORLAntennaWindow";
enum { W_LPRESET = 1, W_PRESET, W_LFREQ, W_FREQ, W_RESULT, W_LGUIDE, W_GUIDES, W_GTEXT };

struct State { HWND self = nullptr, owner = nullptr; HFONT font = nullptr; int dpi = 96; double startHz = 0; } G;

int S(int v) { return MulDiv(v, G.dpi, 96); }
HWND H(int id) { return GetDlgItem(G.self, id); }

void recalc() {
    char b[64] = {0};
    GetWindowTextA(H(W_FREQ), b, sizeof b);
    for (char* p = b; *p; ++p) if (*p == ',') *p = '.';
    const double mhz = std::atof(b);
    SetWindowTextA(H(W_RESULT), describeDesign(designAntenna(mhz * 1e6)).c_str());
}

void showGuide() {
    const int i = (int)SendMessageA(H(W_GUIDES), LB_GETCURSEL, 0, 0);
    const auto& g = antennaGuides();
    if (i >= 0 && i < (int)g.size()) SetWindowTextA(H(W_GTEXT), g[i].text);
}

void layout() {
    RECT rc; GetClientRect(G.self, &rc);
    const int m = S(10), W = (int)rc.right, Hh = (int)rc.bottom, lw = std::max(S(330), W * 2 / 5), x2 = 2 * m + lw, rw = W - x2 - m;
    SetWindowPos(H(W_LPRESET), nullptr, m, m, lw, S(18), SWP_NOZORDER);
    SetWindowPos(H(W_PRESET), nullptr, m, m + S(20), lw, S(300), SWP_NOZORDER);
    SetWindowPos(H(W_LFREQ), nullptr, m, m + S(54), lw, S(18), SWP_NOZORDER);
    SetWindowPos(H(W_FREQ), nullptr, m, m + S(74), S(160), S(24), SWP_NOZORDER);
    SetWindowPos(H(W_RESULT), nullptr, m, m + S(108), lw, Hh - S(108) - 2 * m, SWP_NOZORDER);
    SetWindowPos(H(W_LGUIDE), nullptr, x2, m, rw, S(18), SWP_NOZORDER);
    SetWindowPos(H(W_GUIDES), nullptr, x2, m + S(20), rw, S(150), SWP_NOZORDER);
    SetWindowPos(H(W_GTEXT), nullptr, x2, m + S(176), rw, Hh - S(176) - 2 * m, SWP_NOZORDER);
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            G.self = h;
            HDC dc = GetDC(nullptr); G.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
            auto mk = [&](const char* cls, const char* text, DWORD style, DWORD ex, int id) {
                HWND c = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, nullptr, nullptr);
                SendMessageA(c, WM_SETFONT, (WPARAM)G.font, TRUE);
                return c;
            };
            mk("STATIC", "Pick a common band...", 0, 0, W_LPRESET);
            HWND cb = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, W_PRESET);
            SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)"(type your own below)");
            for (const auto& p : antennaPresets()) SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)p.name);
            SendMessageA(cb, CB_SETCURSEL, 0, 0);
            mk("STATIC", "...or type a frequency in MHz:", 0, 0, W_LFREQ);
            char b[32]; std::snprintf(b, sizeof b, "%.3f", G.startHz > 0 ? G.startHz / 1e6 : 98.1);
            mk("EDIT", b, ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, W_FREQ);
            mk("EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE, W_RESULT);
            mk("STATIC", "Build guides:", 0, 0, W_LGUIDE);
            HWND lb = mk("LISTBOX", "", WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP, WS_EX_CLIENTEDGE, W_GUIDES);
            for (const auto& g : antennaGuides()) SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)g.title);
            SendMessageA(lb, LB_SETCURSEL, 0, 0);
            mk("EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE, W_GTEXT);
            layout(); recalc(); showGuide();
            return 0;
        }
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = S(760); mm->ptMinTrackSize.y = S(480); return 0; }
        case WM_COMMAND:
            if (LOWORD(w) == W_FREQ && HIWORD(w) == EN_CHANGE) recalc();
            else if (LOWORD(w) == W_GUIDES && HIWORD(w) == LBN_SELCHANGE) showGuide();
            else if (LOWORD(w) == W_PRESET && HIWORD(w) == CBN_SELCHANGE) {
                const int i = (int)SendMessageA(H(W_PRESET), CB_GETCURSEL, 0, 0) - 1;
                if (i >= 0 && i < (int)antennaPresets().size()) {
                    char b[32]; std::snprintf(b, sizeof b, "%.3f", antennaPresets()[i].hz / 1e6);
                    SetWindowTextA(H(W_FREQ), b);          // triggers EN_CHANGE -> recalc
                }
            }
            return 0;
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: G.self = nullptr; return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

} // namespace

bool registerAntennaClass(HINSTANCE hi) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = proc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = kClass;
    return RegisterClassA(&wc) != 0;
}

void showAntennaWorkshop(HWND owner, HINSTANCE hi, HFONT font, double startHz) {
    if (G.self) { ShowWindow(G.self, SW_RESTORE); SetForegroundWindow(G.self); return; }
    G.owner = owner; G.font = font; G.startHz = startHz;
    HDC dc = GetDC(nullptr); G.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    CreateWindowExA(0, kClass, "OpenRadio Lab - Antenna Workshop", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                    S(1000), S(640), owner, nullptr, hi, nullptr);
}

} // namespace orl
