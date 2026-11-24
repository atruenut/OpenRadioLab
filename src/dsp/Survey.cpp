// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "Survey.h"

#include "../Bands.h"
#include "../Util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace orl {

std::vector<SurveyRange> surveyRanges(bool quick, double minHz, double maxHz) {
    static const SurveyRange q[] = {
        {108e6, 118e6, "Aircraft navigation beacons"}, {118e6, 137e6, "Airband voice"}, {137e6, 138e6, "Weather satellites"},
        {144e6, 148e6, "2 m amateur"}, {148e6, 174e6, "VHF public service, marine and weather"}, {216e6, 225e6, "1.25 m amateur"},
        {406e6, 413e6, "Emergency beacons / public safety"}, {420e6, 450e6, "70 cm amateur and ISM"}, {450e6, 470e6, "UHF business and family radio"},
        {902e6, 928e6, "915 MHz ISM"}, {1085e6, 1095e6, "Aircraft transponders"},
    };
    std::vector<SurveyRange> out;
    if (quick) {
        for (const auto& r : q) if (r.lo >= minHz && r.hi <= maxHz) out.push_back(r);
    } else {
        // Everything tunable, in 30 MHz blocks, skipping the FM broadcast band (the FM scanner covers it).
        for (double lo = std::max(minHz, 25e6); lo < maxHz - 1e6; lo += 30e6) {
            double hi = std::min(lo + 30e6, maxHz);
            if (hi <= 87.8e6 || lo >= 108e6) out.push_back({lo, hi, "Wide sweep"});
            else {
                if (lo < 87.8e6) out.push_back({lo, 87.8e6, "Wide sweep"});
                if (hi > 108e6) out.push_back({108e6, hi, "Wide sweep"});
            }
        }
    }
    return out;
}

namespace {
double percentile(std::vector<float> v, double p) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[(size_t)(p * (v.size() - 1))]; }
}

std::vector<FoundSignal> findSignals(const std::vector<float>& db, double lo, double binHz, double margin) {
    std::vector<FoundSignal> out;
    const long n = (long)db.size();
    if (n < 400 || binHz <= 0) return out;
    // Local noise floor: 25th percentile of each ~200 kHz block, then the lowest nearby value.
    const long blk = std::max<long>(50, (long)(200e3 / binHz));
    const long nb = (n + blk - 1) / blk;
    std::vector<double> fl(nb);
    for (long b = 0; b < nb; ++b) fl[b] = percentile(std::vector<float>(db.begin() + b * blk, db.begin() + std::min(n, (b + 1) * blk)), 0.25);
    std::vector<double> fs(nb);
    for (long b = 0; b < nb; ++b) {                       // lowest floor within about +-1 MHz, so a wide signal does not raise its own floor
        double m = fl[b];
        for (long k = std::max<long>(0, b - 5); k <= std::min(nb - 1, b + 5); ++k) m = std::min(m, fl[k]);
        fs[b] = m;
    }
    auto floorAt = [&](long i) { return fs[std::min(nb - 1, i / blk)]; };

    long i = 0;
    while (i < n) {
        if (db[i] < floorAt(i) + margin) { ++i; continue; }
        long j = i;                                           // a run of bins above the threshold
        while (j + 1 < n && db[j + 1] >= floorAt(j + 1) + margin - 3.0) ++j;
        const double width = (j - i + 1) * binHz;
        const double f = floorAt((i + j) / 2);
        if (width > 400e3) {                                  // a wide block: one entry, not hundreds
            double s = 0; for (long k = i; k <= j; ++k) s += db[k];
            FoundSignal fsig; fsig.hz = lo + (i + j) / 2.0 * binHz; fsig.widthHz = width; fsig.levelDb = s / (j - i + 1); fsig.snrDb = fsig.levelDb - f;
            out.push_back(fsig);
        } else {                                              // narrow signals: local maxima at least ~12 kHz apart
            const long sep = std::max<long>(2, (long)(12e3 / binHz));
            std::vector<long> peaks;
            for (long k = i; k <= j; ++k) {
                bool top = true;
                for (long m = std::max(i, k - sep); m <= std::min(j, k + sep) && top; ++m) if (db[m] > db[k] || (db[m] == db[k] && m < k)) top = false;
                if (top && db[k] >= f + margin) peaks.push_back(k);
            }
            for (long k : peaks) {
                long a = k, b = k;                            // 6 dB width around the peak
                while (a > i && db[a - 1] >= db[k] - 6.0) --a;
                while (b < j && db[b + 1] >= db[k] - 6.0) ++b;
                FoundSignal fsig; fsig.hz = lo + (a + b) / 2.0 * binHz; fsig.levelDb = db[k]; fsig.snrDb = db[k] - f;
                fsig.widthHz = std::max(binHz, (b - a + 1) * binHz);
                out.push_back(fsig);
            }
        }
        i = j + 1;
    }
    return out;
}

void DiscoveryTable::addSignals(const std::vector<FoundSignal>& s) {
    for (const auto& f : s) {
        const double tol = std::max(8e3, f.widthHz * 0.6);
        E* hit = nullptr;
        for (auto& e : e_) if (std::fabs(e.hz - f.hz) <= std::max(tol, e.width * 0.6)) { hit = &e; break; }
        if (!hit) { e_.push_back(E()); hit = &e_.back(); hit->hz = f.hz; hit->width = f.widthHz; }
        if (hit->lastPass != passes_) { ++hit->seen; hit->lastPass = passes_; }
        if (f.snrDb >= hit->maxSnr) { hit->maxSnr = f.snrDb; hit->hz = f.hz; hit->width = f.widthHz; }
        hit->lastSnr = f.snrDb;
    }
}

void describeSignal(DiscoveryRow& r) {
    const Band* b = findBand(r.hz);
    r.known = b != nullptr;
    r.where = b ? b->name : "outside the band table";
    const double w = r.widthHz;
    std::string shape;
    if (w >= 400e3) { shape = "A wide block of energy (television, cellular or another wideband service)"; r.mode = 0; }
    else if (w >= 120e3) { shape = "A wide signal about as wide as a broadcast FM station or a wide data link"; r.mode = 0; }
    else if (w >= 25e3) { shape = "A medium-wide signal (wide-band voice or a data link)"; r.mode = 1; }
    else if (w >= 5e3) { shape = "A narrow signal the width of a voice channel"; r.mode = 1; }
    else { shape = "A very narrow signal, like a steady carrier, beacon or a data burst"; r.mode = 1; }
    if (b) { r.mode = (w >= 120e3 && b->mode == 1) ? 0 : b->mode; }
    r.what = shape + " in " + r.where;
    if (b && b->note && b->note[0]) r.what += ". " + std::string(b->note);
    r.what += " (a possibility from its frequency and width, not an identification)";
}

std::vector<DiscoveryRow> DiscoveryTable::rows() const {
    std::vector<DiscoveryRow> out;
    for (const auto& e : e_) {
        DiscoveryRow r;
        r.hz = e.hz; r.snrDb = e.maxSnr; r.widthHz = e.width; r.seen = e.seen; r.passes = std::max(1, passes_);
        r.activeNow = passes_ > 0 ? e.lastPass == passes_ - 1 : e.lastPass == 0;
        describeSignal(r);
        r.score = r.snrDb + 4.0 * std::min(e.seen, 5) + (r.known ? 8.0 : 0.0) + (r.widthHz < 120e3 ? 4.0 : -4.0) + (r.activeNow ? 3.0 : 0.0);
        out.push_back(r);
    }
    std::sort(out.begin(), out.end(), [](const DiscoveryRow& a, const DiscoveryRow& b) { return a.score > b.score; });
    return out;
}

} // namespace orl
