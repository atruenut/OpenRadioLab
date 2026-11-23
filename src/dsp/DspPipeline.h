// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// The DSP thread. The device thread only copies samples into a small bounded queue and returns
// immediately; this thread then feeds the spectrum analyser and the demodulator. USB reception
// therefore never waits on FFTs or filters, and the UI never waits on either.
#pragma once

#include "Demodulator.h"
#include "SpectrumProcessor.h"
#include "../Sync.h"

#include <atomic>
#include <deque>
#include <functional>
#include <vector>

namespace orl {

class DspPipeline {
public:
    using AudioSink = std::function<void(const float* audio, size_t frames, int channels)>;

    DspPipeline(SpectrumProcessor& sp, Demodulator& dm) : sp_(sp), dm_(dm) {}
    ~DspPipeline() { stop(); }

    void setAudioSink(AudioSink s) { sink_ = std::move(s); }     // call before start()
    void setAudioEnabled(bool on) { audioOn_ = on; }
    void start();
    void stop();

    void push(const float* iq, size_t numComplex);               // device thread; never blocks on DSP
    void flush();                                                // drop queued samples, reset the demodulator

    uint64_t droppedBlocks() const { return dropped_.load(); }

private:
    void run();

    SpectrumProcessor& sp_;
    Demodulator& dm_;
    AudioSink sink_;
    Mutex qm_;
    std::deque<std::vector<float>> q_;
    std::vector<std::vector<float>> pool_;
    Event ev_;
    Thread th_;
    std::atomic<bool> running_{false}, audioOn_{true};
    std::atomic<uint64_t> dropped_{0};
    std::vector<float> audio_;
    static constexpr size_t kMaxQueue = 12;
};

} // namespace orl
