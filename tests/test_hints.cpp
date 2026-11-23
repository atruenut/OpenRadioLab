// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "../src/Hints.h"
#include <cstdio>
using namespace orl;
static int g_fail = 0;
static void has(const std::string& s, const char* sub) { if (s.find(sub) == std::string::npos) { ++g_fail; std::printf("  FAIL: missing \"%s\" in: %s\n", sub, s.c_str()); } }
static void hasNot(const std::string& s, const char* sub) { if (s.find(sub) != std::string::npos) { ++g_fail; std::printf("  FAIL: unexpected \"%s\" in: %s\n", sub, s.c_str()); } }
int main() {
    has(signalHint(98.1e6, 170e3, 1.0), "200 kHz channel grid");
    has(signalHint(98.1e6, 170e3, 1.0), "WFM");
    hasNot(signalHint(98.0e6, 170e3, 1.0), "channel grid");                  // .0 is not on the Canadian/US grid
    has(signalHint(123.0e6, 9e3, 0.2), "AM");
    has(signalHint(123.0e6, 9e3, 0.2), "comes and goes");
    has(signalHint(162.475e6, 10e3, 1.0), "weather");
    has(signalHint(145.5e6, 12e3, 0.1), "push-to-talk");
    has(signalHint(1090e6, 2e3, 0.1), "transponder");
    has(signalHint(433.92e6, 1e3, 0.1), "occasionally");
    has(signalHint(300e6, 200e3, -1), "wide");
    hasNot(signalHint(98.1e6, 170e3, 1.0), "is a ");                         // wording stays tentative
    has(signalHint(500e6, 10e3, -1), "NFM");
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL HINT TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
