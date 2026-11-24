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
    std::function<void()> saveScan;                        // choose a file and save the list
    std::function<void()> loadScan;                        // choose a file and load a list
    std::function<void()> exportCsv;                       // for a spreadsheet
    std::function<void()> clearScan;                       // start with an empty list
    std::function<void(double hz, const std::string& note)> setNote;
    std::function<void(bool on)> recordClips;              // auto-listen saves a sound clip of each activity
    std::function<void(int minutes)> setWatch;             // 0 = off; otherwise sweep once every N minutes while this window is open
    std::function<void()> compareScan;                     // pick an older scan file and report what changed
};

bool registerDiscoverClass(HINSTANCE hi);
void showDiscover(HWND owner, HINSTANCE hi, HFONT font, DiscoverHost host);
bool discoverOpen();
// A simple read-only text window (used for the comparison report).
void showTextReport(HWND owner, HINSTANCE hi, HFONT font, const std::string& title, const std::string& text);
// UI thread only. Safe to call when the window is closed.
void discoverUpdate(const std::vector<DiscoveryRow>& rows, const std::string& status, bool sweeping, bool autoOn, const std::string& activityLog);

} // namespace orl
