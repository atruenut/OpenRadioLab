// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Morse Decoder window: shows text decoded from the CW audio while the receiver is in CW mode.
#pragma once

#include <windows.h>
#include <cstddef>

namespace orl {

bool registerMorseClass(HINSTANCE hi);
void showMorseWindow(HWND owner, HINSTANCE hi, HFONT font);
bool morseWindowOpen();
// Called from the DSP thread with the audio being played. Cheap no-op unless the window is open and the mode is CW.
void morseFeed(const float* audio, size_t frames, int channels);
// Called from the UI timer so the window knows whether the receiver is in CW mode.
void morseSetCwMode(bool cw);

} // namespace orl
