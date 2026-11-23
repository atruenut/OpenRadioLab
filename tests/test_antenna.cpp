// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "../src/learn/Antenna.h"
#include <cmath>
#include <cstdio>
using namespace orl;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
    auto d = designAntenna(98.1e6);
    CHECK(d.valid, "invalid");
    CHECK(std::fabs(d.wavelengthM - 3.0560) < 0.001, "wavelength %.4f", d.wavelengthM);
    CHECK(std::fabs(d.quarterM - 0.7257) < 0.001, "quarter %.4f", d.quarterM);       // 71.25/98.1 = 0.7263 with c rounding
    CHECK(std::fabs(d.dipoleM - 2 * d.quarterM) < 1e-9, "dipole");
    CHECK(!designAntenna(0).valid && !designAntenna(7e9).valid && !designAntenna(-5).valid, "range check");
    CHECK(formatLength(0.7257) == "72.6 cm (28.6 in)", "fmt cm: %s", formatLength(0.7257).c_str());
    CHECK(formatLength(1.45).find("4 ft") != std::string::npos, "fmt m: %s", formatLength(1.45).c_str());
    CHECK(formatLength(20.0).find("65 ft") != std::string::npos, "fmt big: %s", formatLength(20.0).c_str());
    const std::string t = describeDesign(d);
    CHECK(t.find("98.100 MHz") != std::string::npos, "text lacks frequency");
    CHECK(describeDesign(designAntenna(0)).find("Type a frequency") != std::string::npos, "invalid text");
    CHECK(!antennaGuides().empty() && !antennaPresets().empty(), "empty content");
    bool ascii = true;
    auto chk = [&](const char* s) { for (; *s; ++s) if ((unsigned char)*s > 126) ascii = false; };
    for (auto& g : antennaGuides()) { chk(g.title); chk(g.text); }
    for (auto& p : antennaPresets()) { chk(p.name); CHECK(designAntenna(p.hz).valid, "preset %s", p.name); }
    chk(t.c_str());
    CHECK(ascii, "non-ASCII text");
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL ANTENNA TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
