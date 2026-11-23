// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// The Learning Center window: lesson list, lesson text, optional "try it" button, and the dictionary.
#pragma once

#include <windows.h>

#define WM_ORL_TRYIT (WM_APP + 3)   // wParam = mode (0 WFM, 1 NFM, 2 AM), lParam = frequency in Hz

namespace orl {

bool registerLearnClass(HINSTANCE hi);
// Opens the window, or brings the existing one to the front. "Try it" posts WM_ORL_TRYIT to owner.
void showLearnWindow(HWND owner, HINSTANCE hi, HFONT font);

} // namespace orl
