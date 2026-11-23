// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "BandScanner.h"

#include <algorithm>
#include <cmath>

namespace orl {

BandScanner::Plan BandScanner::fmPlan(double rate, int fftSize) {
    Plan p;
    p.lo = 87.8e6; p.hi = 108.0e6;
    p.rate = rate; p.fftSize = fftSize;
    p.chFirst = 87.9e6; p.chStep = 200e3; p.chCount = 101; p.chHalfWidth = 60e3;
    p.minSnrDb = 5.0;     // an antenna-less dongle only hears the strongest local stations a few dB up
    return p;
}

void BandScanner::begin(const Plan& p) {
    plan_ = p;
    usable_ = 0.6 * p.rate;                         // central 60% of each hop: the edges roll off
    binHz_ = p.rate / p.fftSize;
    hops_ = (int)std::ceil((p.hi - p.lo) / usable_);
    hop_ = 0;
    settleLeft_ = p.settleFrames;
    got_ = 0;
    acc_.clear();
    const size_t n = (size_t)std::ceil((p.hi - p.lo) / binHz_) + 2;
    sum_.assign(n, 0.0);
    cnt_.assign(n, 0);
    active_ = true;
}

bool BandScanner::addFrame(const SpectrumFrame& f) {
    if (!active_ || done()) return false;
    if (std::fabs(f.centerHz - centre()) > 1.0 || std::fabs(f.sampleRate - plan_.rate) > 1.0 ||
        f.db.size() != (size_t)plan_.fftSize)
        return false;                               // stale frame from the previous hop
    if (settleLeft_ > 0) { --settleLeft_; return false; }

    if (acc_.empty()) acc_.assign(plan_.fftSize, 0.0);
    for (int j = 0; j < plan_.fftSize; ++j) acc_[j] += std::pow(10.0, f.db[j] / 10.0);
    if (++got_ < plan_.collectFrames) return false;

    commitHop();
    ++hop_;
    settleLeft_ = plan_.settleFrames;
    got_ = 0;
    acc_.clear();
    return true;
}

void BandScanner::commitHop() {
    const int N = plan_.fftSize;
    const double c = centre();
    for (int j = 0; j < N; ++j) {
        const int d = j - N / 2;
        if (std::abs(d) <= 3) continue;                       // receiver DC spike: filled in later
        const double off = d * binHz_;
        if (std::fabs(off) > usable_ / 2) continue;
        const long idx = std::lround((c + off - plan_.lo) / binHz_);
        if (idx < 0 || (size_t)idx >= sum_.size()) continue;
        sum_[idx] += acc_[j] / got_;
        ++cnt_[idx];
    }
}

ScanReport BandScanner::result() const {
    ScanReport r;
    const size_t n = sum_.size();
    std::vector<double> db(n, 0.0);
    std::vector<char> ok(n, 0);
    for (size_t i = 0; i < n; ++i)
        if (cnt_[i] > 0) { db[i] = 10.0 * std::log10(sum_[i] / cnt_[i] + 1e-20); ok[i] = 1; }

    // Fill gaps (the DC spikes and rounding seams) by interpolating between valid neighbours.
    long prev = -1;
    for (size_t i = 0; i < n; ++i) {
        if (!ok[i]) continue;
        if (prev >= 0 && (long)i - prev > 1)
            for (long k = prev + 1; k < (long)i; ++k)
                db[k] = db[prev] + (db[i] - db[prev]) * (double)(k - prev) / (double)(i - prev);
        prev = (long)i;
    }
    std::vector<double> lin(n);
    for (size_t i = 0; i < n; ++i) lin[i] = std::pow(10.0, db[i] / 10.0);

    for (size_t i = 0; i < n; ++i)
        if (ok[i] && (r.peakDb == 0 || db[i] > r.peakDb)) { r.peakDb = db[i]; r.peakHz = plan_.lo + i * binHz_; }

    // Mean power over each channel's core.
    std::vector<double> ch(plan_.chCount, -200.0);
    for (int k = 0; k < plan_.chCount; ++k) {
        const double fc = plan_.chFirst + k * plan_.chStep;
        const long a = std::lround((fc - plan_.chHalfWidth - plan_.lo) / binHz_);
        const long b = std::lround((fc + plan_.chHalfWidth - plan_.lo) / binHz_);
        if (a < 0 || (size_t)b >= n) continue;
        double s = 0; int c = 0;
        for (long i = a; i <= b; ++i) { s += lin[i]; ++c; }
        ch[k] = 10.0 * std::log10(s / c + 1e-20);
    }
    r.channelsChecked = plan_.chCount;

    std::vector<double> sorted = ch;
    std::sort(sorted.begin(), sorted.end());
    r.floorDb = sorted[sorted.size() / 5];          // 20th percentile: an empty channel, even in a crowded band

    double best = -1e9;
    for (int k = 0; k < plan_.chCount; ++k) {
        const double snr = ch[k] - r.floorDb;
        best = std::max(best, snr);
        if (snr < plan_.minSnrDb) continue;
        // Skirts of a much stronger neighbour are not separate stations.
        if ((k > 0 && ch[k - 1] >= ch[k] + 10.0) || (k + 1 < plan_.chCount && ch[k + 1] >= ch[k] + 10.0)) continue;
        ScanChannel s;
        s.hz = plan_.chFirst + k * plan_.chStep;
        s.levelDb = ch[k];
        s.snrDb = snr;
        r.stations.push_back(s);
    }
    r.strongestSnrDb = best;
    std::sort(r.stations.begin(), r.stations.end(),
              [](const ScanChannel& a, const ScanChannel& b) { return a.snrDb > b.snrDb; });
    return r;
}

} // namespace orl
