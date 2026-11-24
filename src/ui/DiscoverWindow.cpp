// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "DiscoverWindow.h"

#include "../Util.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace orl {
namespace {

const char* kClass = "ORLDiscoverWindow";
enum { D_QUICK = 1, D_FULL, D_STOP, D_AUTO, D_STATUS, D_LIST, D_DETAIL, D_LISTEN, D_FAV, D_LOG, D_LLOG, D_JOURNAL };

struct State {
    HWND self = nullptr, owner = nullptr; HFONT font = nullptr; int dpi = 96;
    DiscoverHost host;
    std::vector<DiscoveryRow> rows;
    bool sweeping = false, autoOn = false;
} G;

int S(int v) { return MulDiv(v, G.dpi, 96); }
HWND H(int id) { return GetDlgItem(G.self, id); }

int selected() { return (int)SendMessageA(H(D_LIST), LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED); }

void showDetail() {
    const int i = selected();
    if (i < 0 || i >= (int)G.rows.size()) { SetWindowTextA(H(D_DETAIL), "Select a signal to read about it. Double-click to listen."); return; }
    const DiscoveryRow& r = G.rows[i];
    char b[200];
    std::snprintf(b, sizeof b, "%s   (%.0f dB above the noise, about %.0f kHz wide, seen in %d of %d sweeps)\r\n", formatFrequency(r.hz).c_str(), r.snrDb, r.widthHz / 1e3, r.seen, r.passes);
    std::string t = b + r.what + ".\r\nDouble-click or press Listen to tune there. If it sounds wrong, try another mode in the main window.";
    SetWindowTextA(H(D_DETAIL), t.c_str());
}

void applyButtons() {
    EnableWindow(H(D_QUICK), !G.sweeping); EnableWindow(H(D_FULL), !G.sweeping); EnableWindow(H(D_STOP), G.sweeping);
    SetWindowTextA(H(D_AUTO), G.autoOn ? "Auto-listen: ON (press to stop)" : "Auto-listen: off");
}

void fill() {
    HWND lv = H(D_LIST);
    const int sel = selected();
    double selHz = (sel >= 0 && sel < (int)G.rows.size()) ? G.rows[sel].hz : 0;
    const int top = (int)SendMessageA(lv, LVM_GETTOPINDEX, 0, 0);
    SendMessageA(lv, WM_SETREDRAW, FALSE, 0);
    SendMessageA(lv, LVM_DELETEALLITEMS, 0, 0);
    static const char* modes[] = {"WFM", "NFM", "AM", "USB", "LSB", "CW"};
    for (size_t k = 0; k < G.rows.size(); ++k) {
        const auto& r = G.rows[k];
        LVITEMA it{}; it.mask = LVIF_TEXT; it.iItem = (int)k; std::string f = formatFrequency(r.hz); it.pszText = (LPSTR)f.c_str();
        SendMessageA(lv, LVM_INSERTITEMA, 0, (LPARAM)&it);
        char b[64];
        std::snprintf(b, sizeof b, "%.0f dB", r.snrDb); ListView_SetItemText(lv, (int)k, 1, b);
        std::snprintf(b, sizeof b, "%d/%d%s", r.seen, r.passes, r.activeNow ? "  now" : ""); ListView_SetItemText(lv, (int)k, 2, b);
        std::snprintf(b, sizeof b, r.widthHz >= 1e6 ? "%.1f MHz" : "%.0f kHz", r.widthHz >= 1e6 ? r.widthHz / 1e6 : r.widthHz / 1e3); ListView_SetItemText(lv, (int)k, 3, b);
        ListView_SetItemText(lv, (int)k, 4, (LPSTR)r.where.c_str());
        ListView_SetItemText(lv, (int)k, 5, (LPSTR)modes[std::max(0, std::min(5, r.mode))]);
        if (selHz > 0 && std::fabs(r.hz - selHz) < 1.0) ListView_SetItemState(lv, (int)k, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    if (top > 0 && top < (int)G.rows.size()) SendMessageA(lv, LVM_ENSUREVISIBLE, (WPARAM)G.rows.size() - 1, FALSE), SendMessageA(lv, LVM_ENSUREVISIBLE, top, FALSE);
    SendMessageA(lv, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lv, nullptr, TRUE);
    showDetail();
}

void layout() {
    RECT rc; GetClientRect(G.self, &rc);
    const int m = S(10), W = (int)rc.right, Hh = (int)rc.bottom, bh = S(28);
    int x = m;
    auto btn = [&](int id, int w) { SetWindowPos(H(id), nullptr, x, m, w, bh, SWP_NOZORDER); x += w + S(6); };
    btn(D_QUICK, S(120)); btn(D_FULL, S(120)); btn(D_STOP, S(70)); x += S(14); btn(D_AUTO, S(210));
    SetWindowPos(H(D_STATUS), nullptr, m, m + bh + S(6), W - 2 * m, S(20), SWP_NOZORDER);
    const int top = m + bh + S(30), detH = S(78), logH = S(70), btnH = S(28);
    const int listH = Hh - top - detH - logH - btnH - S(24) - 3 * m;
    SetWindowPos(H(D_LIST), nullptr, m, top, W - 2 * m, std::max(S(80), listH), SWP_NOZORDER);
    int y = top + std::max(S(80), listH) + m;
    SetWindowPos(H(D_DETAIL), nullptr, m, y, W - 2 * m, detH, SWP_NOZORDER); y += detH + m;
    x = m;
    SetWindowPos(H(D_LISTEN), nullptr, x, y, S(100), btnH, SWP_NOZORDER); x += S(106);
    SetWindowPos(H(D_FAV), nullptr, x, y, S(150), btnH, SWP_NOZORDER); x += S(156);
    SetWindowPos(H(D_JOURNAL), nullptr, x, y, S(190), btnH, SWP_NOZORDER);
    y += btnH + S(4);
    SetWindowPos(H(D_LLOG), nullptr, m, y, W - 2 * m, S(18), SWP_NOZORDER); y += S(20);
    SetWindowPos(H(D_LOG), nullptr, m, y, W - 2 * m, std::max(S(40), Hh - y - m), SWP_NOZORDER);
    // the list's columns share the width
    const int lw = W - 2 * m - S(24);
    const int cw[6] = {S(110), S(80), S(90), S(80), 0, S(60)};
    int used = 0; for (int c : cw) used += c;
    for (int c = 0; c < 6; ++c) SendMessageA(H(D_LIST), LVM_SETCOLUMNWIDTH, c, c == 4 ? std::max(S(120), lw - used) : cw[c]);
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            G.self = h;
            auto mk = [&](const char* cls, const char* text, DWORD style, DWORD ex, int id) {
                HWND c = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, nullptr, nullptr);
                SendMessageA(c, WM_SETFONT, (WPARAM)G.font, TRUE);
                return c;
            };
            mk("BUTTON", "Quick sweep", BS_PUSHBUTTON | WS_TABSTOP, 0, D_QUICK);
            mk("BUTTON", "Full sweep", BS_PUSHBUTTON | WS_TABSTOP, 0, D_FULL);
            mk("BUTTON", "Stop", BS_PUSHBUTTON | WS_TABSTOP, 0, D_STOP);
            mk("BUTTON", "Auto-listen: off", BS_PUSHBUTTON | WS_TABSTOP, 0, D_AUTO);
            mk("STATIC", "Press Quick sweep to look for signals around you. It checks the popular services and repeats until you press Stop.", 0, 0, D_STATUS);
            HWND lv = mk(WC_LISTVIEWA, "", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE, D_LIST);
            ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            const char* cols[6] = {"Frequency", "Strength", "Seen", "Width", "Where", "Listen as"};
            for (int c = 0; c < 6; ++c) { LVCOLUMNA col{}; col.mask = LVCF_TEXT | LVCF_WIDTH; col.pszText = (LPSTR)cols[c]; col.cx = 100; SendMessageA(lv, LVM_INSERTCOLUMNA, c, (LPARAM)&col); }
            mk("EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE, D_DETAIL);
            mk("BUTTON", "Listen", BS_PUSHBUTTON | WS_TABSTOP, 0, D_LISTEN);
            mk("BUTTON", "Save as favourite", BS_PUSHBUTTON | WS_TABSTOP, 0, D_FAV);
            mk("BUTTON", "Add what I hear to the journal", BS_PUSHBUTTON | WS_TABSTOP, 0, D_JOURNAL);
            mk("STATIC", "Activity heard (Auto-listen writes here):", 0, 0, D_LLOG);
            mk("EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE, D_LOG);
            layout(); applyButtons(); showDetail();
            return 0;
        }
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = S(760); mm->ptMinTrackSize.y = S(560); return 0; }
        case WM_NOTIFY: {
            auto* nm = (NMHDR*)l;
            if (nm->idFrom == D_LIST) {
                if (nm->code == LVN_ITEMCHANGED) showDetail();
                else if (nm->code == NM_DBLCLK) { const int i = selected(); if (i >= 0 && i < (int)G.rows.size() && G.host.listen) G.host.listen(G.rows[i]); }
            }
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(w);
            const int i = selected();
            if (id == D_QUICK && G.host.start) G.host.start(true);
            else if (id == D_FULL && G.host.start) G.host.start(false);
            else if (id == D_STOP && G.host.stop) G.host.stop();
            else if (id == D_AUTO && G.host.autoListen) G.host.autoListen(!G.autoOn);
            else if (id == D_LISTEN && i >= 0 && i < (int)G.rows.size() && G.host.listen) G.host.listen(G.rows[i]);
            else if (id == D_FAV && i >= 0 && i < (int)G.rows.size() && G.host.favourite) G.host.favourite(G.rows[i]);
            else if (id == D_JOURNAL && G.host.addToJournal) G.host.addToJournal();
            return 0;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: G.self = nullptr; return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

} // namespace

bool registerDiscoverClass(HINSTANCE hi) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = proc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = kClass;
    return RegisterClassA(&wc) != 0;
}

void showDiscover(HWND owner, HINSTANCE hi, HFONT font, DiscoverHost host) {
    G.host = std::move(host);
    if (G.self) { ShowWindow(G.self, SW_RESTORE); SetForegroundWindow(G.self); return; }
    G.owner = owner; G.font = font;
    HDC dc = GetDC(nullptr); G.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    CreateWindowExA(0, kClass, "OpenRadio Lab - Discover: what is on the air around you", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                    S(980), S(720), owner, nullptr, hi, nullptr);
}

bool discoverOpen() { return G.self != nullptr; }

void discoverUpdate(const std::vector<DiscoveryRow>& rows, const std::string& status, bool sweeping, bool autoOn, const std::string& log) {
    if (!G.self) return;
    const bool changed = rows.size() != G.rows.size() || [&] { for (size_t i = 0; i < rows.size(); ++i) if (rows[i].hz != G.rows[i].hz || rows[i].seen != G.rows[i].seen || rows[i].activeNow != G.rows[i].activeNow) return true; return false; }();
    G.rows = rows; G.sweeping = sweeping; G.autoOn = autoOn;
    if (changed) fill();
    SetWindowTextA(H(D_STATUS), status.c_str());
    static std::string lastLog;
    if (log != lastLog) {
        lastLog = log;
        SetWindowTextA(H(D_LOG), log.c_str());
        SendMessageA(H(D_LOG), EM_SETSEL, (WPARAM)log.size(), (LPARAM)log.size());
        SendMessageA(H(D_LOG), EM_SCROLLCARET, 0, 0);
    }
    applyButtons();
}

} // namespace orl
