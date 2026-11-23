// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "SpectrumCompare.h"

#include "../Util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace orl {

static double floorOf(const std::vector<float>& db) {
    std::vector<float> s = db;
    std::sort(s.begin(), s.end());
    return s.empty() ? 0.0 : s[s.size() * 3 / 10];       // 30th percentile: the spectrum is mostly noise
}

std::vector<SpecPeak> findPeaks(const std::vector<float>& db, double centreHz, double rateHz, double marginDb, double minSepHz) {
    std::vector<SpecPeak> out;
    const int N = (int)db.size();
    if (N < 64 || rateHz <= 0) return out;
    const double binHz = rateHz / N, fl = floorOf(db);
    const int k = std::max(2, (int)std::lround(5e3 / binHz));              // a peak must beat everything within ~5 kHz
    std::vector<SpecPeak> cand;
    for (int i = k; i < N - k; ++i) {
        if (std::abs(i - N / 2) <= 3) continue;                           // the receiver's DC spike is not a signal
        if (db[i] < fl + marginDb) continue;
        bool top = true;
        for (int j = i - k; j <= i + k && top; ++j) if (db[j] > db[i]) top = false;
        if (top) cand.push_back({centreHz + (i - N / 2) * binHz, (double)db[i]});
    }
    std::sort(cand.begin(), cand.end(), [](const SpecPeak& a, const SpecPeak& b) { return a.db > b.db; });
    for (const auto& c : cand) {                                           // strongest first, drop neighbours of stronger peaks
        bool near = false;
        for (const auto& o : out) if (std::fabs(o.hz - c.hz) < minSepHz) { near = true; break; }
        if (!near) out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const SpecPeak& a, const SpecPeak& b) { return a.hz < b.hz; });
    return out;
}

SpecDiff compareSpectra(const std::vector<float>& a, const std::vector<float>& b, double ca, double ra, double cb, double rb,
                        double matchHz, double changeDb) {
    SpecDiff d;
    if (a.size() != b.size() || a.empty() || std::fabs(ca - cb) > 1000.0 || std::fabs(ra - rb) > 1.0) {
        d.reason = "These two snapshots cover different bands or settings, so they cannot be compared bin by bin. Take both at the same centre frequency and sample rate.";
        return d;
    }
    d.comparable = true;
    d.floorBefore = floorOf(a); d.floorAfter = floorOf(b);
    const auto pa = findPeaks(a, ca, ra), pb = findPeaks(b, cb, rb);
    std::vector<char> used(pb.size(), 0);
    for (const auto& x : pa) {
        int best = -1; double bd = matchHz;
        for (size_t j = 0; j < pb.size(); ++j) {
            const double dist = std::fabs(pb[j].hz - x.hz);
            if (!used[j] && dist < bd) { bd = dist; best = (int)j; }
        }
        if (best < 0) { d.gone.push_back(x); continue; }
        used[best] = 1;
        const double diff = pb[best].db - x.db;
        if (std::fabs(diff) >= changeDb) d.changed.push_back({x.hz, x.db, pb[best].db}); else ++d.steady;
    }
    for (size_t j = 0; j < pb.size(); ++j) if (!used[j]) d.appeared.push_back(pb[j]);
    return d;
}

std::string describeDiff(const SpecDiff& d) {
    if (!d.comparable) return d.reason;
    std::string t;
    char b[200];
    auto list = [&](const char* head, const std::vector<SpecPeak>& v) {
        if (v.empty()) return;
        t += head;
        for (size_t i = 0; i < v.size() && i < 12; ++i) { std::snprintf(b, sizeof b, "\r\n    %s  (%.0f dBFS)", formatFrequency(v[i].hz).c_str(), v[i].db); t += b; }
        if (v.size() > 12) t += "\r\n    ... and more";
        t += "\r\n";
    };
    list("New signals in the second snapshot:", d.appeared);
    list("Signals that disappeared:", d.gone);
    if (!d.changed.empty()) {
        t += "Signals whose strength changed:";
        for (size_t i = 0; i < d.changed.size() && i < 12; ++i) {
            const auto& c = d.changed[i];
            std::snprintf(b, sizeof b, "\r\n    %s  %.0f -> %.0f dBFS (%+.0f dB)", formatFrequency(c.hz).c_str(), c.beforeDb, c.afterDb, c.afterDb - c.beforeDb);
            t += b;
        }
        t += "\r\n";
    }
    std::snprintf(b, sizeof b, "Unchanged: %d signal(s).  Background: %.0f -> %.0f dBFS", d.steady, d.floorBefore, d.floorAfter);
    t += b;
    if (d.appeared.empty() && d.gone.empty() && d.changed.empty()) t = std::string("No differences worth noting: the same signals at about the same strength.\r\n") + t;
    t += "\r\n\r\nOnly signals standing at least 8 dB above the background are compared. Different times of day, antenna positions and gain settings all change what you see.";
    return t;
}

} // namespace orl
