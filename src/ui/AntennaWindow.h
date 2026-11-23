// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// The Antenna Workshop: type a frequency, get the lengths to cut, and read step-by-step build guides.
#pragma once

#include <windows.h>

namespace orl {

bool registerAntennaClass(HINSTANCE hi);
// startHz > 0 pre-fills the frequency (the frequency being listened to).
void showAntennaWorkshop(HWND owner, HINSTANCE hi, HFONT font, double startHz);

} // namespace orl
