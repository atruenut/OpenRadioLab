// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Statistics over a recorded series of signal levels: how strong, how steady, how often "on".
#pragma once

#include <string>
#include <vector>

namespace orl {

struct LevelStats {
    size_t n = 0;
    double seconds = 0;
    double minDb = 0, maxDb = 0, meanDb = 0;   // meanDb is a power average
    bool noiseKnown = false;                    // floorDb came from a measured background level
    double floorDb = 0;                         // background level: measured, or else the 10th percentile of the data
    double thresholdDb = 0;                     // floor + margin: above this counts as "on"
    double activeFraction = 0;                  // share of time above the threshold
    int bursts = 0;                             // separate "on" periods
    double meanBurstSec = 0, longestBurstSec = 0, meanGapSec = 0;
};

// db: one level per sample, dt seconds apart.
// noiseDb: the measured background level (e.g. from the spectrum), if known. Without it a perfectly steady
// signal cannot be told from an empty channel, because the data alone never shows an "off" state.
LevelStats analyseLevels(const std::vector<float>& db, double dt, double marginDb = 6.0, double noiseDb = 1e9);

// A cautious plain-language reading of the statistics (an observation, not an identification).
std::string describeActivity(const LevelStats& s);

} // namespace orl
