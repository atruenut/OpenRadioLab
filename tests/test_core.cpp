// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Native (non-Windows-specific) tests for the portable core:
//   FFT correctness, window sanity, the simulator + spectrum pipeline.
// Build/run:  make test
#include "../src/dsp/FFT.h"
#include "../src/dsp/SpectrumProcessor.h"
#include "../src/sdr/SimulatedSDR.h"
#include "../src/Util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

using namespace orl;

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void testFFTvsDFT() {
    std::printf("[FFT vs naive DFT]\n");
    const int N = 64;
    std::vector<cf> x(N), ref(N);
    std::srand(1);
    for (int i = 0; i < N; ++i) { x[i].r = (float)std::rand() / RAND_MAX - 0.5f; x[i].i = (float)std::rand() / RAND_MAX - 0.5f; }
    for (int k = 0; k < N; ++k) {
        double sr = 0, si = 0;
        for (int n = 0; n < N; ++n) {
            double a = -2.0 * M_PI * k * n / N;
            sr += x[n].r * std::cos(a) - x[n].i * std::sin(a);
            si += x[n].r * std::sin(a) + x[n].i * std::cos(a);
        }
        ref[k].r = (float)sr; ref[k].i = (float)si;
    }
    FFT fft(N);
    fft.forward(x.data());
    double maxErr = 0;
    for (int k = 0; k < N; ++k)
        maxErr = std::max(maxErr, (double)std::fabs(x[k].r - ref[k].r) + std::fabs(x[k].i - ref[k].i));
    std::printf("  max abs error = %.2e\n", maxErr);
    CHECK(maxErr < 1e-4, "FFT disagrees with DFT (err %g)", maxErr);
}

static void testToneBin() {
    std::printf("[Tone lands in the expected bin through SpectrumProcessor]\n");
    const int N = 1024;
    const double fs = 1.024e6, center = 100e6, tone = +200e3;     // +200 kHz offset
    SpectrumProcessor sp;
    sp.configure(N, WindowType::Hann);
    sp.setTuning(center, fs);
    sp.setLinesPerSecond(1000);
    std::vector<float> iq(2 * N * 8);
    for (int n = 0; n < N * 8; ++n) {
        double ph = 2 * M_PI * tone / fs * n;
        iq[2 * n] = (float)(0.5 * std::cos(ph));
        iq[2 * n + 1] = (float)(0.5 * std::sin(ph));
    }
    sp.pushSamples(iq.data(), N * 8);
    std::vector<SpectrumFrame> fr;
    sp.drain(fr);
    CHECK(!fr.empty(), "no frames produced");
    if (fr.empty()) return;
    const auto& db = fr.back().db;
    int pk = (int)(std::max_element(db.begin(), db.end()) - db.begin());
    int expect = N / 2 + (int)std::lround(tone / (fs / N));
    std::printf("  peak bin %d (expected %d), level %.1f dBFS\n", pk, expect, db[pk]);
    CHECK(std::abs(pk - expect) <= 1, "peak bin %d != %d", pk, expect);
    // Complex amplitude 0.5 => about -6 dBFS; Hann scalloping can cost up to ~1.5 dB.
    CHECK(db[pk] > -8.0f && db[pk] < -4.0f, "level %.1f dBFS outside expected -6 +/- 2", db[pk]);
}

struct Peak { double hz; float db; };

static std::vector<Peak> findPeaks(const SpectrumFrame& f, float minAboveNoise, int exclusionBins) {
    std::vector<float> s = f.db;
    std::sort(s.begin(), s.end());
    const float noise = s[s.size() / 5];
    const int N = (int)f.db.size();
    std::vector<Peak> out;
    for (int i = 3; i < N - 3; ++i) {
        if (std::abs(i - N / 2) <= 3) continue;          // skip the DC spike
        float v = f.db[i];
        if (v < noise + minAboveNoise) continue;
        bool isMax = true;
        for (int k = -exclusionBins; k <= exclusionBins && isMax; ++k) {
            int j = i + k;
            if (j >= 0 && j < N && f.db[j] > v) isMax = false;
        }
        if (isMax) out.push_back({f.centerHz + (i - N / 2) * f.sampleRate / N, v});
    }
    return out;
}

static void testSimulatedPipeline() {
    std::printf("[SimulatedSDR -> SpectrumProcessor: tuned to 100.0 MHz, 2.048 MS/s]\n");
    SimulatedSDR sim;
    SpectrumProcessor sp;
    sp.configure(2048, WindowType::Hann);
    sp.setLinesPerSecond(30);
    std::string err;
    CHECK(sim.open(err), "open failed");
    sim.setSampleRate(2048000);
    sim.setCenterFrequency(100000000);
    sim.setGainDb(25);
    sp.setTuning(100e6, 2.048e6);

    double c0 = (double)std::clock() / CLOCKS_PER_SEC, w0 = nowSeconds();
    sim.start([&](const float* iq, size_t n) { sp.pushSamples(iq, n); });
    sleepSeconds(2.5);
    sim.stop();
    double cpu = (double)std::clock() / CLOCKS_PER_SEC - c0, wall = nowSeconds() - w0;
    std::printf("  wall %.2fs, cpu %.2fs (%.0f%% of one core for sim+FFT)\n", wall, cpu, 100.0 * cpu / wall);

    std::vector<SpectrumFrame> frames;
    sp.drain(frames);
    std::printf("  frames: %zu (expect roughly 30/s)\n", frames.size());
    CHECK(frames.size() > 40, "too few frames: %zu", frames.size());
    CHECK(cpu / wall < 0.9, "simulator + DSP uses too much CPU");
    if (frames.empty()) return;

    // Average the last 20 frames in the power domain for stable peaks.
    SpectrumFrame avg = frames.back();
    const size_t use = std::min<size_t>(20, frames.size());
    for (size_t b = 0; b < avg.db.size(); ++b) {
        double p = 0;
        for (size_t k = 0; k < use; ++k) p += std::pow(10.0, frames[frames.size() - 1 - k].db[b] / 10.0);
        avg.db[b] = (float)(10.0 * std::log10(p / use));
    }
    auto peaks = findPeaks(avg, 12.0f, 150);   // +/-150 kHz: merge each ~180 kHz-wide FM signal into one peak
    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.db > b.db; });
    for (size_t i = 0; i < peaks.size() && i < 5; ++i)
        std::printf("  peak %zu: %.3f MHz at %.1f dBFS\n", i + 1, peaks[i].hz / 1e6, peaks[i].db);

    // In view (+/-0.94 MHz usable): 99.0? no station; 100.5 strong, 101.9 weak is at +1.9 MHz => out of view.
    // The only station within +/-0.94 MHz of 100.0 is 100.5 MHz... and 98.3/101.9 are outside.
    bool found = false;
    for (auto& p : peaks) if (std::fabs(p.hz - 100.5e6) < 100e3) found = true;   // FM is ~180 kHz wide: peak bin wanders within it
    CHECK(found, "expected simulated FM station near 100.500 MHz was not found");

    float noise;
    { auto s = avg.db; std::sort(s.begin(), s.end()); noise = s[s.size() / 5]; }
    std::printf("  estimated noise floor: %.1f dBFS\n", noise);
    CHECK(noise > -75 && noise < -45, "noise floor %.1f dBFS outside plausible range", noise);

    // DC spike should exist (realistic hardware artefact).
    float dc = avg.db[avg.db.size() / 2];
    std::printf("  DC bin: %.1f dBFS (%.1f dB above noise)\n", dc, dc - noise);
    CHECK(dc - noise > 8, "expected a visible DC spike");
}

static void testRetuneShowsDifferentStations() {
    std::printf("[Retune to 91.0 MHz: 91.1 MHz station must appear, 100.5 must vanish]\n");
    SimulatedSDR sim;
    std::string err; sim.open(err);
    const double fs = 2.048e6;
    const int N = 2048;
    std::vector<float> blk(16384 * 2);
    SpectrumProcessor sp;
    sp.configure(N, WindowType::Hann);
    sp.setLinesPerSecond(100000);
    sp.setTuning(91.0e6, fs);
    double t = 0;
    for (int i = 0; i < 40; ++i) {
        sim.renderBlock(blk.data(), 16384, 91.0e6, fs, t, 25.0);
        sp.pushSamples(blk.data(), 16384);
        t += 16384 / fs;
    }
    std::vector<SpectrumFrame> fr; sp.drain(fr);
    CHECK(!fr.empty(), "no frames");
    if (fr.empty()) return;
    auto peaks = findPeaks(fr.back(), 12.0f, 150);
    bool has911 = false, has1005 = false;
    for (auto& p : peaks) { if (std::fabs(p.hz - 91.1e6) < 100e3) has911 = true; if (std::fabs(p.hz - 100.5e6) < 100e3) has1005 = true; }
    std::printf("  found 91.1 MHz: %s, found 100.5 MHz: %s\n", has911 ? "yes" : "no", has1005 ? "yes" : "no");
    CHECK(has911, "91.1 MHz station missing");
    CHECK(!has1005, "100.5 MHz station should be out of view");
}

static void testGainClipping() {
    std::printf("[High gain clips like a real ADC]\n");
    SimulatedSDR sim; std::string err; sim.open(err);
    std::vector<float> blk(16384 * 2);
    sim.renderBlock(blk.data(), 16384, 100e6, 2.048e6, 0.0, 25.0);
    int clipLo = 0; for (float v : blk) if (std::fabs(v) >= 0.999f) ++clipLo;
    sim.renderBlock(blk.data(), 16384, 100e6, 2.048e6, 0.0, 50.0);
    int clipHi = 0; for (float v : blk) if (std::fabs(v) >= 0.999f) ++clipHi;
    std::printf("  clipped samples at 25 dB: %d, at 50 dB: %d (of %zu)\n", clipLo, clipHi, blk.size());
    CHECK(clipHi > clipLo * 10 + 100, "expected far more clipping at 50 dB");
}


static void testFrequencyParsing() {
    std::printf("[Frequency parsing / formatting]\n");
    struct C { const char* in; double hz; bool ok; };
    const C cases[] = {
        {"100.5", 100.5e6, true}, {" 98 MHz ", 98e6, true}, {"145000k", 145e6, true}, {"145000 kHz", 145e6, true},
        {"1.09g", 1.09e9, true}, {"433920000 Hz", 433.92e6, true}, {"100,5", 100.5e6, true}, {"162.475m", 162.475e6, true},
        {"abc", 0, false}, {"", 0, false}, {"-5", 0, false}, {"100 parsecs", 0, false}, {"0", 0, false},
    };
    for (const auto& c : cases) {
        double hz = 0;
        const bool ok = parseFrequencyHz(c.in, hz);
        CHECK(ok == c.ok, "parse(\"%s\") ok=%d expected %d", c.in, ok, c.ok);
        if (ok && c.ok) CHECK(std::fabs(hz - c.hz) < 1e-3, "parse(\"%s\") = %.3f expected %.3f", c.in, hz, c.hz);
    }
    CHECK(formatFrequency(100.5e6) == "100.500 MHz", "fmt 100.5e6 -> %s", formatFrequency(100.5e6).c_str());
    CHECK(formatFrequency(100.123456e6) == "100.123456 MHz", "fmt precise -> %s", formatFrequency(100.123456e6).c_str());
    CHECK(formatFrequency(433.92e6) == "433.920 MHz", "fmt 433.92 -> %s", formatFrequency(433.92e6).c_str());
    CHECK(formatFrequency(1090e6) == "1.090 GHz", "fmt 1090e6 -> %s", formatFrequency(1090e6).c_str());
    CHECK(formatFrequency(580e3) == "580 kHz", "fmt 580e3 -> %s", formatFrequency(580e3).c_str());
    CHECK(formatFrequency(500) == "500 Hz", "fmt 500 -> %s", formatFrequency(500).c_str());
}

int main() {
    testFrequencyParsing();
    testFFTvsDFT();
    testToneBin();
    testSimulatedPipeline();
    testRetuneShowsDifferentStations();
    testGainClipping();
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
