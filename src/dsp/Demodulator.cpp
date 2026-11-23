// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "Demodulator.h"

#include <algorithm>
#include <cmath>

namespace orl {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kTab = 16384;
struct Tab {
    float v[kTab];
    Tab() { for (int i = 0; i < kTab; ++i) v[i] = (float)std::sin(2.0 * kPi * i / kTab); }
};
const float* sinTab() { static Tab t; return t.v; }
} // namespace

Demodulator::Demodulator() { sinTab(); }

void Demodulator::setSampleRate(double fs) {
    Lock l(m_);
    if (fs != fs_) { fs_ = fs; dirty_ = true; }
}
void Demodulator::setMode(Mode m, double bandwidthHz) {
    Lock l(m_);
    const double bw = (m == Mode::WFM) ? kWfmBandwidth : std::max(1000.0, bandwidthHz);
    if (m != mode_ || bw != bw_) { mode_ = m; bw_ = bw; dirty_ = true; }
}
void Demodulator::setOffsetHz(double hz) {
    Lock l(m_);
    if (hz != off_) { off_ = hz; dirty_ = true; }
}
void Demodulator::setVolume(float v) { Lock l(m_); vol_ = v < 0 ? 0 : (v > 1 ? 1 : v); }
void Demodulator::setMute(bool m) { Lock l(m_); mute_ = m; }
void Demodulator::setSquelchDb(double db) { Lock l(m_); sqlDb_ = db; }
void Demodulator::reset() { Lock l(m_); dirty_ = true; }
void Demodulator::setStereo(bool on) { Lock l(m_); if (on != stereo_) { stereo_ = on; dirty_ = true; } }

void Demodulator::rebuild() {
    dirty_ = false;
    D1_ = std::max(1, (int)std::floor(fs_ / 250000.0));
    r1_ = fs_ / D1_;
    if (D1_ == 1) {
        f1_.configure({1.0f}, 1, true);
    } else {
        const double c1 = (mode_ == Mode::WFM ? 0.39 : 0.35) * r1_;     // passband edge
        f1_.configure(designLowpass(fs_, c1, r1_ - c1), D1_, true);      // stopband starts where aliases would land
    }

    if (mode_ == Mode::WFM) {
        Da_ = std::max(1, (int)std::floor(r1_ / kAudioRate));
        ra_ = r1_ / Da_;
        fa_.configure(designLowpass(r1_, 15000.0, std::max(ra_ - 15000.0, 20000.0)), Da_, false);
        fb_.configure(designLowpass(r1_, 15000.0, std::max(ra_ - 15000.0, 20000.0)), Da_, false);
        pilotBp_.bandpass(r1_, 19000.0, 20.0);
        notchS_.notch(r1_, 19000.0, 8.0);
        notchD_.notch(r1_, 19000.0, 8.0);
        {   // pilot PLL: about 60 Hz loop bandwidth, critically damped
            const double wn = 2.0 * kPi * 60.0 / r1_;
            pllW_ = 2.0 * kPi * 19000.0 / r1_;
            pllKp_ = 2.0 * 0.707 * wn;
            pllKi_ = wn * wn;
            lpAlpha_ = 1.0 - std::exp(-2.0 * kPi * 200.0 / r1_);
            blendStep_ = 1.0 / (0.05 * r1_);                    // 50 ms fade between mono and stereo
        }
        pllPhase_ = 0; pllFreq_ = 0; lpE_ = 0; lpA_ = 0; blend_ = 0; deemphD_ = 0; stereoLock_ = false;
        deemphAlpha_ = (float)(1.0 - std::exp(-1.0 / (r1_ * 75e-6)));   // 75 us, North American broadcast standard
        fmScale_ = (float)(r1_ / (2.0 * kPi * 75000.0));                 // 75 kHz deviation = full scale
    } else {
        const double half = bw_ / 2.0;
        D2_ = std::max(1, (int)std::floor(r1_ / kAudioRate));
        r2_ = r1_ / D2_;
        f2_.configure(designLowpass(r1_, half, half * 2.0), D2_, true);
        post_.lowpass(r2_, mode_ == Mode::NFM ? std::min(3400.0, half) : half);
        ra_ = r2_;
        fmScale_ = (float)(r2_ / (2.0 * kPi * 5000.0));                  // 5 kHz deviation = full scale
        amAlpha_ = (float)(1.0 - std::exp(-1.0 / (r2_ * 0.2)));          // 200 ms carrier tracking (AGC)
        amAvg_ = 0;
    }
    rs_.configure(ra_, kAudioRate);
    rsR_.configure(ra_, kAudioRate);
    outCh_ = (mode_ == Mode::WFM && stereo_) ? 2 : 1;

    inc_ = (uint32_t)((int64_t)std::llround(off_ / fs_ * 4294967296.0) & 0xFFFFFFFFll);
    phase_ = 0;
    prevI_ = 1; prevQ_ = 0; deemph_ = 0; power_ = 0;
    post_.reset();
}

void Demodulator::updateLevel(const std::vector<float>& i, const std::vector<float>& q) {
    if (i.empty()) return;
    double p = 0;
    for (size_t k = 0; k < i.size(); ++k) p += (double)i[k] * i[k] + (double)q[k] * q[k];
    p /= (double)i.size();
    power_ = (power_ == 0) ? p : power_ + 0.3 * (p - power_);
    const double db = 10.0 * std::log10(power_ + 1e-12);
    level_ = db;
    if (sqlDb_ <= -119.0) open_ = true;
    else if (db > sqlDb_) open_ = true;
    else if (db < sqlDb_ - 2.0) open_ = false;          // 2 dB hysteresis stops chatter
}

void Demodulator::process(const float* iq, size_t n, std::vector<float>& out) {
    Lock l(m_);
    if (dirty_) rebuild();
    if (n == 0) return;

    // Mixer: multiply by e^{-j 2 pi off t} to bring the wanted signal to 0 Hz. The receiver's own DC
    // spike lands at -off after mixing and is removed by the channel filter. (A DC blocker here would
    // also delete a wanted carrier that sits exactly on the centre frequency, so there isn't one.)
    wi_.resize(n); wq_.resize(n);
    const float* T = sinTab();
    uint32_t ph = phase_;
    const uint32_t inc = inc_;
    for (size_t k = 0; k < n; ++k) {
        const float xi = iq[2 * k], xq = iq[2 * k + 1];
        const uint32_t idx = ph >> 18;                       // top 14 bits of the 32-bit phase
        const float s = T[idx], c = T[(idx + kTab / 4) & (kTab - 1)];
        wi_[k] = xi * c + xq * s;
        wq_[k] = xq * c - xi * s;
        ph += inc;
    }
    phase_ = ph;

    s1i_.clear(); s1q_.clear();
    f1_.process(wi_.data(), wq_.data(), n, s1i_, s1q_);

    const size_t base = out.size();

    if (mode_ == Mode::WFM && stereo_) {
        updateLevel(s1i_, s1q_);
        const size_t m = s1i_.size();
        au_.resize(m); dd_.resize(m);
        float pi = prevI_, pq = prevQ_, y = deemph_, yd = deemphD_;
        double ph = pllPhase_, fr = pllFreq_, le = lpE_, la = lpA_, bl = blend_;
        bool lock = stereoLock_.load();
        for (size_t k = 0; k < m; ++k) {
            const float re = s1i_[k] * pi + s1q_[k] * pq;
            const float im = s1q_[k] * pi - s1i_[k] * pq;
            const float d = std::atan2(im, re) * fmScale_;
            pi = s1i_[k]; pq = s1q_[k];

            // Lock a PLL to the 19 kHz pilot (pilot = A sin(phi); detector output ~ sin(phi - p)).
            const double bp = pilotBp_.process(d);
            const double sp = std::sin(ph), cp = std::cos(ph);
            le += lpAlpha_ * (bp * cp - le);
            la += lpAlpha_ * (bp * sp - la);                 // ~ A/2 when locked
            const double en = std::max(-1.0, std::min(1.0, le / std::max(la, 0.005)));
            fr += pllKi_ * en;
            ph += pllW_ + fr + pllKp_ * en;
            if (ph >= 2.0 * kPi) ph -= 2.0 * kPi; else if (ph < 0) ph += 2.0 * kPi;
            if (!lock && la > 0.02) lock = true; else if (lock && la < 0.012) lock = false;

            // Sum channel, and the difference channel recovered with the 38 kHz subcarrier (2nd harmonic of the pilot).
            const float sum = notchS_.process(d);
            const float dif = notchD_.process((float)(d * 2.0 * sp * cp * 2.0));
            const double target = lock ? 1.0 : 0.0;
            bl += std::max(-blendStep_, std::min(blendStep_, target - bl));
            y  += deemphAlpha_ * (sum - y);
            yd += deemphAlpha_ * ((float)bl * dif - yd);
            au_[k] = y; dd_[k] = yd;
        }
        prevI_ = pi; prevQ_ = pq; deemph_ = y; deemphD_ = yd;
        pllPhase_ = ph; pllFreq_ = fr; lpE_ = le; lpA_ = la; blend_ = bl;
        stereoLock_ = lock;

        au2_.clear(); sd_.clear();
        fa_.process(au_.data(), nullptr, m, au2_, dummy_);
        fb_.process(dd_.data(), nullptr, m, sd_, dummy_);
        const size_t na = std::min(au2_.size(), sd_.size());
        l1_.resize(na); r1o_.resize(na);
        for (size_t k = 0; k < na; ++k) { l1_[k] = au2_[k] + sd_[k]; r1o_[k] = au2_[k] - sd_[k]; }
        lo_.clear(); ro_.clear();
        rs_.process(l1_.data(), na, lo_);
        rsR_.process(r1o_.data(), na, ro_);
        const size_t nf = std::min(lo_.size(), ro_.size());
        for (size_t k = 0; k < nf; ++k) { out.push_back(lo_[k]); out.push_back(ro_[k]); }
    } else if (mode_ == Mode::WFM) {
        updateLevel(s1i_, s1q_);
        const size_t m = s1i_.size();
        au_.resize(m);
        float pi = prevI_, pq = prevQ_, y = deemph_;
        for (size_t k = 0; k < m; ++k) {
            const float re = s1i_[k] * pi + s1q_[k] * pq;
            const float im = s1q_[k] * pi - s1i_[k] * pq;
            y += deemphAlpha_ * (std::atan2(im, re) * fmScale_ - y);
            au_[k] = y;
            pi = s1i_[k]; pq = s1q_[k];
        }
        prevI_ = pi; prevQ_ = pq; deemph_ = y;
        au2_.clear();
        fa_.process(au_.data(), nullptr, m, au2_, dummy_);
        rs_.process(au2_.data(), au2_.size(), out);
    } else {
        s2i_.clear(); s2q_.clear();
        f2_.process(s1i_.data(), s1q_.data(), s1i_.size(), s2i_, s2q_);
        updateLevel(s2i_, s2q_);
        const size_t m = s2i_.size();
        au_.resize(m);
        if (mode_ == Mode::NFM) {
            float pi = prevI_, pq = prevQ_;
            for (size_t k = 0; k < m; ++k) {
                const float re = s2i_[k] * pi + s2q_[k] * pq;
                const float im = s2q_[k] * pi - s2i_[k] * pq;
                au_[k] = post_.process(std::atan2(im, re) * fmScale_);
                pi = s2i_[k]; pq = s2q_[k];
            }
            prevI_ = pi; prevQ_ = pq;
        } else {
            float avg = amAvg_;
            for (size_t k = 0; k < m; ++k) {
                const float env = std::sqrt(s2i_[k] * s2i_[k] + s2q_[k] * s2q_[k]);
                if (avg == 0.0f) avg = env;                               // start at the first carrier level: no start-up pop
                avg += amAlpha_ * (env - avg);
                au_[k] = post_.process(env / (avg + 1e-6f) - 1.0f);   // divide by the carrier: AGC + DC removal
            }
            amAvg_ = avg;
        }
        rs_.process(au_.data(), m, out);
    }

    // Squelch, mute, volume (quadratic curve feels more natural than linear), hard limit.
    const float g = (mute_ || !open_.load()) ? 0.0f : vol_ * vol_ * 2.0f;
    for (size_t k = base; k < out.size(); ++k) out[k] = std::max(-1.0f, std::min(1.0f, out[k] * g));
}

} // namespace orl
