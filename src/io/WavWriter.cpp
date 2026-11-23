// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "WavWriter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace orl {

static void p32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void p16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

void WavWriter::writeHeader() {
    uint8_t h[44];
    const uint32_t dataBytes = (uint32_t)(frames_ * 4);
    std::memcpy(h, "RIFF", 4); p32(h + 4, 36 + dataBytes); std::memcpy(h + 8, "WAVEfmt ", 8);
    p32(h + 16, 16); p16(h + 20, 1); p16(h + 22, 2); p32(h + 24, rate_); p32(h + 28, rate_ * 4); p16(h + 32, 4); p16(h + 34, 16);
    std::memcpy(h + 36, "data", 4); p32(h + 40, dataBytes);
    std::fseek(f_, 0, SEEK_SET);
    std::fwrite(h, 1, sizeof h, f_);
}

bool WavWriter::start(const std::string& path, uint32_t rate, uint64_t maxBytes, std::string& err) {
    stop();
    Lock l(m_);
    f_ = std::fopen(path.c_str(), "wb");
    if (!f_) { err = "Could not create " + path; return false; }
    path_ = path; rate_ = rate; frames_ = 0; limit_ = false;
    maxFrames_ = std::min<uint64_t>(maxBytes / 4, 0xFFFFFFFFull / 4 - 16);
    writeHeader();                                        // placeholder, rewritten when finished
    active_ = true;
    return true;
}

void WavWriter::write(const float* a, size_t n, int channels) {
    if (!active_) return;
    Lock l(m_);
    if (!f_) return;
    if (frames_ + n > maxFrames_) { n = (size_t)(maxFrames_ - frames_); limit_ = true; active_ = false; }
    std::vector<uint8_t> b(n * 4);
    auto s16 = [](float v) { return (int16_t)std::lrint(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f); };
    for (size_t k = 0; k < n; ++k) {
        const int16_t L = s16(channels == 2 ? a[2 * k] : a[k]), R = s16(channels == 2 ? a[2 * k + 1] : a[k]);
        p16(&b[4 * k], (uint16_t)L); p16(&b[4 * k + 2], (uint16_t)R);
    }
    if (n && std::fwrite(b.data(), 1, b.size(), f_) == b.size()) frames_ += n;
}

void WavWriter::stop() {
    Lock l(m_);
    active_ = false;
    if (!f_) return;
    std::fseek(f_, 0, SEEK_END);
    writeHeader();
    std::fclose(f_);
    f_ = nullptr;
}

} // namespace orl
