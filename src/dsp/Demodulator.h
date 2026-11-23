// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Receiver chain for one channel inside the captured band:
//   mixer (shift the wanted signal to 0 Hz) -> channel filter + decimation
//   -> WFM/NFM/AM detector -> (de-emphasis) -> audio filter -> resample to 48 kHz -> squelch/volume
//
// Thread-safe: settings may be changed from the UI thread while process() runs on the DSP thread.
#pragma once

#include "Filters.h"
#include "../Sync.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace orl {

class Demodulator {
public:
    enum class Mode { WFM = 0, NFM = 1, AM = 2 };
    static constexpr double kAudioRate = 48000.0;
    static constexpr double kWfmBandwidth = 180000.0;

    Demodulator();

    void setSampleRate(double fs);
    void setMode(Mode m, double bandwidthHz);      // bandwidth is ignored for WFM
    void setOffsetHz(double hz);                   // wanted signal's offset from the centre frequency
    void setVolume(float v01);
    void setMute(bool m);
    void setSquelchDb(double db);                  // <= -119 disables the squelch
    void reset();                                  // clear filter memory (after a retune)
    void setStereo(bool on);                       // WFM only: decode the 19 kHz-pilot stereo signal when present

    // Consumes interleaved float I,Q; appends 48 kHz audio in [-1, 1]: mono samples, or interleaved L,R
    // frames when outputChannels() == 2 (stereo enabled, WFM, pilot locked or not).
    void process(const float* iq, size_t numComplex, std::vector<float>& audio);

    double levelDb() const { return level_.load(); }       // channel power, dBFS
    bool squelchOpen() const { return open_.load(); }
    int outputChannels() const { return outCh_.load(); }          // of the most recent process() call
    bool stereoLocked() const { return stereoLock_.load(); }      // 19 kHz pilot found, decoding L/R

private:
    void rebuild();
    void updateLevel(const std::vector<float>& i, const std::vector<float>& q);

    Mutex m_;
    double fs_ = 2048000.0, bw_ = 12000.0, off_ = 0.0, sqlDb_ = -120.0;
    Mode mode_ = Mode::WFM;
    float vol_ = 0.6f;
    bool mute_ = false, dirty_ = true;

    // chain
    int D1_ = 1, D2_ = 1, Da_ = 1;
    double r1_ = 0, r2_ = 0, ra_ = 0;
    uint32_t phase_ = 0, inc_ = 0;
    FirDecimator f1_, f2_, fa_, fb_;
    CubicResampler rs_, rsR_;

    // stereo (WFM): pilot PLL + difference channel
    bool stereo_ = false;
    Biquad pilotBp_, notchS_, notchD_;
    double pllPhase_ = 0, pllFreq_ = 0, pllW_ = 0, pllKp_ = 0, pllKi_ = 0;
    double lpE_ = 0, lpA_ = 0, lpAlpha_ = 0, blend_ = 0, blendStep_ = 0;
    float deemphD_ = 0;
    std::vector<float> sd_, dd_, l1_, r1o_, lo_, ro_;
    std::atomic<int> outCh_{1};
    std::atomic<bool> stereoLock_{false};
    Biquad post_;
    float prevI_ = 1, prevQ_ = 0, deemph_ = 0, deemphAlpha_ = 0.1f, fmScale_ = 1, amAvg_ = 0, amAlpha_ = 1e-3f;
    double power_ = 0;

    std::vector<float> wi_, wq_, s1i_, s1q_, s2i_, s2q_, au_, au2_, dummy_;
    std::atomic<double> level_{-120.0};
    std::atomic<bool> open_{true};
};

} // namespace orl
