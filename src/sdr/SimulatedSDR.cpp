// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "SimulatedSDR.h"

#include <algorithm>
#include <cmath>

namespace orl {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRefGainDb = 25.0;        // station amplitudes are defined at this gain
constexpr float  kNoiseSigmaRef = 0.0142f; // per I/Q component at the reference gain
constexpr float  kDcI = 0.004f, kDcQ = -0.003f;

struct SinTable {
    float v[8192];
    SinTable() { for (int i = 0; i < 8192; ++i) v[i] = (float)std::sin(2.0 * kPi * i / 8192.0); }
};
const SinTable& sinTable() { static SinTable t; return t; }

inline uint32_t phaseInc(double hz, double fs) {
    // Wraps naturally modulo 2^32, which is exactly what a phase accumulator needs.
    const double turns = hz / fs;
    const int64_t v = (int64_t)std::llround(turns * 4294967296.0);
    return (uint32_t)(v & 0xFFFFFFFFll);
}

float keyEnv(double period, double onTime, double phaseOff, double t) {
    if (period <= 0) return 1.0f;
    const double ph = std::fmod(t + phaseOff, period);
    if (ph >= onTime) return 0.0f;
    const double ramp = 0.01;
    return (float)std::min(1.0, std::min(ph / ramp, (onTime - ph) / ramp));
}

} // namespace

SimulatedSDR::SimulatedSDR() {
    // Deterministic fictional radio world. Frequencies are chosen so common
    // beginner tasks (find FM stations, spot an intermittent signal) work.
    const float strong = 0.22f, mid = 0.10f, weak = 0.04f;

    // Broadcast FM, 200 kHz spacing.
    addStation(Kind::WFM,  88.1, weak);
    addStation(Kind::WFM,  89.5, mid);
    addStation(Kind::WFM,  91.1, strong);
    addStation(Kind::WFM,  93.3, weak);
    addStation(Kind::WFM,  94.9, mid);
    addStation(Kind::WFM,  96.5, strong);
    addStation(Kind::WFM,  98.3, mid);
    addStation(Kind::WFM, 100.5, strong);
    addStation(Kind::WFM, 101.9, weak);
    addStation(Kind::WFM, 103.7, mid);
    addStation(Kind::WFM, 105.3, strong);
    addStation(Kind::WFM, 106.1, weak);
    addStation(Kind::WFM, 107.3, mid);

    // Medium-wave and shortwave AM carriers with speech-like modulation.
    addStation(Kind::AM,   0.58, 0.07f);
    addStation(Kind::AM,   0.86, 0.05f);
    addStation(Kind::AM,   1.14, 0.06f);
    addStation(Kind::AM,   5.00, 0.04f);
    addStation(Kind::AM,   9.60, 0.05f);
    addStation(Kind::AM,  11.80, 0.03f);

    // Airband AM: voice transmissions that come and go.
    addStation(Kind::AM, 118.100, 0.06f, 7.0, 2.2);
    addStation(Kind::AM, 121.500, 0.05f, 11.0, 3.0);
    addStation(Kind::AM, 124.350, 0.07f, 9.0, 1.6);

    // Narrowband FM: intermittent voice and a steady weather-style channel.
    addStation(Kind::NFM, 145.000, 0.05f, 5.0, 2.0);
    addStation(Kind::NFM, 146.520, 0.04f, 13.0, 4.0);
    addStation(Kind::NFM, 162.400, 0.05f);
    addStation(Kind::NFM, 162.475, 0.03f);

    // A short keyed carrier, like a remote-control burst.
    addStation(Kind::OOK, 433.920, 0.06f, 1.5, 0.08);
}

SimulatedSDR::~SimulatedSDR() { stop(); }

void SimulatedSDR::addStation(Kind k, double mhz, float amp, double period, double on) {
    Station s{};
    s.kind = k;
    s.freqHz = mhz * 1e6;
    s.amp = amp;
    s.period = period;
    s.onTime = on;
    // Deterministic per-station variation derived from the frequency.
    const double h = std::fmod(mhz * 9973.0, 1.0);
    s.phaseOff = h * (period > 0 ? period : 1.0);
    if (k == Kind::WFM) {            // music-ish tones
        s.tone[0] = 220.0 + 900.0 * h;
        s.tone[1] = 1300.0 + 1900.0 * h;
        s.tone[2] = 3100.0 + 4100.0 * (1.0 - h);
        s.lfoHz = 0.25 + 0.5 * h;
    } else {                         // speech-ish tones
        s.tone[0] = 300.0 + 400.0 * h;
        s.tone[1] = 900.0 + 700.0 * (1.0 - h);
        s.tone[2] = 1800.0 + 900.0 * h;
        s.lfoHz = 2.0 + 3.0 * h;
    }
    s.lfoPh = 6.283 * h;
    st_.push_back(s);
}

float SimulatedSDR::gauss() {
    // Sum of four uniforms ~ Gaussian, unit variance. One 64-bit draw feeds all four.
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 7; rng_ ^= rng_ << 17;
    const uint64_t x = rng_;
    const float u = (float)(x & 0xFFFF) + (float)((x >> 16) & 0xFFFF)
                  + (float)((x >> 32) & 0xFFFF) + (float)((x >> 48) & 0xFFFF);
    return (u / 65536.0f - 2.0f) * 1.7320508f;
}

bool SimulatedSDR::open(std::string&) { opened_ = true; return true; }
void SimulatedSDR::close() { stop(); opened_ = false; }

bool SimulatedSDR::setCenterFrequency(uint32_t hz) {
    // Idealised wide-range tuner: 100 kHz .. 1.766 GHz.
    hz = std::max<uint32_t>(100000u, std::min<uint32_t>(1766000000u, hz));
    fc_ = hz;
    return true;
}
bool SimulatedSDR::setSampleRate(uint32_t hz) {
    hz = std::max<uint32_t>(225000u, std::min<uint32_t>(3200000u, hz));
    fs_ = hz;
    return true;
}
bool SimulatedSDR::setGainDb(double db) {
    gainTenths_ = (int)std::lround(std::max(0.0, std::min(50.0, db)) * 10.0);
    return true;
}
bool SimulatedSDR::setAutoGain(bool on) {
    if (on) gainTenths_ = 250;
    return true;
}

DeviceCaps SimulatedSDR::caps() const {
    DeviceCaps c;
    c.minFreqHz = 100000u;
    c.maxFreqHz = 1766000000u;
    c.sampleRates = {250000, 1024000, 1920000, 2048000, 2400000, 2560000};
    c.minGainDb = 0;
    c.maxGainDb = 50;
    return c;
}

bool SimulatedSDR::start(SampleSink sink) {
    if (!opened_ || running_) return false;
    sink_ = std::move(sink);
    running_ = true;
    th_.start([this] { run(); });
    return true;
}

void SimulatedSDR::stop() {
    running_ = false;
    th_.join();
}

void SimulatedSDR::run() {
    const size_t block = 16384;
    std::vector<float> buf(block * 2);
    double t0 = nowSeconds();
    double simT = 0.0;
    uint64_t produced = 0;
    uint32_t lastFs = fs_;

    while (running_) {
        const uint32_t fs = fs_;
        if (fs != lastFs) { lastFs = fs; t0 = nowSeconds(); produced = 0; }

        renderBlock(buf.data(), block, (double)fc_.load(), (double)fs, simT, gainTenths_.load() / 10.0);
        sink_(buf.data(), block);

        simT += (double)block / fs;
        produced += block;

        // Pace to real time using absolute targets so timer jitter doesn't accumulate.
        const double target = t0 + (double)produced / fs;
        const double now = nowSeconds();
        if (target > now) sleepSeconds(target - now);
        else if (now - target > 0.5) { t0 = now; produced = 0; }   // fell behind: resync
    }
}

void SimulatedSDR::renderBlock(float* out, size_t n, double fc, double fs, double t, double gainDb) {
    const float* T = sinTable().v;
    auto S = [T](uint32_t p) { return T[p >> 19]; };
    auto C = [T](uint32_t p) { return T[((p >> 19) + 2048) & 8191]; };

    // Receiver noise floor plus the small DC offset every real dongle has.
    for (size_t i = 0; i < n; ++i) {
        out[2 * i]     = gauss() * kNoiseSigmaRef + kDcI;
        out[2 * i + 1] = gauss() * kNoiseSigmaRef + kDcQ;
    }

    for (auto& s : st_) {
        const double off = s.freqHz - fc;
        if (std::fabs(off) > 0.46 * fs) continue;      // outside the usable passband

        const float env = keyEnv(s.period, s.onTime, s.phaseOff, t);
        if (env <= 0.0f) continue;
        const float lfo = 0.65f + 0.35f * (float)std::sin(2.0 * kPi * s.lfoHz * t + s.lfoPh);
        const float amp = s.amp * env;

        const uint32_t inc = phaseInc(off, fs);
        const uint32_t i0 = phaseInc(s.tone[0], fs), i1 = phaseInc(s.tone[1], fs), i2 = phaseInc(s.tone[2], fs);
        uint32_t cph = s.cph, p0 = s.ap[0], p1 = s.ap[1], p2 = s.ap[2], pil = s.pilot;

        switch (s.kind) {
            case Kind::WFM: {
                const uint32_t pinc = phaseInc(19000.0, fs);     // stereo pilot tone
                const float devf = (float)(int32_t)((75000.0 / fs) * 4294967296.0);
                for (size_t i = 0; i < n; ++i) {
                    const float m = (0.5f * S(p0) + 0.3f * S(p1) + 0.2f * S(p2)) * lfo * 0.9f + 0.09f * S(pil);
                    p0 += i0; p1 += i1; p2 += i2; pil += pinc;
                    cph += inc + (uint32_t)(int32_t)(m * devf);
                    out[2 * i]     += amp * C(cph);
                    out[2 * i + 1] += amp * S(cph);
                }
                break;
            }
            case Kind::NFM: {
                const float devf = (float)(int32_t)((3000.0 / fs) * 4294967296.0);
                for (size_t i = 0; i < n; ++i) {
                    const float m = (0.5f * S(p0) + 0.3f * S(p1) + 0.2f * S(p2)) * lfo;
                    p0 += i0; p1 += i1; p2 += i2;
                    cph += inc + (uint32_t)(int32_t)(m * devf);
                    out[2 * i]     += amp * C(cph);
                    out[2 * i + 1] += amp * S(cph);
                }
                break;
            }
            case Kind::AM: {
                for (size_t i = 0; i < n; ++i) {
                    const float m = (0.5f * S(p0) + 0.3f * S(p1) + 0.2f * S(p2)) * lfo;
                    p0 += i0; p1 += i1; p2 += i2;
                    cph += inc;
                    const float a = amp * (1.0f + 0.8f * m);
                    out[2 * i]     += a * C(cph);
                    out[2 * i + 1] += a * S(cph);
                }
                break;
            }
            case Kind::OOK: {
                for (size_t i = 0; i < n; ++i) {
                    cph += inc;
                    out[2 * i]     += amp * C(cph);
                    out[2 * i + 1] += amp * S(cph);
                }
                break;
            }
        }
        s.cph = cph; s.ap[0] = p0; s.ap[1] = p1; s.ap[2] = p2; s.pilot = pil;
    }

    // Gain, ADC clipping and 8-bit quantisation - the things that make real
    // receivers behave the way they do (and make "what if I raise the gain?" a real experiment).
    const float g = (float)std::pow(10.0, (gainDb - kRefGainDb) / 20.0);
    for (size_t i = 0; i < n * 2; ++i) {
        float v = out[i] * g;
        v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
        out[i] = std::floor(v * 127.5f + 0.5f) / 127.5f;
    }
}

} // namespace orl
