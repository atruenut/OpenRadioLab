// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// The FM band scanner, driven exactly the way the UI drives it, against the simulator.
#include "../src/dsp/BandScanner.h"
#include "../src/sdr/SimulatedSDR.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace orl;

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void testSimulatorSweep() {
    std::printf("[Sweep the simulator's FM band the way the UI does]\n");
    SimulatedSDR sim; std::string err; sim.open(err);
    SpectrumProcessor sp; sp.configure(2048, WindowType::Hann); sp.setLinesPerSecond(1e9);   // one frame per FFT: offline
    const double fs = 2.048e6;
    BandScanner sc; sc.begin(BandScanner::fmPlan(fs, 2048));
    std::printf("  hops: %d\n", sc.hops());
    std::vector<float> blk(2 * 2048 * 4);
    double t = 0;
    int guard = 0;
    while (!sc.done() && guard++ < 100000) {
        sp.setTuning(sc.centre(), fs);
        bool hopDone = false;
        while (!hopDone && guard++ < 100000) {
            sim.renderBlock(blk.data(), 2048 * 4, sc.centre(), fs, t, 25.0);
            t += 2048 * 4 / fs;
            sp.pushSamples(blk.data(), 2048 * 4);
            std::vector<SpectrumFrame> fr; sp.drain(fr);
            for (auto& f : fr) if (sc.addFrame(f)) { hopDone = true; break; }
        }
    }
    CHECK(sc.done(), "scan did not finish");
    const ScanReport r = sc.result();
    std::printf("  floor %.1f dBFS, peak %.1f dBFS at %.3f MHz, %zu stations:\n", r.floorDb, r.peakDb, r.peakHz / 1e6, r.stations.size());
    for (auto& s : r.stations) std::printf("    %.1f MHz  %.1f dBFS  (+%.1f dB)\n", s.hz / 1e6, s.levelDb, s.snrDb);

    const double expect[] = {88.1, 89.5, 91.1, 93.3, 94.9, 96.5, 98.3, 100.5, 101.9, 103.7, 105.3, 106.1, 107.3};
    int found = 0;
    for (double e : expect) {
        bool hit = false;
        for (auto& s : r.stations) if (std::fabs(s.hz - e * 1e6) < 1e3) hit = true;
        if (hit) ++found; else CHECK(false, "missed the simulated station at %.1f MHz", e);
    }
    const int falsePos = (int)r.stations.size() - found;
    std::printf("  found %d of 13, false positives %d\n", found, falsePos);
    CHECK(found == 13, "only %d of 13 stations found", found);
    CHECK(falsePos == 0, "%d false positives", falsePos);
    // The strongest simulated stations have amplitude 0.22: 91.1, 96.5, 100.5, 105.3.
    if (!r.stations.empty()) {
        const double top = r.stations[0].hz / 1e6;
        CHECK(top == 91.1 || top == 96.5 || top == 100.5 || top == 105.3, "strongest station %.1f is not one of the strong ones", top);
    }
}

static void testNoiseOnly() {
    std::printf("[Pure noise: no stations, and a clear verdict]\n");
    BandScanner sc; sc.begin(BandScanner::fmPlan());
    std::mt19937 rng(7);
    std::normal_distribution<double> nd(0.0, 1.0);
    const double fs = 2.048e6;
    while (!sc.done()) {
        // 2048-bin frames of chi-square-like noise around -62 dBFS, as a real quiet band looks
        for (int k = 0; k < 40 && !sc.done(); ++k) {
            SpectrumFrame f; f.centerHz = sc.centre(); f.sampleRate = fs; f.db.resize(2048);
            for (auto& v : f.db) { const double a = nd(rng), b = nd(rng); v = (float)(-62.0 + 10 * std::log10((a * a + b * b) / 2.0 + 1e-9)); }
            if (sc.addFrame(f)) break;
        }
    }
    const ScanReport r = sc.result();
    std::printf("  stations %zu, floor %.1f dBFS, best channel margin %.1f dB\n", r.stations.size(), r.floorDb, r.strongestSnrDb);
    CHECK(r.stations.empty(), "false stations in pure noise");
    CHECK(r.strongestSnrDb < 4.0, "noise-only margin %.1f dB is too high", r.strongestSnrDb);
}

static void testStaleFramesIgnored() {
    std::printf("[Frames from the wrong hop are ignored]\n");
    BandScanner sc; sc.begin(BandScanner::fmPlan());
    SpectrumFrame f; f.centerHz = sc.centre() + 500000; f.sampleRate = 2.048e6; f.db.assign(2048, -60.0f);
    bool adv = false;
    for (int i = 0; i < 100; ++i) adv |= sc.addFrame(f);
    CHECK(!adv && sc.hop() == 0, "scanner advanced on stale frames");
}

int main() {
    testSimulatorSweep();
    testNoiseOnly();
    testStaleFramesIgnored();
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL SCANNER TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
