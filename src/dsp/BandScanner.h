// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Sweeps a frequency range by hopping the receiver, stitches the spectrum slices together and
// finds occupied channels on a regular raster (e.g. the 200 kHz FM broadcast channels).
// Portable and driven by the caller: the UI retunes the device, feeds spectrum frames, and asks
// the scanner what to do next. That keeps it testable against the simulator with no hardware.
#pragma once

#include "SpectrumProcessor.h"

#include <vector>

namespace orl {

struct ScanChannel {
    double hz = 0;
    double levelDb = 0;     // mean level over the channel core, dBFS
    double snrDb = 0;       // above the estimated noise floor
};

struct ScanReport {
    std::vector<ScanChannel> stations;   // strongest first
    double floorDb = 0;                  // estimated noise floor (dBFS)
    double peakDb = 0;                   // strongest single bin in the whole sweep
    double peakHz = 0;
    double strongestSnrDb = 0;           // best channel's margin over the floor (even if below threshold)
    int channelsChecked = 0;
};

class BandScanner {
public:
    struct Plan {
        double lo = 0, hi = 0;           // range swept (Hz)
        double rate = 2048000;           // sample rate used while scanning
        int fftSize = 2048;
        double chFirst = 0, chStep = 0, chHalfWidth = 0;   // channel raster
        int chCount = 0;
        double minSnrDb = 8.0;
        int settleFrames = 10, collectFrames = 8;
    };

    static Plan fmPlan(double rate = 2048000, int fftSize = 2048);   // Canadian/US FM: 87.9 .. 107.9 MHz, 200 kHz

    void begin(const Plan& p);
    bool active() const { return active_; }
    bool done() const { return active_ && hop_ >= hops_; }
    double centre() const { return plan_.lo + usable_ * (hop_ + 0.5); }
    int hop() const { return hop_; }
    int hops() const { return hops_; }
    double progress() const { return hops_ ? (double)hop_ / hops_ : 0.0; }

    // Feed every spectrum frame. Frames that don't belong to the current hop are ignored.
    // Returns true when a hop was completed (then retune to centre(), unless done()).
    bool addFrame(const SpectrumFrame& f);

    ScanReport result() const;

private:
    void commitHop();

    Plan plan_;
    bool active_ = false;
    double usable_ = 0, binHz_ = 0;
    int hops_ = 0, hop_ = 0, settleLeft_ = 0, got_ = 0;
    std::vector<double> acc_;            // per-FFT-bin power sum for the current hop
    std::vector<double> sum_;            // stitched power (linear)
    std::vector<int> cnt_;
};

} // namespace orl
