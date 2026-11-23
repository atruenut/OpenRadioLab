// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "MorseWindow.h"

#include "../Sync.h"
#include "../dsp/MorseDecoder.h"

#include <atomic>
#include <cstring>
#include <cstdio>
#include <string>

namespace orl {
namespace {

const char* kClass = "ORLMorseWindow";
enum { M_TEXT = 1, M_STATUS, M_CLEAR, M_COPY, M_HELP };

struct State {
    HWND self = nullptr, owner = nullptr; HFONT font = nullptr; int dpi = 96;
    Mutex mu; MorseDecoder dec{48000.0, 700.0};
    std::atomic<bool> open{false}, cw{false};
    std::string shown;
} M;

int S(int v) { return MulDiv(v, M.dpi, 96); }
HWND H(int id) { return GetDlgItem(M.self, id); }

void refresh() {
    std::string t, st; double wpm; bool sig;
    { Lock l(M.mu); t = M.dec.text(); wpm = M.dec.wpm(); sig = M.dec.hearingSignal(); }
    if (t != M.shown) {
        M.shown = t;
        std::string crlf; for (char c : t) { if (c == '\n') crlf += "\r\n"; else crlf += c; }
        SetWindowTextA(H(M_TEXT), crlf.c_str());
        SendMessageA(H(M_TEXT), EM_SETSEL, (WPARAM)crlf.size(), (LPARAM)crlf.size());
        SendMessageA(H(M_TEXT), EM_SCROLLCARET, 0, 0);
    }
    if (!M.cw) st = "Not listening in CW mode. Set 'Listen as' to CW (Morse) and tune a Morse signal.";
    else if (!sig) st = "Listening... no clear Morse tone yet.";
    else { char b[96]; std::snprintf(b, sizeof b, "Hearing a Morse signal, about %.0f words per minute.", wpm); st = b; }
    SetWindowTextA(H(M_STATUS), st.c_str());
}

void layout() {
    RECT rc; GetClientRect(M.self, &rc);
    const int m = S(10), W = (int)rc.right, Hh = (int)rc.bottom;
    SetWindowPos(H(M_STATUS), nullptr, m, m, W - 2 * m, S(20), SWP_NOZORDER);
    SetWindowPos(H(M_TEXT), nullptr, m, m + S(26), W - 2 * m, Hh - S(26) - S(110) - 2 * m, SWP_NOZORDER);
    SetWindowPos(H(M_CLEAR), nullptr, m, Hh - S(104) - m + S(0), S(100), S(26), SWP_NOZORDER);
    SetWindowPos(H(M_COPY), nullptr, m + S(110), Hh - S(104) - m, S(120), S(26), SWP_NOZORDER);
    SetWindowPos(H(M_HELP), nullptr, m, Hh - S(72) - m, W - 2 * m, S(72), SWP_NOZORDER);
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            M.self = h;
            HDC dc = GetDC(nullptr); M.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
            auto mk = [&](const char* cls, const char* text, DWORD style, DWORD ex, int id) {
                HWND c = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, nullptr, nullptr);
                SendMessageA(c, WM_SETFONT, (WPARAM)M.font, TRUE);
                return c;
            };
            mk("STATIC", "", 0, 0, M_STATUS);
            mk("EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE, M_TEXT);
            mk("BUTTON", "Clear", BS_PUSHBUTTON | WS_TABSTOP, 0, M_CLEAR);
            mk("BUTTON", "Copy text", BS_PUSHBUTTON | WS_TABSTOP, 0, M_COPY);
            mk("STATIC", "How it works: the decoder listens to the 700 Hz Morse tone and measures how long each beep and gap is, so it adapts to any speed. "
                         "It works best on a clean, steady signal; weak, fading or overlapping stations produce errors, and * marks a pattern it could not read. "
                         "It writes down what it hears. It does not tell you who is sending.", 0, 0, M_HELP);
            layout(); refresh();
            SetTimer(h, 1, 250, nullptr);
            M.open = true;
            return 0;
        }
        case WM_TIMER: refresh(); return 0;
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = S(520); mm->ptMinTrackSize.y = S(380); return 0; }
        case WM_COMMAND:
            if (LOWORD(w) == M_CLEAR) { { Lock lk(M.mu); M.dec.reset(); } refresh(); }
            else if (LOWORD(w) == M_COPY) {
                const std::string t = M.shown;
                if (!t.empty() && OpenClipboard(h)) {
                    EmptyClipboard();
                    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, t.size() + 1);
                    if (g) { memcpy(GlobalLock(g), t.c_str(), t.size() + 1); GlobalUnlock(g); SetClipboardData(CF_TEXT, g); }
                    CloseClipboard();
                }
            }
            return 0;
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: KillTimer(h, 1); M.open = false; M.self = nullptr; return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

} // namespace

bool registerMorseClass(HINSTANCE hi) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = proc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = kClass;
    return RegisterClassA(&wc) != 0;
}

void showMorseWindow(HWND owner, HINSTANCE hi, HFONT font) {
    if (M.self) { ShowWindow(M.self, SW_RESTORE); SetForegroundWindow(M.self); return; }
    M.owner = owner; M.font = font;
    { Lock l(M.mu); M.dec.reset(); }
    M.shown.clear();
    HDC dc = GetDC(nullptr); M.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    CreateWindowExA(0, kClass, "OpenRadio Lab - Morse Decoder", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                    S(640), S(480), owner, nullptr, hi, nullptr);
}

bool morseWindowOpen() { return M.open; }
void morseSetCwMode(bool cw) { M.cw = cw; }

void morseFeed(const float* a, size_t n, int ch) {
    if (!M.open || !M.cw || n == 0) return;
    static thread_local float mono[4096];
    Lock l(M.mu);
    for (size_t p = 0; p < n;) {
        const size_t k = n - p < 4096 ? n - p : 4096;
        for (size_t i = 0; i < k; ++i) mono[i] = a[(p + i) * (size_t)ch];
        M.dec.feed(mono, k);
        p += k;
    }
}

} // namespace orl
