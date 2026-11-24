// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "RdsDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orl {
namespace {
const double kPi = 3.14159265358979323846;
const int kOffsets[5] = {0x0FC, 0x198, 0x168, 0x350, 0x1B4};     // A, B, C, C', D
const int kBlockOfOffset[5] = {0, 1, 2, 2, 3};

int crc10(uint32_t data16) {
    int r = 0;
    for (int i = 15; i >= 0; --i) {
        const int fb = ((r >> 9) & 1) ^ (int)((data16 >> i) & 1);
        r = (r << 1) & 0x3FF;
        if (fb) r ^= 0x1B9;                                       // x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + 1
    }
    return r;
}
char printable(int c) { return (c >= 0x20 && c <= 0x7E) ? (char)c : ' '; }
}

const char* rdsPtyName(int p) {
    static const char* n[32] = {"None", "News", "Information", "Sports", "Talk", "Rock", "Classic rock", "Adult hits", "Soft rock", "Top 40",
        "Country", "Oldies", "Soft", "Nostalgia", "Jazz", "Classical", "Rhythm and blues", "Soft rhythm and blues", "Language",
        "Religious music", "Religious talk", "Personality", "Public", "College", "Spanish talk", "Spanish music", "Hip hop", "Unassigned",
        "Unassigned", "Weather", "Emergency test", "Emergency"};
    return (p >= 0 && p < 32) ? n[p] : "";
}

void RdsDecoder::setRate(double fs) {
    fs_ = fs; spc_ = fs / 2375.0;
    bp_.bandpass(fs, 57000.0, 12.0);
    for (Biquad* b : {&lp1i_, &lp2i_, &lp1q_, &lp2q_}) b->lowpass(fs, 3000.0);
    const double wn = 2.0 * kPi * 25.0 / fs;
    kp_ = 2.0 * 0.707 * wn; ki_ = wn * wn;
    w_ = 2.0 * kPi * 57000.0 / fs;
    reset();
}

void RdsDecoder::reset() {
    bp_.reset(); lp1i_.reset(); lp2i_.reset(); lp1q_.reset(); lp2q_.reset();
    ph_ = 0; fr_ = 0; amp_ = 0; sgn_ = 0; cph_ = 0; acc_ = 0; prevChip_ = 0; chipCount_ = 0;
    sy_[0].clear(); sy_[1].clear(); sample_ = 0; lastGood_ = 0;
    Lock l(mu_);
    info_ = RdsInfo();
    gType_ = -1; gB_ = false; gC_ = false; piCand_ = 0; piSeen_ = 0; rtFlag_ = -1;
    std::memset(psBuf_, ' ', 8); psBuf_[8] = 0; std::memset(rtBuf_, 0, sizeof rtBuf_);
}

RdsInfo RdsDecoder::info() const {
    Lock l(mu_);
    RdsInfo r = info_;
    r.locked = r.goodBlocks > 0 && (sample_ - lastGood_) < (uint32_t)(3.0 * fs_);
    return r;
}

void RdsDecoder::process(float x) {
    ++sample_;
    // Bring the 57 kHz subcarrier to 0 Hz with a locked oscillator (Costas loop, since the data is BPSK).
    const float b = bp_.process(x);
    const float c = (float)std::cos(ph_), s = (float)std::sin(ph_);
    const float i = lp2i_.process(lp1i_.process(b * c)), q = lp2q_.process(lp1q_.process(-b * s));
    const double pe = std::atan2((double)q * (i >= 0 ? 1.0 : -1.0), (double)std::fabs(i) + 1e-12);
    fr_ += ki_ * pe;
    ph_ += w_ + fr_ + kp_ * pe;
    if (ph_ > 2.0 * kPi) ph_ -= 2.0 * kPi; else if (ph_ < 0) ph_ += 2.0 * kPi;

    // Chip clock: every sign change of the baseband is a chip boundary, so nudge the clock to land there.
    amp_ += 0.002 * (std::fabs(i) - amp_);
    const int ns = (i > 0.3 * amp_) ? 1 : (i < -0.3 * amp_) ? -1 : sgn_;
    if (ns != sgn_) {
        if (sgn_ != 0) {
            double err = cph_ - 0.06; err -= std::floor(err + 0.5);
            cph_ -= 0.12 * err;
        }
        sgn_ = ns;
    }
    acc_ += i;
    cph_ += 1.0 / spc_;
    if (cph_ >= 1.0) { cph_ -= 1.0; const float v = (float)acc_; acc_ = 0; chip(v); }
}

void RdsDecoder::chip(float v) {
    ++chipCount_;
    // Two readings of which chips pair into a bit; whichever one finds valid blocks wins.
    const int bit = prevChip_ > v ? 1 : 0;
    Sync& s = sy_[chipCount_ & 1];
    const int d = bit ^ s.prev; s.prev = bit;
    feedBit(s, d);
    prevChip_ = v;
}

void RdsDecoder::feedBit(Sync& s, int bit) {
    s.reg = ((s.reg << 1) | (uint32_t)bit) & 0x3FFFFFF;
    if (s.run < 26) ++s.run;
    ++s.lastMatchAge;
    if (s.run < 26) return;
    const uint16_t data = (uint16_t)(s.reg >> 10);
    const int chk = (int)(s.reg & 0x3FF) ^ crc10(data);
    int which = -1;
    for (int k = 0; k < 5; ++k) if (chk == kOffsets[k]) which = k;
    if (!s.synced) {
        if (which >= 0) {
            const int t = kBlockOfOffset[which];
            if (s.lastMatchAge == 26 && s.lastType >= 0 && t == (s.lastType + 1) % 4) {      // two blocks in the right order, 26 bits apart
                s.synced = true; s.bitIn = 0; s.expect = (t + 1) % 4; s.bad = 0;
                block(t, data);
            }
            s.lastMatchAge = 0; s.lastType = t;
        }
        return;
    }
    if (++s.bitIn < 26) return;
    s.bitIn = 0;
    const bool ok = which >= 0 && kBlockOfOffset[which] == s.expect;
    if (ok) { s.bad = 0; block(s.expect, data); }
    else {
        { Lock l(mu_); ++info_.badBlocks; }
        if (++s.bad >= 12) { s.synced = false; s.lastType = -1; s.lastMatchAge = 1000; s.bad = 0; }
    }
    s.expect = (s.expect + 1) % 4;
}

void RdsDecoder::block(int type, uint16_t data) {
    Lock l(mu_);
    ++info_.goodBlocks; lastGood_ = sample_;
    switch (type) {
        case 0:
            if (data == piCand_) { if (piSeen_ < 1000) ++piSeen_; } else { piCand_ = data; piSeen_ = 1; }
            if (piSeen_ >= 2) { info_.pi = piCand_; info_.havePi = true; }
            gB_ = false; gC_ = false;
            break;
        case 1:
            if (!(info_.havePi && piSeen_ >= 2)) { gB_ = false; break; }
            gB_ = true; bData_ = data; gType_ = (data >> 11) & 0x1F;             // group number and A/B version
            info_.pty = (data >> 5) & 0x1F; info_.tp = (data >> 10) & 1;
            gC_ = false;
            break;
        case 2:
            cData_ = data; gC_ = true;
            break;
        case 3: {
            if (!gB_) break;
            const int num = gType_ >> 1, verB = gType_ & 1;
            if (num == 0) {                                                       // group 0: station name, two characters at a time
                const int a = bData_ & 3;
                psBuf_[a * 2] = printable(data >> 8); psBuf_[a * 2 + 1] = printable(data & 0xFF);
                info_.ps = psBuf_;
            } else if (num == 2) {                                                // group 2: radio text
                const int flag = (bData_ >> 4) & 1, a = bData_ & 15;
                if (flag != rtFlag_) { rtFlag_ = flag; std::memset(rtBuf_, ' ', 64); rtBuf_[64] = 0; }
                if (!verB && gC_) {
                    rtBuf_[a * 4] = printable(cData_ >> 8); rtBuf_[a * 4 + 1] = printable(cData_ & 0xFF);
                    rtBuf_[a * 4 + 2] = printable(data >> 8); rtBuf_[a * 4 + 3] = printable(data & 0xFF);
                    if ((cData_ >> 8) == 0x0D || (cData_ & 0xFF) == 0x0D || (data >> 8) == 0x0D || (data & 0xFF) == 0x0D) {
                        for (int k = 0; k < 4; ++k) { const int ch = k < 2 ? ((k == 0 ? cData_ >> 8 : cData_ & 0xFF)) : ((k == 2 ? data >> 8 : data & 0xFF));
                            if (ch == 0x0D) { for (int z = a * 4 + k; z < 64; ++z) rtBuf_[z] = ' '; break; } }
                    }
                } else if (verB) {
                    rtBuf_[a * 2] = printable(data >> 8); rtBuf_[a * 2 + 1] = printable(data & 0xFF);
                }
                std::string t(rtBuf_);
                while (!t.empty() && t.back() == ' ') t.pop_back();
                size_t f = t.find_first_not_of(' ');
                info_.rt = f == std::string::npos ? std::string() : t.substr(f);
            }
            gB_ = false;
            break;
        }
    }
}

} // namespace orl
