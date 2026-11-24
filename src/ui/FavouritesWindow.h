// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// The Favourites window: your saved stations and signals in one list you can sort, rename, reorder, remove and tune to.
#pragma once

#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace orl {

struct FavItem { double hz = 0; int mode = 0; std::string name; };

struct FavHost {
    std::function<std::vector<FavItem>()> list;
    std::function<void(int index)> listen;
    std::function<void(int index, const std::string& name)> rename;
    std::function<void(int index)> remove;
    std::function<void(int index, int delta)> move;        // delta -1 up, +1 down
    std::function<void()> addCurrent;                      // save what the receiver is tuned to
};

bool registerFavouritesClass(HINSTANCE hi);
void showFavourites(HWND owner, HINSTANCE hi, HFONT font, FavHost host);
bool favouritesOpen();
void favouritesRefresh();                                  // call after the list changed elsewhere

} // namespace orl
