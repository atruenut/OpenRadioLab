// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// RTL-SDR backend (RTL2832U based dongles). Windows only.
//
// librtlsdr is loaded at RUNTIME (LoadLibrary), not linked. Consequences:
//  - the app builds with no librtlsdr headers/import libs,
//  - the app still starts (simulated mode) when the DLL is missing,
//  - the user can drop in whichever build suits their dongle (the RTL-SDR Blog
//    fork is required for V4 units), and it keeps the app's code separate from
//    librtlsdr's GPL licence. Check licences before redistributing the DLL.
#pragma once

#include "SDRDevice.h"
#include "../Sync.h"

#include <atomic>
#include <string>
#include <vector>

namespace orl {

struct RtlDeviceInfo {
    int index = 0;
    std::string name;          // as reported by librtlsdr
    std::string manufacturer, product, serial;
};

struct RtlProbe {
    bool libraryFound = false;
    std::string libraryPath;   // which DLL name loaded
    std::string error;         // why it could not be used (empty if fine)
    std::vector<RtlDeviceInfo> devices;
};

class RTLSDRDevice : public SDRDevice {
public:
    explicit RTLSDRDevice(int index, const std::string& label = "");
    ~RTLSDRDevice() override;

    // Safe to call at any time; does not require a device to be present.
    // withUsbStrings=false avoids briefly opening each dongle (use while one is streaming).
    static RtlProbe probe(bool withUsbStrings = true);

    std::string name() const override { return label_.empty() ? "RTL-SDR" : label_; }
    std::string description() const override;
    bool isSimulated() const override { return false; }

    bool open(std::string& error) override;
    void close() override;

    bool setCenterFrequency(uint32_t hz) override;
    bool setSampleRate(uint32_t hz) override;
    bool setGainDb(double db) override;
    bool setAutoGain(bool on) override;

    bool start(SampleSink sink) override;
    void stop() override;
    bool isRunning() const override { return reading_; }

    DeviceCaps caps() const override;

private:
    static void readCallback(unsigned char* buf, uint32_t len, void* ctx);

    int index_;
    std::string label_;
    void* dev_ = nullptr;
    int tunerType_ = 0;
    std::vector<int> gains_;          // tenths of a dB, as reported by the tuner
    uint32_t fc_ = 100000000, fs_ = 2048000;

    SampleSink sink_;
    Thread th_;
    std::atomic<bool> reading_{false};
    std::vector<float> conv_;
};

} // namespace orl
