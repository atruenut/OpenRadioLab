// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Small portable helpers (no Windows dependencies, so they can be unit-tested anywhere).
#pragma once

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace orl {

// Parses what a person might type into a frequency box.
//   "100.5"      -> 100.5 MHz      (bare numbers are MHz)
//   "98 MHz"     -> 98 MHz         "145000k" / "145000 kHz" -> 145 MHz
//   "1.09g"      -> 1.09 GHz       "433920000 Hz" -> 433.92 MHz
//   "100,5"      -> 100.5 MHz      (comma decimal separators accepted)
inline bool parseFrequencyHz(const std::string& in, double& hz) {
    std::string s;
    for (char c : in) s += (c == ',') ? '.' : c;
    const char* p = s.c_str();
    while (*p && std::isspace((unsigned char)*p)) ++p;
    char* end = nullptr;
    const double v = std::strtod(p, &end);
    if (end == p || !std::isfinite(v)) return false;

    std::string u;
    for (const char* q = end; *q; ++q)
        if (!std::isspace((unsigned char)*q)) u += (char)std::tolower((unsigned char)*q);

    bool hadHz = false;
    if (u.size() >= 2 && u.compare(u.size() - 2, 2, "hz") == 0) { hadHz = true; u.erase(u.size() - 2); }

    double mult;
    if (u.empty())        mult = hadHz ? 1.0 : 1e6;
    else if (u == "k")    mult = 1e3;
    else if (u == "m")    mult = 1e6;
    else if (u == "g")    mult = 1e9;
    else return false;

    hz = v * mult;
    return hz > 0.0 && std::isfinite(hz);
}

// "100.500 MHz": never silently rounds away precision a person may care about.
// At least 3 decimals are kept for MHz/GHz; extra trailing zeros are trimmed.
inline std::string formatFrequency(double hz) {
    char b[64];
    auto trim = [](std::string t, size_t minDec) {
        const size_t dot = t.find('.');
        if (dot == std::string::npos) return t;
        while (t.size() > dot + 1 + minDec && t.back() == '0') t.pop_back();
        if (t.back() == '.') t.pop_back();      // "580." -> "580"
        return t;
    };
    if (hz >= 1e9)      { std::snprintf(b, sizeof b, "%.6f", hz / 1e9); return trim(b, 3) + " GHz"; }
    if (hz >= 1e6)      { std::snprintf(b, sizeof b, "%.6f", hz / 1e6); return trim(b, 3) + " MHz"; }
    if (hz >= 1e3)      { std::snprintf(b, sizeof b, "%.3f", hz / 1e3); return trim(b, 0) + " kHz"; }
    std::snprintf(b, sizeof b, "%.0f Hz", hz);
    return b;
}

inline std::string formatBandwidth(double hz) {
    char b[48];
    if (hz >= 1e6)      std::snprintf(b, sizeof b, "%.2f MHz", hz / 1e6);
    else if (hz >= 1e3) std::snprintf(b, sizeof b, "%.0f kHz", hz / 1e3);
    else                std::snprintf(b, sizeof b, "%.0f Hz", hz);
    return b;
}

} // namespace orl
