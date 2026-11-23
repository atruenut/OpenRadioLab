// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "../src/io/WavWriter.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace orl;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static uint32_t r32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static int16_t r16(const uint8_t* p) { return (int16_t)(p[0] | (p[1] << 8)); }
static std::vector<uint8_t> slurp(const char* path) { std::vector<uint8_t> v; FILE* f = std::fopen(path, "rb"); if (!f) return v; uint8_t b[4096]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) v.insert(v.end(), b, b + n); std::fclose(f); return v; }
int main() {
    const char* path = "/tmp/orl_test.wav";
    std::printf("[WAV file: stereo and mono input]\n");
    WavWriter w; std::string err;
    CHECK(w.start(path, 48000, 1u << 30, err), "start: %s", err.c_str());
    std::vector<float> st(2 * 4800), mono(4800);
    for (int k = 0; k < 4800; ++k) { st[2 * k] = 0.5f * std::sin(0.1f * k); st[2 * k + 1] = -0.25f; mono[k] = 0.75f; }
    w.write(st.data(), 4800, 2);
    w.write(mono.data(), 4800, 1);
    w.stop(); w.stop();
    const auto f = slurp(path);
    CHECK(f.size() == 44 + 9600 * 4, "size %zu", f.size());
    CHECK(!std::memcmp(f.data(), "RIFF", 4) && !std::memcmp(f.data() + 8, "WAVEfmt ", 8) && !std::memcmp(f.data() + 36, "data", 4), "chunk ids");
    CHECK(r32(&f[4]) == f.size() - 8 && r32(&f[40]) == 9600 * 4, "chunk sizes %u %u", r32(&f[4]), r32(&f[40]));
    CHECK(r16(&f[22]) == 2 && r32(&f[24]) == 48000 && r16(&f[34]) == 16 && r32(&f[28]) == 192000, "format fields");
    CHECK(r16(&f[44 + 4 * 100 + 2]) == (int16_t)std::lrint(-0.25f * 32767), "right sample");
    CHECK(std::abs(r16(&f[44 + 4 * 50]) - (int16_t)std::lrint(0.5f * std::sin(5.0f) * 32767)) <= 1, "left sample");
    CHECK(r16(&f[44 + 4 * 4800]) == r16(&f[44 + 4 * 4800 + 2]) && r16(&f[44 + 4 * 4800]) == (int16_t)std::lrint(0.75f * 32767), "mono duplicated to both ears");
    CHECK(std::fabs(w.seconds() - 0.2) < 1e-9, "duration %.3f", w.seconds());
    std::printf("[Clipping and the size limit]\n");
    WavWriter c; c.start(path, 48000, 4000, err);          // limit counts audio bytes
    std::vector<float> big(2 * 2000, 3.0f);
    c.write(big.data(), 2000, 2);
    c.stop();
    const auto g = slurp(path);
    CHECK(r16(&g[44]) == 32767, "no clipping to full scale");
    CHECK(c.hitLimit() && c.frames() == 1000 && g.size() == 44 + 4000, "limit: frames %llu, size %zu", (unsigned long long)c.frames(), g.size());
    std::remove(path);
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL WAV TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
