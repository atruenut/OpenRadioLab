// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// ADS-B: the position and identity reports aircraft broadcast on 1090 MHz (Mode S extended squitter, DF17).
// Receive-only. Feed it the raw I/Q at about 2 MS/s; it keeps a table of the aircraft heard.
#pragma once

#include "../Sync.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace orl {

struct Aircraft {
    uint32_t icao = 0;                    // 24-bit address
    std::string callsign;                 // flight identification as broadcast (airline code + number, or registration)
    bool hasAlt = false, hasPos = false, hasVel = false;
    int altFt = 0;
    double lat = 0, lon = 0;
    double speedKt = 0, headingDeg = 0;
    int vrateFpm = 0;
    double lastSeen = 0;                  // decoder clock, seconds
    unsigned messages = 0;
};

struct AdsbStats { unsigned long frames = 0, bad = 0, candidates = 0; };

// ---- exposed for tests
bool adsbCrcOk(const uint8_t* msg14);                                   // 112-bit frame
int adsbNL(double lat);                                                  // CPR number of longitude zones
// Global CPR decode from one even and one odd airborne position. latCprE etc. are the raw 17-bit values.
bool adsbCprGlobal(uint32_t latE, uint32_t lonE, uint32_t latO, uint32_t lonO, bool oddIsNewer, double& lat, double& lon);

class AdsbDecoder {
public:
    void setRate(double fs);
    void reset();
    void process(const float* iq, size_t n);                   // interleaved I,Q floats (any thread, one at a time)
    // Handles one 14-byte frame directly (used by process, and by tests). 'now' is the decoder clock.
    bool handleFrame(const uint8_t* msg14, double now);
    std::vector<Aircraft> aircraft(double maxAgeSec) const;    // most recently heard first
    double clock() const { Lock l(mu_); return clock_; }
    AdsbStats stats() const { Lock l(mu_); return st_; }

private:
    bool tryDecode(size_t i);
    double at(double base, double t0, double t1) const;        // mean magnitude from t0 to t1 microseconds after sample position 'base'

    double fs_ = 2048000.0, spc_ = 2.048;
    std::vector<float> mag_;                                    // magnitudes, with the unfinished tail carried over
    size_t consumed_ = 0;                                       // absolute sample number of mag_[0]
    double noise_ = 0.01;

    mutable Mutex mu_;
    double clock_ = 0;
    AdsbStats st_;
    struct Cpr { uint32_t lat = 0, lon = 0; double t = -1e9; };
    struct Entry { Aircraft a; Cpr even, odd; };
    std::map<uint32_t, Entry> tab_;
};

} // namespace orl
