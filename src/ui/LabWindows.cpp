// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "LabWindows.h"

#include "LearnWindow.h"          // WM_ORL_TRYIT
#include "../dsp/LevelStats.h"
#include "../dsp/SpectrumCompare.h"
#include "../Util.h"

#include <commdlg.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

namespace orl {
namespace {

int dpiNow() { HDC dc = GetDC(nullptr); const int d = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc); return d; }

std::string appDataDir() {
    char p[MAX_PATH] = {0};
    std::string d;
    if (SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, p) == S_OK) d = std::string(p) + "\\OpenRadioLab";
    else d = ".";
    CreateDirectoryA(d.c_str(), nullptr);
    return d;
}

std::string getText(HWND w) {
    const int n = GetWindowTextLengthA(w);
    std::string s((size_t)n + 1, '\0');
    GetWindowTextA(w, &s[0], n + 1);
    s.resize((size_t)n);
    return s;
}
// Multi-line edit controls want CR LF.
std::string toCrlf(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n' && (i == 0 || s[i - 1] != '\r')) o += "\r\n"; else o += s[i];
    }
    return o;
}
std::string fromCrlf(const std::string& s) {
    std::string o;
    for (char c : s) if (c != '\r') o += c;
    return o;
}

const char* kJournalClass = "ORLJournalWindow";
const char* kLabClass = "ORLSignalLabWindow";
const char* kPlotClass = "ORLLabPlot";

// ====================================================================== journal

enum { J_LIST = 1, J_TITLE, J_ANT, J_NOTES, J_INFO, J_NEW, J_TUNE, J_DEL, J_EXPORT, J_HELP, J_L1, J_L2, J_L3 };

struct JState {
    HWND self = nullptr, owner = nullptr;
    HFONT font = nullptr;
    HINSTANCE hi = nullptr;
    int dpi = 96;
    JournalHost host;
    Journal j;
    bool loaded = false;
    std::vector<int64_t> ids;
    int64_t cur = 0;
} J;

int JS(int v) { return MulDiv(v, J.dpi, 96); }
HWND JH(int id) { return GetDlgItem(J.self, id); }
std::string journalPath() { return appDataDir() + "\\journal.txt"; }

void jSave() { std::string e; J.j.save(journalPath(), e); }

std::string describeEntry(const JournalEntry& e) {
    std::string t = "Frequency: " + formatFrequency(e.vfoHz) + " (" + e.mode + "), band centre " + formatFrequency(e.centreHz) +
                    (e.rateHz ? ", " + formatBandwidth(e.rateHz) + " wide view" : "") + "\n";
    if (!e.band.empty()) t += "Band: " + e.band + "\n";
    if (!e.device.empty()) t += "Receiver: " + e.device + (e.simulated ? "  (SIMULATED DATA)" : "") + "\n";
    if (!e.gain.empty()) { char b[64]; std::snprintf(b, sizeof b, "Gain: %s, signal level %.0f dBFS\n", e.gain.c_str(), e.levelDb); t += b; }
    if (!e.measurement.empty()) t += "Measurement: " + e.measurement + "\n";
    if (!e.recording.empty()) t += "Recording: " + e.recording + "\n";
    return toCrlf(t);
}

// Write the edit boxes back into the current entry (and the file) if anything changed.
void jCommit() {
    if (!J.self || J.cur == 0) return;
    const JournalEntry* e = J.j.find(J.cur);
    if (!e) return;
    JournalEntry u = *e;
    u.title = getText(JH(J_TITLE));
    u.antenna = getText(JH(J_ANT));
    u.notes = fromCrlf(getText(JH(J_NOTES)));
    if (u.title != e->title || u.antenna != e->antenna || u.notes != e->notes) { J.j.update(u); jSave(); }
}

void jFillList(int64_t select) {
    HWND l = JH(J_LIST);
    SendMessageA(l, LB_RESETCONTENT, 0, 0);
    J.ids.clear();
    std::vector<const JournalEntry*> v;
    for (const auto& e : J.j.entries()) v.push_back(&e);
    std::sort(v.begin(), v.end(), [](const JournalEntry* a, const JournalEntry* b) { return a->when != b->when ? a->when > b->when : a->id > b->id; });
    int sel = -1;
    for (const JournalEntry* e : v) {
        const std::string label = e->when + "   " + (e->title.empty() ? formatFrequency(e->vfoHz) : e->title);
        SendMessageA(l, LB_ADDSTRING, 0, (LPARAM)label.c_str());
        if (e->id == select) sel = (int)J.ids.size();
        J.ids.push_back(e->id);
    }
    if (sel < 0 && !J.ids.empty()) sel = 0;
    if (sel >= 0) SendMessageA(l, LB_SETCURSEL, sel, 0);
}

void jShow(int64_t id) {
    J.cur = id;
    const JournalEntry* e = id ? J.j.find(id) : nullptr;
    const bool on = e != nullptr;
    for (int c : {J_TITLE, J_ANT, J_NOTES, J_TUNE, J_DEL}) EnableWindow(JH(c), on);
    SetWindowTextA(JH(J_TITLE), on ? e->title.c_str() : "");
    SetWindowTextA(JH(J_ANT), on ? e->antenna.c_str() : "");
    SetWindowTextA(JH(J_NOTES), on ? toCrlf(e->notes).c_str() : "");
    SetWindowTextA(JH(J_INFO), on ? describeEntry(*e).c_str()
                                  : "No entries yet.\r\n\r\nTune to something interesting, then press 'New entry from current radio'. "
                                    "The journal records the frequency, mode, gain, what the app measured and (if you have one) the recording, "
                                    "and you add what you heard and what you learned.");
}

void jSelectionChanged() {
    jCommit();
    const int i = (int)SendMessageA(JH(J_LIST), LB_GETCURSEL, 0, 0);
    jShow(i >= 0 && i < (int)J.ids.size() ? J.ids[i] : 0);
}

void jNew() {
    jCommit();
    if (!J.host.capture) return;
    JournalEntry e = J.host.capture();
    if (e.antenna.empty() && !J.j.entries().empty()) e.antenna = J.j.entries().back().antenna;     // same setup as last time
    const int64_t id = J.j.add(e);
    jSave();
    jFillList(id);
    jShow(id);
    SetFocus(JH(J_NOTES));
}

void jLayout() {
    RECT rc; GetClientRect(J.self, &rc);
    const int m = JS(8), lw = JS(280);
    SetWindowPos(JH(J_LIST), nullptr, m, m, lw, (int)rc.bottom - 2 * m - JS(34), SWP_NOZORDER);
    SetWindowPos(JH(J_NEW), nullptr, m, (int)rc.bottom - m - JS(28), lw, JS(28), SWP_NOZORDER);
    const int x = 2 * m + lw, w = (int)rc.right - x - m;
    int y = m;
    auto place = [&](int id, int h) { SetWindowPos(JH(id), nullptr, x, y, w, h, SWP_NOZORDER); y += h + JS(4); };
    place(J_L1, JS(18)); place(J_TITLE, JS(24));
    place(J_L2, JS(18)); place(J_ANT, JS(24));
    place(J_INFO, JS(120));
    place(J_L3, JS(18));
    const int bottom = (int)rc.bottom - m - JS(28) - JS(6);
    SetWindowPos(JH(J_NOTES), nullptr, x, y, w, std::max(JS(60), bottom - y), SWP_NOZORDER);
    const int by = (int)rc.bottom - m - JS(28);
    SetWindowPos(JH(J_TUNE), nullptr, x, by, JS(130), JS(28), SWP_NOZORDER);
    SetWindowPos(JH(J_DEL), nullptr, x + JS(138), by, JS(90), JS(28), SWP_NOZORDER);
    SetWindowPos(JH(J_EXPORT), nullptr, x + JS(236), by, JS(110), JS(28), SWP_NOZORDER);
    SetWindowPos(JH(J_HELP), nullptr, x + JS(354), by + JS(5), std::max(JS(100), w - JS(354)), JS(20), SWP_NOZORDER);
}

void jExport() {
    jCommit();
    char file[MAX_PATH] = "OpenRadioLab-journal.md";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof ofn; ofn.hwndOwner = J.self;
    ofn.lpstrFilter = "Markdown text (*.md)\0*.md\0All files\0*.*\0";
    ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrDefExt = "md";
    ofn.lpstrTitle = "Export the journal as a readable text file";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameA(&ofn)) return;
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f << J.j.toMarkdown();
    if (!f) MessageBoxA(J.self, "Could not write that file.", "OpenRadio Lab", MB_OK | MB_ICONWARNING);
}

LRESULT CALLBACK journalProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            J.self = h; J.dpi = dpiNow();
            auto mk = [&](const char* cls, const char* text, DWORD style, DWORD ex, int id) {
                HWND c = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, J.hi, nullptr);
                SendMessageA(c, WM_SETFONT, (WPARAM)J.font, TRUE);
                return c;
            };
            mk("LISTBOX", "", WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP, WS_EX_CLIENTEDGE, J_LIST);
            mk("STATIC", "Title", 0, 0, J_L1);
            mk("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, J_TITLE);
            mk("STATIC", "Antenna and setup (for example: telescopic antenna 75 cm, by the window)", 0, 0, J_L2);
            mk("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, J_ANT);
            mk("EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE, J_INFO);
            mk("STATIC", "Notes: what you heard, what you tried, what surprised you", 0, 0, J_L3);
            mk("EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_TABSTOP, WS_EX_CLIENTEDGE, J_NOTES);
            mk("BUTTON", "New entry from current radio", BS_PUSHBUTTON | WS_TABSTOP, 0, J_NEW);
            mk("BUTTON", "Tune to this", BS_PUSHBUTTON | WS_TABSTOP, 0, J_TUNE);
            mk("BUTTON", "Delete", BS_PUSHBUTTON | WS_TABSTOP, 0, J_DEL);
            mk("BUTTON", "Export...", BS_PUSHBUTTON | WS_TABSTOP, 0, J_EXPORT);
            mk("STATIC", "Saved automatically.", 0, 0, J_HELP);
            SendMessageA(JH(J_NOTES), EM_SETLIMITTEXT, 100000, 0);
            if (!J.loaded) { std::string e; J.j.load(journalPath(), e); J.loaded = true; }
            jLayout();
            jFillList(0);
            jSelectionChanged();
            return 0;
        }
        case WM_SIZE: jLayout(); return 0;
        case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = JS(760); mm->ptMinTrackSize.y = JS(520); return 0; }
        case WM_COMMAND:
            switch (LOWORD(w)) {
                case J_LIST: if (HIWORD(w) == LBN_SELCHANGE) jSelectionChanged(); break;
                case J_NEW: jNew(); break;
                case J_EXPORT: jExport(); break;
                case J_TUNE: {
                    const JournalEntry* e = J.j.find(J.cur);
                    if (e && e->vfoHz > 0) {
                        const int mode = e->mode == "NFM" ? 1 : e->mode == "AM" ? 2 : 0;
                        PostMessageA(J.owner, WM_ORL_TRYIT, (WPARAM)mode, (LPARAM)(unsigned long)e->vfoHz);
                    }
                    break;
                }
                case J_DEL:
                    if (J.cur && MessageBoxA(h, "Delete this journal entry? This cannot be undone.", "OpenRadio Lab", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        J.j.remove(J.cur); jSave(); J.cur = 0;
                        jFillList(0); jSelectionChanged();
                    }
                    break;
            }
            return 0;
        case WM_CLOSE: jCommit(); DestroyWindow(h); return 0;
        case WM_DESTROY: jCommit(); J.self = nullptr; return 0;      // also runs when the main window closes first
    }
    return DefWindowProcA(h, m, w, l);
}

// ====================================================================== signal lab

enum { L_PAUSE = 1, L_CLEAR, L_CSV, L_JOURNAL, L_PLOT, L_TEXT, L_TITLE, L_SNAP, L_SNAPCLR };

struct LState {
    HWND self = nullptr, owner = nullptr, plot = nullptr;
    HFONT font = nullptr, big = nullptr;
    HINSTANCE hi = nullptr;
    int dpi = 96;
    std::function<void()> addToJournal;
    std::vector<float> db;
    double hz = 0, noiseDb = 1e9;
    std::string label;
    bool paused = false;
    ULONGLONG lastStats = 0;
    LevelStats stats;
    SpectrumGrab grab;
    struct Snap { std::vector<float> db; double centre = 0, rate = 0; std::string when; };
    std::vector<Snap> snaps;               // at most two: "before" and "after"
    std::string text, status;              // status: why nothing is being recorded right now (empty = recording)
    unsigned long pushes = 0;
} L;

const double kDt = 0.1;                    // seconds between readings
const size_t kMaxSamples = 6000;           // ten minutes
int LS(int v) { return MulDiv(v, L.dpi, 96); }
HWND LH(int id) { return GetDlgItem(L.self, id); }

std::string mmss(double s) { const int t = (int)std::lround(s); char b[24]; std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60); return b; }

void lUpdateText() {
    L.stats = analyseLevels(L.db, kDt, 6.0, L.noiseDb);
    char b[600];
    std::string t = "Watching " + formatFrequency(L.hz) + " (" + L.label + ")\r\n\r\n";
    if (!L.status.empty()) t += "Not recording right now: " + L.status + "\r\n\r\n";
    if (L.db.empty()) t += "Waiting for data...";
    else {
        const LevelStats& s = L.stats;
        std::snprintf(b, sizeof b, "Time watched: %s     Readings: %zu\r\nStrongest: %.0f dBFS     Average: %.0f dBFS     Weakest: %.0f dBFS\r\n"
                      "Background: %.0f dBFS%s     Counts as 'on' above: %.0f dBFS\r\nOn for %.0f%% of the time, %d burst(s)",
                      mmss(s.seconds).c_str(), s.n, s.maxDb, s.meanDb, s.minDb, s.floorDb, s.noiseKnown ? " (from the spectrum)" : " (estimated from this data)",
                      s.thresholdDb, s.activeFraction * 100.0, s.bursts);
        t += b;
        t += "\r\n\r\n" + describeActivity(s);
    }
    t += "\r\n\r\nThis describes the signal level only. It is an observation, not an identification.";
    if (L.snaps.size() == 1) t += "\r\n\r\n--- Spectrum snapshots ---\r\nSnapshot 1 taken at " + L.snaps[0].when + ". Take another later (or after moving the antenna) and the two are compared here.";
    if (L.snaps.size() == 2) {
        t += "\r\n\r\n--- Spectrum snapshots: " + L.snaps[0].when + " compared with " + L.snaps[1].when + " ---\r\n";
        t += describeDiff(compareSpectra(L.snaps[0].db, L.snaps[1].db, L.snaps[0].centre, L.snaps[0].rate, L.snaps[1].centre, L.snaps[1].rate));
    }
    L.text = t;
    SetWindowTextA(LH(L_TEXT), t.c_str());
    if (L.plot) InvalidateRect(L.plot, nullptr, FALSE);
}

void lLayout() {
    RECT rc; GetClientRect(L.self, &rc);
    const int m = LS(8), bh = LS(28);
    int x = m;
    auto btn = [&](int id, int w) { SetWindowPos(LH(id), nullptr, x, m, w, bh, SWP_NOZORDER); x += w + LS(6); };
    btn(L_PAUSE, LS(90)); btn(L_CLEAR, LS(80)); btn(L_CSV, LS(120)); btn(L_JOURNAL, LS(190)); btn(L_SNAP, LS(120)); btn(L_SNAPCLR, LS(110));
    SetWindowPos(LH(L_TITLE), nullptr, x + LS(10), m + LS(5), std::max(LS(60), (int)rc.right - x - LS(10) - m), LS(20), SWP_NOZORDER);
    const int top = 2 * m + bh, textH = LS(230);
    SetWindowPos(L.plot, nullptr, m, top, (int)rc.right - 2 * m, std::max(LS(100), (int)rc.bottom - top - textH - 2 * m), SWP_NOZORDER);
    SetWindowPos(LH(L_TEXT), nullptr, m, (int)rc.bottom - textH - m, (int)rc.right - 2 * m, textH, SWP_NOZORDER);
}

void lPaintPlot(HWND hw) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(hw, &ps);
    RECT rc; GetClientRect(hw, &rc);
    const int W = (int)rc.right, H = (int)rc.bottom;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
    HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(16, 20, 28)); FillRect(mem, &rc, bg); DeleteObject(bg);
    SelectObject(mem, L.font); SetBkMode(mem, TRANSPARENT);

    const int left = LS(52), right = W - LS(10), top = LS(10), bot = H - LS(24);
    // vertical range: the data, the noise floor, and some headroom
    double lo = -90, hi = -10;
    if (!L.db.empty()) {
        lo = std::min<double>(L.stats.minDb, L.stats.floorDb) - 6; hi = L.stats.maxDb + 6;
        if (hi - lo < 30) { const double c = (hi + lo) / 2; lo = c - 15; hi = c + 15; }
        lo = std::max(lo, -120.0); hi = std::min(hi, 0.0);
        if (hi - lo < 10) lo = hi - 10;
    }
    auto Y = [&](double v) { return bot - (int)((v - lo) / (hi - lo) * (bot - top)); };
    // grid
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(48, 56, 70)); HGDIOBJ op = SelectObject(mem, grid);
    SetTextColor(mem, RGB(150, 160, 175));
    const double step = (hi - lo) > 60 ? 20 : (hi - lo) > 30 ? 10 : 5;
    for (double v = std::ceil(lo / step) * step; v <= hi; v += step) {
        MoveToEx(mem, left, Y(v), nullptr); LineTo(mem, right, Y(v));
        char b[16]; std::snprintf(b, sizeof b, "%.0f", v);
        RECT tr{0, Y(v) - LS(8), left - LS(6), Y(v) + LS(8)}; DrawTextA(mem, b, -1, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    RECT yl{0, top - LS(2), left - LS(6), top + LS(14)};
    SetTextColor(mem, RGB(110, 120, 135));
    DrawTextA(mem, "dBFS", -1, &yl, DT_RIGHT | DT_SINGLELINE);
    SelectObject(mem, op); DeleteObject(grid);

    if (L.db.empty()) {
        SetTextColor(mem, RGB(170, 180, 195));
        RECT tr = rc; DrawTextA(mem, "Waiting for the first readings...", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        // reference lines: background (blue) and the 'on' threshold (amber), dashed
        auto hline = [&](double v, COLORREF c, const char* name) {
            if (v < lo || v > hi) return;
            HPEN p = CreatePen(PS_DASH, 1, c); HGDIOBJ o = SelectObject(mem, p);
            MoveToEx(mem, left, Y(v), nullptr); LineTo(mem, right, Y(v));
            SelectObject(mem, o); DeleteObject(p);
            SetTextColor(mem, c); RECT tr{left + LS(4), Y(v) - LS(15), right, Y(v)}; DrawTextA(mem, name, -1, &tr, DT_LEFT | DT_SINGLELINE);
        };
        hline(L.stats.floorDb, RGB(90, 150, 230), "background");
        hline(L.stats.thresholdDb, RGB(230, 170, 60), "counts as 'on' above this line");
        // the trace: newest on the right; the span is always the whole window up to ten minutes
        const double span = std::max<double>(30.0, L.db.size() * kDt);
        HPEN tp = CreatePen(PS_SOLID, std::max(1, LS(2) / 1), RGB(255, 120, 110)); HGDIOBJ o = SelectObject(mem, tp);
        bool first = true;
        const size_t n = L.db.size();
        const int pw = right - left;
        for (size_t i = 0; i < n; ++i) {
            const int x = left + (int)((double)i * kDt / span * pw);
            const int y = std::max(top, std::min(bot, Y(L.db[i])));
            if (first) { MoveToEx(mem, x, y, nullptr); first = false; } else LineTo(mem, x, y);
        }
        SelectObject(mem, o); DeleteObject(tp);
        SetTextColor(mem, RGB(150, 160, 175));
        for (double t = 0; t <= span; t += (span > 240 ? 60 : span > 90 ? 30 : 10)) {
            const int x = left + (int)(t / span * pw);
            RECT tr{x - LS(24), bot + LS(3), x + LS(24), H}; DrawTextA(mem, mmss(t).c_str(), -1, &tr, DT_CENTER | DT_SINGLELINE);
        }
    }
    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
    EndPaint(hw, &ps);
}

LRESULT CALLBACK plotProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) { lPaintPlot(h); return 0; }
    if (m == WM_ERASEBKGND) return 1;
    return DefWindowProcA(h, m, w, l);
}

void lExportCsv() {
    char file[MAX_PATH] = "signal-levels.csv";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof ofn; ofn.hwndOwner = L.self;
    ofn.lpstrFilter = "CSV spreadsheet file (*.csv)\0*.csv\0"; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrDefExt = "csv";
    ofn.lpstrTitle = "Save the readings (opens in Excel or any spreadsheet)";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameA(&ofn)) return;
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f << "# OpenRadio Lab signal levels at " << formatFrequency(L.hz) << " (" << L.label << ")\r\ntime_s,level_dbfs\r\n";
    char b[64];
    for (size_t i = 0; i < L.db.size(); ++i) { std::snprintf(b, sizeof b, "%.1f,%.2f\r\n", i * kDt, L.db[i]); f << b; }
}

LRESULT CALLBACK labProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            L.self = h; L.dpi = dpiNow();
            auto mk = [&](const char* cls, const char* text, DWORD style, DWORD ex, int id) {
                HWND c = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, L.hi, nullptr);
                SendMessageA(c, WM_SETFONT, (WPARAM)L.font, TRUE);
                return c;
            };
            mk("BUTTON", "Pause", BS_PUSHBUTTON | WS_TABSTOP, 0, L_PAUSE);
            mk("BUTTON", "Clear", BS_PUSHBUTTON | WS_TABSTOP, 0, L_CLEAR);
            mk("BUTTON", "Save as CSV...", BS_PUSHBUTTON | WS_TABSTOP, 0, L_CSV);
            mk("BUTTON", "Add this to the journal", BS_PUSHBUTTON | WS_TABSTOP, 0, L_JOURNAL);
            mk("BUTTON", "Take snapshot", BS_PUSHBUTTON | WS_TABSTOP, 0, L_SNAP);
            mk("BUTTON", "Clear snapshots", BS_PUSHBUTTON | WS_TABSTOP, 0, L_SNAPCLR);
            mk("STATIC", "Level of the signal you are listening to, over time", 0, 0, L_TITLE);
            mk("EDIT", "", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE, L_TEXT);
            L.plot = CreateWindowExA(WS_EX_CLIENTEDGE, kPlotClass, "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, (HMENU)(INT_PTR)L_PLOT, L.hi, nullptr);
            lLayout();
            lUpdateText();
            SetTimer(h, 1, 250, nullptr);          // redraw on our own clock, whatever the main window is doing
            return 0;
        }
        case WM_TIMER: if (w == 1) lUpdateText(); return 0;
        case WM_SIZE: lLayout(); return 0;
        case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = LS(700); mm->ptMinTrackSize.y = LS(480); return 0; }
        case WM_COMMAND:
            switch (LOWORD(w)) {
                case L_PAUSE: L.paused = !L.paused; SetWindowTextA(LH(L_PAUSE), L.paused ? "Resume" : "Pause"); break;
                case L_CLEAR: L.db.clear(); lUpdateText(); break;
                case L_CSV: lExportCsv(); break;
                case L_JOURNAL: if (L.addToJournal) L.addToJournal(); break;
                case L_SNAP: {
                    LState::Snap sn;
                    if (!L.grab || !L.grab(sn.db, sn.centre, sn.rate)) { L.status = "no spectrum is available yet for a snapshot"; lUpdateText(); break; }
                    SYSTEMTIME st; GetLocalTime(&st);
                    char b[16]; std::snprintf(b, sizeof b, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
                    sn.when = b;
                    if (L.snaps.size() == 2) L.snaps.erase(L.snaps.begin());
                    L.snaps.push_back(std::move(sn));
                    lUpdateText();
                    break;
                }
                case L_SNAPCLR: L.snaps.clear(); lUpdateText(); break;
            }
            return 0;
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: KillTimer(h, 1); L.self = nullptr; L.plot = nullptr; if (L.big) { DeleteObject(L.big); L.big = nullptr; } return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

} // namespace

bool registerLabClasses(HINSTANCE hi) {
    WNDCLASSA wc{};
    wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpfnWndProc = journalProc; wc.lpszClassName = kJournalClass;
    bool ok = RegisterClassA(&wc) != 0;
    wc.lpfnWndProc = labProc; wc.lpszClassName = kLabClass;
    ok &= RegisterClassA(&wc) != 0;
    wc.lpfnWndProc = plotProc; wc.lpszClassName = kPlotClass; wc.hbrBackground = nullptr;
    ok &= RegisterClassA(&wc) != 0;
    return ok;
}

void showJournal(HWND owner, HINSTANCE hi, HFONT font, const JournalHost& host, bool newEntry) {
    J.owner = owner; J.hi = hi; J.font = font; J.host = host;
    if (!J.self) {
        J.dpi = dpiNow();
        CreateWindowExA(0, kJournalClass, "OpenRadio Lab - Experiment Journal", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                        JS(900), JS(640), owner, nullptr, hi, nullptr);
    } else { ShowWindow(J.self, SW_RESTORE); SetForegroundWindow(J.self); }
    if (newEntry && J.self) jNew();
}

void showSignalLab(HWND owner, HINSTANCE hi, HFONT font, std::function<void()> addToJournal, SpectrumGrab grab) {
    L.owner = owner; L.hi = hi; L.font = font; L.addToJournal = std::move(addToJournal); L.grab = std::move(grab);
    if (L.self) { ShowWindow(L.self, SW_RESTORE); SetForegroundWindow(L.self); return; }
    L.dpi = dpiNow();
    CreateWindowExA(0, kLabClass, "OpenRadio Lab - Signal Lab", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                    LS(900), LS(640), owner, nullptr, hi, nullptr);
}

bool signalLabOpen() { return L.self != nullptr; }

void signalLabSetStatus(const std::string& why) { if (L.self) L.status = why; }

void signalLabPush(double levelDb, double hz, const std::string& label, double noiseDb) {
    if (!L.self) return;
    if (std::fabs(hz - L.hz) > 500.0 || label != L.label) { L.db.clear(); L.hz = hz; L.label = label; }   // new frequency: start fresh
    L.noiseDb = noiseDb;
    L.status = L.paused ? "paused with the Pause button" : "";
    if (L.paused) return;
    if (!std::isfinite(levelDb)) { L.status = "the receiver reported no level"; return; }
    ++L.pushes;
    L.db.push_back((float)levelDb);
    if (L.db.size() > kMaxSamples) L.db.erase(L.db.begin(), L.db.begin() + (L.db.size() - kMaxSamples));
    const ULONGLONG now = GetTickCount64();
    if (now - L.lastStats > 400) { L.lastStats = now; lUpdateText(); }
}

} // namespace orl
