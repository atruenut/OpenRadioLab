// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "../src/dsp/SpectrumCompare.h"
#include <cmath>
#include <cstdio>
#include <random>
using namespace orl;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const int N = 2048; static const double fs = 2.048e6, fc = 98e6;
static std::vector<float> make(std::initializer_list<std::pair<double, double>> peaks, unsigned seed) {
    std::mt19937 rng(seed); std::normal_distribution<double> nd(0, 1.0);
    std::vector<float> v(N);
    for (auto& x : v) x = (float)(-65 + nd(rng));
    v[N / 2] = -20;                                         // DC spike: must be ignored
    for (auto& p : peaks) {
        const int c = N / 2 + (int)std::lround((p.first - fc) / (fs / N));
        for (int j = -6; j <= 6; ++j) v[c + j] = std::max<float>(v[c + j], (float)(p.second - std::abs(j) * 3));   // 13-bin-wide hump
    }
    return v;
}

int main() {
    std::printf("[Peaks]\n");
    auto a = make({{98.3e6, -40}, {98.6e6, -45}}, 1);
    auto pk = findPeaks(a, fc, fs);
    CHECK(pk.size() == 2, "expected 2 peaks, found %zu", pk.size());
    CHECK(pk.size() == 2 && std::fabs(pk[0].hz - 98.3e6) < 2e3 && std::fabs(pk[1].hz - 98.6e6) < 2e3, "peak positions");
    std::printf("[Compare: one stays, one strengthens, one vanishes, one appears]\n");
    auto b1 = make({{98.3e6, -40}, {98.6e6, -45}}, 2);
    auto b2 = make({{98.3e6, -40}, {98.6e6, -45}, {98.9e6, -50}}, 3);
    auto before = make({{98.3e6, -40}, {98.6e6, -45}, {97.9e6, -42}}, 4);
    auto after = make({{98.3e6, -40.5}, {98.6e6, -38}, {98.9e6, -50}}, 5);
    SpecDiff d = compareSpectra(before, after, fc, fs, fc, fs);
    std::printf("%s\n", describeDiff(d).c_str());
    CHECK(d.comparable, "not comparable");
    CHECK(d.appeared.size() == 1 && std::fabs(d.appeared[0].hz - 98.9e6) < 2e3, "appeared wrong (%zu)", d.appeared.size());
    CHECK(d.gone.size() == 1 && std::fabs(d.gone[0].hz - 97.9e6) < 2e3, "gone wrong (%zu)", d.gone.size());
    CHECK(d.changed.size() == 1 && std::fabs(d.changed[0].hz - 98.6e6) < 2e3 && d.changed[0].afterDb - d.changed[0].beforeDb > 5, "changed wrong (%zu)", d.changed.size());
    CHECK(d.steady == 1, "steady %d", d.steady);
    SpecDiff same = compareSpectra(b1, b1, fc, fs, fc, fs);
    CHECK(same.appeared.empty() && same.gone.empty() && same.changed.empty(), "identical snapshots differ");
    CHECK(describeDiff(same).find("No differences") != std::string::npos, "no-difference message");
    (void)b2;
    std::printf("[Different bands are refused]\n");
    SpecDiff bad = compareSpectra(before, after, fc, fs, fc + 1e6, fs);
    CHECK(!bad.comparable && !bad.reason.empty(), "different centres accepted");
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL COMPARE TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
