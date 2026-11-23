// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "IqFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orl {

static const char kMagic[8] = {'O', 'R', 'L', 'I', 'Q', '0', '0', '1'};
static const long kHeaderBytes = 32;

static void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get32(const uint8_t* p) { uint32_t v = 0; for (int i = 3; i >= 0; --i) v = (v << 8) | p[i]; return v; }
static uint64_t get64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | p[i]; return v; }

bool writeIqHeader(FILE* f, const IqHeader& h) {
    uint8_t b[kHeaderBytes] = {0};
    std::memcpy(b, kMagic, 8);
    put32(b + 8, h.rate); put32(b + 12, 1); put64(b + 16, h.centreHz);
    put32(b + 24, h.simulated ? 1u : 0u); put32(b + 28, 0);
    return std::fwrite(b, 1, sizeof b, f) == sizeof b;
}

bool readIqHeader(FILE* f, IqHeader& h, std::string& err) {
    uint8_t b[kHeaderBytes];
    if (std::fread(b, 1, sizeof b, f) != sizeof b || std::memcmp(b, kMagic, 8) != 0) {
        err = "This is not an OpenRadio Lab recording (.orliq).";
        return false;
    }
    if (get32(b + 12) != 1) { err = "This recording uses a sample format this version cannot play."; return false; }
    h.rate = get32(b + 8); h.centreHz = get64(b + 16); h.simulated = (get32(b + 24) & 1u) != 0;
    if (h.rate < 100000 || h.rate > 20000000 || h.centreHz == 0 || h.centreHz > 4000000000ull) {
        err = "The recording's header looks damaged.";
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ recorder

bool IqRecorder::start(const std::string& path, const IqHeader& h, uint64_t maxBytes, std::string& err) {
    stop();
    f_ = std::fopen(path.c_str(), "wb");
    if (!f_) { err = "Could not create " + path; return false; }
    if (!writeIqHeader(f_, h)) { err = "Could not write to " + path; std::fclose(f_); f_ = nullptr; return false; }
    path_ = path; maxBytes_ = maxBytes;
    bytes_ = 0; dropped_ = 0; queued_ = 0; limit_ = false; quit_ = false;
    active_ = true;
    th_.start([this] { run(); });
    return true;
}

void IqRecorder::write(const float* iq, size_t n) {
    if (!active_.load()) return;
    if (queued_.load() > (64u << 20)) { ++dropped_; return; }             // disk too slow: drop rather than stall the receiver
    std::vector<uint8_t> b(2 * n);
    for (size_t k = 0; k < 2 * n; ++k) {
        const float v = iq[k] * 127.5f + 127.5f;
        b[k] = (uint8_t)std::max(0.0f, std::min(255.0f, std::floor(v + 0.5f)));
    }
    {
        Lock l(m_);
        queued_ += b.size();
        q_.push_back(std::move(b));
    }
    ev_.set();
}

void IqRecorder::run() {
    for (;;) {
        std::vector<uint8_t> b;
        bool have = false;
        {
            Lock l(m_);
            if (!q_.empty()) { b = std::move(q_.front()); q_.pop_front(); have = true; queued_ -= b.size(); }
        }
        if (!have) {
            if (quit_.load()) break;
            ev_.wait(0.1);
            continue;
        }
        if (bytes_.load() + b.size() > maxBytes_) {
            b.resize((size_t)(maxBytes_ - bytes_.load()));
            limit_ = true;
        }
        if (!b.empty() && std::fwrite(b.data(), 1, b.size(), f_) == b.size()) bytes_ += b.size();
        if (limit_.load()) active_ = false;            // stop accepting new samples; the queue drains
    }
}

void IqRecorder::stop() {
    if (!f_) return;
    active_ = false;
    quit_ = true;
    ev_.set();
    th_.join();
    std::fclose(f_);
    f_ = nullptr;
}

// ------------------------------------------------------------------ playback

std::string IqFileDevice::baseName(const std::string& p) {
    const size_t i = p.find_last_of("\\/");
    return i == std::string::npos ? p : p.substr(i + 1);
}

std::string IqFileDevice::description() const {
    char b[200];
    std::snprintf(b, sizeof b, " - %.3f MS/s at %.3f MHz, %.1f s, repeating", hdr_.rate / 1e6, hdr_.centreHz / 1e6, durationSeconds());
    return "Recording: " + base() + b;
}

bool IqFileDevice::open(std::string& error) {
    close();
    f_ = std::fopen(path_.c_str(), "rb");
    if (!f_) { error = "Could not open " + path_; return false; }
    if (!readIqHeader(f_, hdr_, error)) { close(); return false; }
    std::fseek(f_, 0, SEEK_END);
    const long size = std::ftell(f_);
    dataBytes_ = size > kHeaderBytes ? (uint64_t)(size - kHeaderBytes) & ~(uint64_t)1 : 0;
    if (dataBytes_ < 2 * 16384) { error = "This recording is too short to play."; close(); return false; }
    std::fseek(f_, kHeaderBytes, SEEK_SET);
    return true;
}

void IqFileDevice::close() {
    stop();
    if (f_) { std::fclose(f_); f_ = nullptr; }
}

DeviceCaps IqFileDevice::caps() const {
    DeviceCaps c;
    const double half = 0.4 * hdr_.rate;
    c.minFreqHz = (uint32_t)std::max(0.0, hdr_.centreHz - half);
    c.maxFreqHz = (uint32_t)(hdr_.centreHz + half);
    c.sampleRates = {hdr_.rate};
    c.minGainDb = c.maxGainDb = 0;
    return c;
}

bool IqFileDevice::start(SampleSink sink) {
    if (!f_ || running_) return false;
    sink_ = std::move(sink);
    running_ = true;
    th_.start([this] { run(); });
    return true;
}

void IqFileDevice::stop() {
    running_ = false;
    th_.join();
}

void IqFileDevice::run() {
    const size_t blk = 16384;
    std::vector<uint8_t> raw(2 * blk);
    std::vector<float> iq(2 * blk);
    uint64_t pos = 0;                      // byte offset into the sample data
    double t0 = nowSeconds();
    uint64_t sent = 0;
    bool wasPaused = false;
    while (running_) {
        const int64_t req = seekReq_.exchange(-1);
        if (req >= 0) {
            pos = std::min<uint64_t>((uint64_t)req * 2, dataBytes_ - raw.size()) & ~(uint64_t)1;
            std::fseek(f_, (long)(kHeaderBytes + pos), SEEK_SET);
            pos_ = pos / 2;
            t0 = nowSeconds(); sent = 0;
        }
        if (paused_.load()) { wasPaused = true; sleepSeconds(0.02); continue; }
        if (wasPaused) { wasPaused = false; t0 = nowSeconds(); sent = 0; }   // do not rush to "catch up" after a pause

        if (pos + raw.size() > dataBytes_) {
            if (loop_.load()) {                         // loop: back to the start
                std::fseek(f_, kHeaderBytes, SEEK_SET);
                pos = 0;
            } else {                                    // stop at the end (the device stays alive, paused)
                pos_ = dataBytes_ / 2;
                paused_ = true;
                continue;
            }
        }
        if (std::fread(raw.data(), 1, raw.size(), f_) != raw.size()) break;
        pos += raw.size();
        pos_ = pos / 2;
        for (size_t k = 0; k < raw.size(); ++k) iq[k] = ((float)raw[k] - 127.5f) / 127.5f;
        if (sink_) sink_(iq.data(), blk);
        sent += blk;
        const double due = t0 + (double)sent / hdr_.rate - nowSeconds();    // pace at the recorded sample rate
        if (due > 0) sleepSeconds(std::min(due, 0.2));
        else if (due < -1.0) { t0 = nowSeconds(); sent = 0; }               // fell far behind: restart the clock
    }
    running_ = false;
}

} // namespace orl
