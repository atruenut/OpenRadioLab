// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// RDS / RBDS: the small data stream FM stations send on a 57 kHz subcarrier (station name, song text, programme type).
// Input is the FM-demodulated multiplex ("MPX") signal, one sample at a time. Thread-safe for info().
#pragma once

#include "Filters.h"
#include "../Sync.h"

#include <cstdint>
#include <string>

namespace orl {

struct RdsInfo {
    bool locked = false;          // valid data blocks are arriving right now
    bool havePi = false;
    uint16_t pi = 0;              // programme identification code (the station's ID number)
    std::string ps;               // 8-character station name, as sent
    std::string rt;               // radio text (song, show, slogan), as sent
    int pty = -1;                 // programme type, 0..31 (-1 = unknown)
    bool tp = false;              // traffic programme flag
    unsigned goodBlocks = 0, badBlocks = 0;     // diagnostics: how many 26-bit blocks passed / failed their check
};

// North American (RBDS) programme type names, which is what Canadian and US stations use.
const char* rdsPtyName(int pty);

class RdsDecoder {
public:
    void setRate(double mpxRate);       // sample rate of the samples given to process()
    void reset();
    void process(float mpx);
    RdsInfo info() const;

private:
    struct Sync {
        uint32_t reg = 0; int run = 0;         // recent bits and how many have been collected
        bool synced = false; int bitIn = 0, expect = 0, bad = 0;
        int lastMatchAge = 0, lastType = -1;
        int prev = 0;
        void clear() { *this = Sync(); }
    };
    void chip(float v);
    void feedBit(Sync& s, int bit);
    void block(int type, uint16_t data);

    double fs_ = 256000.0, spc_ = 107.8;
    Biquad bp_, lp1i_, lp2i_, lp1q_, lp2q_;
    double ph_ = 0, fr_ = 0, kp_ = 0, ki_ = 0, w_ = 0;
    double amp_ = 0;
    int sgn_ = 0;
    double cph_ = 0, acc_ = 0;                 // chip clock phase 0..1 and the integrator
    float prevChip_ = 0; unsigned chipCount_ = 0;
    Sync sy_[2];
    uint32_t sample_ = 0, lastGood_ = 0;

    mutable Mutex mu_;
    RdsInfo info_;
    // group assembly (guarded by mu_)
    int gType_ = -1; bool gB_ = false; uint16_t bData_ = 0;
    uint16_t piCand_ = 0; int piSeen_ = 0;
    char psBuf_[9] = {' ',' ',' ',' ',' ',' ',' ',' ',0};
    char rtBuf_[65] = {0}; int rtFlag_ = -1;
    uint16_t cData_ = 0; bool gC_ = false;
};

} // namespace orl
