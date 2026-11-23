// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// The Frequency Atlas data: a curated list of well-known radio bands, used for hints in the UI
// ("what is here, and which mode usually fits") and shown in the Frequency Atlas window.
//
// It is a plain-language SUMMARY, not the official allocation table. The authoritative reference is the
// Canadian Table of Frequency Allocations (CTFA) published by ISED. Each entry says where its information
// comes from. If you improve the data, keep `source` honest and ASCII-only.
#pragma once

#include <cstddef>

namespace orl {

struct Band {
    double lo, hi;          // Hz
    const char* name;
    int mode;               // suggested demodulation: 0 = WFM, 1 = NFM, 2 = AM, 3 = USB, 4 = LSB, 5 = CW
    const char* note;       // what it is and what to expect, in plain language
    const char* category;
    double tryHz;           // a good example frequency to jump to (0 = none, e.g. out of the dongle's range)
    const char* source;
};

inline const Band* bandTable(size_t& count) {
    static const char* kGen = "General allocation knowledge; check the Canadian Table of Frequency Allocations (ISED).";
    static const char* kRac = "Radio Amateurs of Canada band plans (via the RepeaterBook wiki, abbreviated); check RAC / ISED.";
    static const char* kEcc = "Environment and Climate Change Canada notice, reported Feb-Mar 2026; frequencies from the ECCC Weatheradio pages.";
    static const Band t[] = {
        // ---- broadcast
        {530e3,   1710e3,  "AM broadcast band (medium wave)", 2, "Stations every 10 kHz (540 to 1700 kHz). Needs HF capability on most dongles.", "Broadcast", 0, kGen},
        {87.8e6,  108e6,   "FM broadcast band", 0, "Music and talk stations on channels 200 kHz apart (88.1, 88.3 ... 107.9 MHz). Wide, loud, and the best first target.", "Broadcast", 98.1e6, kGen},
        {3e6,     30e6,    "Shortwave / HF", 2, "Shortwave broadcasters, amateur radio, time signals and more. Needs HF capability on most dongles.", "Broadcast", 0, kGen},
        {5.9e6,   6.2e6,   "49 m shortwave broadcast band", 2, "International broadcasters, best at night. Needs HF capability.", "Broadcast", 0, kGen},
        {9.4e6,   9.9e6,   "31 m shortwave broadcast band", 2, "International broadcasters. Needs HF capability.", "Broadcast", 0, kGen},
        {11.6e6,  12.1e6,  "25 m shortwave broadcast band", 2, "International broadcasters, best by day. Needs HF capability.", "Broadcast", 0, kGen},
        // ---- aviation
        {108e6,   117.975e6, "Aircraft navigation beacons (VOR / ILS)", 2, "Navigation aids. You hear a Morse identifier and tones, not conversation.", "Aviation", 0, kGen},
        {118e6,   137e6,   "Airband (aircraft voice)", 2, "Voice is AM. Transmissions are short and intermittent, and mostly heard within line of sight of the aircraft or tower.", "Aviation", 123.0e6, kGen},
        {121.45e6, 121.55e6, "121.5 MHz aircraft emergency frequency", 2, "The international aeronautical emergency frequency. Normally silent.", "Aviation", 121.5e6, kGen},
        {1085e6,  1095e6,  "Aircraft transponders (1090 MHz)", 1, "Digital pulses. They look like blips and are not heard as speech.", "Aviation", 0, kGen},
        // ---- amateur radio
        {3.5e6,   4.0e6,   "80 m amateur band", 4, "Amateur radio. Mostly single-sideband voice (lower sideband by convention) plus Morse near the bottom of the band. Needs HF capability.", "Amateur radio", 0, kGen},
        {7.0e6,   7.3e6,   "40 m amateur band", 4, "Amateur radio. Single-sideband voice (lower sideband by convention) and Morse near 7.0 to 7.05 MHz. Needs HF capability.", "Amateur radio", 0, kGen},
        {14.0e6,  14.35e6, "20 m amateur band", 3, "Amateur radio, worldwide contacts. Mostly single-sideband (upper sideband by convention) and Morse near 14.0 to 14.07 MHz. Needs HF capability.", "Amateur radio", 0, kGen},
        {28.0e6,  29.7e6,  "10 m amateur band", 3, "Amateur radio. Single-sideband voice (upper sideband by convention). Needs HF capability.", "Amateur radio", 0, kGen},
        {50e6,    54e6,    "6 m amateur band", 1, "Amateur radio: FM and single-sideband voice, digital modes. Often quiet.", "Amateur radio", 0, kRac},
        {144e6,   148e6,   "2 m amateur radio band", 1, "Narrowband FM voice through local repeaters and direct contacts, usually bursty.", "Amateur radio", 146.52e6, kRac},
        {430e6,   450e6,   "70 cm amateur radio band", 1, "Narrowband FM voice, repeaters and digital modes.", "Amateur radio", 0, kRac},
        // ---- marine and weather
        {156e6,   162.025e6, "Marine VHF band", 1, "Narrowband FM voice: boats, harbours and the Coast Guard.", "Marine and weather", 0, kGen},
        {156.775e6, 156.825e6, "Marine channel 16 (distress and calling)", 1, "Boats call each other and the Coast Guard here, then move to another channel. Mostly quiet.", "Marine and weather", 156.8e6, kGen},
        {162.4e6, 162.55e6, "VHF weather channels (162.400 to 162.550 MHz)", 1,
         "Weatheradio Canada used these seven channels (162.400, .425, .450, .475, .500, .525, .550) until Environment and Climate Change Canada permanently shut the service down on 16 March 2026. US NOAA Weather Radio uses the same frequencies, so a station may only be heard near the US border. An empty channel here is now normal in Canada.",
         "Marine and weather", 162.475e6, kEcc},
        // ---- personal radio
        {26.965e6, 27.405e6, "Citizens' band (CB), 40 channels", 2, "Short-range AM voice for the public. Mostly truckers and hobbyists. Needs HF capability.", "Personal radio", 0, kGen},
        {462.5e6, 462.75e6, "Family Radio Service (FRS) walkie-talkies, 462 MHz", 1, "Everyday walkie-talkies. These are people's private conversations: do not record, share or act on them.", "Personal radio", 0, kGen},
        {467.5e6, 467.725e6, "Family Radio Service (FRS) walkie-talkies, 467 MHz", 1, "Everyday walkie-talkies. These are people's private conversations: do not record, share or act on them.", "Personal radio", 0, kGen},
        {420e6,   450e6,   "UHF: amateur radio and licence-exempt devices", 1, "Mixed use: voice, remote controls, sensors.", "Personal radio", 0, kGen},
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
