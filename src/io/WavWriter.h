// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Saves the audio you hear as a standard 16-bit stereo WAV file (plays in any media player).
#pragma once

#include "../Sync.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace orl {

class WavWriter {
public:
    ~WavWriter() { stop(); }
    bool start(const std::string& path, uint32_t rate, uint64_t maxBytes, std::string& err);
    // channels = 1: mono samples (duplicated to both ears); 2: interleaved L,R frames. Any thread.
    void write(const float* audio, size_t frames, int channels);
    void stop();                                          // finishes the header; safe to call twice
    bool active() const { return active_; }
    bool hitLimit() const { return limit_; }
    uint64_t frames() const { return frames_; }
    double seconds() const { return rate_ ? (double)frames_ / rate_ : 0.0; }
    const std::string& path() const { return path_; }

private:
    void writeHeader();
    mutable Mutex m_;
    FILE* f_ = nullptr;
    std::string path_;
    uint32_t rate_ = 0;
    uint64_t frames_ = 0, maxFrames_ = 0;
    volatile bool active_ = false, limit_ = false;
};

} // namespace orl
