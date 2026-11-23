// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// The Experiment Journal: a plain-text notebook of what you listened to, how, and what you found.
// Stored as a simple readable text file so it is never locked inside the program.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orl {

struct JournalEntry {
    int64_t id = 0;
    std::string when;          // local date and time, "2026-11-22 15:47"
    std::string title;
    std::string notes;
    std::string antenna;
    std::string device;
    std::string mode;          // WFM / NFM / AM
    std::string gain;          // "auto" or "30 dB"
    std::string band;          // what the band table says is here
    std::string measurement;   // the click-to-measure text at the time
    std::string recording;     // path of a recording, if any
    double vfoHz = 0, centreHz = 0, levelDb = 0;
    uint32_t rateHz = 0;
    bool simulated = false;
};

class Journal {
public:
    bool load(const std::string& path, std::string& err);       // a missing file is an empty journal, not an error
    bool save(const std::string& path, std::string& err) const;
    int64_t add(JournalEntry e);                                 // assigns the id
    bool update(const JournalEntry& e);
    bool remove(int64_t id);
    const JournalEntry* find(int64_t id) const;
    const std::vector<JournalEntry>& entries() const { return e_; }
    std::string toMarkdown() const;                              // newest first

private:
    std::vector<JournalEntry> e_;
    int64_t next_ = 1;
};

} // namespace orl
