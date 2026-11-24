// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// The Aircraft Tracker window: lists the aircraft heard on 1090 MHz (ADS-B) and plots those that report a position.
// Receive-only. The program tunes the receiver for you while tracking and puts it back afterwards.
#pragma once

#include "../dsp/AdsbDecoder.h"

#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace orl {

struct AdsbHost {
    std::function<bool(std::string& why)> start;            // tune to 1090 MHz and begin; false + reason if impossible
    std::function<void()> stop;                             // stop and restore the previous tuning
    std::function<bool()> running;
    std::function<std::vector<Aircraft>()> aircraft;
    std::function<AdsbStats()> stats;
    std::function<double()> clock;                          // decoder clock, to show how long ago each aircraft was heard
    std::function<void(bool has, double lat, double lon)> saveHome;
    std::function<std::string()> note;                      // extra status, e.g. "simulated receiver"
    bool hasHome = false; double homeLat = 0, homeLon = 0;
};

bool registerAdsbClass(HINSTANCE hi);
void showAdsbWindow(HWND owner, HINSTANCE hi, HFONT font, AdsbHost host);
bool adsbWindowOpen();
void closeAdsbWindow();                                     // e.g. when the app exits

// Great-circle distance in kilometres (exposed for tests).
double adsbDistanceKm(double lat1, double lon1, double lat2, double lon2);

} // namespace orl
