// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "FFT.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace orl {

namespace { constexpr double kPi = 3.14159265358979323846; }

FFT::FFT(int n) : n_(n) {
    if (n < 2 || (n & (n - 1)) != 0)
        throw std::invalid_argument("FFT size must be a power of two");

    int bits = 0;
    while ((1 << bits) < n) ++bits;

    rev_.resize(n);
    for (int i = 0; i < n; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        rev_[i] = r;
    }

    cs_.resize(n / 2);
    sn_.resize(n / 2);
    for (int k = 0; k < n / 2; ++k) {
        double a = -2.0 * kPi * k / n;
        cs_[k] = (float)std::cos(a);
        sn_[k] = (float)std::sin(a);
    }
}

void FFT::forward(cf* x) const {
    for (int i = 0; i < n_; ++i) {
        int j = rev_[i];
        if (j > i) std::swap(x[i], x[j]);
    }
    for (int len = 2; len <= n_; len <<= 1) {
        const int half = len >> 1;
        const int step = n_ / len;
        for (int i = 0; i < n_; i += len) {
            for (int j = 0; j < half; ++j) {
                const float wr = cs_[j * step];
                const float wi = sn_[j * step];
                cf& a = x[i + j];
                cf& b = x[i + j + half];
                const float tr = b.r * wr - b.i * wi;
                const float ti = b.r * wi + b.i * wr;
                b.r = a.r - tr;  b.i = a.i - ti;
                a.r += tr;       a.i += ti;
            }
        }
    }
}

std::vector<float> makeWindow(WindowType t, int n) {
    std::vector<float> w(n, 1.0f);
    for (int i = 0; i < n; ++i) {
        const double x = 2.0 * kPi * i / n;   // periodic window: right for spectral analysis
        switch (t) {
            case WindowType::Rectangular: w[i] = 1.0f; break;
            case WindowType::Hann:        w[i] = (float)(0.5 - 0.5 * std::cos(x)); break;
            case WindowType::Hamming:     w[i] = (float)(0.54 - 0.46 * std::cos(x)); break;
            case WindowType::BlackmanHarris:
                w[i] = (float)(0.35875 - 0.48829 * std::cos(x)
                               + 0.14128 * std::cos(2 * x) - 0.01168 * std::cos(3 * x));
                break;
        }
    }
    return w;
}

const char* windowName(WindowType t) {
    switch (t) {
        case WindowType::Rectangular:    return "Rectangular";
        case WindowType::Hann:           return "Hann";
        case WindowType::Hamming:        return "Hamming";
        case WindowType::BlackmanHarris: return "Blackman-Harris";
    }
    return "?";
}

} // namespace orl
