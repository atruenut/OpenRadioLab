// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// ADS-B: published example frames (from "The 1090 MHz Riddle") and a synthetic over-the-air signal.
#include "../src/dsp/AdsbDecoder.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace orl;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void hex(const char* s, uint8_t* out) { for (int i = 0; i < 14; ++i) { unsigned v; std::sscanf(s + 2 * i, "%2x", &v); out[i] = (uint8_t)v; } }

// Modulate a frame: preamble pulses and pulse-position data bits at 1 Mbit/s, sampled at fs.
static void modulate(const uint8_t* m, double fs, double amp, std::vector<float>& iq, size_t startSample, double phase) {
    const double spc = fs / 1e6;
    auto pulse = [&](double t0, double t1) {
        for (size_t k = (size_t)(startSample + t0 * spc); k < (size_t)(startSample + t1 * spc) && 2 * k + 1 < iq.size(); ++k) {
            iq[2 * k] += (float)(amp * std::cos(phase)); iq[2 * k + 1] += (float)(amp * std::sin(phase));
        }
    };
    pulse(0, 0.5); pulse(1, 1.5); pulse(3.5, 4.0); pulse(4.5, 5.0);
    for (int b = 0; b < 112; ++b) {
        const bool one = (m[b >> 3] >> (7 - (b & 7))) & 1;
        pulse(8 + b + (one ? 0 : 0.5), 8 + b + (one ? 0.5 : 1.0));
    }
}

int main() {
    uint8_t id[14], ev[14], od[14], vel[14];
    hex("8D4840D6202CC371C32CE0576098", id);
    hex("8D40621D58C382D690C8AC2863A7", ev);
    hex("8D40621D58C386435CC412692AD6", od);
    hex("8D485020994409940838175B284F", vel);

    std::printf("[CRC]\n");
    CHECK(adsbCrcOk(id) && adsbCrcOk(ev) && adsbCrcOk(od) && adsbCrcOk(vel), "published frames fail the CRC");
    uint8_t bad[14]; std::memcpy(bad, id, 14); bad[5] ^= 0x10;
    CHECK(!adsbCrcOk(bad), "damaged frame passes the CRC");

    std::printf("[Decoding the published frames]\n");
    AdsbDecoder d;
    CHECK(d.handleFrame(id, 1.0), "identity frame refused");
    d.handleFrame(od, 2.0); d.handleFrame(ev, 3.0); d.handleFrame(vel, 3.5);
    auto a = d.aircraft(60);
    CHECK(a.size() == 3, "expected 3 aircraft, got %zu", a.size());
    for (auto& x : a) {
        if (x.icao == 0x4840D6) CHECK(x.callsign == "KLM1023", "callsign '%s'", x.callsign.c_str());
        if (x.icao == 0x40621D) {
            CHECK(x.hasAlt && x.altFt == 38000, "altitude %d", x.altFt);
            CHECK(x.hasPos && std::fabs(x.lat - 52.2572) < 0.001 && std::fabs(x.lon - 3.91937) < 0.001, "position %.5f %.5f", x.lat, x.lon);
        }
        if (x.icao == 0x485020) CHECK(x.hasVel && std::fabs(x.speedKt - 159.2) < 0.5 && std::fabs(x.headingDeg - 182.88) < 0.1 && x.vrateFpm == -832, "velocity %.1f kt %.2f deg %d fpm", x.speedKt, x.headingDeg, x.vrateFpm);
    }
    CHECK(adsbNL(0) == 59 && adsbNL(52.0) == 36 && adsbNL(88) == 1, "NL table");

    std::printf("[Over the air: noisy I/Q at 2.048 and 2.4 MS/s, two frames, arbitrary carrier phase]\n");
    for (double fs : {2048000.0, 2400000.0}) {
        std::mt19937 rng(7); std::normal_distribution<double> nd(0, 0.004);
        std::vector<float> iq((size_t)(fs * 0.01) * 2);
        for (auto& x : iq) x = (float)nd(rng);
        modulate(od, fs, 0.08, iq, (size_t)(fs * 0.001) + 3, 0.7);
        modulate(ev, fs, 0.05, iq, (size_t)(fs * 0.004) + 1, 2.1);
        AdsbDecoder r; r.setRate(fs);
        for (size_t p = 0; p < iq.size(); p += 8192) r.process(iq.data() + p, std::min<size_t>(4096, (iq.size() - p) / 2));
        auto got = r.aircraft(60);
        auto st = r.stats();
        std::printf("  %.3f MS/s: %lu frames, %lu damaged, %lu candidates, %zu aircraft\n", fs / 1e6, st.frames, st.bad, st.candidates, got.size());
        CHECK(st.frames == 2, "frames decoded: %lu", st.frames);
        CHECK(got.size() == 1 && got[0].hasPos && std::fabs(got[0].lat - 52.2572) < 0.001, "position from the air");
    }
    std::printf("[Stress: 200 frames at random alignments and strengths]\n");
    for (double fs : {2048000.0, 2400000.0}) {
        std::mt19937 rng(11); std::normal_distribution<double> nd(0, 0.004); std::uniform_real_distribution<double> ud(0, 1);
        const size_t per = (size_t)(fs * 0.0004);                      // one frame every 400 us
        std::vector<float> iq(per * 200 * 2);
        for (auto& x : iq) x = (float)nd(rng);
        int sent = 0;
        for (int k = 0; k < 200; ++k) {
            uint8_t f[14]; std::memcpy(f, k % 2 ? ev : od, 14);
            f[1] = (uint8_t)k;                                          // a different aircraft each time: fix the CRC
            f[11] = f[12] = f[13] = 0;
            { uint32_t r = 0; for (int i = 0; i < 11; ++i) { r ^= (uint32_t)f[i] << 16; for (int b = 0; b < 8; ++b) { r <<= 1; if (r & 0x1000000) r ^= 0x1FFF409; } } r &= 0xFFFFFF; f[11] = (uint8_t)(r >> 16); f[12] = (uint8_t)(r >> 8); f[13] = (uint8_t)r; }
            modulate(f, fs, 0.03 + 0.1 * ud(rng), iq, k * per + (size_t)(ud(rng) * 40), ud(rng) * 6.28);
            ++sent;
        }
        AdsbDecoder r; r.setRate(fs);
        for (size_t p = 0; p < iq.size(); p += 8192) r.process(iq.data() + p, std::min<size_t>(4096, (iq.size() - p) / 2));
        const unsigned long got = r.stats().frames;
        std::printf("  %.3f MS/s: decoded %lu of %d\n", fs / 1e6, got, sent);
        CHECK(got >= 180, "only %lu of %d decoded", got, sent);
    }
    std::printf("[Noise alone gives nothing]\n");
    {
        std::mt19937 rng(3); std::normal_distribution<double> nd(0, 0.01);
        std::vector<float> iq(2 * 400000); for (auto& x : iq) x = (float)nd(rng);
        AdsbDecoder r; r.setRate(2048000);
        for (size_t p = 0; p < iq.size(); p += 8192) r.process(iq.data() + p, std::min<size_t>(4096, (iq.size() - p) / 2));
        CHECK(r.stats().frames == 0 && r.aircraft(60).empty(), "decoded from noise");
    }
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL ADS-B TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
