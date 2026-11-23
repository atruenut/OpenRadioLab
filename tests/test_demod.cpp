// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Tests for the receive chain. Each test renders the simulator's stations, runs them through the
// demodulator, and checks that the AUDIO contains exactly the tones the simulator embedded in that
// station. That exercises the mixer, channel filters, detectors and resampler end to end.
#include "../src/Bands.h"
#include "../src/dsp/DspPipeline.h"
#include "../src/dsp/Demodulator.h"
#include "../src/dsp/FFT.h"
#include "../src/dsp/Filters.h"
#include "../src/sdr/SimulatedSDR.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace orl;

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_fail; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::vector<float> renderIq(double centre, double fs, double secs) {
    SimulatedSDR sim;
    std::string err; sim.open(err);
    const size_t blk = 16384;
    const size_t total = (size_t)(fs * secs) / blk * blk;
    std::vector<float> iq(total * 2);
    double t = 0;
    for (size_t pos = 0; pos < total; pos += blk, t += blk / fs)
        sim.renderBlock(iq.data() + 2 * pos, blk, centre, fs, t, 25.0);
    return iq;
}

static std::vector<float> demod(Demodulator& d, const std::vector<float>& iq) {
    std::vector<float> audio;
    const size_t blk = 16384;
    for (size_t pos = 0; pos + blk <= iq.size() / 2; pos += blk) d.process(iq.data() + 2 * pos, blk, audio);
    return audio;
}

// Strongest spectral peaks of an audio clip (frequencies in Hz), ignoring the first 0.4 s.
static std::vector<double> topPeaks(const std::vector<float>& a, int count) {
    const int N = 32768;
    std::vector<double> none;
    if (a.size() < (size_t)(N + 19200)) return none;
    std::vector<cf> x(N);
    const std::vector<float> w = makeWindow(WindowType::Hann, N);
    const size_t start = a.size() - N;
    for (int i = 0; i < N; ++i) { x[i].r = a[start + i] * w[i]; x[i].i = 0; }
    FFT fft(N); fft.forward(x.data());
    std::vector<double> p(N / 2);
    for (int k = 0; k < N / 2; ++k) p[k] = (double)x[k].r * x[k].r + (double)x[k].i * x[k].i;
    std::vector<double> out;
    const int guard = (int)(40.0 / (48000.0 / N));
    for (int n = 0; n < count; ++n) {
        int best = -1;
        for (int k = 5; k < N / 2; ++k) if (best < 0 || p[k] > p[best]) best = k;
        if (best < 0 || p[best] <= 0) break;
        out.push_back(best * 48000.0 / N);
        for (int k = std::max(0, best - guard); k <= std::min(N / 2 - 1, best + guard); ++k) p[k] = 0;
    }
    return out;
}

static double rms(const std::vector<float>& a, size_t from) {
    double s = 0; size_t n = 0;
    for (size_t i = from; i < a.size(); ++i) { s += (double)a[i] * a[i]; ++n; }
    return n ? std::sqrt(s / n) : 0;
}

static bool hasTone(const std::vector<double>& peaks, double f, double tol = 25.0) {
    for (double p : peaks) if (std::fabs(p - f) <= tol) return true;
    return false;
}

static double hfrac(double mhz) { return std::fmod(mhz * 9973.0, 1.0); }   // mirrors the simulator's station design

static void testFirDecimator() {
    std::printf("[FIR decimator: passband and alias rejection]\n");
    const double fs = 2.048e6;
    auto taps = designLowpass(fs, 100e3, 156e3);
    std::printf("  taps: %zu\n", taps.size());
    auto gainAt = [&](double f) {
        FirDecimator d; d.configure(taps, 8, true);
        const size_t n = 65536;
        std::vector<float> xi(n), xq(n), oi, oq;
        for (size_t k = 0; k < n; ++k) { xi[k] = (float)std::cos(2 * M_PI * f / fs * k); xq[k] = (float)std::sin(2 * M_PI * f / fs * k); }
        d.process(xi.data(), xq.data(), n, oi, oq);
        double p = 0; size_t c = 0;
        for (size_t k = oi.size() / 2; k < oi.size(); ++k) { p += (double)oi[k] * oi[k] + (double)oq[k] * oq[k]; ++c; }
        return 10.0 * std::log10(p / c + 1e-30);
    };
    const double g50 = gainAt(50e3), g90 = gainAt(90e3), g300 = gainAt(300e3), g500 = gainAt(500e3);
    std::printf("  gain at 50 kHz %.2f dB, 90 kHz %.2f dB, 300 kHz (aliases to 44 kHz) %.1f dB, 500 kHz %.1f dB\n", g50, g90, g300, g500);
    CHECK(std::fabs(g50) < 0.3, "passband gain at 50 kHz is %.2f dB", g50);
    CHECK(std::fabs(g90) < 0.5, "passband gain at 90 kHz is %.2f dB", g90);
    CHECK(g300 < -60, "alias rejection at 300 kHz only %.1f dB", g300);
    CHECK(g500 < -60, "alias rejection at 500 kHz only %.1f dB", g500);
}

static void testWfm() {
    std::printf("[WFM: 100.5 MHz station, view centred on 100.0 MHz (offset +500 kHz)]\n");
    const double fs = 2.048e6;
    auto iq = renderIq(100.0e6, fs, 2.5);
    Demodulator d; d.setSampleRate(fs); d.setMode(Demodulator::Mode::WFM, 0); d.setOffsetHz(500e3); d.setVolume(1.0f);
    auto audio = demod(d, iq);
    const double expectN = iq.size() / 2.0 * 48000.0 / fs;
    std::printf("  audio samples: %zu (expected about %.0f)\n", audio.size(), expectN);
    CHECK(std::fabs(audio.size() - expectN) < expectN * 0.01 + 50, "wrong audio length");
    const double h = hfrac(100.5);
    const double t0 = 220 + 900 * h, t1 = 1300 + 1900 * h, t2 = 3100 + 4100 * (1 - h);
    auto pk = topPeaks(audio, 5);
    std::printf("  expected tones %.0f, %.0f, %.0f Hz; strongest peaks:", t0, t1, t2);
    for (double p : pk) std::printf(" %.0f", p);
    std::printf("\n  channel level %.1f dBFS, rms %.3f\n", d.levelDb(), rms(audio, 24000));
    CHECK(hasTone(pk, t0) && hasTone(pk, t1) && hasTone(pk, t2), "WFM tones not recovered");
}

static void testNfmAndSelectivity() {
    std::printf("[NFM: 162.400 MHz weather channel, view centred on 162.0 MHz]\n");
    const double fs = 2.048e6;
    auto iq = renderIq(162.0e6, fs, 2.5);
    Demodulator d; d.setSampleRate(fs); d.setMode(Demodulator::Mode::NFM, 12000); d.setOffsetHz(400e3); d.setVolume(1.0f);
    auto audio = demod(d, iq);
    const double h = hfrac(162.4);
    const double t0 = 300 + 400 * h, t1 = 900 + 700 * (1 - h), t2 = 1800 + 900 * h;
    auto pk = topPeaks(audio, 4);
    std::printf("  expected tones %.0f, %.0f, %.0f Hz; strongest peaks:", t0, t1, t2);
    for (double p : pk) std::printf(" %.0f", p);
    const double lvlOn = d.levelDb();
    std::printf("\n  channel level on station: %.1f dBFS\n", lvlOn);
    CHECK(hasTone(pk, t0) && hasTone(pk, t1) && hasTone(pk, t2), "NFM tones not recovered");

    // Selectivity: 30 kHz away from the station there is only noise.
    Demodulator e; e.setSampleRate(fs); e.setMode(Demodulator::Mode::NFM, 12000); e.setOffsetHz(430e3); e.setVolume(1.0f);
    demod(e, iq);
    const double lvlOff = e.levelDb();
    std::printf("  channel level 30 kHz away: %.1f dBFS (selectivity %.1f dB)\n", lvlOff, lvlOn - lvlOff);
    CHECK(lvlOn - lvlOff > 25, "channel filter selectivity only %.1f dB", lvlOn - lvlOff);

    // Squelch should open on the station and close on the empty channel.
    const double thr = 0.5 * (lvlOn + lvlOff);
    Demodulator s1; s1.setSampleRate(fs); s1.setMode(Demodulator::Mode::NFM, 12000); s1.setOffsetHz(400e3); s1.setVolume(1.0f); s1.setSquelchDb(thr);
    auto a1 = demod(s1, iq);
    Demodulator s2; s2.setSampleRate(fs); s2.setMode(Demodulator::Mode::NFM, 12000); s2.setOffsetHz(430e3); s2.setVolume(1.0f); s2.setSquelchDb(thr);
    auto a2 = demod(s2, iq);
    std::printf("  squelch at %.1f dBFS: rms on station %.3f (open=%d), rms on empty channel %.4f (open=%d)\n",
                thr, rms(a1, 24000), (int)s1.squelchOpen(), rms(a2, 24000), (int)s2.squelchOpen());
    CHECK(s1.squelchOpen() && rms(a1, 24000) > 0.02, "squelch should be open on the station");
    CHECK(!s2.squelchOpen() && rms(a2, 24000) < 1e-6, "squelch should close on the empty channel");

    // Mute silences output.
    Demodulator m; m.setSampleRate(fs); m.setMode(Demodulator::Mode::NFM, 12000); m.setOffsetHz(400e3); m.setMute(true);
    auto am = demod(m, iq);
    CHECK(rms(am, 0) == 0.0, "mute should give silence");
}

static void testAm() {
    std::printf("[AM: 5.000 MHz station, on centre and offset]\n");
    const double fs = 2.048e6;
    const double h = hfrac(5.0);
    const double t0 = 300 + 400 * h, t1 = 900 + 700 * (1 - h), t2 = 1800 + 900 * h;
    for (int variant = 0; variant < 2; ++variant) {
        const double centre = variant == 0 ? 5.0e6 : 4.9e6, off = variant == 0 ? 0.0 : 100e3;
        auto iq = renderIq(centre, fs, 2.5);
        Demodulator d; d.setSampleRate(fs); d.setMode(Demodulator::Mode::AM, 8000); d.setOffsetHz(off); d.setVolume(1.0f);
        auto audio = demod(d, iq);
        auto pk = topPeaks(audio, 4);
        std::printf("  centre %.1f MHz, offset %+.0f kHz: expected %.0f, %.0f, %.0f Hz; peaks:", centre / 1e6, off / 1e3, t0, t1, t2);
        for (double p : pk) std::printf(" %.0f", p);
        std::printf("\n");
        CHECK(hasTone(pk, t0) && hasTone(pk, t1) && hasTone(pk, t2), "AM tones not recovered (variant %d)", variant);
        const double r = rms(audio, 24000);
        std::printf("  audio rms %.3f (carrier-normalised: modulation index 0.8 * tone mix)\n", r);
        CHECK(r > 0.15 && r < 0.9, "AM audio level %.3f outside the expected range", r);
    }
}

static void testSampleRates() {
    std::printf("[All offered sample rates demodulate to the right audio length]\n");
    for (double fs : {250000.0, 1024000.0, 1920000.0, 2048000.0, 2400000.0, 2560000.0}) {
        auto iq = renderIq(100.0e6, fs, 1.0);
        Demodulator d; d.setSampleRate(fs); d.setMode(Demodulator::Mode::WFM, 0); d.setOffsetHz(0);
        auto audio = demod(d, iq);
        const double expectN = iq.size() / 2.0 * 48000.0 / fs;
        std::printf("  %.3f MS/s -> %zu audio samples (expected %.0f)\n", fs / 1e6, audio.size(), expectN);
        CHECK(std::fabs(audio.size() - expectN) < expectN * 0.02 + 100, "audio length wrong at %.0f S/s", fs);
    }
}

static void testCpuCost() {
    std::printf("[CPU cost at 2.4 MS/s]\n");
    const double fs = 2.4e6;
    auto iq = renderIq(100.0e6, fs, 1.0);
    for (int m = 0; m < 3; ++m) {
        Demodulator d; d.setSampleRate(fs);
        d.setMode((Demodulator::Mode)m, m == 1 ? 12000 : 8000); d.setOffsetHz(300e3);
        const double t0 = nowSeconds();
        demod(d, iq);
        const double dt = nowSeconds() - t0;
        std::printf("  %s: %.0f ms of CPU per second of signal (%.1f%% of one core)\n", m == 0 ? "WFM" : (m == 1 ? "NFM" : "AM "), dt * 1000, dt * 100);
        CHECK(dt < 0.6, "demodulator too slow: %.2f s per second of signal", dt);
    }
}

static void testPipeline() {
    std::printf("[DSP thread pipeline fed by the live simulator for 1.5 s]\n");
    SpectrumProcessor sp; sp.configure(2048, WindowType::Hann); sp.setLinesPerSecond(30); sp.setTuning(100e6, 2.048e6);
    Demodulator dm; dm.setSampleRate(2.048e6); dm.setMode(Demodulator::Mode::WFM, 0); dm.setOffsetHz(500e3);
    DspPipeline pipe(sp, dm);
    Mutex m; size_t got = 0;
    pipe.setAudioSink([&](const float*, size_t n, int) { Lock l(m); got += n; });
    pipe.start();
    SimulatedSDR sim; std::string err; sim.open(err);
    sim.setCenterFrequency(100000000); sim.setSampleRate(2048000);
    sim.start([&](const float* iq, size_t n) { pipe.push(iq, n); });
    sleepSeconds(1.5);
    sim.stop(); sleepSeconds(0.2); pipe.stop();
    size_t g; { Lock l(m); g = got; }
    std::vector<SpectrumFrame> fr; sp.drain(fr);
    std::printf("  audio samples %zu (about %.0f expected), spectrum frames %zu, dropped blocks %llu\n",
                g, 1.5 * 48000.0, fr.size(), (unsigned long long)pipe.droppedBlocks());
    CHECK(g > 55000 && g < 80000, "pipeline audio count %zu is off", g);
    CHECK(fr.size() > 30, "pipeline produced too few spectrum frames");
    CHECK(pipe.droppedBlocks() == 0, "pipeline dropped blocks under normal load");
}

static void testBands() {
    std::printf("[Band table]\n");
    CHECK(findBand(100.5e6) && findBand(100.5e6)->mode == 0, "100.5 MHz should suggest WFM");
    CHECK(findBand(123.0e6) && findBand(123.0e6)->mode == 2, "123 MHz airband should suggest AM");
    CHECK(findBand(162.475e6) && findBand(162.475e6)->mode == 1, "162.475 MHz should suggest NFM");
    CHECK(findBand(5e6) && findBand(5e6)->mode == 2, "5 MHz shortwave should suggest AM");
    CHECK(findBand(50e6) == nullptr, "50 MHz is not in the table");
}

int main() {
    testBands();
    testFirDecimator();
    testWfm();
    testNfmAndSelectivity();
    testAm();
    testSampleRates();
    testCpuCost();
    testPipeline();
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL DEMOD TESTS PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
