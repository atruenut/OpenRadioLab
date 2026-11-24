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
#include <ctime>

namespace orl {
namespace {

const char* kClass = "ORLDiscoverWindow";
enum { D_QUICK = 1, D_FULL, D_STOP, D_AUTO, D_STATUS, D_LIST, D_DETAIL, D_LISTEN, D_FAV, D_LOG, D_LLOG, D_JOURNAL, D_SAVE, D_OPEN, D_CSV, D_CLEAR, D_ONLYNEW, D_NOWIDE, D_NOTEL, D_NOTE, D_NOTESAVE, D_CLIPS, D_BEEP, D_WATCH, D_COMPARE, D_WATCHL };
const int kCols = 9;

struct State {
    HWND self = nullptr, owner = nullptr; HFONT font = nullptr; int dpi = 96;
    DiscoverHost host;
    std::vector<DiscoveryRow> rows;      // what the list shows (filtered and sorted)
    std::vector<DiscoveryRow> all;       // what the program found, best first
    int sortCol = -1; bool sortAsc = true;
    bool sweeping = false, autoOn = false;
} G;

int S(int v) { return MulDiv(v, G.dpi, 96); }
HWND H(int id) { return GetDlgItem(G.self, id); }

int selected() { return (int)SendMessageA(H(D_LIST), LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED); }

std::string whenText(long long t) {
    if (t <= 0) return "";
    std::time_t tt = (std::time_t)t; char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", std::localtime(&tt));
    return b;
}

bool checked(int id) { return SendMessageA(H(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }

// Applies the filters and the chosen sort order to everything found.
void rebuildRows() {
    G.rows.clear();
    const bool onlyNew = checked(D_ONLYNEW), noWide = checked(D_NOWIDE);
    for (const auto& r : G.all) {
        if (onlyNew && !r.isNew) continue;
        if (noWide && r.widthHz >= 400e3) continue;
        G.rows.push_back(r);
    }
    if (G.sortCol < 0) return;
    const int c = G.sortCol; const bool asc = G.sortAsc;
    std::stable_sort(G.rows.begin(), G.rows.end(), [&](const DiscoveryRow& a, const DiscoveryRow& b) {
        double x = 0, y = 0; int t = 0;
        switch (c) {
            case 0: x = a.hz; y = b.hz; break;
            case 1: x = a.snrDb; y = b.snrDb; break;
            case 2: x = (double)a.seen / std::max(1, a.passes) + (a.activeNow ? 0.001 : 0); y = (double)b.seen / std::max(1, b.passes) + (b.activeNow ? 0.001 : 0); break;
            case 3: x = a.widthHz; y = b.widthHz; break;
            case 4: t = a.where.compare(b.where); break;
            case 5: x = a.mode; y = b.mode; break;
            case 6: x = (double)a.firstT; y = (double)b.firstT; break;
            case 7: x = (double)a.lastT; y = (double)b.lastT; break;
            default: t = a.note.compare(b.note); break;
        }
        if (c == 4 || c >= 8) return asc ? t < 0 : t > 0;
        return asc ? x < y : x > y;
    });
}

void markSortColumn() {
    static const char* names[kCols] = {"Frequency", "Strength", "Seen", "Width", "Where", "Listen as", "First seen", "Last seen", "Your note"};
    for (int c = 0; c < kCols; ++c) {
        std::string t = names[c];
        if (c == G.sortCol) t += G.sortAsc ? "  ^" : "  v";
        LVCOLUMNA col{}; col.mask = LVCF_TEXT; col.pszText = (LPSTR)t.c_str();
        SendMessageA(H(D_LIST), LVM_SETCOLUMNA, c, (LPARAM)&col);
    }
}

void showDetail() {
    const int i = selected();
    if (i < 0 || i >= (int)G.rows.size()) { SetWindowTextA(H(D_DETAIL), "Select a signal to read about it. Double-click to listen. Click a column heading to sort."); return; }
    const DiscoveryRow& r = G.rows[i];
    char b[200];
    std::snprintf(b, sizeof b, "%s   (%.0f dB above the noise, about %.0f kHz wide, seen in %d of %d sweeps)\r\n", formatFrequency(r.hz).c_str(), r.snrDb, r.widthHz / 1e3, r.seen, r.passes);
    std::string t = b + r.what + ".\r\n";
    if (r.firstT > 0) t += "First seen " + whenText(r.firstT) + ", last seen " + whenText(r.lastT) + (r.isNew ? ". New since the scan you loaded.\r\n" : ".\r\n");
    t += "When: " + activityPattern(r) + "\r\n";
    t += "Double-click or press Listen to tune there. If it sounds wrong, try another mode in the main window.";
    SetWindowTextA(H(D_DETAIL), t.c_str());
    static double shownHz = -1;                         // load the note box only when a different signal is selected
    if (shownHz != r.hz) { shownHz = r.hz; SetWindowTextA(H(D_NOTE), r.note.c_str()); }
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
        std::snprintf(b, sizeof b, "%d/%d%s%s", r.seen, r.passes, r.activeNow ? "  now" : "", r.isNew ? "  NEW" : ""); ListView_SetItemText(lv, (int)k, 2, b);
        std::snprintf(b, sizeof b, r.widthHz >= 1e6 ? "%.1f MHz" : "%.0f kHz", r.widthHz >= 1e6 ? r.widthHz / 1e6 : r.widthHz / 1e3); ListView_SetItemText(lv, (int)k, 3, b);
        ListView_SetItemText(lv, (int)k, 4, (LPSTR)r.where.c_str());
        ListView_SetItemText(lv, (int)k, 5, (LPSTR)modes[std::max(0, std::min(5, r.mode))]);
        { const std::string a = whenText(r.firstT), z = whenText(r.lastT);
          ListView_SetItemText(lv, (int)k, 6, (LPSTR)a.c_str()); ListView_SetItemText(lv, (int)k, 7, (LPSTR)z.c_str());
          ListView_SetItemText(lv, (int)k, 8, (LPSTR)r.note.c_str()); }
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
    int y2 = m + bh + S(6); x = m;
    auto b2 = [&](int id, int w) { SetWindowPos(H(id), nullptr, x, y2, w, S(24), SWP_NOZORDER); x += w + S(6); };
    b2(D_SAVE, S(100)); b2(D_OPEN, S(100)); b2(D_CSV, S(110)); b2(D_CLEAR, S(90)); x += S(10); b2(D_ONLYNEW, S(190)); b2(D_NOWIDE, S(200));
    SetWindowPos(H(D_STATUS), nullptr, m, y2 + S(30), W - 2 * m, S(20), SWP_NOZORDER);
    const int top = y2 + S(54), detH = S(104), logH = S(70), btnH = S(28);
    const int listH = Hh - top - detH - logH - btnH * 3 - S(30) - 5 * m;
    SetWindowPos(H(D_LIST), nullptr, m, top, W - 2 * m, std::max(S(80), listH), SWP_NOZORDER);
    int y = top + std::max(S(80), listH) + m;
    SetWindowPos(H(D_DETAIL), nullptr, m, y, W - 2 * m, detH, SWP_NOZORDER); y += detH + m;
    x = m;
    SetWindowPos(H(D_LISTEN), nullptr, x, y, S(100), btnH, SWP_NOZORDER); x += S(106);
    SetWindowPos(H(D_FAV), nullptr, x, y, S(150), btnH, SWP_NOZORDER); x += S(156);
    SetWindowPos(H(D_JOURNAL), nullptr, x, y, S(190), btnH, SWP_NOZORDER);
    y += btnH + S(4);
    SetWindowPos(H(D_NOTEL), nullptr, m, y + S(4), S(80), S(20), SWP_NOZORDER);
    SetWindowPos(H(D_NOTE), nullptr, m + S(84), y, W - 2 * m - S(84) - S(110), S(26), SWP_NOZORDER);
    SetWindowPos(H(D_NOTESAVE), nullptr, W - m - S(104), y, S(104), S(26), SWP_NOZORDER);
    y += S(32);
    SetWindowPos(H(D_WATCH), nullptr, m, y, S(250), S(200), SWP_NOZORDER);
    SetWindowPos(H(D_COMPARE), nullptr, m + S(260), y, S(230), S(26), SWP_NOZORDER);
    SetWindowPos(H(D_WATCHL), nullptr, m + S(500), y + S(4), W - 2 * m - S(500), S(20), SWP_NOZORDER);
    y += S(32);
    SetWindowPos(H(D_LLOG), nullptr, m, y, S(290), S(18), SWP_NOZORDER);
    SetWindowPos(H(D_CLIPS), nullptr, m + S(300), y - S(3), S(280), S(22), SWP_NOZORDER);
    SetWindowPos(H(D_BEEP), nullptr, m + S(590), y - S(3), S(200), S(22), SWP_NOZORDER);
    y += S(22);
    SetWindowPos(H(D_LOG), nullptr, m, y, W - 2 * m, std::max(S(40), Hh - y - m), SWP_NOZORDER);
    // the list's columns share the width
    const int lw = W - 2 * m - S(24);
    const int cw[kCols] = {S(105), S(80), S(110), S(75), 0, S(70), S(115), S(115), S(160)};
    int used = 0; for (int c : cw) used += c;
    for (int c = 0; c < kCols; ++c) SendMessageA(H(D_LIST), LVM_SETCOLUMNWIDTH, c, c == 4 ? std::max(S(150), lw - used) : cw[c]);
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
            mk("BUTTON", "Save scan...", BS_PUSHBUTTON | WS_TABSTOP, 0, D_SAVE);
            mk("BUTTON", "Open scan...", BS_PUSHBUTTON | WS_TABSTOP, 0, D_OPEN);
            mk("BUTTON", "Export to CSV...", BS_PUSHBUTTON | WS_TABSTOP, 0, D_CSV);
            mk("BUTTON", "Clear list", BS_PUSHBUTTON | WS_TABSTOP, 0, D_CLEAR);
            mk("BUTTON", "Only new since last scan", BS_AUTOCHECKBOX | WS_TABSTOP, 0, D_ONLYNEW);
            mk("BUTTON", "Hide wide blocks (TV, cellular)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, D_NOWIDE);
            mk("STATIC", "Your note:", 0, 0, D_NOTEL);
            mk("EDIT", "", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, D_NOTE);
            mk("BUTTON", "Save note", BS_PUSHBUTTON | WS_TABSTOP, 0, D_NOTESAVE);
            mk("STATIC", "Press Quick sweep to look for signals around you. It checks the popular services and repeats until you press Stop.", 0, 0, D_STATUS);
            HWND lv = mk(WC_LISTVIEWA, "", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE, D_LIST);
            ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            const char* cols[kCols] = {"Frequency", "Strength", "Seen", "Width", "Where", "Listen as", "First seen", "Last seen", "Your note"};
            for (int c = 0; c < kCols; ++c) { LVCOLUMNA col{}; col.mask = LVCF_TEXT | LVCF_WIDTH; col.pszText = (LPSTR)cols[c]; col.cx = 100; SendMessageA(lv, LVM_INSERTCOLUMNA, c, (LPARAM)&col); }
            mk("EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE, D_DETAIL);
            mk("BUTTON", "Listen", BS_PUSHBUTTON | WS_TABSTOP, 0, D_LISTEN);
            mk("BUTTON", "Save as favourite", BS_PUSHBUTTON | WS_TABSTOP, 0, D_FAV);
            mk("BUTTON", "Add what I hear to the journal", BS_PUSHBUTTON | WS_TABSTOP, 0, D_JOURNAL);
            mk("STATIC", "Activity and new signals (log):", 0, 0, D_LLOG);
            { HWND cb = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, D_WATCH);
              const char* it[] = {"Watch: off", "Watch: sweep every 10 minutes", "Watch: sweep every 30 minutes", "Watch: sweep every hour"};
              for (const char* t : it) SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)t);
              SendMessageA(cb, CB_SETCURSEL, 0, 0); }
            mk("BUTTON", "Compare with an older scan...", BS_PUSHBUTTON | WS_TABSTOP, 0, D_COMPARE);
            mk("STATIC", "Watch retunes the radio for a minute each time, so it suits leaving the program running.", 0, 0, D_WATCHL);
            mk("BUTTON", "Auto-listen saves a sound clip", BS_AUTOCHECKBOX | WS_TABSTOP, 0, D_CLIPS);
            mk("BUTTON", "Beep on a new signal", BS_AUTOCHECKBOX | WS_TABSTOP, 0, D_BEEP);
            mk("EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE, D_LOG);
            layout(); markSortColumn(); applyButtons(); showDetail();
            return 0;
        }
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = S(820); mm->ptMinTrackSize.y = S(640); return 0; }
        case WM_NOTIFY: {
            auto* nm = (NMHDR*)l;
            if (nm->idFrom == D_LIST) {
                if (nm->code == LVN_ITEMCHANGED) showDetail();
                else if (nm->code == LVN_COLUMNCLICK) {
                    const int c = ((NMLISTVIEW*)l)->iSubItem;
                    if (c == G.sortCol) G.sortAsc = !G.sortAsc;
                    else { G.sortCol = c; G.sortAsc = (c == 0 || c == 4 || c == 5 || c == 8); }   // numbers that mean 'more' start with the biggest
                    markSortColumn(); rebuildRows(); fill();
                }
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
            else if (id == D_SAVE && G.host.saveScan) G.host.saveScan();
            else if (id == D_OPEN && G.host.loadScan) G.host.loadScan();
            else if (id == D_CSV && G.host.exportCsv) G.host.exportCsv();
            else if (id == D_CLEAR && G.host.clearScan) {
                if (MessageBoxA(h, "Clear the list of signals? Scans you saved to a file with Save scan are not touched.", "Discover", MB_YESNO | MB_ICONQUESTION) == IDYES) G.host.clearScan();
            }
            else if (id == D_COMPARE && G.host.compareScan) G.host.compareScan();
            else if (id == D_WATCH && HIWORD(w) == CBN_SELCHANGE && G.host.setWatch) {
                static const int mins[] = {0, 10, 30, 60};
                const int k = (int)SendMessageA(H(D_WATCH), CB_GETCURSEL, 0, 0);
                G.host.setWatch(k >= 0 && k < 4 ? mins[k] : 0);
            }
            else if (id == D_CLIPS && G.host.recordClips) G.host.recordClips(checked(D_CLIPS));
            else if (id == D_BEEP && G.host.beepOnNew) G.host.beepOnNew(checked(D_BEEP));
            else if (id == D_ONLYNEW || id == D_NOWIDE) { rebuildRows(); fill(); }
            else if (id == D_NOTESAVE && i >= 0 && i < (int)G.rows.size() && G.host.setNote) {
                char b[200] = {0}; GetWindowTextA(H(D_NOTE), b, sizeof b);
                G.host.setNote(G.rows[i].hz, b);
            }
            return 0;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: G.self = nullptr; return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

const char* kTextClass = "ORLTextReportWindow";
LRESULT CALLBACK textProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_SIZE: SetWindowPos(GetDlgItem(h, 1), nullptr, 8, 8, LOWORD(l) - 16, HIWORD(l) - 16, SWP_NOZORDER); return 0;
        case WM_CLOSE: DestroyWindow(h); return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

} // namespace

void showTextReport(HWND owner, HINSTANCE hi, HFONT font, const std::string& title, const std::string& text) {
    static bool reg = false;
    if (!reg) { WNDCLASSA wc{}; wc.lpfnWndProc = textProc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = kTextClass; RegisterClassA(&wc); reg = true; }
    HWND w = CreateWindowExA(0, kTextClass, title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 760, 560, owner, nullptr, hi, nullptr);
    std::string crlf; for (char c : text) { if (c == '\n') crlf += "\r\n"; else crlf += c; }
    HWND e = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", crlf.c_str(), WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 8, 8, 700, 500, w, (HMENU)(INT_PTR)1, nullptr, nullptr);
    SendMessageA(e, WM_SETFONT, (WPARAM)font, TRUE);
    RECT rc; GetClientRect(w, &rc); SetWindowPos(e, nullptr, 8, 8, rc.right - 16, rc.bottom - 16, SWP_NOZORDER);
}

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
    const std::vector<DiscoveryRow> before = G.rows;
    G.all = rows; G.sweeping = sweeping; G.autoOn = autoOn;
    rebuildRows();
    const bool changed = before.size() != G.rows.size() || [&] { for (size_t i = 0; i < before.size(); ++i) if (before[i].hz != G.rows[i].hz || before[i].seen != G.rows[i].seen || before[i].activeNow != G.rows[i].activeNow || before[i].note != G.rows[i].note || before[i].isNew != G.rows[i].isNew) return true; return false; }();
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
