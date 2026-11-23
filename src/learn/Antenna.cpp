// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "Antenna.h"

#include "../Util.h"

#include <cmath>
#include <cstdio>

namespace orl {

AntennaDesign designAntenna(double hz) {
    AntennaDesign d;
    if (!(hz >= 10e3 && hz <= 6e9)) return d;
    d.valid = true; d.freqHz = hz;
    d.wavelengthM = 299792458.0 / hz;
    d.fullWaveM = d.wavelengthM;
    d.quarterM = d.wavelengthM / 4.0 * 0.95;
    d.dipoleM = d.quarterM * 2.0;
    d.radialM = d.wavelengthM / 4.0 * 0.98;
    return d;
}

std::string formatLength(double m) {
    char b[80];
    const double in = m / 0.0254;
    if (m >= 10.0) { const int ft = (int)(in / 12.0); std::snprintf(b, sizeof b, "%.1f m (%d ft %.0f in)", m, ft, in - ft * 12.0); }
    else if (m >= 1.0) { const int ft = (int)(in / 12.0); std::snprintf(b, sizeof b, "%.2f m (%d ft %.0f in)", m, ft, in - ft * 12.0); }
    else std::snprintf(b, sizeof b, "%.1f cm (%.1f in)", m * 100.0, in);
    return b;
}

std::string describeDesign(const AntennaDesign& d) {
    if (!d.valid) return "Type a frequency between 0.01 MHz and 6000 MHz, or pick a preset.";
    std::string t = "For " + formatFrequency(d.freqHz) + ":\r\n\r\n";
    t += "  Wavelength (the length of one full wave): " + formatLength(d.wavelengthM) + "\r\n\r\n";
    t += "  Simple whip or rod (quarter wave):         " + formatLength(d.quarterM) + "\r\n";
    t += "  Dipole, two equal wires end to end:        " + formatLength(d.dipoleM) + " in total,\r\n";
    t += "                                             " + formatLength(d.quarterM) + " for EACH side\r\n";
    t += "  Ground-plane radials (3 or 4 of them):     " + formatLength(d.radialM) + " each\r\n\r\n";
    t += "The lengths are the quarter wavelength trimmed by about 5%, because a real wire acts slightly longer than it measures. "
         "Cut a little long and trim; it is easier than adding wire back.\r\n\r\n";
    if (d.quarterM > 3.0) t += "That is a big antenna. A long wire does not need to be exact: for listening, anything much longer than this works too.\r\n";
    else if (d.quarterM < 0.03) t += "Very small. At these frequencies the antenna is tiny and the connector and cable matter as much as the element.\r\n";
    else t += "A telescopic whip or two pieces of wire are about the right size for this.\r\n";
    t += "\r\nA shorter antenna still hears strong signals, which is why a paperclip can pick up a nearby FM station. It just hears weaker ones less well. "
         "Length matters most for the signals you can barely hear.";
    return t;
}

const std::vector<Preset>& antennaPresets() {
    static const std::vector<Preset> p = {
        {"FM broadcast (98.1 MHz)", 98.1e6}, {"Airband (123.0 MHz)", 123.0e6}, {"Marine VHF channel 16 (156.8 MHz)", 156.8e6},
        {"Weather satellites (137.5 MHz)", 137.5e6}, {"2 m amateur (146.52 MHz)", 146.52e6}, {"VHF weather (162.475 MHz)", 162.475e6},
        {"Aircraft transponders (1090 MHz)", 1090e6}, {"40 m amateur (7.1 MHz)", 7.1e6}, {"20 m amateur (14.2 MHz)", 14.2e6},
        {"Citizens' band (27.185 MHz)", 27.185e6},
    };
    return p;
}

const std::vector<Guide>& antennaGuides() {
    static const std::vector<Guide> g = {
        {"1. Start here: what an antenna does",
         "An antenna is just metal that waves push a tiny electric current into. Radio waves come in different lengths: FM broadcast waves are about 3 metres long, "
         "airband about 2.4 m, shortwave tens of metres. An antenna works best when its length is a simple fraction of the wave it is catching, usually a quarter or a half.\r\n\r\n"
         "Three things matter in this order:\r\n"
         "  1. Height and a clear view. Outside beats inside; high beats low; a window beats a basement. Metal walls and buildings block radio.\r\n"
         "  2. The right length for the band you care about (use the calculator on the left).\r\n"
         "  3. Keeping it away from noise: computers, chargers, LED lights and the dongle itself all make radio hiss. Put the antenna on a short extension cable away from the PC.\r\n\r\n"
         "This program only receives. Nothing here transmits, and you never need a licence to listen with a receive-only antenna."},
        {"2. The paperclip and the telescopic whip",
         "What you already tried works because strong stations are strong. A short piece of wire is a poor antenna, but a loud local station overpowers its weakness.\r\n\r\n"
         "To do better with almost no cost:\r\n"
         "  - Use the dipole kit that came with the dongle (two telescopic arms and a stand). Extend each arm to the length the calculator shows for your band.\r\n"
         "  - Or use a telescopic radio antenna from an old radio. Extend it to the quarter-wave length. It works best standing straight up on something metal, like a cookie tin, a baking sheet or a car roof.\r\n"
         "  - Orient it vertically for most voice and broadcast signals (they are vertically polarised).\r\n\r\n"
         "Check: switch to the Spectrum view, watch the signal height of a station you know, and change the length or position. Taller peak means a better antenna for that frequency."},
        {"3. Build a simple dipole (about 10 minutes)",
         "A dipole is two equal wires pointing away from each other, fed in the middle. It is the classic first antenna.\r\n\r\n"
         "You need:\r\n"
         "  - Two pieces of wire cut to the length shown for EACH side in the calculator. Speaker wire or hookup wire is fine.\r\n"
         "  - Coaxial cable (like TV cable) as long as you need, with a connector that fits your dongle (usually MCX or SMA; check which one, or use the adapter).\r\n"
         "  - Tape, a plastic clothes peg or a wooden stick for support.\r\n\r\n"
         "How:\r\n"
         "  1. Strip the outer jacket from one end of the coax. Twist the braided shield into one wire, the centre conductor is the other.\r\n"
         "  2. Solder or firmly twist and tape one antenna wire to the centre conductor, and the other wire to the braid. Neither wire touches the other.\r\n"
         "  3. Stretch the wires in a straight line opposite each other: horizontal between two supports, or vertical with the cable leaving sideways.\r\n"
         "  4. Tape the join so no bare copper shows. Plug the other end into the dongle.\r\n\r\n"
         "Tip: for FM the whole thing is about 1.4 m wide, so it fits across a window."},
        {"4. Shape it into a V for weather satellites",
         "Satellites passing overhead are usually received with a dipole bent into a flat V whose arms are about 120 degrees apart, laid horizontally and pointed so the open side faces north-south (the satellite passes along that line). "
         "Cut each arm to the quarter-wave length for the satellite frequency (use the calculator, for example 137.5 MHz). "
         "Put it outdoors with a clear sky. Reception also needs software outside this program, so treat this as a project for later."},
        {"5. Ground plane: a better single whip",
         "A single vertical rod on its own works but is lopsided because the other half of the antenna is missing. Add three or four wires (radials) of the length shown, "
         "spread out from the base and sloping slightly downward, like the legs of a spider. This gives a steadier signal and is how most vertical antennas are built. "
         "The vertical rod is the quarter-wave whip length; connect the cable centre to the rod and the braid to all the radials."},
        {"6. Shortwave and long wires (HF)",
         "Below about 30 MHz the waves are tens of metres long, so a full-size antenna is impractical indoors. For listening, a long wire works: "
         "as much wire as you can run, 10 m or more, as high as safely possible and kept away from metal, connected to the antenna input through a small matching device called an unun (a 9:1 type is common).\r\n\r\n"
         "Most RTL-SDR dongles cannot tune below about 24 MHz at all unless they have direct sampling or an upconverter. Check your dongle's documentation: the RTL-SDR Blog V4, for instance, has HF input built in."},
        {"7. Safety",
         "  - Stay far away from power lines. Never put up a wire or pole where it could fall onto one, and never use a metal pole near them.\r\n"
         "  - Thunderstorms: disconnect and move the antenna away from the equipment. Do not leave an outdoor antenna connected to your PC in a storm.\r\n"
         "  - Do not climb on roofs for a hobby antenna.\r\n"
         "  - Respect neighbours and the law: you may listen to broadcasts and calls freely, but private conversations such as walkie-talkies or phone calls are not for recording or sharing."},
    };
    return g;
}

} // namespace orl
