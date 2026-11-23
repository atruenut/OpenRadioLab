// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// "What could this be?" - a cautious hint built from three observations: where the signal is
// (band table), how wide it is, and whether it is continuous. It never claims an identification.
#pragma once

#include "Bands.h"
#include "Util.h"

#include <cmath>
#include <string>

namespace orl {

// activity: fraction of recent time the signal was above the noise (0..1), or -1 if unknown.
inline std::string signalHint(double hz, double widthHz, double activity) {
    const Band* b = findBand(hz);
    const bool wide = widthHz >= 100e3, voice = widthHz >= 4e3 && widthHz < 100e3, narrow = widthHz < 4e3;
    const bool steady = activity >= 0.9, bursty = activity >= 0 && activity < 0.9;
    std::string s = "Hint (a possibility, not an identification): ";

    if (b && b->mode == 0 && wide) {
        s += "it is in the " + std::string(b->name) + " and about as wide as a broadcast FM station";
        const double k = (hz - 87.9e6) / 200e3;
        if (hz >= 87.9e6 && hz <= 107.9e6 && std::fabs(k - std::round(k)) * 200e3 < 30e3)
            s += ", and it sits on the 200 kHz channel grid (" + formatFrequency(87.9e6 + std::round(k) * 200e3) + ")";
        s += ". That fits a normal FM station. Double-click to listen as WFM.";
        return s;
    }
    if (wide) {
        s += "a signal this wide (~" + formatBandwidth(widthHz) + ") is typical of broadcast FM or a wideband data link";
        if (b) s += "; but it is in the " + std::string(b->name) + ", where that would be unusual";
        s += ". Try listening as WFM.";
        return s;
    }
    if (b && b->mode == 2 && voice) {
        s += "it is in the " + std::string(b->name) + " with a voice-sized width";
        s += bursty ? " and it comes and goes, which fits short radio calls. Double-click to listen as AM."
                    : ". Voice here is AM. Double-click to listen.";
        return s;
    }
    if (b && b->mode == 1 && voice) {
        s += "it is in the " + std::string(b->name) + " with a voice-sized width";
        if (steady && hz >= 162.4e6 && hz <= 162.55e6) s += " and it is continuous, which fits an automated weather broadcast";
        else if (bursty) s += " and it comes and goes, which fits push-to-talk radio";
        s += ". These are usually narrowband FM, so try NFM.";
        return s;
    }
    if (b && hz >= 1085e6 && hz <= 1095e6) {
        s += "short blips near 1090 MHz are what aircraft transponders look like. They are digital pulses, so you will not hear speech.";
        return s;
    }
    if (narrow) {
        s += "a very narrow signal is typical of a steady carrier, a test tone, Morse code or a simple sensor";
        if (bursty) s += "; because it comes and goes it may be a device that only sends occasionally (remote control, sensor)";
        s += ".";
        return s;
    }
    s += "a width of ~" + formatBandwidth(widthHz) + " is typical of voice or data";
    if (b) s += "; in the " + std::string(b->name) + " that is " + b->note;
    s += " Try NFM or AM and compare.";
    return s;
}

} // namespace orl
