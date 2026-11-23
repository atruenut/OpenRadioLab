// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Stereo 48 kHz audio output (mono sources are played in both ears) through the Windows Multimedia API (waveOut).
// write() may be called from any thread; a dedicated audio thread keeps the device fed. If audio
// arrives faster than the sound card plays it (the dongle's clock and the sound card's clock never
// match exactly) the oldest audio is dropped so latency cannot grow; if it arrives too slowly the
// device is fed silence.
#pragma once

#include "../Sync.h"

#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace orl {

class AudioOutput {
public:
    AudioOutput() = default;
    ~AudioOutput() { close(); }

    // Index 0 is always "System default"; the rest are the installed output devices.
    static std::vector<std::string> listDevices();

    bool open(int deviceIndex, std::string& error);
    void close();
    bool isOpen() const { return hwo_ != nullptr; }

    // channels = 1: n mono samples; channels = 2: n interleaved L,R frames.
    void write(const float* data, size_t frames, int channels = 1);
    void clear();

private:
    void run();
    void fill(int i);

    static const int kBufs = 8;
    static const int kFrames = 960;          // 20 ms per buffer at 48 kHz

    HWAVEOUT hwo_ = nullptr;
    WAVEHDR hdr_[kBufs];
    std::vector<int16_t> data_[kBufs];
    HANDLE ev_ = nullptr;
    Thread th_;
    std::atomic<bool> stop_{false};
    Mutex m_;
    std::deque<float> ring_;
};

} // namespace orl
