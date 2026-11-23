// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Antenna Workshop maths and build guides. Plain functions with no Windows dependency, so they are unit-tested.
#pragma once

#include <string>
#include <vector>

namespace orl {

struct AntennaDesign {
    bool valid = false;
    double freqHz = 0, wavelengthM = 0;
    double quarterM = 0;      // one quarter-wave element (whip, or one leg of a dipole), with the usual 5% end-effect shortening
    double dipoleM = 0;       // half-wave dipole, both legs together
    double fullWaveM = 0;     // whole wavelength, for reference
    double radialM = 0;       // ground-plane radial: a little longer than the whip
};

// Frequency in Hz (10 kHz to 6 GHz). Not valid outside that range.
AntennaDesign designAntenna(double freqHz);

// "76.0 cm (29.9 in)" or "1.45 m (4 ft 9 in)".
std::string formatLength(double metres);

// The full text shown for a design: lengths, what they mean, and which of them to cut.
std::string describeDesign(const AntennaDesign& d);

struct Guide { const char* title; const char* text; };
const std::vector<Guide>& antennaGuides();

struct Preset { const char* name; double hz; };
const std::vector<Preset>& antennaPresets();

} // namespace orl
