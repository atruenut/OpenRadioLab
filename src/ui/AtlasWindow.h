// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// The Frequency Atlas window: browse well-known bands, read what they are, jump to an example.
#pragma once

#include <windows.h>

namespace orl {

bool registerAtlasClass(HINSTANCE hi);
// "Tune here" posts WM_ORL_TRYIT (see LearnWindow.h) to the owner.
void showAtlasWindow(HWND owner, HINSTANCE hi, HFONT font);

} // namespace orl
