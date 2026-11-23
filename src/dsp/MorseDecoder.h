// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Turns the CW audio (a keyed tone) into text. Adapts to the sending speed. Not thread-safe: the caller locks.
#pragma once

#include <cstddef>
#include <string>

namespace orl {

class MorseDecoder {
public:
    explicit MorseDecoder(double sampleRate = 48000.0, double toneHz = 700.0);
    void reset();
    void feed(const float* mono, size_t n);        // audio, mono
    std::string text() const { return text_; }      // everything decoded so far (trimmed to the last 2000 characters)
    double wpm() const;                             // estimated sending speed, words per minute (PARIS standard)
    bool hearingSignal() const { return signal_; }  // a clear tone is currently well above the background

private:
    void block(double e);
    void symbolDone(bool dash);
    void emitChar();
    void put(char c);

    double fs_, tone_;
    size_t blockLen_, fill_ = 0;
    double s1_ = 0, s2_ = 0, coeff_ = 0;            // Goertzel state
    double peak_ = 0, floor_ = 0;                   // envelope tracking
    bool on_ = false, signal_ = false;
    int warm_ = 0, syms_ = 0;
    int run_ = 0;                                   // blocks in the current on/off state
    double unit_ = 0.08;                            // seconds per dot
    std::string code_, text_;
    bool spaced_ = true;
};

} // namespace orl
