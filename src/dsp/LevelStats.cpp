// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "LevelStats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace orl {

LevelStats analyseLevels(const std::vector<float>& db, double dt, double marginDb, double noiseDb) {
    LevelStats s;
    s.n = db.size();
    if (db.empty() || dt <= 0) return s;
    s.seconds = db.size() * dt;
    double pw = 0;
    s.minDb = s.maxDb = db[0];
    for (float v : db) { s.minDb = std::min<double>(s.minDb, v); s.maxDb = std::max<double>(s.maxDb, v); pw += std::pow(10.0, v / 10.0); }
    s.meanDb = 10.0 * std::log10(pw / db.size() + 1e-20);
    std::vector<float> sorted = db;
    std::sort(sorted.begin(), sorted.end());
    s.noiseKnown = noiseDb < 1e8;
    s.floorDb = s.noiseKnown ? std::min<double>(noiseDb, sorted[sorted.size() / 10]) : sorted[sorted.size() / 10];
    s.thresholdDb = s.floorDb + marginDb;

    bool on = false; size_t runStart = 0, active = 0, gapStart = 0;
    double burstTotal = 0, gapTotal = 0; int gaps = 0;
    for (size_t i = 0; i < db.size(); ++i) {
        if (!on && db[i] > s.thresholdDb) {
            on = true; runStart = i;
            if (s.bursts > 0) { gapTotal += (i - gapStart) * dt; ++gaps; }
        } else if (on && db[i] < s.thresholdDb - 2.0) {           // 2 dB hysteresis so noise does not chop a burst in pieces
            on = false; gapStart = i;
            const double len = (i - runStart) * dt;
            ++s.bursts; burstTotal += len; s.longestBurstSec = std::max(s.longestBurstSec, len);
        }
        if (on || db[i] > s.thresholdDb) ++active;
    }
    if (on) {                                                      // still on at the end of the data
        const double len = (db.size() - runStart) * dt;
        ++s.bursts; burstTotal += len; s.longestBurstSec = std::max(s.longestBurstSec, len);
    }
    s.activeFraction = (double)active / db.size();
    s.meanBurstSec = s.bursts ? burstTotal / s.bursts : 0;
    s.meanGapSec = gaps ? gapTotal / gaps : 0;
    return s;
}

std::string describeActivity(const LevelStats& s) {
    char b[320];
    if (s.n < 20 || s.seconds < 2.0) return "Keep watching: there is not enough data yet.";
    const double range = s.maxDb - s.floorDb;
    if (range < 6.0 && !s.noiseKnown)
        return "The level hardly changes. That means either an empty frequency or a perfectly steady signal; compare it with the noise floor on the spectrum to tell which.";
    if (range < 6.0)
        return "Nothing rises clearly above the background: this frequency looks empty (the level only varies by " +
               std::string(std::to_string((int)std::lround(range))) + " dB).";
    if (s.activeFraction > 0.9 && s.bursts <= 1)
        return "Present almost all the time. That fits a continuous transmission, such as a broadcast station or a carrier.";
    if (s.bursts >= 2) {
        std::snprintf(b, sizeof b, "Comes and goes: %d separate bursts, on for %.0f%% of the time, bursts averaging %.1f s "
                      "(longest %.1f s) with gaps of about %.1f s. That fits push-to-talk voice, a device that reports now and then, or a signal fading in and out.",
                      s.bursts, s.activeFraction * 100.0, s.meanBurstSec, s.longestBurstSec, s.meanGapSec);
        return b;
    }
    std::snprintf(b, sizeof b, "One stretch of activity (%.1f s) so far, on for %.0f%% of the time. Keep watching to see whether it repeats.",
                  s.longestBurstSec, s.activeFraction * 100.0);
    return b;
}

} // namespace orl
