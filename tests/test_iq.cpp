// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Record the simulator to a file, play it back as a device, and check the spectrum still has the stations.
#include "../src/io/IqFile.h"
#include "../src/dsp/SpectrumProcessor.h"
#include "../src/sdr/SimulatedSDR.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace orl;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const char* kPath = "/tmp/orl_test.orliq";

static void testRoundTrip() {
    std::printf("[Record -> file -> playback]\n");
    SimulatedSDR sim; std::string err; sim.open(err);
    const double fs = 2.048e6, fc = 98.0e6;
    IqRecorder rec; IqHeader h; h.rate = (uint32_t)fs; h.centreHz = (uint64_t)fc; h.simulated = true;
    CHECK(rec.start(kPath, h, 1ull << 30, err), "start: %s", err.c_str());
    std::vector<float> blk(2 * 16384);
    double t = 0;
    for (int i = 0; i < 20; ++i) { sim.renderBlock(blk.data(), 16384, fc, fs, t, 25.0); t += 16384 / fs; rec.write(blk.data(), 16384); }
    rec.stop();
    std::printf("  recorded %llu bytes\n", (unsigned long long)rec.bytes());
    CHECK(rec.bytes() == 20ull * 16384 * 2, "byte count %llu", (unsigned long long)rec.bytes());

    IqFileDevice dev(kPath);
    CHECK(dev.open(err), "open: %s", err.c_str());
    CHECK(dev.isSimulated() && dev.isPlayback(), "flags lost");
    CHECK(dev.header().rate == 2048000 && dev.header().centreHz == 98000000ull, "header mismatch");
    CHECK(std::fabs(dev.durationSeconds() - 20 * 16384 / fs) < 1e-6, "duration %.4f", dev.durationSeconds());
    const DeviceCaps c = dev.caps();
    CHECK(c.sampleRates.size() == 1 && c.sampleRates[0] == 2048000, "caps rate");

    SpectrumProcessor sp; sp.configure(2048, WindowType::Hann); sp.setTuning(fc, fs); sp.setLinesPerSecond(1e9);
    std::atomic<size_t> got{0};
    const double t0 = nowSeconds();
    dev.start([&](const float* iq, size_t n) { sp.pushSamples(iq, n); got += n; });
    while (got < 3 * 16384 * 20 && nowSeconds() - t0 < 5.0) sleepSeconds(0.01);   // 3 loops of the file
    const double el = nowSeconds() - t0;
    dev.stop();
    std::printf("  played %zu samples in %.2f s (real time would be %.2f s)\n", got.load(), el, got.load() / fs);
    CHECK(got >= 3 * 16384 * 20, "did not loop (got %zu)", got.load());
    CHECK(el > 0.8 * got.load() / fs, "playback ran far faster than real time");
    CHECK(el < 1.5 * got.load() / fs + 0.3, "playback far slower than real time");

    std::vector<SpectrumFrame> fr; sp.drain(fr);
    CHECK(fr.size() > 20, "too few frames: %zu", fr.size());
    // average the spectrum, then look at the simulator's 98.3 MHz station
    std::vector<double> avg(2048, 0.0);
    for (auto& f : fr) for (int j = 0; j < 2048; ++j) avg[j] += std::pow(10.0, f.db[j] / 10.0);
    auto bin = [&](double hz) { return 1024 + (int)std::lround((hz - fc) / (fs / 2048)); };
    auto level = [&](double hz) { double m = 0; for (int j = bin(hz) - 90; j <= bin(hz) + 90; ++j) m += avg[j]; return 10 * std::log10(m / 181 / fr.size()); };
    const double st = level(98.3e6), empty = level(97.3e6);
    std::printf("  98.3 MHz: %.1f dB, empty 97.3 MHz: %.1f dB\n", st, empty);
    CHECK(st - empty > 20.0, "recorded station not visible (%.1f dB over the gap)", st - empty);
    std::remove(kPath);
}

static void testRejects() {
    std::printf("[Bad files are refused politely]\n");
    FILE* f = std::fopen(kPath, "wb"); const char junk[64] = "not a recording at all"; std::fwrite(junk, 1, sizeof junk, f); std::fclose(f);
    IqFileDevice d(kPath); std::string err;
    CHECK(!d.open(err) && !err.empty(), "junk accepted");
    std::printf("  message: %s\n", err.c_str());
    IqFileDevice d2("/tmp/definitely_missing.orliq");
    CHECK(!d2.open(err), "missing file accepted");
    // header only, no data
    f = std::fopen(kPath, "wb"); IqHeader h; h.rate = 2048000; h.centreHz = 100000000; writeIqHeader(f, h); std::fclose(f);
    IqFileDevice d3(kPath);
    CHECK(!d3.open(err), "empty recording accepted");
    std::remove(kPath);
}

static void testSizeLimit() {
    std::printf("[The size limit stops a recording]\n");
    IqRecorder rec; IqHeader h; h.rate = 2048000; h.centreHz = 100000000; std::string err;
    CHECK(rec.start(kPath, h, 100000, err), "start");
    std::vector<float> blk(2 * 16384, 0.1f);
    for (int i = 0; i < 20; ++i) { rec.write(blk.data(), 16384); sleepSeconds(0.01); }
    rec.stop();
    std::printf("  bytes %llu, hit limit %d\n", (unsigned long long)rec.bytes(), (int)rec.hitLimit());
    CHECK(rec.bytes() == 100000 && rec.hitLimit(), "limit not honoured");
    std::remove(kPath);
}

static void testTransport() {
    std::printf("[Pause, seek, play once]\n");
    SimulatedSDR sim; std::string err; sim.open(err);
    const double fs = 2.048e6;
    IqRecorder rec; IqHeader h; h.rate = (uint32_t)fs; h.centreHz = 98000000; std::vector<float> blk(2 * 16384);
    rec.start(kPath, h, 1ull << 30, err);
    double t = 0;
    for (int i = 0; i < 125; ++i) { sim.renderBlock(blk.data(), 16384, 98e6, fs, t, 25.0); t += 16384 / fs; rec.write(blk.data(), 16384); }
    rec.stop();                                           // about 1.0 s
    IqFileDevice dev(kPath); CHECK(dev.open(err), "open");
    std::atomic<size_t> got{0};
    dev.start([&](const float*, size_t n) { got += n; });
    sleepSeconds(0.2);
    dev.setPaused(true); sleepSeconds(0.1);
    const size_t g1 = got.load(); const double p1 = dev.positionSeconds();
    sleepSeconds(0.3);
    CHECK(got.load() == g1, "samples kept flowing while paused (%zu -> %zu)", g1, got.load());
    CHECK(std::fabs(dev.positionSeconds() - p1) < 1e-9, "position moved while paused");
    std::printf("  paused at %.2f s\n", p1);
    dev.seekSeconds(0.7); sleepSeconds(0.1);
    CHECK(std::fabs(dev.positionSeconds() - 0.7) < 0.03, "seek to 0.7 s landed at %.3f s", dev.positionSeconds());
    dev.setLoop(false); dev.setPaused(false);
    const double t0 = nowSeconds();
    while (!dev.atEnd() && nowSeconds() - t0 < 2.0) sleepSeconds(0.01);
    std::printf("  reached the end after %.2f s (0.3 s of recording left), at end: %d\n", nowSeconds() - t0, (int)dev.atEnd());
    CHECK(dev.atEnd(), "did not stop at the end");
    CHECK(dev.isRunning(), "device died at the end (the app would fall back to the simulator)");
    const size_t g2 = got.load(); sleepSeconds(0.2);
    CHECK(got.load() == g2, "still playing after the end");
    dev.seekSeconds(0); dev.setPaused(false); sleepSeconds(0.15);
    CHECK(got.load() > g2 && dev.positionSeconds() < 0.4, "restart from the beginning failed");
    dev.stop();
    std::remove(kPath);
}

int main() {
    testTransport();
    testRoundTrip();
    testRejects();
    testSizeLimit();
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL IQ FILE TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
