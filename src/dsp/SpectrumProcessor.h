// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Turns a stream of IQ samples into spectrum "lines" (dBFS per frequency bin)
// at a limited, steady rate, ready for the spectrum plot and waterfall.
#pragma once

#include "FFT.h"
#include "../Sync.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <vector>

namespace orl {

struct SpectrumFrame {
    std::vector<float> db;      // dBFS, DC bin in the middle (index size/2)
    double centerHz   = 0;
    double sampleRate = 0;
};

class SpectrumProcessor {
public:
    SpectrumProcessor();

    void configure(int fftSize, WindowType w);        // any thread
    void setTuning(double centerHz, double rate);     // flushes in-flight data
    void setLinesPerSecond(double lps);

    // Called from the device thread. Interleaved float I,Q in [-1,1].
    void pushSamples(const float* iq, size_t numComplex);

    // Called from the UI thread; appends all pending frames to `out`.
    size_t drain(std::vector<SpectrumFrame>& out);

    int fftSize() const;
    WindowType window() const;

private:
    void rebuildLocked();
    void emitLocked();

    mutable Mutex m_;
    int fftSize_ = 2048;
    WindowType win_ = WindowType::Hann;
    std::unique_ptr<FFT> fft_;
    std::vector<float> window_;
    double norm_ = 1.0;                 // 1 / (sum of window)^2
    std::vector<cf> buf_;
    size_t fill_ = 0;
    std::vector<float> accum_;
    int accumCount_ = 0;
    double center_ = 0, rate_ = 0;
    double lps_ = 30.0;
    double lastEmit_ = 0.0;

    mutable Mutex outM_;
    std::deque<SpectrumFrame> out_;
};

} // namespace orl
