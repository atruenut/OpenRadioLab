// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// The Experiment Journal window and the Signal Lab window.
#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "../journal/Journal.h"

namespace orl {

bool registerLabClasses(HINSTANCE hi);

// ---- Experiment Journal
struct JournalHost {
    std::function<JournalEntry()> capture;     // snapshot of what the radio is doing right now
};
// Opens the journal (or brings it to the front). newEntry = also add an entry for the current radio.
// "Tune to this" posts WM_ORL_TRYIT (see LearnWindow.h) to the owner.
void showJournal(HWND owner, HINSTANCE hi, HFONT font, const JournalHost& host, bool newEntry);

// ---- Signal Lab: level of the signal being listened to, over time
void showSignalLab(HWND owner, HINSTANCE hi, HFONT font, std::function<void()> addToJournal);
bool signalLabOpen();
void signalLabSetStatus(const std::string& why);   // shown in the lab when it is not recording
// Feed one level reading (dBFS) about every 100 ms. Clears itself when the frequency changes.
// noiseDb = the spectrum's background estimate (or 1e9 if unknown).
void signalLabPush(double levelDb, double hz, const std::string& label, double noiseDb);

} // namespace orl
