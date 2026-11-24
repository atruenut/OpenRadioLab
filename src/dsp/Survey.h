// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Discovery: find signals anywhere in a swept spectrum, remember them across passes, and describe them.
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace orl {

struct SurveyRange { double lo, hi; const char* name; };
// quick = the popular services (aircraft, marine, public service, amateur, ISM...); otherwise everything the dongle can tune.
std::vector<SurveyRange> surveyRanges(bool quick, double minHz, double maxHz);

struct FoundSignal { double hz = 0, levelDb = 0, snrDb = 0, widthHz = 0; };

// db: stitched spectrum, bin i at loHz + i*binHz. Wide blocks (TV, cellular, ...) come out as one signal with a large width.
std::vector<FoundSignal> findSignals(const std::vector<float>& db, double loHz, double binHz, double marginDb = 9.0, double minSepHz = 12e3);

struct DiscoveryRow {
    double hz = 0, snrDb = 0, widthHz = 0;
    int seen = 0, passes = 0;       // seen in this many of this many passes
    bool activeNow = false;         // present in the most recent pass
    int mode = 1;                   // suggested way to listen: 0 WFM 1 NFM 2 AM 3 USB 4 LSB 5 CW
    bool known = false;             // inside a band the atlas describes
    std::string where;              // "Marine VHF" or similar
    std::string what;               // plain-language observation (never an identification)
    double score = 0;
    long long firstT = 0, lastT = 0;   // when it was first / last seen (seconds since 1970)
    bool isNew = false;                // first seen in this session, when an earlier saved scan exists to compare with
    std::string note;                  // the user's own label
    bool junk = false;                 // very probably the receiver, the computer or plain noise rather than a station
    std::string junkWhy;               // plain-language reason
    int hourSeen[24] = {0};            // sweeps (by local hour of day) in which it was present
    int hourPasses[24] = {0};          // sweeps made in each local hour, for all signals
};

// One line about when a signal tends to be heard, e.g. "Heard in most sweeps between 18:00 and 23:00", plus a 24-character bar (one per hour).
std::string activityPattern(const DiscoveryRow& r);

struct ScanChange { double hz = 0; int kind = 0; double snrOld = 0, snrNew = 0, widthHz = 0; std::string where, note; };   // kind: 0 gone, 1 appeared, 2 stronger, 3 weaker
class DiscoveryTable;


class DiscoveryTable {
public:
    void clear() { e_.clear(); passes_ = 0; hadBaseline_ = false; for (int& h : hourPasses_) h = 0; }
    void addSignals(const std::vector<FoundSignal>& s);   // from one swept range during the current pass
    void endPass(int hour = -1);                          // hour = local hour 0..23 (default: now)
    int passes() const { return passes_; }
    std::vector<DiscoveryRow> rows() const;               // best first
    size_t size() const { return e_.size(); }
    void setHourForTesting(int h) { curHour_ = h; }
    bool setNote(double hz, const std::string& note);     // label the signal nearest hz (within 1 kHz)
    // Saved scans (plain text, one signal per line). load() replaces the table; signals found afterwards that were not in it are marked new.
    bool save(const std::string& path) const;
    bool load(const std::string& path);
    std::string toCsv() const;                            // for spreadsheets
    // What changed from 'older' to this table: signals gone, new, or at least 6 dB stronger/weaker.
    std::vector<ScanChange> changesSince(const DiscoveryTable& older) const;

private:
    struct E { double hz = 0, width = 0, maxSnr = 0, lastSnr = 0; int seen = 0, lastPass = -1; long long firstT = 0, lastT = 0; bool loaded = false; std::string note; int hours[24] = {0}; };
    std::vector<E> e_;
    int passes_ = 0;
    int hourPasses_[24] = {0};
    int curHour_ = -1;                                    // test hook: pretend it is this hour (-1 = real clock)
    bool hadBaseline_ = false;
};

// Describe a signal from where it is and how wide it is.
void describeSignal(DiscoveryRow& r);

} // namespace orl
