// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "../src/dsp/MorseDecoder.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
using namespace orl;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const char* kM[] = {".-","-...","-.-.","-..",".","..-.","--.","....","..",".---","-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-","..-","...-",".--","-..-","-.--","--.."};
static const char* kD[] = {"-----",".----","..---","...--","....-",".....","-....","--...","---..","----."};

// key a message at the given words per minute, tone at toneHz, with background noise
static std::vector<float> send(const std::string& msg, double wpm, double toneHz, double amp, double noise, unsigned seed) {
    const double fs = 48000, u = 1.2 / wpm;
    std::vector<float> a; std::mt19937 rng(seed); std::normal_distribution<double> nd(0, noise);
    double ph = 0;
    auto tone = [&](double sec, bool on) {
        const size_t n = (size_t)(sec * fs);
        for (size_t i = 0; i < n; ++i) { ph += 2 * 3.14159265358979 * toneHz / fs; a.push_back((float)((on ? amp * std::sin(ph) : 0.0) + nd(rng))); }
    };
    tone(0.8, false);
    for (char ch : msg) {
        if (ch == ' ') { tone(4 * u, false); continue; }        // 3 already added after the letter
        const char* c = ch >= 'A' && ch <= 'Z' ? kM[ch - 'A'] : kD[ch - '0'];
        for (const char* p = c; *p; ++p) { tone(*p == '.' ? u : 3 * u, true); if (p[1]) tone(u, false); }
        tone(3 * u, false);
    }
    tone(1.0, false);
    return a;
}
static std::string decode(const std::vector<float>& a, double* wpm = nullptr) {
    MorseDecoder d; for (size_t i = 0; i < a.size(); i += 480) d.feed(a.data() + i, std::min<size_t>(480, a.size() - i));
    if (wpm) *wpm = d.wpm();
    return d.text();
}
static std::string trim(std::string s) { while (!s.empty() && s.back() == ' ') s.pop_back(); return s; }

int main() {
    double w;
    std::printf("[Clean 15 wpm]\n");
    std::string t = trim(decode(send("CQ CQ DE VE1ABC K", 15, 700, 0.3, 0.001, 1), &w));
    std::printf("  '%s' (%.1f wpm)\n", t.c_str(), w);
    CHECK(t == "CQ CQ DE VE1ABC K", "decoded '%s'", t.c_str());
    CHECK(std::fabs(w - 15) < 3, "wpm estimate %.1f", w);
    std::printf("[Fast 25 wpm]\n");
    t = trim(decode(send("THE QUICK BROWN FOX 73", 25, 700, 0.3, 0.001, 2), &w));
    std::printf("  '%s' (%.1f wpm)\n", t.c_str(), w);
    CHECK(t == "THE QUICK BROWN FOX 73", "decoded '%s'", t.c_str());
    std::printf("[Slow 10 wpm, noisy]\n");
    t = trim(decode(send("TEST DE VE9XYZ", 10, 700, 0.2, 0.03, 3)));
    std::printf("  '%s'\n", t.c_str());
    CHECK(t == "TEST DE VE9XYZ", "decoded '%s'", t.c_str());
    std::printf("[Silence and noise alone produce no text]\n");
    t = decode(send("", 15, 700, 0.3, 0.02, 4));
    CHECK(t.empty(), "invented text from noise: '%s'", t.c_str());
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL MORSE TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
