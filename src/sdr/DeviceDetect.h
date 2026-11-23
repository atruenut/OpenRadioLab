// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Automatic SDR hardware detection (Windows).
//
// Tier 1 - USB level (this file): enumerate the USB bus with SetupAPI and match
//          vendor/product IDs against a table of known SDRs. Needs no extra
//          DLLs, works even when no driver is installed, and can tell the user
//          *why* a dongle is not usable (wrong driver, driver missing).
// Tier 2 - driver level (RTLSDRDevice::probe): ask librtlsdr what it can open
//          and which tuner chip is inside.
#pragma once

#include "RTLSDRDevice.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orl {

enum class SdrFamily { RTLSDR, Airspy, HackRF, LimeSDR, BladeRF, SDRplay, PlutoSDR, USRP, Funcube, Other };
enum class DriverState { Ok, WrongDriver, NotInstalled, Unknown };

struct DetectedDevice {
    uint16_t vid = 0, pid = 0;
    std::string name;
    SdrFamily family = SdrFamily::Other;
    int count = 1;
    DriverState driver = DriverState::Unknown;
    std::string services;      // driver service names Windows reports (e.g. "WinUSB")
    bool streamingSupported = false;   // does OpenRadio Lab have a backend for it yet?
};

const char* familyName(SdrFamily f);

std::vector<DetectedDevice> detectUsbSdrs();

// Human-readable explanation combining both tiers, with concrete next steps.
std::string buildDeviceReport(const std::vector<DetectedDevice>& usb, const RtlProbe& rtl);

} // namespace orl
