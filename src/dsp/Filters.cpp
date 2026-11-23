// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "Filters.h"

#include <algorithm>
#include <cmath>

namespace orl {

namespace { constexpr double kPi = 3.14159265358979323846; }

std::vector<float> designLowpass(double fs, double passEdge, double stopEdge) {
    const double trans = std::max(stopEdge - passEdge, fs * 1e-4);
    int n = (int)std::ceil(5.5 * fs / trans);          // Blackman: transition ~ 5.5 * fs / N
    if (n % 2 == 0) ++n;
    n = std::max(5, std::min(2049, n));
    const double fc = 0.5 * (passEdge + stopEdge) / fs;   // cycles/sample, centre of the transition
    const int mid = (n - 1) / 2;
    std::vector<double> h(n);
    double sum = 0;
    for (int i = 0; i < n; ++i) {
        const double x = i - mid;
        const double sinc = (i == mid) ? 2.0 * fc : std::sin(2.0 * kPi * fc * x) / (kPi * x);
        const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (n - 1)) + 0.08 * std::cos(4.0 * kPi * i / (n - 1));
        h[i] = sinc * w;
        sum += h[i];
    }
    std::vector<float> out(n);
    for (int i = 0; i < n; ++i) out[i] = (float)(h[i] / sum);
    return out;
}

void FirDecimator::configure(const std::vector<float>& taps, int decimation, bool complexData) {
    h_ = taps;
    D_ = std::max(1, decimation);
    cplx_ = complexData;
    reset();
}

void FirDecimator::reset() {
    const size_t H = h_.empty() ? 0 : h_.size() - 1;
    histI_.assign(H, 0.0f);
    histQ_.assign(H, 0.0f);
    skip_ = 0;
}

namespace {
inline float dot(const float* a, const float* h, size_t T) {
    float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    size_t j = 0;
    for (; j + 4 <= T; j += 4) {
        s0 += a[j] * h[j]; s1 += a[j + 1] * h[j + 1]; s2 += a[j + 2] * h[j + 2]; s3 += a[j + 3] * h[j + 3];
    }
    for (; j < T; ++j) s0 += a[j] * h[j];
    return (s0 + s1) + (s2 + s3);
}
} // namespace

size_t FirDecimator::process(const float* xi, const float* xq, size_t n, std::vector<float>& oi, std::vector<float>& oq) {
    const size_t T = h_.size();
    if (T == 0 || n == 0) return 0;
    const size_t H = T - 1;

    wi_.resize(H + n);
    std::copy(histI_.begin(), histI_.end(), wi_.begin());
    std::copy(xi, xi + n, wi_.begin() + H);
    if (cplx_) {
        wq_.resize(H + n);
        std::copy(histQ_.begin(), histQ_.end(), wq_.begin());
        std::copy(xq, xq + n, wq_.begin() + H);
    }

    size_t m = skip_, produced = 0;
    for (; m < n; m += D_, ++produced) {
        oi.push_back(dot(&wi_[m], h_.data(), T));
        if (cplx_) oq.push_back(dot(&wq_[m], h_.data(), T));
    }
    skip_ = m - n;

    if (H > 0) {
        histI_.assign(wi_.end() - H, wi_.end());
        if (cplx_) histQ_.assign(wq_.end() - H, wq_.end());
    }
    return produced;
}

void Biquad::lowpass(double fs, double fc, double q) {
    fc = std::min(fc, 0.45 * fs);
    const double w0 = 2.0 * kPi * fc / fs, cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0_ = (1.0 - cw) / 2.0 / a0;
    b1_ = (1.0 - cw) / a0;
    b2_ = b0_;
    a1_ = -2.0 * cw / a0;
    a2_ = (1.0 - alpha) / a0;
    reset();
}

void Biquad::bandpass(double fs, double fc, double q) {
    const double w0 = 2.0 * kPi * fc / fs, cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0_ = alpha / a0; b1_ = 0; b2_ = -alpha / a0;
    a1_ = -2.0 * cw / a0; a2_ = (1.0 - alpha) / a0;
    reset();
}

void Biquad::notch(double fs, double fc, double q) {
    const double w0 = 2.0 * kPi * fc / fs, cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0_ = 1.0 / a0; b1_ = -2.0 * cw / a0; b2_ = 1.0 / a0;
    a1_ = -2.0 * cw / a0; a2_ = (1.0 - alpha) / a0;
    reset();
}

void CubicResampler::configure(double inRate, double outRate) {
    step_ = inRate / outRate;
    reset();
}

void CubicResampler::reset() {
    buf_.assign(2, 0.0f);
    t_ = 1.0;
}

void CubicResampler::process(const float* in, size_t n, std::vector<float>& out) {
    buf_.insert(buf_.end(), in, in + n);
    const size_t N = buf_.size();
    while ((size_t)t_ + 2 < N) {
        const size_t i = (size_t)t_;
        const float f = (float)(t_ - (double)i);
        const float p0 = buf_[i - 1], p1 = buf_[i], p2 = buf_[i + 1], p3 = buf_[i + 2];
        out.push_back(p1 + 0.5f * f * (p2 - p0 + f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + f * (3.0f * (p1 - p2) + p3 - p0))));
        t_ += step_;
    }
    const size_t drop = (size_t)t_ - 1;
    buf_.erase(buf_.begin(), buf_.begin() + (long)drop);
    t_ -= (double)drop;
}

} // namespace orl
