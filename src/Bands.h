// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// A small, SIMPLIFIED table of common radio bands used for hints in the UI ("what is here, and
// which mode usually fits"). It is not authoritative. The Frequency Atlas milestone will replace
// it with real Canadian (ISED) allocation data.
#pragma once

#include <cstddef>

namespace orl {

struct Band {
    double lo, hi;          // Hz
    const char* name;
    int mode;               // suggested demodulation: 0 = WFM, 1 = NFM, 2 = AM
    const char* note;
};

inline const Band* bandTable(size_t& count) {
    static const Band t[] = {
        {530e3,    1710e3,   "AM broadcast band (medium wave)", 2, "Needs HF capability on most dongles."},
        {3e6,      30e6,     "Shortwave / HF", 2, "Needs HF capability on most dongles."},
        {87.5e6,   108e6,    "FM broadcast band", 0, "Music and talk stations, about 200 kHz apart."},
        {118e6,    137e6,    "Airband (aircraft voice)", 2, "Voice is AM. Transmissions are short and intermittent."},
        {144e6,    148e6,    "2 m amateur radio band", 1, "Narrowband FM voice, usually bursty."},
        {156e6,    162.025e6,"Marine VHF band", 1, "Narrowband FM voice."},
        {162.4e6,  162.55e6, "Weather radio", 1, "Continuous narrowband FM voice broadcasts."},
        {420e6,    450e6,    "UHF: 70 cm amateur / licence-exempt devices", 1, "Mixed use: voice, remote controls, sensors."},
        {1085e6,   1095e6,   "Aircraft transponders (1090 MHz)", 1, "Digital pulses. They look like blips and are not heard as speech."},
    };
    count = sizeof t / sizeof t[0];
    return t;
}

// Narrowest band containing hz, or nullptr.
inline const Band* findBand(double hz) {
    size_t n; const Band* t = bandTable(n);
    const Band* best = nullptr;
    for (size_t i = 0; i < n; ++i)
        if (hz >= t[i].lo && hz <= t[i].hi && (!best || (t[i].hi - t[i].lo) < (best->hi - best->lo))) best = &t[i];
    return best;
}

} // namespace orl
