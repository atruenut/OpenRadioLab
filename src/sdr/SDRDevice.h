// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Hardware abstraction. The rest of the application only ever talks to this
// interface, so a simulated device and a real dongle are interchangeable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orl {

// Interleaved float I,Q samples in [-1, 1]; numComplex = number of I/Q pairs.
using SampleSink = std::function<void(const float* iq, size_t numComplex)>;

struct DeviceCaps {
    uint32_t minFreqHz = 0;
    uint32_t maxFreqHz = 0;
    std::vector<uint32_t> sampleRates;   // suggested rates for the UI
    double minGainDb = 0;
    double maxGainDb = 50;
};

class SDRDevice {
public:
    virtual ~SDRDevice() = default;

    virtual std::string name() const = 0;          // short, for the UI
    virtual std::string description() const = 0;   // longer, e.g. tuner chip
    virtual bool isSimulated() const = 0;
    virtual bool isPlayback() const { return false; }   // a recording: fixed tuning, loops forever

    virtual bool open(std::string& error) = 0;
    virtual void close() = 0;

    virtual bool setCenterFrequency(uint32_t hz) = 0;
    virtual bool setSampleRate(uint32_t hz) = 0;
    virtual bool setGainDb(double db) = 0;
    virtual bool setAutoGain(bool on) = 0;

    virtual bool start(SampleSink sink) = 0;       // returns immediately
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;           // false after an unplug / USB error

    virtual DeviceCaps caps() const = 0;
};

} // namespace orl
