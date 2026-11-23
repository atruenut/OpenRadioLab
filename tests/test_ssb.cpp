// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// SSB and CW: a synthetic tone placed above/below a "suppressed carrier" frequency must come out at the
// right audio pitch in the right sideband mode, and be rejected in the opposite one.
#include "../src/dsp/Demodulator.h"
#include "../src/dsp/FFT.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace orl;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const double fs = 2048000.0, kOff = 150000.0;

// complex tone at (kOff + toneHz) relative to the capture centre
static std::vector<float> tone(double toneHz, double secs, double amp) {
    const size_t n = (size_t)(fs * secs) / 16384 * 16384;
    std::vector<float> iq(2 * n);
    for (size_t k = 0; k < n; ++k) {
        const double ph = 2.0 * 3.14159265358979 * (kOff + toneHz) * k / fs;
        iq[2 * k] = (float)(amp * std::cos(ph)); iq[2 * k + 1] = (float)(amp * std::sin(ph));
    }
    return iq;
}
static std::vector<float> run(Demodulator::Mode m, const std::vector<float>& iq) {
    Demodulator d; d.setSampleRate(fs); d.setOffsetHz(kOff); d.setVolume(0.7f);
    d.setMode(m, m == Demodulator::Mode::CW ? 400.0 : Demodulator::kSsbBandwidth);
    std::vector<float> a;
    for (size_t p = 0; p + 16384 <= iq.size() / 2; p += 16384) d.process(iq.data() + 2 * p, 16384, a);
    return a;
}
// peak frequency and RMS of the last second
static void analyse(const std::vector<float>& a, double& peakHz, double& rms) {
    const int N = 32768; peakHz = 0; rms = 0;
    if (a.size() < (size_t)N + 9600) return;
    std::vector<cf> x(N); const auto w = makeWindow(WindowType::Hann, N);
    const size_t s = a.size() - N;
    for (int i = 0; i < N; ++i) { x[i].r = a[s + i] * w[i]; x[i].i = 0; rms += (double)a[s + i] * a[s + i]; }
    rms = std::sqrt(rms / N);
    FFT f(N); f.forward(x.data());
    double best = 0; int bk = 0;
    for (int k = 2; k < N / 2; ++k) { const double p = (double)x[k].r * x[k].r + (double)x[k].i * x[k].i; if (p > best) { best = p; bk = k; } }
    peakHz = bk * 48000.0 / N;
}

// level (dB) of the audio at one frequency, relative to the strongest bin
static double binDb(const std::vector<float>& a, double hz) {
    const int N = 32768;
    std::vector<cf> x(N); const auto w = makeWindow(WindowType::Hann, N);
    const size_t s0 = a.size() - N;
    for (int i = 0; i < N; ++i) { x[i].r = a[s0 + i] * w[i]; x[i].i = 0; }
    FFT f(N); f.forward(x.data());
    auto P = [&](int k) { return (double)x[k].r * x[k].r + (double)x[k].i * x[k].i; };
    double best = 1e-30; for (int k = 2; k < N / 2; ++k) best = std::max(best, P(k));
    const int c = (int)std::lround(hz * N / 48000.0); double m = 1e-30;
    for (int k = c - 3; k <= c + 3; ++k) m = std::max(m, P(k));
    return 10.0 * std::log10(m / best);
}
static std::vector<float> two(double aHz, double bHz) {     // two equal tones
    auto x = tone(aHz, 1.5, 0.03), y = tone(bHz, 1.5, 0.03);
    for (size_t i = 0; i < x.size(); ++i) x[i] += y[i];
    return x;
}

int main() {
    double hz, rms;
    std::printf("[USB keeps +1000 Hz and rejects -2000 Hz]\n");
    auto both = two(+1000, -2000);
    auto u = run(Demodulator::Mode::USB, both);
    analyse(u, hz, rms);
    CHECK(std::fabs(hz - 1000) < 15, "USB pitch %.1f", hz);
    CHECK(rms > 0.05, "USB too quiet (%.4f)", rms);
    CHECK(binDb(u, 2000) < -35, "USB leaked the lower sideband (%.1f dB)", binDb(u, 2000));
    std::printf("[LSB keeps -1000 Hz and rejects +2000 Hz]\n");
    auto both2 = two(-1000, +2000);
    auto l = run(Demodulator::Mode::LSB, both2);
    analyse(l, hz, rms);
    CHECK(std::fabs(hz - 1000) < 15, "LSB pitch %.1f", hz);
    CHECK(binDb(l, 2000) < -35, "LSB leaked the upper sideband (%.1f dB)", binDb(l, 2000));
    std::printf("[CW: a carrier on the tuned frequency becomes a 700 Hz tone]\n");
    analyse(run(Demodulator::Mode::CW, tone(0, 1.5, 0.05)), hz, rms);
    CHECK(std::fabs(hz - Demodulator::kCwTone) < 15, "CW pitch %.1f", hz);
    CHECK(rms > 0.05, "CW too quiet (%.4f)", rms);
    std::printf("[AGC: a 20 dB weaker tone still comes out at a usable loudness]\n");
    double rmsWeak;
    analyse(run(Demodulator::Mode::USB, tone(+1000, 1.5, 0.005)), hz, rmsWeak);
    CHECK(rmsWeak > 0.04, "AGC left weak signal too quiet (%.4f)", rmsWeak);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL SSB TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
