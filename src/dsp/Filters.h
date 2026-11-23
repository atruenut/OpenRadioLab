// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Small DSP toolkit: windowed-sinc FIR design, a polyphase-style decimating FIR (complex or
// real), a biquad low-pass and a streaming cubic resampler. Planar float buffers throughout.
#pragma once

#include <cstddef>
#include <vector>

namespace orl {

// Blackman-windowed sinc low-pass (about -74 dB stopband). Unity gain at DC.
// passEdge/stopEdge in Hz; the filter length follows from the transition width.
std::vector<float> designLowpass(double fs, double passEdge, double stopEdge);

// FIR filter that only computes the output samples it keeps (1 in D).
class FirDecimator {
public:
    void configure(const std::vector<float>& taps, int decimation, bool complexData);
    void reset();
    // Appends to oi (and oq for complex data). xq/oq are ignored for real data.
    size_t process(const float* xi, const float* xq, size_t n, std::vector<float>& oi, std::vector<float>& oq);
    size_t taps() const { return h_.size(); }

private:
    std::vector<float> h_, histI_, histQ_, wi_, wq_;
    int D_ = 1;
    bool cplx_ = true;
    size_t skip_ = 0;
};

class Biquad {
public:
    void lowpass(double fs, double fc, double q = 0.70710678);
    void bandpass(double fs, double fc, double q);     // constant 0 dB peak gain
    void notch(double fs, double fc, double q);
    void reset() { x1_ = x2_ = y1_ = y2_ = 0; }
    float process(float x) {
        const double y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = x; y2_ = y1_; y1_ = y;
        return (float)y;
    }
private:
    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
    double x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

// Streaming fractional resampler using Catmull-Rom (cubic) interpolation.
class CubicResampler {
public:
    void configure(double inRate, double outRate);
    void reset();
    void process(const float* in, size_t n, std::vector<float>& out);   // appends
private:
    std::vector<float> buf_ = {0.0f, 0.0f};
    double t_ = 1.0, step_ = 1.0;
};

} // namespace orl
