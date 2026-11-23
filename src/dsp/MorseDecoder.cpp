// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "MorseDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orl {
namespace {
const char* const kCodes[] = {
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.",
    "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.."};
const char* const kDigits[] = {"-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."};
char lookup(const std::string& c) {
    for (int i = 0; i < 26; ++i) if (c == kCodes[i]) return (char)('A' + i);
    for (int i = 0; i < 10; ++i) if (c == kDigits[i]) return (char)('0' + i);
    if (c == ".-.-.-") return '.';
    if (c == "--..--") return ',';
    if (c == "..--..") return '?';
    if (c == "-..-.") return '/';
    if (c == "-...-") return '=';
    return '*';                                     // a pattern we do not know: shown as * rather than guessed
}
const double kBlockSec = 0.005;
}

MorseDecoder::MorseDecoder(double fs, double tone) : fs_(fs), tone_(tone) {
    blockLen_ = (size_t)(fs * kBlockSec);
    coeff_ = 2.0 * std::cos(2.0 * 3.14159265358979 * tone / fs);
}

void MorseDecoder::reset() {
    fill_ = 0; s1_ = s2_ = 0; peak_ = floor_ = 0; warm_ = 0; syms_ = 0; on_ = false; signal_ = false; run_ = 0; unit_ = 0.08;
    code_.clear(); text_.clear(); spaced_ = true;
}

double MorseDecoder::wpm() const { return 1.2 / unit_; }      // PARIS: one word = 50 units

void MorseDecoder::put(char c) {
    text_ += c; spaced_ = (c == ' ');
    if (text_.size() > 2000) text_.erase(0, text_.size() - 2000);
}

void MorseDecoder::emitChar() {
    if (code_.empty()) return;
    put(lookup(code_));
    code_.clear();
}

void MorseDecoder::symbolDone(bool dash) {
    code_ += dash ? '-' : '.';
    if (code_.size() > 8) { code_.clear(); put('*'); }
}

void MorseDecoder::feed(const float* x, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const double s = x[i] + coeff_ * s1_ - s2_;
        s2_ = s1_; s1_ = s;
        if (++fill_ == blockLen_) {
            const double e = std::sqrt(std::max(0.0, s1_ * s1_ + s2_ * s2_ - coeff_ * s1_ * s2_)) / (double)blockLen_;
            s1_ = s2_ = 0; fill_ = 0;
            block(e);
        }
    }
}

void MorseDecoder::block(double e) {
    // Envelope bookkeeping: peak decays slowly, floor creeps up slowly and drops fast.
    peak_ = std::max(e, peak_ * 0.9995);
    if (warm_ < 100) {                                            // first half second: just learn the background level
        ++warm_; floor_ += (e - floor_) / warm_; peak_ = floor_; floor_ = std::max(floor_, 1e-6);
        return;
    }
    if (!on_) floor_ += (e - floor_) * 0.01;                      // background level: average of the blocks while the key is up
    signal_ = peak_ > floor_ * 5.0 && peak_ > 1e-4;
    const double hi = floor_ + (peak_ - floor_) * 0.5, lo = floor_ + (peak_ - floor_) * 0.3;
    const bool now = signal_ && (on_ ? e > lo : e > hi);
    const double sec = kBlockSec;
    if (now == on_) {
        ++run_;
        if (!on_ && !code_.empty() && run_ * sec >= 2.0 * unit_) emitChar();                 // letter gap
        if (!on_ && !spaced_ && run_ * sec >= 5.0 * unit_) { emitChar(); put(' '); }         // word gap
        return;
    }
    if (on_) {                                                                               // a mark just ended
        const double len = run_ * sec;
        if (len > 0.015) {                                                                   // ignore blips shorter than 15 ms
            const bool dash = len >= 1.7 * unit_;
            const double u = dash ? len / 3.0 : len;
            const double a = syms_ < 10 ? 0.5 : 0.2;          // learn the speed quickly from the first few marks
            ++syms_;
            unit_ = std::min(0.24, std::max(0.03, unit_ * (1 - a) + u * a));
            symbolDone(dash);
        }
    }
    on_ = now; run_ = 1;
}

} // namespace orl
