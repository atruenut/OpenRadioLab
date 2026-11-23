// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Recording and playback of raw radio (I/Q) captures.
//
// File format ".orliq": a 32-byte little-endian header followed by 8-bit unsigned interleaved I,Q
// pairs, exactly what an RTL-SDR produces (about 4 MB per second at 2.048 MS/s).
//   0   8 bytes  magic "ORLIQ001"
//   8   u32      sample rate (Hz)
//   12  u32      sample format (1 = unsigned 8-bit I,Q)
//   16  u64      centre frequency (Hz)
//   24  u32      flags (bit 0: the data is SIMULATED)
//   28  u32      reserved (0)
// A capture is a snapshot of one slice of the radio world, so anyone can replay it with no hardware.
#pragma once

#include "../Sync.h"
#include "../sdr/SDRDevice.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

namespace orl {

struct IqHeader {
    uint32_t rate = 0;
    uint64_t centreHz = 0;
    bool simulated = false;
};

bool writeIqHeader(FILE* f, const IqHeader& h);
bool readIqHeader(FILE* f, IqHeader& h, std::string& err);

// Writes samples handed over by the device thread; the disk is touched only on a separate thread.
class IqRecorder {
public:
    ~IqRecorder() { stop(); }
    bool start(const std::string& path, const IqHeader& h, uint64_t maxBytes, std::string& err);
    void write(const float* iq, size_t numComplex);      // device thread: converts and queues, never blocks on disk
    void stop();
    bool active() const { return active_.load(); }
    uint64_t bytes() const { return bytes_.load(); }     // sample bytes written so far
    uint64_t droppedBlocks() const { return dropped_.load(); }
    bool hitLimit() const { return limit_.load(); }
    const std::string& path() const { return path_; }

private:
    void run();
    FILE* f_ = nullptr;
    std::string path_;
    uint64_t maxBytes_ = 0;
    Mutex m_;
    Event ev_;
    Thread th_;
    std::deque<std::vector<uint8_t>> q_;
    std::atomic<bool> active_{false}, quit_{false}, limit_{false};
    std::atomic<uint64_t> bytes_{0}, dropped_{0}, queued_{0};
};

// Plays a capture back in real time, looping forever, as if it were a receiver.
class IqFileDevice : public SDRDevice {
public:
    explicit IqFileDevice(const std::string& path) : path_(path) {}
    ~IqFileDevice() override { close(); }

    std::string name() const override { return "Recording: " + base(); }
    std::string description() const override;
    bool isSimulated() const override { return hdr_.simulated; }
    bool isPlayback() const override { return true; }

    bool open(std::string& error) override;
    void close() override;
    bool setCenterFrequency(uint32_t) override { return false; }   // a recording cannot be retuned
    bool setSampleRate(uint32_t) override { return false; }
    bool setGainDb(double) override { return true; }
    bool setAutoGain(bool) override { return true; }
    bool start(SampleSink sink) override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }
    DeviceCaps caps() const override;

    // Transport controls (any thread). Playback starts playing and looping.
    void setPaused(bool p) { paused_ = p; }
    bool paused() const { return paused_.load(); }
    void setLoop(bool l) { loop_ = l; }
    bool loop() const { return loop_.load(); }
    void seekSeconds(double s) { seekReq_ = (int64_t)(std::max(0.0, s) * hdr_.rate); }
    double positionSeconds() const { return hdr_.rate ? (double)pos_.load() / hdr_.rate : 0.0; }
    bool atEnd() const { return !loop_.load() && paused_.load() && pos_.load() + 16384 >= dataBytes_ / 2; }

    double durationSeconds() const { return hdr_.rate ? (double)dataBytes_ / 2.0 / hdr_.rate : 0.0; }
    const IqHeader& header() const { return hdr_; }
    static std::string baseName(const std::string& path);

private:
    std::string base() const { return baseName(path_); }
    void run();
    std::string path_;
    FILE* f_ = nullptr;
    IqHeader hdr_;
    uint64_t dataBytes_ = 0;
    SampleSink sink_;
    Thread th_;
    std::atomic<bool> running_{false}, paused_{false}, loop_{true};
    std::atomic<int64_t> seekReq_{-1};
    std::atomic<uint64_t> pos_{0};         // current position in sample frames
};

} // namespace orl
