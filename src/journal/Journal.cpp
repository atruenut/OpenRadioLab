// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "Journal.h"

#include "../Util.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace orl {

static const char* kMagic = "OPENRADIO-JOURNAL 1";

static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') continue;
        else o += c;
    }
    return o;
}
static std::string unesc(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            o += s[i] == 'n' ? '\n' : s[i] == 't' ? '\t' : s[i];
        } else o += s[i];
    }
    return o;
}

bool Journal::save(const std::string& path, std::string& err) const {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { err = "Could not write " + tmp; return false; }
        f << kMagic << "\n";
        char b[64];
        for (const auto& e : e_) {
            f << "---\n";
            auto kv = [&](const char* k, const std::string& v) { f << k << "\t" << esc(v) << "\n"; };
            std::snprintf(b, sizeof b, "%lld", (long long)e.id); kv("id", b);
            kv("when", e.when); kv("title", e.title); kv("notes", e.notes); kv("antenna", e.antenna);
            kv("device", e.device); kv("mode", e.mode); kv("gain", e.gain); kv("band", e.band);
            kv("measurement", e.measurement); kv("recording", e.recording);
            std::snprintf(b, sizeof b, "%.0f", e.vfoHz); kv("vfo", b);
            std::snprintf(b, sizeof b, "%.0f", e.centreHz); kv("centre", b);
            std::snprintf(b, sizeof b, "%.1f", e.levelDb); kv("level", b);
            std::snprintf(b, sizeof b, "%u", e.rateHz); kv("rate", b);
            kv("simulated", e.simulated ? "1" : "0");
        }
        if (!f) { err = "Could not write " + tmp; return false; }
    }
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { err = "Could not replace " + path; return false; }
    return true;
}

bool Journal::load(const std::string& path, std::string& err) {
    e_.clear(); next_ = 1;
    std::ifstream f(path, std::ios::binary);
    if (!f) return true;                                  // no journal yet
    std::string line;
    std::getline(f, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != kMagic) { err = "That file is not an OpenRadio Lab journal."; return false; }
    JournalEntry cur; bool have = false;
    auto flush = [&] { if (have) { if (cur.id <= 0) cur.id = next_; next_ = std::max(next_, cur.id + 1); e_.push_back(cur); } cur = JournalEntry(); have = false; };
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "---") { flush(); have = true; continue; }
        const size_t t = line.find('\t');
        if (t == std::string::npos || !have) continue;
        const std::string k = line.substr(0, t), v = unesc(line.substr(t + 1));
        if (k == "id") cur.id = std::atoll(v.c_str());
        else if (k == "when") cur.when = v; else if (k == "title") cur.title = v; else if (k == "notes") cur.notes = v;
        else if (k == "antenna") cur.antenna = v; else if (k == "device") cur.device = v; else if (k == "mode") cur.mode = v;
        else if (k == "gain") cur.gain = v; else if (k == "band") cur.band = v; else if (k == "measurement") cur.measurement = v;
        else if (k == "recording") cur.recording = v;
        else if (k == "vfo") cur.vfoHz = std::atof(v.c_str()); else if (k == "centre") cur.centreHz = std::atof(v.c_str());
        else if (k == "level") cur.levelDb = std::atof(v.c_str()); else if (k == "rate") cur.rateHz = (uint32_t)std::atoll(v.c_str());
        else if (k == "simulated") cur.simulated = v == "1";
    }
    flush();
    return true;
}

int64_t Journal::add(JournalEntry e) { e.id = next_++; e_.push_back(e); return e.id; }

bool Journal::update(const JournalEntry& e) {
    for (auto& x : e_) if (x.id == e.id) { x = e; return true; }
    return false;
}

bool Journal::remove(int64_t id) {
    const size_t n = e_.size();
    e_.erase(std::remove_if(e_.begin(), e_.end(), [&](const JournalEntry& x) { return x.id == id; }), e_.end());
    return e_.size() != n;
}

const JournalEntry* Journal::find(int64_t id) const {
    for (const auto& x : e_) if (x.id == id) return &x;
    return nullptr;
}

std::string Journal::toMarkdown() const {
    std::ostringstream o;
    o << "# My OpenRadio Lab journal\n\n";
    std::vector<const JournalEntry*> v;
    for (const auto& e : e_) v.push_back(&e);
    std::sort(v.begin(), v.end(), [](const JournalEntry* a, const JournalEntry* b) { return a->when > b->when; });
    for (const JournalEntry* e : v) {
        o << "## " << (e->title.empty() ? formatFrequency(e->vfoHz) : e->title) << "\n\n";
        o << "*" << e->when << "*" << (e->simulated ? " - **simulated data**" : "") << "\n\n";
        o << "- Frequency: " << formatFrequency(e->vfoHz) << " (" << e->mode << "), band centre " << formatFrequency(e->centreHz) << "\n";
        if (!e->band.empty()) o << "- Band: " << e->band << "\n";
        if (!e->device.empty()) o << "- Receiver: " << e->device << "\n";
        if (!e->gain.empty()) o << "- Gain: " << e->gain << "\n";
        if (!e->antenna.empty()) o << "- Antenna: " << e->antenna << "\n";
        if (!e->recording.empty()) o << "- Recording: " << e->recording << "\n";
        o << "\n";
        if (!e->measurement.empty()) o << "Measurement: " << e->measurement << "\n\n";
        if (!e->notes.empty()) o << e->notes << "\n\n";
    }
    return o.str();
}

} // namespace orl
