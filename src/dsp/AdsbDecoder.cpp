// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "AdsbDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orl {
namespace {
const double kPi = 3.14159265358979323846;

uint32_t crc24(const uint8_t* m, int bytes) {                 // remainder of the whole frame by x^24+x^23+x^22+x^21+x^20+x^19+x^18+x^17+x^16+x^15+x^14+x^13+x^12+x^10+x^3+1
    uint32_t r = 0;
    for (int i = 0; i < bytes; ++i) {
        r ^= (uint32_t)m[i] << 16;
        for (int b = 0; b < 8; ++b) { r <<= 1; if (r & 0x1000000) r ^= 0x1FFF409; }
    }
    return r & 0xFFFFFF;
}
uint32_t bits(const uint8_t* m, int first, int count) {         // bit 'first' is the most significant bit of m[0]
    uint32_t v = 0;
    for (int i = 0; i < count; ++i) v = (v << 1) | ((m[(first + i) >> 3] >> (7 - ((first + i) & 7))) & 1);
    return v;
}
int mod(int a, int b) { const int r = a % b; return r < 0 ? r + b : r; }
}

bool adsbCrcOk(const uint8_t* m) { return crc24(m, 14) == 0; }

int adsbNL(double lat) {
    lat = std::fabs(lat);
    if (lat < 1e-9) return 59;
    if (lat == 87.0) return 2;
    if (lat > 87.0) return 1;
    const double a = 1.0 - (1.0 - std::cos(kPi / 30.0)) / std::pow(std::cos(kPi / 180.0 * lat), 2.0);
    return (int)std::floor(2.0 * kPi / std::acos(a));
}

bool adsbCprGlobal(uint32_t latE, uint32_t lonE, uint32_t latO, uint32_t lonO, bool oddNewer, double& lat, double& lon) {
    const double lE = latE / 131072.0, lO = latO / 131072.0, nE = lonE / 131072.0, nO = lonO / 131072.0;
    const int j = (int)std::floor(59.0 * lE - 60.0 * lO + 0.5);
    double latEven = (360.0 / 60.0) * (mod(j, 60) + lE), latOdd = (360.0 / 59.0) * (mod(j, 59) + lO);
    if (latEven >= 270.0) latEven -= 360.0;
    if (latOdd >= 270.0) latOdd -= 360.0;
    if (adsbNL(latEven) != adsbNL(latOdd)) return false;                     // the two reports straddle a zone boundary
    if (oddNewer) {
        const int ni = std::max(adsbNL(latOdd) - 1, 1);
        const int m = (int)std::floor(nE * (adsbNL(latOdd) - 1) - nO * adsbNL(latOdd) + 0.5);
        lon = (360.0 / ni) * (mod(m, ni) + nO); lat = latOdd;
    } else {
        const int ni = std::max(adsbNL(latEven), 1);
        const int m = (int)std::floor(nE * (adsbNL(latEven) - 1) - nO * adsbNL(latEven) + 0.5);
        lon = (360.0 / ni) * (mod(m, ni) + nE); lat = latEven;
    }
    if (lon >= 180.0) lon -= 360.0;
    return lat >= -90.0 && lat <= 90.0;
}

void AdsbDecoder::setRate(double fs) { fs_ = fs; spc_ = fs / 1e6; reset(); }

void AdsbDecoder::reset() {
    Lock l(mu_);
    mag_.clear(); consumed_ = 0; noise_ = 0.01; clock_ = 0; st_ = AdsbStats(); tab_.clear();
}

// Mean magnitude of the samples whose instants fall in [t0, t1) microseconds after 'base' (a fractional sample position).
double AdsbDecoder::at(double base, double t0, double t1) const {
    long a = (long)std::ceil(base + t0 * spc_), b = (long)std::ceil(base + t1 * spc_);
    if (a < 0) a = 0;
    if (b <= a) b = a + 1;
    double s = 0;
    for (long k = a; k < b; ++k) s += mag_[(size_t)k];
    return s / (double)(b - a);
}

void AdsbDecoder::process(const float* iq, size_t n) {
    for (size_t k = 0; k < n; ++k) mag_.push_back(std::sqrt(iq[2 * k] * iq[2 * k] + iq[2 * k + 1] * iq[2 * k + 1]));
    const size_t need = (size_t)std::ceil(122.0 * spc_) + 6;            // a whole frame: 8 us preamble + 112 us data
    size_t i = 1;
    while (i + need < mag_.size()) {
        const double m = mag_[i];
        noise_ += 0.0005 * (m - noise_);
        if (m > 3.0 * noise_ && m > 0.004 && m > 1.5 * mag_[i - 1] && tryDecode(i)) { i += (size_t)std::ceil(110.0 * spc_); continue; }
        ++i;
    }
    {
        Lock l(mu_);
        clock_ = (double)(consumed_ + i) / fs_;
    }
    const size_t keep = i - 1;                                          // keep one sample of history for the edge test
    mag_.erase(mag_.begin(), mag_.begin() + (long)keep);
    consumed_ += keep;
}

bool AdsbDecoder::tryDecode(size_t i) {
    // The first strong sample is somewhere in the first pulse, which started up to one sample earlier: try a few alignments.
    static const double shifts[] = {-0.5, -0.25, -0.75, 0.0, 0.25, -1.0};
    bool pre = false;
    for (double sh : shifts) {
        const double base = (double)i + sh;
        // Preamble: pulses 0.5 us wide at 0, 1, 3.5 and 4.5 us; quiet between them.
        const double p[4] = {at(base, 0, 0.5), at(base, 1, 1.5), at(base, 3.5, 4.0), at(base, 4.5, 5.0)};
        const double q[4] = {at(base, 0.5, 1.0), at(base, 1.5, 3.5), at(base, 4.0, 4.5), at(base, 5.0, 8.0)};
        const double hiMin = std::min(std::min(p[0], p[1]), std::min(p[2], p[3])), hiMean = (p[0] + p[1] + p[2] + p[3]) / 4.0;
        const double lo = (q[0] + q[1] + q[2] + q[3]) / 4.0;
        if (!(hiMean > 2.0 * lo && hiMin > 1.4 * lo)) continue;
        if (!pre) { pre = true; Lock l(mu_); ++st_.candidates; }
        uint8_t msg[14] = {0};
        for (int b = 0; b < 112; ++b) {
            const double t0 = 8.0 + b;
            if (at(base, t0, t0 + 0.5) > at(base, t0 + 0.5, t0 + 1.0)) msg[b >> 3] |= (uint8_t)(0x80 >> (b & 7));
        }
        const int df = msg[0] >> 3;
        if (df != 17 && df != 18) continue;
        if (!adsbCrcOk(msg)) continue;
        return handleFrame(msg, (double)(consumed_ + i) / fs_);
    }
    if (pre) { Lock l(mu_); ++st_.bad; }
    return false;
}

bool AdsbDecoder::handleFrame(const uint8_t* m, double now) {
    if (!adsbCrcOk(m)) return false;
    const int df = m[0] >> 3;
    if (df != 17 && df != 18) return false;
    const uint32_t icao = bits(m, 8, 24);
    const int tc = (int)bits(m, 32, 5);
    Lock l(mu_);
    ++st_.frames;
    Entry& e = tab_[icao];
    e.a.icao = icao; e.a.lastSeen = now; ++e.a.messages;
    if (tc >= 1 && tc <= 4) {
        static const char cs[] = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ#####_###############0123456789######";
        std::string s;
        for (int k = 0; k < 8; ++k) { const char c = cs[bits(m, 40 + 6 * k, 6)]; s += c; }
        while (!s.empty() && (s.back() == '_' || s.back() == '#')) s.pop_back();
        if (!s.empty()) e.a.callsign = s;
    } else if ((tc >= 9 && tc <= 18) || (tc >= 20 && tc <= 22)) {
        const uint32_t ac = bits(m, 40, 12);
        if (tc <= 18 && (ac & 0x10)) {                                         // Q bit set: 25 ft steps
            const int n = (int)(((ac & 0xFE0) >> 1) | (ac & 0xF));
            e.a.altFt = n * 25 - 1000; e.a.hasAlt = true;
        }
        const bool odd = bits(m, 53, 1) != 0;
        Cpr& c = odd ? e.odd : e.even;
        c.lat = bits(m, 54, 17); c.lon = bits(m, 71, 17); c.t = now;
        if (std::fabs(e.even.t - e.odd.t) <= 10.0) {
            double la, lo;
            if (adsbCprGlobal(e.even.lat, e.even.lon, e.odd.lat, e.odd.lon, e.odd.t > e.even.t, la, lo)) {
                e.a.lat = la; e.a.lon = lo; e.a.hasPos = true;
            }
        }
    } else if (tc == 19) {
        const int sub = (int)bits(m, 37, 3);
        if (sub == 1 || sub == 2) {
            const double sc = sub == 2 ? 4.0 : 1.0;
            const int ewS = (int)bits(m, 45, 1), ewV = (int)bits(m, 46, 10), nsS = (int)bits(m, 56, 1), nsV = (int)bits(m, 57, 10);
            if (ewV && nsV) {
                const double ew = (ewS ? -1.0 : 1.0) * (ewV - 1) * sc, ns = (nsS ? -1.0 : 1.0) * (nsV - 1) * sc;
                e.a.speedKt = std::sqrt(ew * ew + ns * ns);
                double h = std::atan2(ew, ns) * 180.0 / kPi; if (h < 0) h += 360.0;
                e.a.headingDeg = h; e.a.hasVel = true;
                const int vr = (int)bits(m, 69, 9);
                if (vr) e.a.vrateFpm = (bits(m, 68, 1) ? -1 : 1) * (vr - 1) * 64;
            }
        }
    }
    return true;
}

std::vector<Aircraft> AdsbDecoder::aircraft(double maxAge) const {
    Lock l(mu_);
    std::vector<Aircraft> out;
    for (const auto& kv : tab_) if (clock_ - kv.second.a.lastSeen <= maxAge) out.push_back(kv.second.a);
    std::sort(out.begin(), out.end(), [](const Aircraft& a, const Aircraft& b) { return a.lastSeen > b.lastSeen; });
    return out;
}

} // namespace orl
