// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "../src/dsp/BandScanner.h"
#include "../src/dsp/Survey.h"
#include <cmath>
#include <cstdio>
#include <random>
using namespace orl;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// a 30 MHz stitched spectrum, 1 kHz bins, noise floor about -70 dBFS
static const double lo = 144e6, bin = 1e3;
static std::vector<float> noise(unsigned seed, size_t n = 4000) {
    std::mt19937 rng(seed); std::normal_distribution<double> nd(0, 1.0);
    std::vector<float> v(n); for (auto& x : v) x = (float)(-70 + nd(rng)); return v;
}
static void hump(std::vector<float>& v, double hz, double db, double widthHz) {
    const int c = (int)std::lround((hz - lo) / bin), h = (int)(widthHz / bin / 2);
    for (int k = -h - 3; k <= h + 3; ++k) { const float val = (float)(k >= -h && k <= h ? db : db - 8 * (std::abs(k) - h)); if (c + k >= 0 && c + k < (int)v.size()) v[c + k] = std::max(v[c + k], val); }
}

int main() {
    std::printf("[Finding signals]\n");
    auto v = noise(1);
    hump(v, 146.52e6, -40, 12e3);            // a voice channel
    hump(v, 147.0e6, -55, 2e3);              // a weak narrow carrier
    for (int k = 3300; k < 4000; ++k) v[k] = std::max<float>(v[k], -45);      // a 700 kHz wide block
    auto s = findSignals(v, lo, bin);
    for (auto& f : s) std::printf("  %.3f MHz  width %.0f kHz  snr %.0f dB\n", f.hz / 1e6, f.widthHz / 1e3, f.snrDb);
    CHECK(s.size() == 3, "expected 3 signals, found %zu", s.size());
    bool voice = false, carrier = false, block = false;
    for (auto& f : s) {
        if (std::fabs(f.hz - 146.52e6) < 8e3 && f.widthHz > 6e3 && f.widthHz < 25e3) voice = true;
        if (std::fabs(f.hz - 147.0e6) < 5e3 && f.widthHz < 8e3) carrier = true;
        if (f.widthHz > 600e3) block = true;
    }
    CHECK(voice, "voice channel"); CHECK(carrier, "carrier"); CHECK(block, "wide block");
    CHECK(findSignals(noise(2), lo, bin).empty(), "found signals in pure noise");

    std::printf("[Remembering across passes]\n");
    DiscoveryTable t;
    auto p1 = noise(3), p2 = noise(4), p3 = noise(5);
    hump(p1, 146.52e6, -40, 12e3); hump(p2, 146.521e6, -42, 12e3); hump(p3, 146.52e6, -41, 12e3);
    hump(p1, 147.0e6, -50, 12e3);                                     // heard once only
    t.addSignals(findSignals(p1, lo, bin)); t.endPass();
    t.addSignals(findSignals(p2, lo, bin)); t.endPass();
    t.addSignals(findSignals(p3, lo, bin)); t.endPass();
    auto rows = t.rows();
    for (auto& r : rows) std::printf("  %.3f MHz seen %d/%d active %d  %s\n", r.hz / 1e6, r.seen, r.passes, r.activeNow, r.where.c_str());
    CHECK(rows.size() == 2, "rows %zu", rows.size());
    CHECK(!rows.empty() && std::fabs(rows[0].hz - 146.52e6) < 10e3 && rows[0].seen == 3 && rows[0].activeNow, "best row");
    CHECK(rows.size() == 2 && rows[1].seen == 1 && !rows[1].activeNow, "once-heard row");
    CHECK(!rows.empty() && rows[0].known && rows[0].mode == 1, "146.52 should be NFM in a known band: mode %d known %d", rows.empty() ? -1 : rows[0].mode, rows.empty() ? 0 : rows[0].known);
    CHECK(!rows.empty() && rows[0].what.find("not an identification") != std::string::npos, "wording");

    std::printf("[Whole pipeline: hop across 144-148 MHz, stitch, find a 12 kHz voice signal at 146.52 MHz]\n");
    {
        BandScanner sc; BandScanner::Plan p; p.lo = 144e6; p.hi = 148e6; p.rate = 2048000; p.fftSize = 2048; p.chCount = 0; p.settleFrames = 2; p.collectFrames = 3;
        sc.begin(p);
        std::mt19937 rng(9); std::normal_distribution<double> nd(0, 1.0);
        int guard = 0;
        while (!sc.done() && guard++ < 1000) {
            SpectrumFrame f; f.centerHz = sc.centre(); f.sampleRate = p.rate; f.db.resize(2048);
            for (int j = 0; j < 2048; ++j) {
                const double hz = f.centerHz + (j - 1024) * (p.rate / 2048.0);
                double v = -70 + nd(rng);
                if (std::fabs(hz - 146.52e6) < 6e3) v = std::max(v, -42.0);
                if (j == 1024) v = -30;                                 // the receiver's own centre spike
                f.db[j] = (float)v;
            }
            sc.addFrame(f);
        }
        const ScanReport rep = sc.result();
        auto sig = findSignals(rep.spectrumDb, rep.spectrumLoHz, rep.spectrumBinHz);
        for (auto& x : sig) std::printf("  %.4f MHz width %.0f kHz snr %.0f dB\n", x.hz / 1e6, x.widthHz / 1e3, x.snrDb);
        CHECK(sig.size() == 1 && std::fabs(sig[0].hz - 146.52e6) < 4e3, "pipeline found %zu signals", sig.size());
    }
    std::printf("[Sweep plans]\n");
    auto q = surveyRanges(true, 24e6, 1766e6), f = surveyRanges(false, 24e6, 1766e6);
    CHECK(q.size() >= 8 && f.size() > 40, "plans %zu %zu", q.size(), f.size());
    auto qh = surveyRanges(true, 24e6, 960e6);
    CHECK(qh.size() < q.size(), "range limits ignored");
    for (auto& r : f) CHECK(!(r.lo < 100e6 && r.hi > 90e6 && r.hi <= 108e6 && r.lo >= 87.8e6), "FM band not skipped");
    std::printf("[Saving and loading a scan]\n");
    {
        DiscoveryTable t;
        FoundSignal a; a.hz = 146.52e6; a.widthHz = 12e3; a.snrDb = 25; a.levelDb = -45;
        FoundSignal b; b.hz = 162.55e6; b.widthHz = 10e3; b.snrDb = 30; b.levelDb = -40;
        t.addSignals({a, b}); t.endPass(); t.addSignals({a}); t.endPass();
        CHECK(t.setNote(162.5504e6, "Weather\tradio"), "note not set");
        CHECK(!t.setNote(100e6, "x"), "note set on nothing");
        const std::string path = "build/_disc_test.tsv";
        CHECK(t.save(path), "save failed");
        DiscoveryTable u;
        CHECK(u.load(path), "load failed");
        auto rs = u.rows();
        CHECK(rs.size() == 2 && u.passes() == 2, "loaded %zu rows, %d passes", rs.size(), u.passes());
        bool wx = false; for (auto& r : rs) { if (std::fabs(r.hz - 162.55e6) < 1) wx = r.note == "Weather radio" && r.seen == 1; CHECK(!r.isNew && !r.activeNow, "loaded row marked new/active"); }
        CHECK(wx, "note or seen count lost");
        FoundSignal c; c.hz = 156.8e6; c.widthHz = 12e3; c.snrDb = 20; c.levelDb = -50;
        u.addSignals({a, c}); u.endPass();                      // one known, one new
        int nw = 0, act = 0; for (auto& r : u.rows()) { nw += r.isNew; act += r.activeNow; }
        CHECK(nw == 1, "%d new, want 1", nw);
        CHECK(act == 2, "%d active, want 2", act);
        const std::string csv = u.toCsv();
        CHECK(csv.find("156.8000") != std::string::npos && csv.find("Weather radio") != std::string::npos, "csv incomplete");
        DiscoveryTable v; CHECK(!v.load("build/does_not_exist.tsv"), "loaded a missing file");
        std::FILE* g = std::fopen("build/_bad.tsv", "wb"); std::fputs("hello\n", g); std::fclose(g);
        CHECK(!v.load("build/_bad.tsv"), "loaded junk");
        std::remove(path.c_str()); std::remove("build/_bad.tsv");
    }
    std::printf("[Time-of-day pattern and comparing scans]\n");
    {
        DiscoveryTable t; t.setHourForTesting(19);
        FoundSignal a; a.hz = 146.52e6; a.widthHz = 12e3; a.snrDb = 25; a.levelDb = -45;
        FoundSignal b; b.hz = 162.55e6; b.widthHz = 10e3; b.snrDb = 30; b.levelDb = -40;
        for (int h : {8, 19, 20, 3}) {
            t.setHourForTesting(h);
            if (h >= 19) t.addSignals({a, b}); else t.addSignals({b});
            t.endPass();
        }
        auto rs = t.rows(); std::string pa, pb;
        for (auto& r : rs) { if (std::fabs(r.hz - 146.52e6) < 1) pa = activityPattern(r); else pb = activityPattern(r); }
        std::printf("  %s\n", pa.c_str());
        CHECK(pa.find("19:00-20:59") != std::string::npos, "evening run not found: %s", pa.c_str());
        CHECK(pb.find("03:00-03:59") != std::string::npos && pb.find("19:00-20:59") != std::string::npos, "always-on pattern wrong: %s", pb.c_str());
        DiscoveryTable few; few.setHourForTesting(5); few.addSignals({a}); few.endPass();
        CHECK(activityPattern(few.rows()[0]).find("Not enough") != std::string::npos, "pattern shown from one hour");
        t.save("build/_h.tsv"); DiscoveryTable u; CHECK(u.load("build/_h.tsv"), "load");
        std::string pu; for (auto& r : u.rows()) if (std::fabs(r.hz - 146.52e6) < 1) pu = activityPattern(r);
        CHECK(pu == pa, "hour history lost in save/load:\n%s\nvs\n%s", pu.c_str(), pa.c_str());
        std::remove("build/_h.tsv");
        // comparing: older has a(25) and b(30); newer has b(15, weaker), c (new)
        DiscoveryTable o, n;
        o.addSignals({a, b}); o.endPass();
        FoundSignal b2 = b; b2.snrDb = 15; FoundSignal c; c.hz = 156.8e6; c.widthHz = 12e3; c.snrDb = 20; c.levelDb = -50;
        n.addSignals({b2, c}); n.endPass();
        auto ch = n.changesSince(o);
        int gone = 0, app = 0, weak = 0, strong = 0;
        for (auto& x : ch) { gone += x.kind == 0; app += x.kind == 1; strong += x.kind == 2; weak += x.kind == 3; }
        CHECK(ch.size() == 3 && gone == 1 && app == 1 && weak == 1 && strong == 0, "changes: %zu gone %d new %d weaker %d", ch.size(), gone, app, weak);
        CHECK(o.changesSince(o).empty(), "identical scans differ");
    }
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL SURVEY TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
