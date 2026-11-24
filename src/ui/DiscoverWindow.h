// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// The Discover window: sweep the airwaves, list what is out there, listen with a double-click, or let the program
// hop between active channels and stop where something is being said.
#pragma once

#include "../dsp/Survey.h"

#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace orl {

struct DiscoverHost {
    std::function<void(bool quick)> start;                 // sweep repeatedly until stopped
    std::function<void()> stop;
    std::function<void(const DiscoveryRow&)> listen;
    std::function<void(const DiscoveryRow&)> favourite;
    std::function<void(bool on)> autoListen;
    std::function<void()> addToJournal;
};

bool registerDiscoverClass(HINSTANCE hi);
void showDiscover(HWND owner, HINSTANCE hi, HFONT font, DiscoverHost host);
bool discoverOpen();
// UI thread only. Safe to call when the window is closed.
void discoverUpdate(const std::vector<DiscoveryRow>& rows, const std::string& status, bool sweeping, bool autoOn, const std::string& activityLog);

} // namespace orl
