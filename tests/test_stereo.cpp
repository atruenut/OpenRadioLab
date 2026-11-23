// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Stereo FM: a synthetic broadcast with different tones in the left and right channels.
#include "../src/dsp/Demodulator.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace orl;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const double kPi = 3.14159265358979323846;

static double tone(const std::vector<float>& x, int ch, int nch, double hz, size_t skipFrames) {
    double re = 0, im = 0; size_t n = 0;
    for (size_t k = skipFrames; k * nch + ch < x.size(); ++k, ++n) {
        const double a = 2 * kPi * hz * k / 48000.0;
        re += x[k * nch + ch] * std::cos(a); im += x[k * nch + ch] * std::sin(a);
    }
    return 2.0 * std::sqrt(re * re + im * im) / (double)n;
}
static double db(double v) { return 20 * std::log10(v + 1e-9); }

// Build the stereo multiplex and FM-modulate it (75 kHz deviation = 1.0).
static std::vector<float> makeIq(double fs, double secs, double lHz, double rHz, bool pilot, bool mono) {
    const size_t n = (size_t)(fs * secs);
    std::vector<float> iq(2 * n);
    double ph = 0;
    for (size_t k = 0; k < n; ++k) {
        const double t = k / fs;
        const double L = 0.8 * std::sin(2 * kPi * lHz * t), R = mono ? L : 0.8 * std::sin(2 * kPi * rHz * t);
        const double phi = 2 * kPi * 19000.0 * t;
        double m = 0.45 * (L + R) + 0.45 * (L - R) * std::sin(2 * phi);
        if (pilot) m += 0.09 * std::sin(phi);
        ph += 2 * kPi * 75000.0 * m / fs;
        iq[2 * k] = (float)(0.5 * std::cos(ph)); iq[2 * k + 1] = (float)(0.5 * std::sin(ph));
    }
    return iq;
}

static std::vector<float> run(Demodulator& dm, const std::vector<float>& iq) {
    std::vector<float> out;
    const size_t blk = 8192, n = iq.size() / 2;
    for (size_t i = 0; i < n; i += blk) dm.process(iq.data() + 2 * i, std::min(blk, n - i), out);
    return out;
}

static void testStereo() {
    std::printf("[Stereo: 1 kHz left, 3 kHz right]\n");
    const double fs = 2048000;
    Demodulator dm; dm.setSampleRate(fs); dm.setMode(Demodulator::Mode::WFM, 0); dm.setOffsetHz(0); dm.setVolume(0.7f); dm.setStereo(true);
    const auto out = run(dm, makeIq(fs, 1.5, 1000, 3000, true, false));
    CHECK(dm.outputChannels() == 2, "expected 2 channels");
    CHECK(dm.stereoLocked(), "pilot not locked");
    const size_t skip = 30000;
    const double l1 = tone(out, 0, 2, 1000, skip), l3 = tone(out, 0, 2, 3000, skip);
    const double r1 = tone(out, 1, 2, 1000, skip), r3 = tone(out, 1, 2, 3000, skip);
    std::printf("  left : 1 kHz %.1f dB, 3 kHz %.1f dB\n  right: 1 kHz %.1f dB, 3 kHz %.1f dB\n", db(l1), db(l3), db(r1), db(r3));
    CHECK(db(l1) - db(l3) > 25, "left channel leaks the right tone (separation %.1f dB)", db(l1) - db(l3));
    CHECK(db(r3) - db(r1) > 25, "right channel leaks the left tone (separation %.1f dB)", db(r3) - db(r1));
    CHECK(l1 > 0.05 && r3 > 0.03, "channels too quiet");
}

static void testMonoStation() {
    std::printf("[A mono station (no pilot) stays mono even with stereo enabled]\n");
    const double fs = 2048000;
    Demodulator dm; dm.setSampleRate(fs); dm.setMode(Demodulator::Mode::WFM, 0); dm.setOffsetHz(0); dm.setVolume(0.7f); dm.setStereo(true);
    const auto out = run(dm, makeIq(fs, 1.0, 1000, 1000, false, true));
    CHECK(!dm.stereoLocked(), "locked onto a pilot that is not there");
    const size_t skip = 30000;
    const double l = tone(out, 0, 2, 1000, skip), r = tone(out, 1, 2, 1000, skip);
    std::printf("  L %.1f dB, R %.1f dB\n", db(l), db(r));
    CHECK(std::fabs(db(l) - db(r)) < 0.5, "L and R differ on a mono station");
    CHECK(l > 0.05, "mono station is silent");
}

static void testStereoOffMatchesMono() {
    std::printf("[Stereo off: one channel, same level as before]\n");
    const double fs = 2048000;
    Demodulator dm; dm.setSampleRate(fs); dm.setMode(Demodulator::Mode::WFM, 0); dm.setOffsetHz(0); dm.setVolume(0.7f);
    const auto iq = makeIq(fs, 1.0, 1000, 1000, true, true);
    const auto mono = run(dm, iq);
    CHECK(dm.outputChannels() == 1, "expected 1 channel");
    Demodulator ds; ds.setSampleRate(fs); ds.setMode(Demodulator::Mode::WFM, 0); ds.setOffsetHz(0); ds.setVolume(0.7f); ds.setStereo(true);
    const auto st = run(ds, iq);
    const double a = tone(mono, 0, 1, 1000, 30000), b = tone(st, 0, 2, 1000, 30000);
    std::printf("  mono %.1f dB, stereo-decoder left %.1f dB (same L=R programme)\n", db(a), db(b));
    CHECK(std::fabs(db(a) - db(b)) < 1.5, "levels differ by %.1f dB", db(a) - db(b));
}

int main() {
    testStereo();
    testMonoStation();
    testStereoOffMatchesMono();
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL STEREO TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
