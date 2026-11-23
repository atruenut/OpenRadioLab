// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include <vector>
#include "RTLSDRDevice.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace orl {

namespace {

// Function table resolved from rtlsdr.dll at runtime. Signatures follow rtl-sdr.h.
struct Api {
    HMODULE mod = nullptr;
    std::string path;
    bool tried = false;
    std::string error;

    int         (*get_device_count)(void) = nullptr;
    const char* (*get_device_name)(uint32_t) = nullptr;
    int         (*get_device_usb_strings)(uint32_t, char*, char*, char*) = nullptr;
    int         (*open)(void**, uint32_t) = nullptr;
    int         (*close)(void*) = nullptr;
    int         (*set_center_freq)(void*, uint32_t) = nullptr;
    int         (*set_sample_rate)(void*, uint32_t) = nullptr;
    int         (*set_tuner_gain_mode)(void*, int) = nullptr;
    int         (*set_tuner_gain)(void*, int) = nullptr;
    int         (*get_tuner_gains)(void*, int*) = nullptr;
    int         (*get_tuner_type)(void*) = nullptr;
    int         (*reset_buffer)(void*) = nullptr;
    int         (*read_async)(void*, void (*)(unsigned char*, uint32_t, void*), void*, uint32_t, uint32_t) = nullptr;
    int         (*cancel_async)(void*) = nullptr;
};

Api g_api;
Mutex g_apiMutex;

template <class T>
bool bind(HMODULE m, T& fn, const char* name) {
    fn = reinterpret_cast<T>((void*)GetProcAddress(m, name));
    return fn != nullptr;
}

bool loadApi() {
    Lock l(g_apiMutex);
    if (g_api.tried) return g_api.mod != nullptr;
    g_api.tried = true;

    // Look next to the program, in a "libs" folder next to it, and in a per-user folder that survives
    // re-installing or unzipping a new version (%APPDATA%\\OpenRadioLab\\libs). Names used by the common builds.
    std::vector<std::string> dirs;
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string d = exe;
    d = d.substr(0, d.find_last_of("\\/") + 1);
    dirs.push_back(d);
    dirs.push_back(d + "libs\\");
    char ad[MAX_PATH] = {0};
    if (GetEnvironmentVariableA("APPDATA", ad, MAX_PATH)) dirs.push_back(std::string(ad) + "\\OpenRadioLab\\libs\\");
    static const char* names[] = {"rtlsdr.dll", "librtlsdr.dll", "librtlsdr-0.dll"};
    for (const auto& dir : dirs) {
        for (const char* n : names) {
            const std::string full = dir + n;
            if (GetFileAttributesA(full.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            // Altered search path: libusb-1.0.dll is also found in the same folder as the library.
            HMODULE m = LoadLibraryExA(full.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (m) { g_api.mod = m; g_api.path = full; break; }
        }
        if (g_api.mod) break;
    }
    if (!g_api.mod)
        for (const char* n : names) {
            HMODULE m = LoadLibraryA(n);
            if (m) { g_api.mod = m; g_api.path = n; break; }
        }
    if (!g_api.mod) {
        g_api.error = "rtlsdr.dll was not found next to OpenRadioLab.exe, in a 'libs' folder beside it, or in "
                      "%APPDATA%\\OpenRadioLab\\libs (or on PATH).";
        return false;
    }

    HMODULE m = g_api.mod;
    bool ok = true;
    ok &= bind(m, g_api.get_device_count, "rtlsdr_get_device_count");
    ok &= bind(m, g_api.get_device_name, "rtlsdr_get_device_name");
    ok &= bind(m, g_api.get_device_usb_strings, "rtlsdr_get_device_usb_strings");
    ok &= bind(m, g_api.open, "rtlsdr_open");
    ok &= bind(m, g_api.close, "rtlsdr_close");
    ok &= bind(m, g_api.set_center_freq, "rtlsdr_set_center_freq");
    ok &= bind(m, g_api.set_sample_rate, "rtlsdr_set_sample_rate");
    ok &= bind(m, g_api.set_tuner_gain_mode, "rtlsdr_set_tuner_gain_mode");
    ok &= bind(m, g_api.set_tuner_gain, "rtlsdr_set_tuner_gain");
    ok &= bind(m, g_api.get_tuner_gains, "rtlsdr_get_tuner_gains");
    ok &= bind(m, g_api.get_tuner_type, "rtlsdr_get_tuner_type");
    ok &= bind(m, g_api.reset_buffer, "rtlsdr_reset_buffer");
    ok &= bind(m, g_api.read_async, "rtlsdr_read_async");
    ok &= bind(m, g_api.cancel_async, "rtlsdr_cancel_async");
    if (!ok) {
        g_api.error = g_api.path + " was loaded but is missing required rtlsdr_* functions "
                      "(wrong or incompatible DLL, or a 32/64-bit mismatch with this program).";
        FreeLibrary(g_api.mod);
        g_api.mod = nullptr;
        return false;
    }
    return true;
}

const char* tunerName(int t) {
    switch (t) {
        case 1: return "Elonics E4000";
        case 2: return "Fitipower FC0012";
        case 3: return "Fitipower FC0013";
        case 4: return "FCI FC2580";
        case 5: return "Rafael Micro R820T/R820T2";
        case 6: return "Rafael Micro R828D";
        default: return "unknown tuner";
    }
}

// Typical tuning ranges per tuner chip (datasheet-level; real units vary a little).
void tunerRange(int t, uint32_t& lo, uint32_t& hi) {
    switch (t) {
        case 1: lo = 52000000u;  hi = 2200000000u; break;   // E4000 (has a gap ~1.1-1.25 GHz)
        case 2: lo = 22000000u;  hi = 948000000u;  break;
        case 3: lo = 22000000u;  hi = 1100000000u; break;
        case 4: lo = 146000000u; hi = 924000000u;  break;   // FC2580 (has a gap 308-438 MHz)
        default: lo = 24000000u; hi = 1766000000u; break;   // R820T/R820T2/R828D
    }
}

} // namespace

RtlProbe RTLSDRDevice::probe(bool withUsbStrings) {
    RtlProbe p;
    if (!loadApi()) {
        p.error = g_api.error;
        return p;
    }
    p.libraryFound = true;
    p.libraryPath = g_api.path;
    const int n = g_api.get_device_count();
    for (int i = 0; i < n; ++i) {
        RtlDeviceInfo d;
        d.index = i;
        const char* nm = g_api.get_device_name((uint32_t)i);
        d.name = nm ? nm : "RTL-SDR";
        char m[256] = {0}, pr[256] = {0}, se[256] = {0};
        if (withUsbStrings && g_api.get_device_usb_strings((uint32_t)i, m, pr, se) == 0) {
            d.manufacturer = m; d.product = pr; d.serial = se;
        }
        p.devices.push_back(d);
    }
    return p;
}

RTLSDRDevice::RTLSDRDevice(int index, const std::string& label) : index_(index), label_(label) {}
RTLSDRDevice::~RTLSDRDevice() { close(); }

std::string RTLSDRDevice::description() const {
    std::string s = "RTL-SDR #" + std::to_string(index_);
    if (dev_) s += std::string(" - tuner: ") + tunerName(tunerType_) + (v4_ ? " (Blog V4, HF input)" : "");
    return s;
}

bool RTLSDRDevice::open(std::string& error) {
    if (!loadApi()) { error = g_api.error; return false; }
    if (dev_) return true;

    void* d = nullptr;
    const int r = g_api.open(&d, (uint32_t)index_);
    if (r != 0 || !d) {
        char b[64]; std::snprintf(b, sizeof b, "%d", r);
        error = std::string("rtlsdr_open failed (code ") + b + "). Usual causes: another program "
                "(SDR#, rtl_tcp, ...) is using the dongle, or the WinUSB driver is not installed "
                "for it - see the Device report.";
        return false;
    }
    dev_ = d;
    tunerType_ = g_api.get_tuner_type(dev_);
    {
        char m[256] = {0}, pr[256] = {0}, se[256] = {0};
        v4_ = g_api.get_device_usb_strings((uint32_t)index_, m, pr, se) == 0 &&
              (std::string(pr).find("V4") != std::string::npos);
    }

    const int ng = g_api.get_tuner_gains(dev_, nullptr);
    gains_.clear();
    if (ng > 0) {
        gains_.resize((size_t)ng);
        g_api.get_tuner_gains(dev_, gains_.data());
    }

    g_api.set_sample_rate(dev_, fs_);
    g_api.set_center_freq(dev_, fc_);
    g_api.set_tuner_gain_mode(dev_, 0);      // start in automatic gain
    return true;
}

void RTLSDRDevice::close() {
    stop();
    if (dev_) { g_api.close(dev_); dev_ = nullptr; }
}

bool RTLSDRDevice::setCenterFrequency(uint32_t hz) {
    fc_ = hz;
    return dev_ ? g_api.set_center_freq(dev_, hz) == 0 : true;
}

bool RTLSDRDevice::setSampleRate(uint32_t hz) {
    fs_ = hz;
    return dev_ ? g_api.set_sample_rate(dev_, hz) == 0 : true;
}

bool RTLSDRDevice::setGainDb(double db) {
    if (!dev_) return true;
    g_api.set_tuner_gain_mode(dev_, 1);      // manual
    if (gains_.empty()) return false;
    const int want = (int)std::lround(db * 10.0);
    int best = gains_[0];
    for (int g : gains_) if (std::abs(g - want) < std::abs(best - want)) best = g;
    return g_api.set_tuner_gain(dev_, best) == 0;
}

bool RTLSDRDevice::setAutoGain(bool on) {
    if (!dev_) return true;
    return g_api.set_tuner_gain_mode(dev_, on ? 0 : 1) == 0;
}

void RTLSDRDevice::readCallback(unsigned char* buf, uint32_t len, void* ctx) {
    auto* self = static_cast<RTLSDRDevice*>(ctx);
    static float lut[256];
    static bool init = false;
    if (!init) { for (int i = 0; i < 256; ++i) lut[i] = ((float)i - 127.5f) / 127.5f; init = true; }

    if (self->conv_.size() < len) self->conv_.resize(len);
    for (uint32_t i = 0; i < len; ++i) self->conv_[i] = lut[buf[i]];   // unsigned 8-bit I,Q -> float
    if (self->sink_) self->sink_(self->conv_.data(), len / 2);
}

bool RTLSDRDevice::start(SampleSink sink) {
    if (!dev_ || reading_) return false;
    sink_ = std::move(sink);
    g_api.reset_buffer(dev_);
    reading_ = true;
    th_.start([this] {
        // Blocks until rtlsdr_cancel_async() is called. 128 KiB per transfer (multiple of 512).
        g_api.read_async(dev_, &RTLSDRDevice::readCallback, this, 0, 131072);
        reading_ = false;
    });
    return true;
}

void RTLSDRDevice::stop() {
    if (!dev_) { th_.join(); return; }
    // cancel_async only works once read_async is actually running, so retry briefly.
    for (int i = 0; i < 300 && reading_; ++i) {
        g_api.cancel_async(dev_);
        sleepSeconds(0.01);
    }
    th_.join();
    reading_ = false;
}

DeviceCaps RTLSDRDevice::caps() const {
    DeviceCaps c;
    tunerRange(tunerType_, c.minFreqHz, c.maxFreqHz);
    if (v4_) c.minFreqHz = 500000u;           // V4 converts HF up internally; the Blog driver handles the offset
    c.sampleRates = {250000, 1024000, 1920000, 2048000, 2400000, 2560000};
    c.minGainDb = gains_.empty() ? 0.0 : gains_.front() / 10.0;
    c.maxGainDb = gains_.empty() ? 50.0 : gains_.back() / 10.0;
    return c;
}

} // namespace orl
