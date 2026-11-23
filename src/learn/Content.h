// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// The Learning Center text: short lessons for complete beginners and a plain-language dictionary.
// Pure data so it can be tested and later moved to files. ASCII only (the Windows edit control is ANSI).
#pragma once

#include <cstddef>

namespace orl {

struct Lesson {
    const char* title;
    const char* body;       // paragraphs separated by '\n'
    double tryHz;           // 0 = no "Try it" button
    int tryMode;            // 0 WFM, 1 NFM, 2 AM (only used with tryHz)
    const char* tryLabel;
};

struct Term { const char* term; const char* meaning; };

const Lesson* lessons(size_t& count);
const Term* dictionary(size_t& count);

} // namespace orl
