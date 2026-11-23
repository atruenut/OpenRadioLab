// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "SpectrumProcessor.h"

#include <cmath>

namespace orl {

SpectrumProcessor::SpectrumProcessor() {
    Lock l(m_);
    rebuildLocked();
}

void SpectrumProcessor::rebuildLocked() {
    fft_.reset(new FFT(fftSize_));
    window_ = makeWindow(win_, fftSize_);
    double s = 0;
    for (float w : window_) s += w;
    norm_ = 1.0 / (s * s);
    buf_.assign(fftSize_, cf{0, 0});
    fill_ = 0;
    accum_.assign(fftSize_, 0.0f);
    accumCount_ = 0;
}

void SpectrumProcessor::configure(int fftSize, WindowType w) {
    Lock l(m_);
    fftSize_ = fftSize;
    win_ = w;
    rebuildLocked();
}

void SpectrumProcessor::setTuning(double centerHz, double rate) {
    Lock l(m_);
    center_ = centerHz;
    rate_ = rate;
    fill_ = 0;
    accum_.assign(fftSize_, 0.0f);
    accumCount_ = 0;
    Lock lo(outM_);
    out_.clear();
}

void SpectrumProcessor::setLinesPerSecond(double lps) {
    Lock l(m_);
    lps_ = lps < 1 ? 1 : lps;
}

int SpectrumProcessor::fftSize() const { Lock l(m_); return fftSize_; }
WindowType SpectrumProcessor::window() const { Lock l(m_); return win_; }

void SpectrumProcessor::pushSamples(const float* iq, size_t n) {
    Lock l(m_);
    size_t pos = 0;
    while (pos < n) {
        size_t take = (size_t)fftSize_ - fill_;
        if (take > n - pos) take = n - pos;
        for (size_t k = 0; k < take; ++k) {
            const size_t i = fill_ + k;
            const float w = window_[i];
            buf_[i].r = iq[2 * (pos + k)]     * w;
            buf_[i].i = iq[2 * (pos + k) + 1] * w;
        }
        fill_ += take;
        pos += take;

        if (fill_ == (size_t)fftSize_) {
            fft_->forward(buf_.data());
            for (int k = 0; k < fftSize_; ++k)
                accum_[k] += buf_[k].r * buf_[k].r + buf_[k].i * buf_[k].i;
            ++accumCount_;
            fill_ = 0;

            const double now = nowSeconds();
            if (now - lastEmit_ >= 1.0 / lps_) {
                lastEmit_ = now;
                emitLocked();
            }
        }
    }
}

void SpectrumProcessor::emitLocked() {
    SpectrumFrame f;
    f.centerHz = center_;
    f.sampleRate = rate_;
    f.db.resize(fftSize_);
    const double scale = norm_ / (accumCount_ > 0 ? accumCount_ : 1);
    const int half = fftSize_ / 2;
    for (int j = 0; j < fftSize_; ++j) {
        const int k = (j + half) % fftSize_;           // fftshift: DC to the middle
        const double p = (double)accum_[k] * scale;
        f.db[j] = (float)(10.0 * std::log10(p + 1e-20));
    }
    accum_.assign(fftSize_, 0.0f);
    accumCount_ = 0;

    Lock lo(outM_);
    out_.push_back(std::move(f));
    while (out_.size() > 120) out_.pop_front();
}

size_t SpectrumProcessor::drain(std::vector<SpectrumFrame>& out) {
    Lock lo(outM_);
    const size_t n = out_.size();
    for (auto& f : out_) out.push_back(std::move(f));
    out_.clear();
    return n;
}

} // namespace orl
