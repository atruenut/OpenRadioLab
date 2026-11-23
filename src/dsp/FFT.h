// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Small self-contained radix-2 FFT and window functions (no external dependency).
#pragma once

#include <vector>

namespace orl {

struct cf { float r, i; };

class FFT {
public:
    explicit FFT(int n);              // n must be a power of two
    int size() const { return n_; }
    void forward(cf* x) const;        // in-place, unnormalised

private:
    int n_;
    std::vector<int>   rev_;
    std::vector<float> cs_, sn_;
};

enum class WindowType { Rectangular, Hann, Hamming, BlackmanHarris };

std::vector<float> makeWindow(WindowType t, int n);
const char* windowName(WindowType t);

} // namespace orl
