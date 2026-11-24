// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "AdsbWindow.h"

#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace orl {

double adsbDistanceKm(double la1, double lo1, double la2, double lo2) {
    const double R = 6371.0, d2r = 3.14159265358979323846 / 180.0;
    const double dla = (la2 - la1) * d2r, dlo = (lo2 - lo1) * d2r;
    const double a = std::sin(dla / 2) * std::sin(dla / 2) + std::cos(la1 * d2r) * std::cos(la2 * d2r) * std::sin(dlo / 2) * std::sin(dlo / 2);
    return 2 * R * std::asin(std::min(1.0, std::sqrt(a)));
}

namespace {

const char* kClass = "ORLAdsbWindow";
enum { A_STATUS = 1, A_START, A_LIST, A_PLOT, A_LATL, A_LAT, A_LONL, A_LON, A_SETHOME, A_HELP };

struct State {
    HWND self = nullptr, owner = nullptr; HFONT font = nullptr; int dpi = 96;
    AdsbHost host;
    std::vector<Aircraft> ac;
    bool haveHome = false; double hLat = 0, hLon = 0;
} W;

int S(int v) { return MulDiv(v, W.dpi, 96); }
HWND H(int id) { return GetDlgItem(W.self, id); }

std::string readEdit(int id) { char b[64] = {0}; GetWindowTextA(H(id), b, sizeof b); return b; }

void layout() {
    RECT rc; GetClientRect(W.self, &rc);
    const int m = S(10), Wd = (int)rc.right, Ht = (int)rc.bottom;
    const int helpH = S(64), topH = S(30);
    const int plotW = std::min(S(330), Wd / 2);
    SetWindowPos(H(A_START), nullptr, m, m, S(150), S(26), SWP_NOZORDER);
    SetWindowPos(H(A_STATUS), nullptr, m + S(160), m + S(4), Wd - 2 * m - S(160), S(20), SWP_NOZORDER);
    const int y2 = m + topH;
    SetWindowPos(H(A_LATL), nullptr, m, y2 + S(4), S(110), S(20), SWP_NOZORDER);
    SetWindowPos(H(A_LAT), nullptr, m + S(112), y2, S(90), S(24), SWP_NOZORDER);
    SetWindowPos(H(A_LONL), nullptr, m + S(212), y2 + S(4), S(70), S(20), SWP_NOZORDER);
    SetWindowPos(H(A_LON), nullptr, m + S(284), y2, S(90), S(24), SWP_NOZORDER);
    SetWindowPos(H(A_SETHOME), nullptr, m + S(384), y2, S(110), S(24), SWP_NOZORDER);
    const int y3 = y2 + S(32), bodyH = Ht - y3 - helpH - 2 * m;
    SetWindowPos(H(A_LIST), nullptr, m, y3, Wd - 3 * m - plotW, bodyH, SWP_NOZORDER);
    SetWindowPos(H(A_PLOT), nullptr, Wd - m - plotW, y3, plotW, bodyH, SWP_NOZORDER);
    SetWindowPos(H(A_HELP), nullptr, m, Ht - helpH - m, Wd - 2 * m, helpH, SWP_NOZORDER);
}

std::string fmtAge(double s) { char b[24]; if (s < 90) std::snprintf(b, sizeof b, "%.0f s ago", s); else std::snprintf(b, sizeof b, "%.0f min ago", s / 60); return b; }

void fillList(double now) {
    HWND lv = H(A_LIST);
    SendMessageA(lv, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(lv);
    int row = 0;
    for (const Aircraft& a : W.ac) {
        char icao[16]; std::snprintf(icao, sizeof icao, "%06X", a.icao);
        std::string cs = a.callsign.empty() ? "(not yet heard)" : a.callsign;
        char alt[24] = "", spd[24] = "", hdg[24] = "", vr[24] = "", pos[40] = "", dist[24] = "";
        if (a.hasAlt) std::snprintf(alt, sizeof alt, "%d ft", a.altFt);
        if (a.hasVel) { std::snprintf(spd, sizeof spd, "%.0f kt", a.speedKt); std::snprintf(hdg, sizeof hdg, "%.0f deg", a.headingDeg); std::snprintf(vr, sizeof vr, "%+d", a.vrateFpm); }
        if (a.hasPos) {
            std::snprintf(pos, sizeof pos, "%.3f, %.3f", a.lat, a.lon);
            if (W.haveHome) std::snprintf(dist, sizeof dist, "%.0f km", adsbDistanceKm(W.hLat, W.hLon, a.lat, a.lon));
        }
        const std::string age = fmtAge(std::max(0.0, now - a.lastSeen));
        LVITEMA it{}; it.mask = LVIF_TEXT; it.iItem = row; it.pszText = (LPSTR)cs.c_str();
        const int i = (int)SendMessageA(lv, LVM_INSERTITEMA, 0, (LPARAM)&it);
        const char* cols[] = {icao, alt, spd, hdg, vr, pos, dist, age.c_str()};
        for (int c = 0; c < 8; ++c) { LVITEMA s{}; s.iSubItem = c + 1; s.pszText = (LPSTR)cols[c]; SendMessageA(lv, LVM_SETITEMTEXTA, i, (LPARAM)&s); }
        ++row;
    }
    SendMessageA(lv, WM_SETREDRAW, TRUE, 0);
}

void drawPlot(DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC; RECT rc = d->rcItem;
    HBRUSH bg = CreateSolidBrush(RGB(14, 22, 30)); FillRect(dc, &rc, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT); SelectObject(dc, W.font);
    const int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2, R = std::min(rc.right - rc.left, rc.bottom - rc.top) / 2 - S(14);
    double cLat = W.hLat, cLon = W.hLon; bool centred = W.haveHome;
    if (!centred) {
        int n = 0; double sl = 0, so = 0;
        for (auto& a : W.ac) if (a.hasPos) { sl += a.lat; so += a.lon; ++n; }
        if (n) { cLat = sl / n; cLon = so / n; }
    }
    double maxKm = 20;
    for (auto& a : W.ac) if (a.hasPos) maxKm = std::max(maxKm, adsbDistanceKm(cLat, cLon, a.lat, a.lon));
    const double steps[] = {20, 50, 100, 150, 200, 300, 400, 500, 700};
    double scale = 700; for (double s : steps) if (s >= maxKm * 1.05) { scale = s; break; }
    HPEN ring = CreatePen(PS_SOLID, 1, RGB(50, 80, 100)); HGDIOBJ old = SelectObject(dc, ring);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    for (int k = 1; k <= 2; ++k) { const int r = R * k / 2; Ellipse(dc, cx - r, cy - r, cx + r, cy + r); }
    MoveToEx(dc, cx - R, cy, nullptr); LineTo(dc, cx + R, cy); MoveToEx(dc, cx, cy - R, nullptr); LineTo(dc, cx, cy + R);
    SelectObject(dc, old); DeleteObject(ring);
    SetTextColor(dc, RGB(130, 160, 180));
    char t[64]; std::snprintf(t, sizeof t, "N   (outer ring %.0f km)", scale);
    TextOutA(dc, rc.left + S(6), rc.top + S(4), t, (int)strlen(t));
    const char* who = centred ? "Centre: your position" : "Centre: middle of the aircraft (set your position for distances)";
    TextOutA(dc, rc.left + S(6), rc.bottom - S(18), who, (int)strlen(who));
    if (centred) { HBRUSH hb = CreateSolidBrush(RGB(255, 220, 80)); RECT r{cx - S(3), cy - S(3), cx + S(4), cy + S(4)}; FillRect(dc, &r, hb); DeleteObject(hb); }
    const double kmLat = 111.19, kmLon = 111.19 * std::cos(cLat * 3.14159265358979 / 180.0);
    for (auto& a : W.ac) {
        if (!a.hasPos) continue;
        const double dx = (a.lon - cLon) * kmLon, dy = (a.lat - cLat) * kmLat;
        const int x = cx + (int)std::lround(dx / scale * R), y = cy - (int)std::lround(dy / scale * R);
        HBRUSH hb = CreateSolidBrush(RGB(90, 220, 140)); RECT r{x - S(3), y - S(3), x + S(4), y + S(4)}; FillRect(dc, &r, hb); DeleteObject(hb);
        if (a.hasVel) {                                         // a short tick pointing the way it is heading
            const double th = a.headingDeg * 3.14159265358979 / 180.0;
            HPEN p = CreatePen(PS_SOLID, 1, RGB(90, 220, 140)); HGDIOBJ o = SelectObject(dc, p);
            MoveToEx(dc, x, y, nullptr); LineTo(dc, x + (int)std::lround(std::sin(th) * S(12)), y - (int)std::lround(std::cos(th) * S(12)));
            SelectObject(dc, o); DeleteObject(p);
        }
        SetTextColor(dc, RGB(220, 235, 245));
        const std::string lab = a.callsign.empty() ? "?" : a.callsign;
        TextOutA(dc, x + S(5), y - S(14), lab.c_str(), (int)lab.size());
    }
}

void refresh() {
    const bool run = W.host.running && W.host.running();
    SetWindowTextA(H(A_START), run ? "Stop tracking" : "Start tracking");
    W.ac = W.host.aircraft ? W.host.aircraft() : std::vector<Aircraft>();
    const AdsbStats st = W.host.stats ? W.host.stats() : AdsbStats();
    double now = W.host.clock ? W.host.clock() : 0;
    for (auto& a : W.ac) now = std::max(now, a.lastSeen);
    fillList(now);
    InvalidateRect(H(A_PLOT), nullptr, FALSE);
    char b[256];
    int withPos = 0; for (auto& a : W.ac) withPos += a.hasPos;
    if (!run) std::snprintf(b, sizeof b, "Not tracking. Press Start tracking (the receiver is tuned to 1090 MHz for you).");
    else if (W.ac.empty()) std::snprintf(b, sizeof b, "Listening on 1090 MHz... %lu messages decoded, no aircraft yet. Needs sky view and a suitable antenna.", st.frames);
    else std::snprintf(b, sizeof b, "Tracking: %zu aircraft (%d with a position), %lu messages decoded, %lu damaged ones ignored.", W.ac.size(), withPos, st.frames, st.bad);
    std::string s = b;
    if (W.host.note) { const std::string n = W.host.note(); if (!n.empty()) s += " " + n; }
    SetWindowTextA(H(A_STATUS), s.c_str());
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            W.self = h;
            HDC dc = GetDC(nullptr); W.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
            auto mk = [&](const char* cls, const char* text, DWORD style, DWORD ex, int id) {
                HWND c = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, nullptr, nullptr);
                SendMessageA(c, WM_SETFONT, (WPARAM)W.font, TRUE);
                return c;
            };
            mk("BUTTON", "Start tracking", BS_PUSHBUTTON | WS_TABSTOP, 0, A_START);
            mk("STATIC", "", 0, 0, A_STATUS);
            mk("STATIC", "Your latitude:", 0, 0, A_LATL);
            char t[32] = "";
            if (W.haveHome) std::snprintf(t, sizeof t, "%.4f", W.hLat);
            mk("EDIT", t, WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, A_LAT);
            mk("STATIC", "longitude:", 0, 0, A_LONL);
            if (W.haveHome) std::snprintf(t, sizeof t, "%.4f", W.hLon); else t[0] = 0;
            mk("EDIT", t, WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, A_LON);
            mk("BUTTON", "Set position", BS_PUSHBUTTON | WS_TABSTOP, 0, A_SETHOME);
            HWND lv = mk(WC_LISTVIEWA, "", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP | WS_BORDER, 0, A_LIST);
            ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            const char* cn[] = {"Flight", "ICAO", "Altitude", "Speed", "Heading", "Climb ft/min", "Position", "Distance", "Heard"};
            const int cw[] = {90, 62, 74, 62, 66, 78, 130, 70, 80};
            for (int i = 0; i < 9; ++i) { LVCOLUMNA c{}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = (LPSTR)cn[i]; c.cx = S(cw[i]); SendMessageA(lv, LVM_INSERTCOLUMNA, i, (LPARAM)&c); }
            mk("STATIC", "", SS_OWNERDRAW, 0, A_PLOT);
            mk("STATIC", "How it works: aircraft broadcast their identity, height, speed and position on 1090 MHz. This tab only listens. "
                         "A small antenna (about 7 cm long, vertical) with a clear view of the sky works best; aircraft in line of sight can be heard from 100 km or more. "
                         "Indoors you may hear only the nearest ones. The list shows what the aircraft report about themselves, as an observation; it is not verified. "
                         "Enter your latitude and longitude (decimal degrees, west is negative) to see distances.", 0, 0, A_HELP);
            layout();
            SetTimer(h, 1, 500, nullptr);
            refresh();
            return 0;
        }
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = S(760); mm->ptMinTrackSize.y = S(480); return 0; }
        case WM_TIMER: refresh(); return 0;
        case WM_DRAWITEM: if (w == A_PLOT) { drawPlot((DRAWITEMSTRUCT*)l); return TRUE; } break;
        case WM_COMMAND:
            if (LOWORD(w) == A_START) {
                if (W.host.running && W.host.running()) { if (W.host.stop) W.host.stop(); }
                else if (W.host.start) {
                    std::string why;
                    if (!W.host.start(why)) MessageBoxA(h, why.c_str(), "Aircraft tracker", MB_OK | MB_ICONINFORMATION);
                }
                refresh();
            } else if (LOWORD(w) == A_SETHOME) {
                const std::string a = readEdit(A_LAT), o = readEdit(A_LON);
                if (a.empty() && o.empty()) { W.haveHome = false; if (W.host.saveHome) W.host.saveHome(false, 0, 0); }
                else {
                    char *e1 = nullptr, *e2 = nullptr;
                    const double la = std::strtod(a.c_str(), &e1), lo = std::strtod(o.c_str(), &e2);
                    if (a.empty() || o.empty() || *e1 || *e2 || la < -90 || la > 90 || lo < -180 || lo > 180)
                        MessageBoxA(h, "Enter latitude and longitude as decimal degrees, for example 47.5 and -64.9 (west and south are negative).", "Your position", MB_OK | MB_ICONINFORMATION);
                    else { W.haveHome = true; W.hLat = la; W.hLon = lo; if (W.host.saveHome) W.host.saveHome(true, la, lo); }
                }
                refresh();
            }
            return 0;
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: KillTimer(h, 1); if (W.host.stop && W.host.running && W.host.running()) W.host.stop(); W.self = nullptr; return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

} // namespace

bool registerAdsbClass(HINSTANCE hi) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = proc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = kClass;
    return RegisterClassA(&wc) != 0;
}

void showAdsbWindow(HWND owner, HINSTANCE hi, HFONT font, AdsbHost host) {
    if (W.self) { ShowWindow(W.self, SW_RESTORE); SetForegroundWindow(W.self); return; }
    W.owner = owner; W.font = font; W.host = std::move(host);
    W.haveHome = W.host.hasHome; W.hLat = W.host.homeLat; W.hLon = W.host.homeLon;
    HDC dc = GetDC(nullptr); W.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    CreateWindowExA(0, kClass, "OpenRadio Lab - Aircraft Tracker (ADS-B)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                    S(980), S(600), owner, nullptr, hi, nullptr);
}

bool adsbWindowOpen() { return W.self != nullptr; }
void closeAdsbWindow() { if (W.self) DestroyWindow(W.self); }

} // namespace orl
