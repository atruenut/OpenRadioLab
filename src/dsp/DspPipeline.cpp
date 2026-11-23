// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "DspPipeline.h"

#include <algorithm>

namespace orl {

void DspPipeline::start() {
    if (running_) return;
    running_ = true;
    th_.start([this] { run(); });
}

void DspPipeline::stop() {
    if (!running_) return;
    running_ = false;
    ev_.set();
    th_.join();
}

void DspPipeline::push(const float* iq, size_t n) {
    {
        Lock l(qm_);
        std::vector<float> b;
        if (!pool_.empty()) { b = std::move(pool_.back()); pool_.pop_back(); }
        b.assign(iq, iq + 2 * n);
        q_.push_back(std::move(b));
        if (q_.size() > kMaxQueue) {                 // DSP can't keep up: drop the oldest block
            pool_.push_back(std::move(q_.front()));
            q_.pop_front();
            ++dropped_;
        }
    }
    ev_.set();
}

void DspPipeline::flush() {
    {
        Lock l(qm_);
        for (auto& b : q_) pool_.push_back(std::move(b));
        q_.clear();
    }
    dm_.reset();
}

void DspPipeline::run() {
    while (running_) {
        ev_.wait(0.1);
        for (;;) {
            std::vector<float> blk;
            {
                Lock l(qm_);
                if (q_.empty()) break;
                blk = std::move(q_.front());
                q_.pop_front();
            }
            const size_t n = blk.size() / 2;
            sp_.pushSamples(blk.data(), n);
            if (audioOn_) {
                audio_.clear();
                dm_.process(blk.data(), n, audio_);
                if (!audio_.empty() && sink_) { const int ch = dm_.outputChannels(); sink_(audio_.data(), audio_.size() / ch, ch); }
            }
            {
                Lock l(qm_);
                if (pool_.size() < 16) pool_.push_back(std::move(blk));
            }
        }
    }
}

} // namespace orl
