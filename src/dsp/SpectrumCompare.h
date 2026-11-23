// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Compares two spectrum snapshots of the same band: which signals appeared, vanished or changed strength.
#pragma once

#include <string>
#include <vector>

namespace orl {

struct SpecPeak { double hz = 0, db = 0; };
struct SpecChange { double hz = 0, beforeDb = 0, afterDb = 0; };

struct SpecDiff {
    bool comparable = false;
    std::string reason;                    // why not, when !comparable
    double floorBefore = 0, floorAfter = 0;
    std::vector<SpecPeak> appeared, gone;
    std::vector<SpecChange> changed;       // present in both, level moved by >= changeDb
    int steady = 0;                        // present in both, level about the same
};

// db: one level per FFT bin, DC bin in the middle (as produced by SpectrumProcessor).
std::vector<SpecPeak> findPeaks(const std::vector<float>& db, double centreHz, double rateHz,
                                double marginDb = 8.0, double minSeparationHz = 40e3);

SpecDiff compareSpectra(const std::vector<float>& before, const std::vector<float>& after, double centreHz, double rateHz,
                        double centreAfterHz, double rateAfterHz, double matchHz = 30e3, double changeDb = 3.0);

std::string describeDiff(const SpecDiff& d);

} // namespace orl
