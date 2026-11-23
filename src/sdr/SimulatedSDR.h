// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// A software-only SDR that synthesises a small, fictional radio world
// (broadcast FM, AM, intermittent narrowband bursts, a keyed carrier) plus the
// imperfections of real hardware (noise floor, DC spike, 8-bit quantisation,
// ADC clipping at high gain). Everything it produces is SIMULATED and must
// always be labelled as such in the UI.
#pragma once

#include "SDRDevice.h"
#include "../Sync.h"

#include <atomic>
#include <vector>

namespace orl {

class SimulatedSDR : public SDRDevice {
public:
    SimulatedSDR();
    ~SimulatedSDR() override;

    std::string name() const override { return "Simulated SDR"; }
    std::string description() const override {
        return "Simulated SDR - synthetic signals, no hardware attached";
    }
    bool isSimulated() const override { return true; }

    bool open(std::string& error) override;
    void close() override;

    bool setCenterFrequency(uint32_t hz) override;
    bool setSampleRate(uint32_t hz) override;
    bool setGainDb(double db) override;
    bool setAutoGain(bool on) override;

    bool start(SampleSink sink) override;
    void stop() override;
    bool isRunning() const override { return running_; }

    DeviceCaps caps() const override;

    // Exposed for tests: render one block synchronously.
    void renderBlock(float* out, size_t n, double centerHz, double rate, double tSec, double gainDb);

private:
    enum class Kind { WFM, AM, NFM, OOK };
    struct Station {
        Kind kind;
        double freqHz;
        float amp;                 // linear amplitude at the 25 dB reference gain
        double period, onTime, phaseOff;   // keying (period 0 = always on)
        double tone[3];            // "audio" tone frequencies in Hz
        double lfoHz, lfoPh;       // slow level variation
        uint32_t cph, ap[3], pilot;
    };

    void addStation(Kind k, double mhz, float amp, double period = 0, double on = 0);
    void run();
    float gauss();

    std::vector<Station> st_;
    uint64_t rng_ = 0x9E3779B97F4A7C15ull;

    std::atomic<uint32_t> fc_{100000000};
    std::atomic<uint32_t> fs_{2048000};
    std::atomic<int> gainTenths_{250};
    std::atomic<bool> running_{false};
    SampleSink sink_;
    Thread th_;
    bool opened_ = false;
};

} // namespace orl
