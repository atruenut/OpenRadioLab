// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "FavouritesWindow.h"

#include "../Util.h"

#include <commctrl.h>
#include <algorithm>

namespace orl {
namespace {

const char* kClass = "ORLFavouritesWindow";
enum { F_LIST = 1, F_LISTEN, F_REMOVE, F_UP, F_DOWN, F_ADD, F_NAMEL, F_NAME, F_NAMESAVE, F_HELP };

struct State {
    HWND self = nullptr, owner = nullptr; HFONT font = nullptr; int dpi = 96;
    FavHost host;
    std::vector<FavItem> items;
    int sortCol = -1; bool sortAsc = true;      // sorting is for looking only; Move up/down needs the real order
} F;

int S(int v) { return MulDiv(v, F.dpi, 96); }
HWND H(int id) { return GetDlgItem(F.self, id); }
int selected() { return (int)SendMessageA(H(F_LIST), LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED); }

std::vector<int> order;                          // list row -> index in F.items

void fill(int selectIndex = -2) {
    HWND lv = H(F_LIST);
    int keep = selectIndex;
    if (keep == -2) { const int s = selected(); keep = (s >= 0 && s < (int)order.size()) ? order[s] : -1; }
    F.items = F.host.list ? F.host.list() : std::vector<FavItem>();
    order.clear(); for (size_t i = 0; i < F.items.size(); ++i) order.push_back((int)i);
    if (F.sortCol >= 0) {
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const FavItem &x = F.items[a], &y = F.items[b];
            int t = 0;
            if (F.sortCol == 0) t = x.name.compare(y.name);
            else if (F.sortCol == 1) t = x.hz < y.hz ? -1 : (x.hz > y.hz ? 1 : 0);
            else t = x.mode - y.mode;
            return F.sortAsc ? t < 0 : t > 0;
        });
    }
    static const char* modes[] = {"WFM", "NFM", "AM", "USB", "LSB", "CW"};
    SendMessageA(lv, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(lv);
    for (size_t k = 0; k < order.size(); ++k) {
        const FavItem& f = F.items[order[k]];
        LVITEMA it{}; it.mask = LVIF_TEXT; it.iItem = (int)k; it.pszText = (LPSTR)f.name.c_str();
        SendMessageA(lv, LVM_INSERTITEMA, 0, (LPARAM)&it);
        const std::string fr = formatFrequency(f.hz);
        ListView_SetItemText(lv, (int)k, 1, (LPSTR)fr.c_str());
        ListView_SetItemText(lv, (int)k, 2, (LPSTR)modes[std::max(0, std::min(5, f.mode))]);
        if (order[k] == keep) ListView_SetItemState(lv, (int)k, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    SendMessageA(lv, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lv, nullptr, TRUE);
    char b[96]; std::snprintf(b, sizeof b, "%zu favourite%s. Double-click one to tune to it. Click a heading to sort.", F.items.size(), F.items.size() == 1 ? "" : "s");
    SetWindowTextA(H(F_HELP), b);
}

int selIndex() { const int s = selected(); return (s >= 0 && s < (int)order.size()) ? order[s] : -1; }

void markSort() {
    static const char* names[3] = {"Name", "Frequency", "Listen as"};
    for (int c = 0; c < 3; ++c) {
        std::string t = names[c]; if (c == F.sortCol) t += F.sortAsc ? "  ^" : "  v";
        LVCOLUMNA col{}; col.mask = LVCF_TEXT; col.pszText = (LPSTR)t.c_str();
        SendMessageA(H(F_LIST), LVM_SETCOLUMNA, c, (LPARAM)&col);
    }
}

void layout() {
    RECT rc; GetClientRect(F.self, &rc);
    const int m = S(10), W = (int)rc.right, Hh = (int)rc.bottom, bh = S(28);
    int x = m;
    auto btn = [&](int id, int w) { SetWindowPos(H(id), nullptr, x, m, w, bh, SWP_NOZORDER); x += w + S(6); };
    btn(F_LISTEN, S(90)); btn(F_UP, S(90)); btn(F_DOWN, S(100)); btn(F_REMOVE, S(90)); x += S(10); btn(F_ADD, S(230));
    const int bottom = S(34);
    SetWindowPos(H(F_LIST), nullptr, m, m + bh + S(8), W - 2 * m, Hh - bh - S(8) - 3 * m - bottom - S(22), SWP_NOZORDER);
    const int y = Hh - m - bottom - S(18) + S(22) - S(22);
    SetWindowPos(H(F_HELP), nullptr, m, Hh - m - bottom - S(20), W - 2 * m, S(18), SWP_NOZORDER);
    (void)y;
    SetWindowPos(H(F_NAMEL), nullptr, m, Hh - m - S(24) + S(4), S(60), S(20), SWP_NOZORDER);
    SetWindowPos(H(F_NAME), nullptr, m + S(64), Hh - m - S(26), W - 2 * m - S(64) - S(110), S(26), SWP_NOZORDER);
    SetWindowPos(H(F_NAMESAVE), nullptr, W - m - S(104), Hh - m - S(26), S(104), S(26), SWP_NOZORDER);
    const int lw = W - 2 * m - S(24);
    SendMessageA(H(F_LIST), LVM_SETCOLUMNWIDTH, 0, std::max(S(160), lw - S(120) - S(90)));
    SendMessageA(H(F_LIST), LVM_SETCOLUMNWIDTH, 1, S(120));
    SendMessageA(H(F_LIST), LVM_SETCOLUMNWIDTH, 2, S(90));
}

void onSelect() {
    const int i = selIndex();
    SetWindowTextA(H(F_NAME), i >= 0 ? F.items[i].name.c_str() : "");
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            F.self = h;
            auto mk = [&](const char* cls, const char* text, DWORD style, DWORD ex, int id) {
                HWND c = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, nullptr, nullptr);
                SendMessageA(c, WM_SETFONT, (WPARAM)F.font, TRUE);
                return c;
            };
            mk("BUTTON", "Listen", BS_PUSHBUTTON | WS_TABSTOP, 0, F_LISTEN);
            mk("BUTTON", "Move up", BS_PUSHBUTTON | WS_TABSTOP, 0, F_UP);
            mk("BUTTON", "Move down", BS_PUSHBUTTON | WS_TABSTOP, 0, F_DOWN);
            mk("BUTTON", "Remove", BS_PUSHBUTTON | WS_TABSTOP, 0, F_REMOVE);
            mk("BUTTON", "Add what I am listening to now", BS_PUSHBUTTON | WS_TABSTOP, 0, F_ADD);
            HWND lv = mk(WC_LISTVIEWA, "", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE, F_LIST);
            ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            const char* cols[3] = {"Name", "Frequency", "Listen as"};
            for (int c = 0; c < 3; ++c) { LVCOLUMNA col{}; col.mask = LVCF_TEXT | LVCF_WIDTH; col.pszText = (LPSTR)cols[c]; col.cx = 100; SendMessageA(lv, LVM_INSERTCOLUMNA, c, (LPARAM)&col); }
            mk("STATIC", "", 0, 0, F_HELP);
            mk("STATIC", "Name:", 0, 0, F_NAMEL);
            mk("EDIT", "", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, F_NAME);
            mk("BUTTON", "Rename", BS_PUSHBUTTON | WS_TABSTOP, 0, F_NAMESAVE);
            layout(); fill(-1);
            return 0;
        }
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = S(620); mm->ptMinTrackSize.y = S(360); return 0; }
        case WM_NOTIFY: {
            auto* nm = (NMHDR*)l;
            if (nm->idFrom == F_LIST) {
                if (nm->code == LVN_ITEMCHANGED && (((NMLISTVIEW*)l)->uChanged & LVIF_STATE)) onSelect();
                else if (nm->code == NM_DBLCLK) { const int i = selIndex(); if (i >= 0 && F.host.listen) F.host.listen(i); }
                else if (nm->code == LVN_COLUMNCLICK) {
                    const int c = ((NMLISTVIEW*)l)->iSubItem;
                    if (c == F.sortCol) F.sortAsc = !F.sortAsc; else { F.sortCol = c; F.sortAsc = true; }
                    markSort(); fill();
                }
            }
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(w); const int i = selIndex();
            if (id == F_LISTEN && i >= 0 && F.host.listen) F.host.listen(i);
            else if (id == F_ADD && F.host.addCurrent) F.host.addCurrent();
            else if (id == F_REMOVE && i >= 0 && F.host.remove) {
                if (MessageBoxA(h, ("Remove '" + F.items[i].name + "' from your favourites?").c_str(), "Favourites", MB_YESNO | MB_ICONQUESTION) == IDYES) { F.host.remove(i); fill(-1); }
            }
            else if ((id == F_UP || id == F_DOWN) && i >= 0 && F.host.move) {
                if (F.sortCol >= 0) { F.sortCol = -1; markSort(); }          // moving only makes sense in the saved order
                const int to = std::max(0, std::min((int)F.items.size() - 1, i + (id == F_UP ? -1 : 1)));
                F.host.move(i, id == F_UP ? -1 : 1); fill(to);
            }
            else if (id == F_NAMESAVE && i >= 0 && F.host.rename) {
                char b[120] = {0}; GetWindowTextA(H(F_NAME), b, sizeof b);
                if (b[0]) { F.host.rename(i, b); fill(i); }
            }
            return 0;
        }
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: F.self = nullptr; return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

} // namespace

bool registerFavouritesClass(HINSTANCE hi) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = proc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = kClass;
    return RegisterClassA(&wc) != 0;
}

void showFavourites(HWND owner, HINSTANCE hi, HFONT font, FavHost host) {
    F.host = std::move(host);
    if (F.self) { ShowWindow(F.self, SW_RESTORE); SetForegroundWindow(F.self); fill(); return; }
    F.owner = owner; F.font = font;
    HDC dc = GetDC(nullptr); F.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    CreateWindowExA(0, kClass, "OpenRadio Lab - Favourites", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, S(720), S(520), owner, nullptr, hi, nullptr);
}

bool favouritesOpen() { return F.self != nullptr; }
void favouritesRefresh() { if (F.self) fill(); }

} // namespace orl
