// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// RDS: a synthetic multiplex carrying a station name, radio text and programme type.
#include "../src/dsp/Demodulator.h"
#include "../src/dsp/RdsDecoder.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace orl;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const double kPi = 3.14159265358979323846;

static int crc10(unsigned d) { int r = 0; for (int i = 15; i >= 0; --i) { int fb = ((r >> 9) & 1) ^ ((d >> i) & 1); r = (r << 1) & 0x3FF; if (fb) r ^= 0x1B9; } return r; }
static const int kOff[4] = {0x0FC, 0x198, 0x168, 0x1B4};
static void putBlock(std::vector<int>& bits, unsigned data, int off) {
    const unsigned w = ((data & 0xFFFF) << 10) | (unsigned)((crc10(data) ^ off) & 0x3FF);
    for (int i = 25; i >= 0; --i) bits.push_back((w >> i) & 1);
}
static void putGroup(std::vector<int>& bits, unsigned pi, unsigned b, unsigned c, unsigned d) {
    putBlock(bits, pi, kOff[0]); putBlock(bits, b, kOff[1]); putBlock(bits, c, kOff[2]); putBlock(bits, d, kOff[3]);
}
static std::vector<int> makeStream(unsigned pi, const std::string& ps, const std::string& rt, int pty, int repeats) {
    std::vector<int> bits;
    std::string r = rt; while (r.size() % 4) r += ' ';
    for (int rep = 0; rep < repeats; ++rep) {
        for (int a = 0; a < 4; ++a)                     // group 0A: name
            putGroup(bits, pi, (0u << 12) | (0u << 11) | (1u << 10) | ((unsigned)pty << 5) | (unsigned)a, 0xE0E0, ((unsigned)ps[a * 2] << 8) | (unsigned)ps[a * 2 + 1]);
        for (size_t a = 0; a * 4 < r.size(); ++a)       // group 2A: text
            putGroup(bits, pi, (2u << 12) | (1u << 10) | ((unsigned)pty << 5) | (unsigned)a, ((unsigned)r[a * 4] << 8) | (unsigned)r[a * 4 + 1], ((unsigned)r[a * 4 + 2] << 8) | (unsigned)r[a * 4 + 3]);
    }
    return bits;
}
// differential + biphase + BPSK on 57 kHz, added to a pilot and noise
static std::vector<float> makeMpx(const std::vector<int>& bits, double fs, double amp, double noise, double carrierPhase, bool invert, unsigned seed) {
    std::vector<int> chips; int prev = 0;
    for (int d : bits) { const int b = d ^ prev; prev = b; chips.push_back(b ? 1 : -1); chips.push_back(b ? -1 : 1); }
    const double spc = fs / 2375.0;
    const size_t n = (size_t)(chips.size() * spc);
    std::vector<float> x(n); std::mt19937 rng(seed); std::normal_distribution<double> nd(0, noise);
    double lp = 0; const double a = 1.0 - std::exp(-2 * kPi * 4500.0 / fs);
    for (size_t k = 0; k < n; ++k) {
        const size_t ci = std::min(chips.size() - 1, (size_t)(k / spc));
        lp += a * ((invert ? -1.0 : 1.0) * chips[ci] - lp);
        const double t = k / fs;
        x[k] = (float)(0.09 * std::sin(2 * kPi * 19000.0 * t) + amp * lp * std::sin(2 * kPi * 57000.0 * t + carrierPhase) + nd(rng)
                       + 0.3 * std::sin(2 * kPi * 1000.0 * t) + 0.2 * std::sin(2 * kPi * 38000.0 * t) * std::sin(2 * kPi * 3000.0 * t));
    }
    return x;
}
static RdsInfo decode(const std::vector<float>& x, double fs) {
    RdsDecoder d; d.setRate(fs);
    for (float v : x) d.process(v);
    return d.info();
}

int main() {
    const double fs = 256000.0;
    const auto bits = makeStream(0xC201, "TESTFM  ", "Now playing: Test Song - The Examples", 5, 6);
    std::printf("[Clean signal]\n");
    RdsInfo r = decode(makeMpx(bits, fs, 0.03, 0.0005, 0.0, false, 1), fs);
    std::printf("  PI %04X  PS '%s'  RT '%s'  PTY %d (%s)  good %u bad %u\n", r.pi, r.ps.c_str(), r.rt.c_str(), r.pty, rdsPtyName(r.pty), r.goodBlocks, r.badBlocks);
    CHECK(r.havePi && r.pi == 0xC201, "PI %04X", r.pi);
    CHECK(r.ps == "TESTFM  ", "PS '%s'", r.ps.c_str());
    CHECK(r.rt == "Now playing: Test Song - The Examples", "RT '%s'", r.rt.c_str());
    CHECK(r.pty == 5 && std::string(rdsPtyName(5)) == "Rock", "PTY %d", r.pty);
    CHECK(r.tp, "traffic flag");
    std::printf("[Inverted carrier phase and an odd phase offset]\n");
    r = decode(makeMpx(bits, fs, 0.03, 0.0005, 1.9, true, 2), fs);
    CHECK(r.havePi && r.pi == 0xC201 && r.ps == "TESTFM  ", "inverted: PI %04X PS '%s'", r.pi, r.ps.c_str());
    std::printf("[Noisy]\n");
    r = decode(makeMpx(bits, fs, 0.03, 0.01, 0.7, false, 3), fs);
    std::printf("  PI %04X  PS '%s'  good %u bad %u\n", r.pi, r.ps.c_str(), r.goodBlocks, r.badBlocks);
    CHECK(r.havePi && r.pi == 0xC201 && r.ps == "TESTFM  ", "noisy: PI %04X PS '%s'", r.pi, r.ps.c_str());
    std::printf("[Starting in the middle of the stream]\n");
    std::vector<int> cut(bits.begin() + 37, bits.end());
    r = decode(makeMpx(cut, fs, 0.03, 0.0005, 0.3, false, 4), fs);
    CHECK(r.havePi && r.pi == 0xC201 && r.ps == "TESTFM  ", "cut: PI %04X PS '%s'", r.pi, r.ps.c_str());
    std::printf("[No RDS: pilot and audio only gives nothing]\n");
    r = decode(makeMpx(bits, fs, 0.0, 0.002, 0.0, false, 5), fs);
    CHECK(!r.havePi && r.ps.empty() && r.rt.empty(), "invented data: PI %d PS '%s'", r.havePi, r.ps.c_str());
    std::printf("[Whole chain: FM-modulated at 2.048 MS/s, through the demodulator]\n");
    {
        const double fsr = 2048000.0;
        const auto mpx = makeMpx(bits, fsr, 0.03, 0.0005, 0.4, false, 6);
        std::vector<float> iq(2 * mpx.size());
        double ph = 0;
        for (size_t k = 0; k < mpx.size(); ++k) { ph += 2 * kPi * 75000.0 * mpx[k] / fsr; iq[2 * k] = (float)(0.5 * std::cos(ph)); iq[2 * k + 1] = (float)(0.5 * std::sin(ph)); }
        Demodulator dm; dm.setSampleRate(fsr); dm.setMode(Demodulator::Mode::WFM, 180000); dm.setOffsetHz(0);
        std::vector<float> out;
        for (size_t i = 0; i < mpx.size(); i += 8192) dm.process(iq.data() + 2 * i, std::min<size_t>(8192, mpx.size() - i), out);
        RdsInfo q = dm.rdsInfo();
        std::printf("  PI %04X  PS '%s'  RT '%s'  good %u bad %u\n", q.pi, q.ps.c_str(), q.rt.c_str(), q.goodBlocks, q.badBlocks);
        CHECK(q.havePi && q.pi == 0xC201 && q.ps == "TESTFM  " && q.rt == "Now playing: Test Song - The Examples", "chain: PI %04X PS '%s' RT '%s'", q.pi, q.ps.c_str(), q.rt.c_str());
        dm.setMode(Demodulator::Mode::NFM, 12000);
        dm.process(iq.data(), 8192, out);
        CHECK(!dm.rdsInfo().havePi, "RDS data should be cleared outside wide FM");
    }
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL RDS TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
