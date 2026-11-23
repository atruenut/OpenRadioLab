// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Journal storage and Signal Lab statistics.
#include "../src/journal/Journal.h"
#include "../src/dsp/LevelStats.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace orl;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void testJournal() {
    std::printf("[Journal round trip, with awkward text]\n");
    const char* path = "/tmp/orl_journal_test.txt";
    Journal j;
    JournalEntry a; a.when = "2026-11-22 15:47"; a.title = "First station"; a.vfoHz = 98100000; a.centreHz = 98000000; a.rateHz = 2048000;
    a.notes = "Line one\nLine two with a\ttab and a back\\slash.\n\nBlank line above."; a.mode = "WFM"; a.gain = "auto"; a.antenna = "Paperclip, 3 cm";
    a.levelDb = -31.5; a.simulated = true; a.measurement = "Signal ~98.1 MHz, 20 dB over noise";
    const int64_t ia = j.add(a);
    JournalEntry b; b.when = "2026-11-23 09:00"; b.title = ""; b.vfoHz = 162475000; b.mode = "NFM";
    const int64_t ib = j.add(b);
    std::string err;
    CHECK(j.save(path, err), "save: %s", err.c_str());

    Journal k;
    CHECK(k.load(path, err), "load: %s", err.c_str());
    CHECK(k.entries().size() == 2, "expected 2 entries, got %zu", k.entries().size());
    const JournalEntry* x = k.find(ia);
    CHECK(x && x->notes == a.notes, "notes changed in the round trip");
    CHECK(x && x->title == a.title && x->antenna == a.antenna && x->simulated && x->rateHz == 2048000, "fields lost");
    CHECK(x && std::fabs(x->levelDb + 31.5) < 0.05 && x->vfoHz == 98100000, "numbers lost");
    CHECK(k.find(ib) && k.find(ib)->mode == "NFM", "second entry lost");
    const int64_t ic = k.add(a);
    CHECK(ic > ib, "ids must keep increasing (%lld after %lld)", (long long)ic, (long long)ib);
    JournalEntry u = *k.find(ia); u.title = "Renamed";
    CHECK(k.update(u) && k.find(ia)->title == "Renamed", "update failed");
    CHECK(k.remove(ib) && !k.find(ib) && k.entries().size() == 2, "remove failed");
    const std::string md = k.toMarkdown();
    CHECK(md.find("Renamed") != std::string::npos && md.find("simulated data") != std::string::npos, "markdown missing content");
    CHECK(md.find("162.475") == std::string::npos, "removed entry still exported");

    Journal none; CHECK(none.load("/tmp/orl_definitely_missing.txt", err) && none.entries().empty(), "a missing file must be an empty journal");
    FILE* f = std::fopen(path, "wb"); std::fputs("hello this is not a journal\n", f); std::fclose(f);
    Journal bad; CHECK(!bad.load(path, err), "garbage accepted as a journal");
    std::remove(path);
}

static std::vector<float> series(const char* kind, double dt, double secs, unsigned seed = 3) {
    std::mt19937 rng(seed); std::normal_distribution<double> n(0, 1.2);
    std::vector<float> v; const size_t N = (size_t)(secs / dt);
    for (size_t i = 0; i < N; ++i) {
        const double t = i * dt; double base = -65;
        if (!std::string(kind).compare("steady")) base = -30;
        else if (!std::string(kind).compare("bursty")) base = (std::fmod(t, 4.0) < 1.0) ? -35 : -65;     // 1 s on, 3 s off
        v.push_back((float)(base + n(rng)));
    }
    return v;
}

static void testStats() {
    std::printf("[Signal statistics]\n");
    const double dt = 0.1;
    {
        const LevelStats s = analyseLevels(series("empty", dt, 60), dt, 6.0, -65.0);
        std::printf("  empty : active %.0f%%, bursts %d -> %s\n", s.activeFraction * 100, s.bursts, describeActivity(s).c_str());
        CHECK(s.activeFraction < 0.05, "empty band reads as active");
        CHECK(describeActivity(s).find("empty") != std::string::npos, "empty band not described as empty");
    }
    {
        const LevelStats s = analyseLevels(series("steady", dt, 60), dt, 6.0, -65.0);
        std::printf("  steady: active %.0f%%, bursts %d -> %s\n", s.activeFraction * 100, s.bursts, describeActivity(s).c_str());
        CHECK(describeActivity(s).find("continuous") != std::string::npos, "steady carrier not recognised");
    }
    {
        const LevelStats s = analyseLevels(series("bursty", dt, 60), dt);
        std::printf("  bursty: active %.0f%%, bursts %d, mean %.2f s, gap %.2f s\n", s.activeFraction * 100, s.bursts, s.meanBurstSec, s.meanGapSec);
        CHECK(s.bursts == 15, "expected 15 bursts in 60 s, got %d", s.bursts);
        CHECK(std::fabs(s.activeFraction - 0.25) < 0.03, "duty cycle %.2f, expected 0.25", s.activeFraction);
        CHECK(std::fabs(s.meanBurstSec - 1.0) < 0.15, "burst length %.2f", s.meanBurstSec);
        CHECK(std::fabs(s.meanGapSec - 3.0) < 0.2, "gap length %.2f", s.meanGapSec);
        CHECK(describeActivity(s).find("Comes and goes") != std::string::npos, "bursty not described");
    }
    {
        const LevelStats s = analyseLevels(series("steady", dt, 60), dt);
        CHECK(describeActivity(s).find("hardly changes") != std::string::npos, "unknown-noise steady case not flagged as ambiguous");
    }
    {
        const LevelStats s = analyseLevels({}, dt);
        CHECK(s.n == 0, "empty input");
        CHECK(describeActivity(analyseLevels(series("steady", dt, 1), dt)).find("not enough") != std::string::npos, "short data not flagged");
    }
}

int main() {
    testJournal();
    testStats();
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL LAB TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
