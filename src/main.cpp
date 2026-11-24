// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// OpenRadio Lab - main window, device management and controls.
#include <windows.h>
#include <commctrl.h>
#include <dbt.h>
#include <shellapi.h>
#include <shlobj.h>

#include <atomic>
#include <cmath>
#include <map>
#include <set>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Bands.h"
#include "Util.h"
#include "audio/AudioOutput.h"
#include "dsp/BandScanner.h"
#include "io/IqFile.h"
#include "io/WavWriter.h"
#include "dsp/Demodulator.h"
#include "dsp/DspPipeline.h"
#include "dsp/SpectrumProcessor.h"
#include "sdr/DeviceDetect.h"
#include "sdr/RTLSDRDevice.h"
#include "sdr/SimulatedSDR.h"
#include "ui/AtlasWindow.h"
#include "ui/AntennaWindow.h"
#include "ui/MorseWindow.h"
#include "ui/AdsbWindow.h"
#include "ui/FavouritesWindow.h"
#include "dsp/AdsbDecoder.h"
#include "ui/DiscoverWindow.h"
#include "dsp/Survey.h"
#include "ui/LabWindows.h"
#include "ui/LearnWindow.h"
#include "ui/SpectrumView.h"

using namespace orl;

namespace {

const char* kAppName = "OpenRadio Lab 0.16.0";
const char* kMainClass = "ORLMainWindow";
const char* kReportClass = "ORLReportWindow";

enum {
    ID_DEVICE = 100, ID_RESCAN, ID_REPORT, ID_MODE, ID_PAUSE, ID_HELP, ID_LEARN, ID_REC, ID_OPENREC, ID_PB_PLAY, ID_PB_SEEK, ID_PB_TIME, ID_PB_LOOP, ID_PB_LIVE,
    ID_FREQ, ID_GO, ID_DOWN, ID_UP, ID_STEP, ID_PRESET, ID_SCAN, ID_STATIONS,
    ID_LISTEN, ID_VOL, ID_MUTE, ID_STEREO, ID_WAV, ID_BW, ID_SQL,
    ID_RATE, ID_FFT, ID_WINDOW, ID_AUDIODEV,
    ID_AUTOGAIN, ID_GAIN, ID_SMOOTH, ID_RANGE,
    ID_VIEW, ID_INFO, ID_STATION, ID_STATUS,
    ID_M_LIVE = 700, ID_M_JOURNAL, ID_M_NEWENTRY, ID_M_LAB, ID_M_EXIT, ID_M_ATLAS, ID_M_WAV, ID_M_ANTENNA, ID_M_MORSE, ID_M_FAVADD, ID_M_DISCOVER, ID_M_ADSB, ID_M_FAVLIST,
    ID_L_DEVICE = 300, ID_L_FREQ, ID_L_STEP, ID_L_LISTEN, ID_L_VOL, ID_L_BW, ID_L_BWV, ID_L_SQL, ID_L_SQLV,
    ID_L_RATE, ID_L_FFT, ID_L_WINDOW, ID_L_AUDIO, ID_L_GAIN, ID_L_GAINV, ID_L_SMOOTH, ID_L_RANGE,
};
const UINT_PTR TIMER_UI = 1, TIMER_RESCAN = 2;

struct Preset { const char* label; double hz; };
const Preset kPresets[] = {
    {"Quick jump to...", 0},
    {"FM broadcast (98.0 MHz)", 98.0e6},
    {"Airband - aircraft voice (123.0 MHz)", 123.0e6},
    {"2 m amateur radio (145.0 MHz)", 145.0e6},
    {"VHF weather band (162.475 MHz; Weatheradio Canada ended 2026)", 162.475e6},
    {"Remote controls / ISM (433.92 MHz)", 433.92e6},
    {"Aircraft transponders (1090 MHz)", 1090.0e6},
    {"AM broadcast (1.0 MHz) - needs HF capability", 1.0e6},
    {"40 m amateur voice, LSB (7.150 MHz) - needs HF", 7.150e6},
    {"20 m amateur voice, USB (14.200 MHz) - needs HF", 14.200e6},
    {"40 m amateur Morse, CW (7.030 MHz) - needs HF", 7.030e6},
};
const double kSteps[] = {100e3, 200e3, 500e3, 1e6, 2e6, 10e6, 100.0, 500.0, 1e3, 5e3, 25e3};   // the small ones are for sideband and Morse
const char* kStepNames[] = {"100 kHz", "200 kHz", "500 kHz", "1 MHz", "2 MHz", "10 MHz", "100 Hz (fine)", "500 Hz (fine)", "1 kHz", "5 kHz", "25 kHz"};
const int kFftSizes[] = {512, 1024, 2048, 4096, 8192, 16384};
const WindowType kWindows[] = {WindowType::Hann, WindowType::Hamming, WindowType::BlackmanHarris, WindowType::Rectangular};
const char* kModeNames[] = {"WFM - wide FM (broadcast music/talk)", "NFM - narrow FM (two-way voice)", "AM - amplitude (aircraft, AM radio)",
                          "USB - upper sideband (amateur voice above 10 MHz)", "LSB - lower sideband (amateur voice below 10 MHz)", "CW - Morse code (a pitched tone)"};
const char* kModeShort[] = {"WFM", "NFM", "AM", "USB", "LSB", "CW"};

struct DevEntry { bool sim; int rtlIndex; std::string label; std::string path; };

struct Settings {
    double freqHz = 100.0e6;       // centre frequency of the captured band
    double vfoHz = 100.0e6;        // frequency being listened to (inside the captured band)
    uint32_t rate = 2048000;
    double gainDb = 25.0;
    bool autoGain = true;
    int fftIdx = 2, windowIdx = 0, stepIdx = 3;
    bool advanced = false;
    int smoothPct = 60, rangeDb = 60;
    int mode = 0;                  // 0 WFM, 1 NFM, 2 AM
    int volume = 60;               // 0..100
    bool mute = false;
    bool stereo = true;            // decode stereo FM when the station sends it
    int squelchPos = 0;            // 0 = off, 1..81 = -100..-20 dBFS
    int nfmBwKhz = 12, amBwKhz = 8;
    int audioDev = 0;
    std::string recDir;            // where the Save dialog starts for recordings
    bool welcomed = false;         // the first-run welcome has been shown
};

struct App {
    HINSTANCE hi = nullptr;
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    int dpi = 96;

    SpectrumView view;
    SpectrumProcessor proc;
    Demodulator dm;
    DspPipeline pipe{proc, dm};
    AudioOutput audio;
    std::unique_ptr<SDRDevice> dev;
    DeviceCaps caps;
    std::atomic<uint64_t> samples{0};

    std::vector<DetectedDevice> usb;
    RtlProbe rtl;
    std::vector<DevEntry> entries;
    int current = -1;

    Settings cfg;
    std::string notice, audioNotice;
    bool paused = false;

    ULONGLONG lastStatusMs = 0;
    uint64_t lastSamples = 0;
    int framesSince = 0;
    double fps = 0, msps = 0;

    // recording / playback
    IqRecorder rec;
    std::string filePath;          // the recording offered in the device list
    std::string lastRec;           // the most recent recording saved or opened (for the journal)
    bool recWas = false;
    WavWriter wav;                 // "Save audio": what you hear, as a WAV file
    bool wavWas = false;

    // FM band scan
    BandScanner scan;
    bool scanning = false;
    ULONGLONG scanProgressMs = 0;
    double scanSavedCentre = 0, scanSavedVfo = 0;
    int scanSavedLps = 30;
    std::vector<ScanChannel> stations;
    std::string scanText;            // plain-language result of the last scan
} A;

std::string fmt1(double v) { char b[32]; std::snprintf(b, sizeof b, "%.1f", v); return b; }
void stopRecording(bool ask = false);
void stopAudioCapture(bool ask = false);
void layout();
void fillPresets();
void addFavourite();
void openFavourites();
std::string pickFile(bool save, const char* filter, const char* ext, const char* title, const std::string& initial);
void openJournal(bool);
void stopSurvey(const std::string& why);
bool surveyRunning();
void stopAdsb(bool restore);
void updatePlaybackBar(bool force = false);
IqFileDevice* player();

int S(int v) { return MulDiv(v, A.dpi, 96); }
HWND H(int id) { return GetDlgItem(A.hwnd, id); }

// ------------------------------------------------------------------ settings

std::string settingsPath() {
    char dir[MAX_PATH] = {0};
    if (SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, dir) != S_OK) return "OpenRadioLab.ini";
    std::string d = std::string(dir) + "\\OpenRadioLab";
    CreateDirectoryA(d.c_str(), nullptr);
    return d + "\\settings.ini";
}

void loadFavs(const std::string& path);
void loadSettings() {
    const std::string p = settingsPath();
    auto geti = [&](const char* k, int d) { return (int)GetPrivateProfileIntA("app", k, d, p.c_str()); };
    auto clampi = [](int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); };
    char b[64];
    GetPrivateProfileStringA("app", "freq", "100000000", b, sizeof b, p.c_str());
    A.cfg.freqHz = std::atof(b);
    if (A.cfg.freqHz < 1e5) A.cfg.freqHz = 100e6;
    GetPrivateProfileStringA("app", "vfo", b, b, sizeof b, p.c_str());       // defaults to the centre
    A.cfg.vfoHz = std::atof(b);
    if (A.cfg.vfoHz < 1e5) A.cfg.vfoHz = A.cfg.freqHz;
    A.cfg.rate = (uint32_t)geti("rate", 2048000);
    GetPrivateProfileStringA("app", "gain", "25", b, sizeof b, p.c_str());
    A.cfg.gainDb = std::atof(b);
    A.cfg.autoGain = geti("autogain", 1) != 0;
    A.cfg.fftIdx = clampi(geti("fft", 2), 0, 5);
    A.cfg.windowIdx = clampi(geti("window", 0), 0, 3);
    A.cfg.stepIdx = clampi(geti("step", 3), 0, 10);
    A.cfg.advanced = geti("advanced", 0) != 0;
    A.cfg.smoothPct = clampi(geti("smooth", 60), 0, 90);
    A.cfg.rangeDb = clampi(geti("range", 60), 30, 100);
    A.cfg.mode = clampi(geti("mode", 0), 0, 5);
    A.cfg.volume = clampi(geti("volume", 60), 0, 100);
    A.cfg.mute = geti("mute", 0) != 0;
    A.cfg.stereo = geti("stereo", 1) != 0;
    A.cfg.squelchPos = clampi(geti("squelch", 0), 0, 81);
    A.cfg.nfmBwKhz = clampi(geti("nfmbw", 12), 3, 30);
    A.cfg.amBwKhz = clampi(geti("ambw", 8), 3, 15);
    A.cfg.audioDev = std::max(0, geti("audiodev", 0));
    A.cfg.welcomed = geti("welcomed", 0) != 0;
    loadFavs(p);
    char rd[MAX_PATH] = {0};
    GetPrivateProfileStringA("app", "recdir", "", rd, sizeof rd, p.c_str());
    A.cfg.recDir = rd;
}

// Favourite stations: saved in the settings file as fav0, fav1, ... = "hz|mode|name".
struct Fav { double hz = 0; int mode = 0; std::string name; };
std::vector<Fav> g_favs;
const size_t kMaxFavs = 40;

void loadFavs(const std::string& path) {
    g_favs.clear();
    for (size_t i = 0; i < kMaxFavs; ++i) {
        char key[16], v[256] = {0};
        std::snprintf(key, sizeof key, "fav%zu", i);
        GetPrivateProfileStringA("favs", key, "", v, sizeof v, path.c_str());
        if (!v[0]) continue;
        Fav f; const std::string t = v;
        const size_t a = t.find('|'), b = a == std::string::npos ? a : t.find('|', a + 1);
        if (b == std::string::npos) continue;
        f.hz = std::atof(t.substr(0, a).c_str()); f.mode = std::max(0, std::min(5, std::atoi(t.substr(a + 1, b - a - 1).c_str()))); f.name = t.substr(b + 1);
        if (f.hz > 1e4) g_favs.push_back(f);
    }
}
void saveFavs(const std::string& path) {
    WritePrivateProfileStringA("favs", nullptr, nullptr, path.c_str());          // clear the section, then rewrite it
    for (size_t i = 0; i < g_favs.size(); ++i) {
        char key[16], v[300];
        std::snprintf(key, sizeof key, "fav%zu", i);
        std::snprintf(v, sizeof v, "%.0f|%d|%s", g_favs[i].hz, g_favs[i].mode, g_favs[i].name.c_str());
        WritePrivateProfileStringA("favs", key, v, path.c_str());
    }
}

void saveSettings() {
    const std::string p = settingsPath();
    auto puti = [&](const char* k, long long v) { WritePrivateProfileStringA("app", k, std::to_string(v).c_str(), p.c_str()); };
    puti("freq", (long long)std::llround(A.cfg.freqHz));
    puti("vfo", (long long)std::llround(A.cfg.vfoHz));
    puti("rate", A.cfg.rate);
    char b[32]; std::snprintf(b, sizeof b, "%.1f", A.cfg.gainDb);
    WritePrivateProfileStringA("app", "gain", b, p.c_str());
    puti("autogain", A.cfg.autoGain); puti("fft", A.cfg.fftIdx); puti("window", A.cfg.windowIdx);
    puti("step", A.cfg.stepIdx); puti("advanced", A.cfg.advanced);
    puti("smooth", A.cfg.smoothPct); puti("range", A.cfg.rangeDb);
    puti("mode", A.cfg.mode); puti("volume", A.cfg.volume); puti("mute", A.cfg.mute); puti("stereo", A.cfg.stereo);
    puti("squelch", A.cfg.squelchPos); puti("nfmbw", A.cfg.nfmBwKhz); puti("ambw", A.cfg.amBwKhz);
    puti("audiodev", A.cfg.audioDev); puti("welcomed", A.cfg.welcomed);
    saveFavs(p);
    WritePrivateProfileStringA("app", "recdir", A.cfg.recDir.c_str(), p.c_str());
}

// ------------------------------------------------------------------ small control helpers

HWND mk(const char* cls, const char* text, DWORD style, int id, DWORD ex = 0) {
    HWND h = CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, A.hwnd,
                             (HMENU)(INT_PTR)id, A.hi, nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)A.font, TRUE);
    return h;
}
void cbAdd(int id, const std::string& s) { SendMessageA(H(id), CB_ADDSTRING, 0, (LPARAM)s.c_str()); }
void cbSel(int id, int i) { SendMessageA(H(id), CB_SETCURSEL, i, 0); }
int cbGet(int id) { return (int)SendMessageA(H(id), CB_GETCURSEL, 0, 0); }
void setText(int id, const std::string& s) { SetWindowTextA(H(id), s.c_str()); }
// For the read-only text panels: do nothing if the text is unchanged, and keep the reader's scroll position when it does change.
void setPanelText(int id, const std::string& s) {
    static std::map<int, std::string> last;
    auto it = last.find(id);
    if (it != last.end() && it->second == s) return;
    last[id] = s;
    HWND w = H(id);
    const int first = (int)SendMessageA(w, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageA(w, WM_SETREDRAW, FALSE, 0);
    SetWindowTextA(w, s.c_str());
    const int now = (int)SendMessageA(w, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (first > now) SendMessageA(w, EM_LINESCROLL, 0, first - now);
    SendMessageA(w, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(w, nullptr, TRUE);
}
int tbGet(int id) { return (int)SendMessageA(H(id), TBM_GETPOS, 0, 0); }
void tbSet(int id, int v) { SendMessageA(H(id), TBM_SETPOS, TRUE, v); }
void tbRange(int id, int lo, int hi) { SendMessageA(H(id), TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi)); }

std::string rateName(uint32_t r) {
    char b[32]; std::snprintf(b, sizeof b, "%.3f MS/s", r / 1e6);
    return b;
}

// ------------------------------------------------------------------ listening

double bandwidthHz() {
    if (A.cfg.mode == 0) return Demodulator::kWfmBandwidth;
    if (A.cfg.mode == 3 || A.cfg.mode == 4) return Demodulator::kSsbBandwidth;
    if (A.cfg.mode == 5) return 400.0;
    return (A.cfg.mode == 1 ? A.cfg.nfmBwKhz : A.cfg.amBwKhz) * 1000.0;
}
double squelchDb() {
    if (!A.cfg.advanced || A.cfg.squelchPos == 0) return -120.0;     // Beginner mode never squelches
    return -101.0 + A.cfg.squelchPos;
}

void updateListenLabels() {
    char b[48];
    if (A.cfg.mode == 0) std::snprintf(b, sizeof b, "180 kHz");
    else if (A.cfg.mode >= 3) std::snprintf(b, sizeof b, "%.1f kHz", bandwidthHz() / 1000.0);
    else std::snprintf(b, sizeof b, "%.0f kHz", bandwidthHz() / 1000.0);
    setText(ID_L_BWV, b);
    if (A.cfg.squelchPos == 0) std::snprintf(b, sizeof b, "off");
    else std::snprintf(b, sizeof b, "%.0f dBFS", -101.0 + A.cfg.squelchPos);
    setText(ID_L_SQLV, b);
}

void syncBwControl() {
    if (A.cfg.mode == 0 || A.cfg.mode >= 3) {                      // WFM, SSB and CW use a fixed bandwidth
        EnableWindow(H(ID_BW), FALSE);
    } else {
        EnableWindow(H(ID_BW), TRUE);
        if (A.cfg.mode == 1) { tbRange(ID_BW, 3, 30); tbSet(ID_BW, A.cfg.nfmBwKhz); }
        else                 { tbRange(ID_BW, 3, 15); tbSet(ID_BW, A.cfg.amBwKhz); }
    }
}

void applyListen() {
    // Keep the listening frequency inside the usable part of the captured band.
    if (std::fabs(A.cfg.vfoHz - A.cfg.freqHz) > 0.45 * A.cfg.rate) A.cfg.vfoHz = A.cfg.freqHz;
    A.dm.setSampleRate((double)A.cfg.rate);
    A.dm.setMode((Demodulator::Mode)A.cfg.mode, bandwidthHz());
    A.dm.setOffsetHz(A.cfg.vfoHz - A.cfg.freqHz);
    A.dm.setVolume(A.cfg.volume / 100.0f);
    A.dm.setMute(A.cfg.mute);
    A.dm.setStereo(A.cfg.stereo);
    A.dm.setSquelchDb(squelchDb());
    A.view.setListen(A.cfg.vfoHz, bandwidthHz(), true);
    updateListenLabels();
}

// ------------------------------------------------------------------ device handling

std::string deviceBanner() {
    if (!A.dev) return "No device.";
    if (A.dev->isPlayback())
        return "RECORDING PLAYBACK - " + A.dev->description() + ". This is a saved capture replayed from a file, not live radio" +
               (A.dev->isSimulated() ? ", and it contains SIMULATED data." : ".") + " Its tuning is fixed; click or double-click inside the view to listen.";
    if (A.dev->isSimulated())
        return "SIMULATED SDR - every signal on this screen is generated by software. Nothing is being received "
               "from the air. Connect an RTL-SDR dongle and press Rescan to listen to real radio.";
    return "LIVE RECEIVER - " + A.dev->description() + ". These are real radio signals picked up by your antenna.";
}

std::string detectionSummary() {
    const bool rtlUsable = !A.rtl.devices.empty();
    std::string s;
    for (const auto& d : A.usb) {
        if (d.family == SdrFamily::RTLSDR) {
            if (rtlUsable) continue;
            if (!A.rtl.libraryFound)
                s += "Found " + d.name + ", but rtlsdr.dll is missing - see 'Device report' and the Help manual. ";
            else if (d.driver == DriverState::WrongDriver || d.driver == DriverState::NotInstalled)
                s += "Found " + d.name + ", but Windows is using the wrong driver for it (it needs WinUSB) - "
                     "see 'Device report' for the Zadig steps. ";
            else
                s += "Found " + d.name + ", but it could not be opened - another program may be using it. "
                     "See 'Device report'. ";
        } else {
            s += "Found " + d.name + " (" + familyName(d.family) + "): recognised, but not supported yet. ";
        }
    }
    if (s.empty() && !rtlUsable && A.usb.empty())
        s = "No SDR hardware detected. Plug in a dongle and press Rescan, or press Help for the setup manual. ";
    return s;
}

// What the station itself is sending (RDS), as plain text; empty when not listening to wide FM.
std::string rdsSection(bool forJournal = false) {
    if (A.cfg.mode != 0 || !A.dev) return "";
    const RdsInfo r = A.dm.rdsInfo();
    char b[160];
    if (r.locked && (!r.ps.empty() || !r.rt.empty())) {
        std::string t = forJournal ? "Station data sent by the station (RDS): " : "Station data (RDS) - sent by the station itself\r\n";
        std::string ps = r.ps; while (!ps.empty() && ps.back() == ' ') ps.pop_back();
        const std::string nl = forJournal ? "; " : "\r\n";
        if (!ps.empty()) t += (forJournal ? "name " : "  Name: ") + ps + nl;
        if (!r.rt.empty()) t += (forJournal ? "text '" : "  Text (often the song or show): ") + r.rt + (forJournal ? "'" : "") + nl;
        if (r.pty > 0) t += std::string(forJournal ? "type " : "  Programme type: ") + rdsPtyName(r.pty) + nl;
        if (r.havePi && !forJournal) { std::snprintf(b, sizeof b, "  Station ID code: %04X\r\n", r.pi); t += b; }
        return t;
    }
    if (forJournal) return "";
    std::string t = "Station data (RDS): none received yet.\r\n  Many FM stations send their name and song text. Weak signals often lose it; it can take a few seconds to appear.\r\n";
    if (A.cfg.advanced && (r.goodBlocks || r.badBlocks)) { std::snprintf(b, sizeof b, "  (%u data blocks read correctly, %u damaged)\r\n", r.goodBlocks, r.badBlocks); t += b; }
    return t;
}

bool stationPanelWanted() { return A.cfg.mode == 0 && A.dev; }

// The station card: what the station says about itself, laid out to read at a glance.
void updateStation() {
    if (!stationPanelWanted()) return;
    const RdsInfo r = A.dm.rdsInfo();
    std::string t = "STATION DATA (RDS)\r\nSent by the station itself.\r\n\r\n";
    char b[160];
    if (r.locked && (!r.ps.empty() || !r.rt.empty())) {
        std::string ps = r.ps; while (!ps.empty() && ps.back() == ' ') ps.pop_back();
        t += "Name\r\n  " + (ps.empty() ? std::string("(not received yet)") : ps) + "\r\n\r\n";
        t += "Text (often the song or show)\r\n  " + (r.rt.empty() ? std::string("(not received yet)") : r.rt) + "\r\n\r\n";
        t += std::string("Programme type\r\n  ") + (r.pty > 0 ? rdsPtyName(r.pty) : "not stated") + "\r\n\r\n";
        if (r.havePi) { std::snprintf(b, sizeof b, "Station ID code\r\n  %04X\r\n", r.pi); t += b; }
    } else {
        t += "None received yet.\r\n\r\nMany FM stations send their name and song text. It can take a few seconds to appear, and weak signals often lose it.\r\n";
        if (A.cfg.advanced && (r.goodBlocks || r.badBlocks)) { std::snprintf(b, sizeof b, "\r\n(%u data blocks read correctly, %u damaged)\r\n", r.goodBlocks, r.badBlocks); t += b; }
    }
    setPanelText(ID_STATION, t);
}

void updateInfo() {
    std::string t = deviceBanner() + "\r\n";
    const std::string det = detectionSummary();
    if (!det.empty()) t += det + "\r\n";
    if (!A.notice.empty()) t += A.notice + "\r\n";
    if (!A.audioNotice.empty()) t += A.audioNotice + "\r\n";
    if (A.dev && !A.dev->isSimulated() && A.dev->description().find("R828D") != std::string::npos)
        t += "This looks like a V4-style dongle (R828D tuner). These need a recent RTL-SDR Blog build of rtlsdr.dll "
             "or you may see only noise or wrong frequencies - see Help, 'Only noise on FM'.\r\n";
    if (!A.scanText.empty()) t += A.scanText + "\r\n";
    t += "\r\n";

    // What is here, and how to listen to it.
    t += "Listening to " + formatFrequency(A.cfg.vfoHz) + " as " + kModeShort[A.cfg.mode];
    const Band* b = findBand(A.cfg.vfoHz);
    if (b) {
        t += std::string(" - ") + b->name + ". " + b->note;
        if (b->mode != A.cfg.mode)
            t += std::string(" This band normally uses ") + kModeShort[b->mode] + "; switching mode is a good experiment - "
                 "the wrong mode sounds distorted or silent.";
    } else {
        t += ". This frequency is not in the built-in band table.";
    }
    t += "\r\n";

    if (!A.cfg.advanced) {
        t += "\r\nHow to read this screen\r\n"
             "  Top graph (spectrum): how strong the radio energy is at each frequency right now. Peaks are "
             "transmissions; the ragged baseline is background noise.\r\n"
             "  Bottom graph (waterfall): the same data scrolling downwards over time - brighter means stronger.\r\n"
             "  Red marker: the frequency you are listening to. Click a peak to measure it; double-click to listen to it.\r\n"
             "  Hiss on an empty frequency is normal: that is the receiver amplifying background noise.\r\n";
    }
    if (!A.view.observation().empty())
        t += "\r\nMeasurement (an estimate from this display, not an identification)\r\n  " + A.view.observation() + "\r\n";
    setPanelText(ID_INFO, t);
}

void applyGain() {
    if (!A.dev) return;
    const bool autoG = !A.cfg.advanced || A.cfg.autoGain;      // beginner mode always uses automatic gain
    if (autoG) A.dev->setAutoGain(true);
    else A.dev->setGainDb(A.cfg.gainDb);
    EnableWindow(H(ID_GAIN), A.cfg.advanced && !A.cfg.autoGain);
}

void applyTuning(bool clear = true) {
    if (!A.dev) return;
    if (A.rec.active()) stopRecording();           // a recording has one centre frequency; retuning ends it
    A.dev->setSampleRate(A.cfg.rate);
    A.dev->setCenterFrequency((uint32_t)std::llround(A.cfg.freqHz));
    A.proc.setTuning(A.cfg.freqHz, (double)A.cfg.rate);
    A.pipe.flush();
    if (clear) A.view.clearHistory();
    applyListen();
    setText(ID_FREQ, formatFrequency(A.cfg.vfoHz));
}

void applyDsp() {
    A.proc.configure(kFftSizes[A.cfg.fftIdx], kWindows[A.cfg.windowIdx]);
    A.proc.setTuning(A.cfg.freqHz, (double)A.cfg.rate);
    A.view.clearHistory();
}

std::string megabytes(uint64_t b) { char t[32]; std::snprintf(t, sizeof t, "%.1f MB", b / 1048576.0); return t; }

void offerShowInFolder(const std::string& headline, const std::string& path, const std::string& detail) {
    const std::string msg = headline + "\n\n" + path + "\n\n" + detail + "\n\nShow it in its folder?";
    if (MessageBoxA(A.hwnd, msg.c_str(), kAppName, MB_YESNO | MB_ICONINFORMATION) == IDYES) {
        const std::string arg = "/select,\"" + path + "\"";
        ShellExecuteA(A.hwnd, "open", "explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

void stopRecording(bool ask) {
    if (!A.rec.active() && !A.recWas) return;
    const uint64_t bytes = A.rec.bytes();
    const bool limit = A.rec.hitLimit();
    const std::string path = A.rec.path();
    A.rec.stop();
    A.recWas = false;
    if (bytes > 0) A.lastRec = path;
    SetWindowTextA(GetDlgItem(A.hwnd, ID_REC), "Record");
    A.notice = "Recording saved (" + megabytes(bytes) + ", " + fmt1(bytes / 2.0 / std::max<uint32_t>(1, A.cfg.rate)) + " s" +
               (limit ? ", size limit reached" : "") + "): " + path + "  -  press 'Open recording...' to play it back, or share the file.";
    if (ask && bytes > 0)
        offerShowInFolder("Recording saved:", path, megabytes(bytes) + ", about " + fmt1(bytes / 2.0 / std::max<uint32_t>(1, A.cfg.rate)) + " seconds of radio.");
}

void stopAudioCapture(bool ask) {
    if (!A.wav.active() && !A.wavWas) return;
    const double secs = A.wav.seconds();
    const bool limit = A.wav.hitLimit();
    const std::string path = A.wav.path();
    A.wav.stop();
    A.wavWas = false;
    SetWindowTextA(GetDlgItem(A.hwnd, ID_WAV), "Save audio");
    CheckMenuItem(GetMenu(A.hwnd), ID_M_WAV, MF_BYCOMMAND | MF_UNCHECKED);
    A.notice = "Audio saved (" + fmt1(secs) + " s" + (limit ? ", size limit reached" : "") + "): " + path;
    if (ask && secs > 0) offerShowInFolder("Audio saved:", path, fmt1(secs) + " seconds. It is a normal WAV file that any media player can open.");
}

void stopDevice() {
    stopRecording();
    if (A.scanning) {
        A.scanning = false;
        SetWindowTextA(GetDlgItem(A.hwnd, ID_SCAN), "Scan FM");
        A.proc.setLinesPerSecond(A.scanSavedLps);
        A.dm.setMute(A.cfg.mute);
    }
    stopAdsb(false);
    if (A.dev) { A.dev->stop(); A.dev->close(); A.dev.reset(); }
}

void fillRateCombo() {
    SendMessageA(H(ID_RATE), CB_RESETCONTENT, 0, 0);
    int sel = -1;
    for (size_t i = 0; i < A.caps.sampleRates.size(); ++i) {
        cbAdd(ID_RATE, rateName(A.caps.sampleRates[i]));
        if (A.caps.sampleRates[i] == A.cfg.rate) sel = (int)i;
    }
    if (sel < 0 && !A.caps.sampleRates.empty()) {
        for (size_t i = 0; i < A.caps.sampleRates.size(); ++i)
            if (A.caps.sampleRates[i] == 2048000) sel = (int)i;
        if (sel < 0) sel = 0;
        A.cfg.rate = A.caps.sampleRates[sel];
    }
    cbSel(ID_RATE, sel);
}

bool startEntry(int idx, bool allowFallback) {
    stopDevice();
    if (idx < 0 || idx >= (int)A.entries.size()) idx = (int)A.entries.size() - 1;
    const DevEntry& e = A.entries[idx];

    std::unique_ptr<SDRDevice> d;
    if (!e.path.empty()) d.reset(new IqFileDevice(e.path));
    else if (e.sim)      d.reset(new SimulatedSDR());
    else                 d.reset(new RTLSDRDevice(e.rtlIndex, e.label));

    std::string err;
    if (!d->open(err)) {
        A.notice = "Could not open " + e.label + ": " + err;
        if (allowFallback && !e.sim) {
            for (size_t i = 0; i < A.entries.size(); ++i)
                if (A.entries[i].sim) return startEntry((int)i, false);
        }
        updateInfo();
        return false;
    }
    A.dev = std::move(d);
    A.current = idx;
    A.caps = A.dev->caps();

    double f = A.cfg.freqHz;
    if (A.dev->isPlayback()) f = 0.5 * ((double)A.caps.minFreqHz + (double)A.caps.maxFreqHz);   // a recording has one fixed centre
    else if (f < A.caps.minFreqHz || f > A.caps.maxFreqHz)
        f = std::max<double>(A.caps.minFreqHz, std::min<double>(A.caps.maxFreqHz, 100e6));
    if (f != A.cfg.freqHz || A.dev->isPlayback()) A.cfg.vfoHz = f;
    A.cfg.freqHz = f;
    fillRateCombo();

    SendMessageA(H(ID_GAIN), TBM_SETRANGE, TRUE, MAKELPARAM(0, (int)std::lround(A.caps.maxGainDb)));
    SendMessageA(H(ID_GAIN), TBM_SETPOS, TRUE, (int)std::lround(A.cfg.gainDb));

    applyDsp();
    applyTuning();
    applyGain();
    A.view.setBanner(A.dev->isSimulated() ? "SIMULATED SIGNALS" : "");
    A.samples = 0; A.lastSamples = 0;
    // The device thread only hands samples to the DSP thread; it never waits on FFTs or filters.
    A.dev->start([](const float* iq, size_t n) { A.pipe.push(iq, n); A.samples += n; if (A.rec.active()) A.rec.write(iq, n); });
    SendMessageA(H(ID_DEVICE), CB_SETCURSEL, idx, 0);
    if (e.sim && !A.notice.empty() && A.notice.compare(0, 9, "Could not") != 0) A.notice.clear();
    layout();
    updatePlaybackBar(true);
    updateInfo();
    return true;
}

void rescan(bool switchToRealIfIdle) {
    A.usb = detectUsbSdrs();
    const bool liveReal = A.dev && !A.dev->isSimulated() && A.dev->isRunning();
    A.rtl = RTLSDRDevice::probe(!liveReal);

    A.entries.clear();
    for (const auto& d : A.rtl.devices) {
        DevEntry e;
        e.sim = false; e.rtlIndex = d.index;
        e.label = "RTL-SDR #" + std::to_string(d.index) + ": " + d.name;
        if (!d.serial.empty()) e.label += " (serial " + d.serial + ")";
        A.entries.push_back(e);
    }
    if (!A.filePath.empty()) A.entries.push_back({false, -1, "Recording: " + IqFileDevice::baseName(A.filePath), A.filePath});
    A.entries.push_back({true, -1, "Simulated SDR (no hardware needed)", ""});

    SendMessageA(H(ID_DEVICE), CB_RESETCONTENT, 0, 0);
    for (auto& e : A.entries) cbAdd(ID_DEVICE, e.label);

    const bool haveReal = !A.rtl.devices.empty();
    if (!A.dev) {
        startEntry(haveReal ? 0 : (int)A.entries.size() - 1, true);
    } else if (A.dev->isSimulated()) {
        if (haveReal && switchToRealIfIdle) {
            A.notice = "Hardware detected: switched from the simulator to " + A.entries[0].label + ".";
            startEntry(0, true);
        } else {
            A.current = (int)A.entries.size() - 1;
            cbSel(ID_DEVICE, A.current);
            updateInfo();
        }
    } else {
        int keep = -1;
        for (size_t i = 0; i < A.entries.size(); ++i)
            if (!A.entries[i].sim && A.dev->name() == A.entries[i].label) keep = (int)i;
        if (keep >= 0) { A.current = keep; cbSel(ID_DEVICE, keep); }
        updateInfo();
    }
}

// ------------------------------------------------------------------ tuning actions

void setModeIndex(int m, bool fromUser) {
    A.cfg.mode = std::max(0, std::min(5, m));
    cbSel(ID_LISTEN, A.cfg.mode);
    syncBwControl();
    applyListen();
    A.pipe.flush();
    (void)fromUser;
}

// Beginner mode picks the usual mode for a band automatically; Advanced mode leaves it to you.
void autoModeFor(double hz) {
    if (A.cfg.advanced) return;
    if (const Band* b = findBand(hz)) if (b->mode != A.cfg.mode) setModeIndex(b->mode, false);
}

// Move the whole captured band (and the listening frequency with it).
void listenTo(double hz);

void tuneTo(double hz) {
    if (!A.dev) return;
    if (A.dev->isPlayback()) {                      // a recording cannot be retuned, only listened into
        if (std::fabs(hz - A.cfg.freqHz) <= 0.42 * A.cfg.rate) { listenTo(hz); return; }
        A.notice = "This recording only covers " + formatFrequency(A.cfg.freqHz - 0.42 * A.cfg.rate) + " to " +
                   formatFrequency(A.cfg.freqHz + 0.42 * A.cfg.rate) + ". Choose the live receiver to go elsewhere.";
        updateInfo();
        setText(ID_FREQ, formatFrequency(A.cfg.vfoHz));
        return;
    }
    if (hz < A.caps.minFreqHz || hz > A.caps.maxFreqHz) {
        char b[256];
        std::snprintf(b, sizeof b, "%s is outside what this receiver can tune (%s to %s).%s",
                      formatFrequency(hz).c_str(), formatFrequency(A.caps.minFreqHz).c_str(),
                      formatFrequency(A.caps.maxFreqHz).c_str(),
                      (!A.dev->isSimulated() && hz < 24e6)
                          ? " Most RTL-SDR dongles cannot tune below 24 MHz. The RTL-SDR Blog V4 can (it has an HF input), with the Blog's driver files."
                          : "");
        A.notice = b;
        updateInfo();
        setText(ID_FREQ, formatFrequency(A.cfg.vfoHz));
        return;
    }
    A.notice.clear();
    A.cfg.freqHz = hz;
    A.cfg.vfoHz = hz;
    autoModeFor(hz);
    applyTuning();
    updateInfo();
}

// Listen to a frequency inside the current view without moving the view.
void listenTo(double hz) {
    if (!A.dev) return;
    if (std::fabs(hz - A.cfg.freqHz) > 0.42 * A.cfg.rate) { tuneTo(hz); return; }
    A.notice.clear();
    A.cfg.vfoHz = hz;
    autoModeFor(hz);
    A.pipe.flush();
    applyListen();
    setText(ID_FREQ, formatFrequency(hz));
    updateInfo();
}

void setMode(bool advanced);
void layout();

// ------------------------------------------------------------------ FM band scan

void fillStations() {
    SendMessageA(H(ID_STATIONS), CB_RESETCONTENT, 0, 0);
    cbAdd(ID_STATIONS, A.stations.empty() ? "Stations found: none yet" : "Stations found (" + std::to_string(A.stations.size()) + ")...");
    // Listed by frequency so they read like a radio dial; the strength is shown beside each.
    std::vector<ScanChannel> bySpot = A.stations;
    std::sort(bySpot.begin(), bySpot.end(), [](const ScanChannel& a, const ScanChannel& b) { return a.hz < b.hz; });
    A.stations = bySpot;
    for (auto& s : A.stations) cbAdd(ID_STATIONS, formatFrequency(s.hz) + "   (+" + fmt1(s.snrDb) + " dB)");
    cbSel(ID_STATIONS, 0);
}

void finishScan(bool aborted, const std::string& why) {
    A.scanning = false;
    setText(ID_SCAN, "Scan FM");
    A.proc.setLinesPerSecond(A.scanSavedLps);
    A.dm.setMute(A.cfg.mute);
    if (aborted) {
        A.scanText = "Scan stopped: " + why;
        A.cfg.freqHz = A.scanSavedCentre; A.cfg.vfoHz = A.scanSavedVfo;
        applyTuning();
        updateInfo();
        return;
    }
    const ScanReport r = A.scan.result();
    A.stations = r.stations;
    fillStations();
    std::string t;
    if (!r.stations.empty()) {
        ScanChannel best = r.stations.front();
        for (auto& s : r.stations) if (s.snrDb > best.snrDb) best = s;
        t = "FM scan: found " + std::to_string(r.stations.size()) + " station(s). Strongest: " + formatFrequency(best.hz) +
            " (" + fmt1(best.snrDb) + " dB above the noise). Pick one from the Stations list.";
        if (r.peakDb > -12.0)
            t += " Warning: a very strong signal (" + fmt1(r.peakDb) + " dBFS at " + formatFrequency(r.peakHz) +
                 ") may be overloading the receiver; in Advanced mode try lowering the gain.";
        A.cfg.freqHz = best.hz; A.cfg.vfoHz = best.hz;
        autoModeFor(best.hz);
    } else {
        t = "FM scan: no stations found. Noise floor " + fmt1(r.floorDb) + " dBFS; the best channel was only " +
            fmt1(r.strongestSnrDb) + " dB above it. Usual causes, most likely first: (1) no antenna, or it is not plugged in "
            "tightly / not extended - attach the telescopic antenna, extend it to ~75 cm, and put it by a window; "
            "(2) gain: in Advanced mode untick Auto gain and try 30-45 dB; (3) a library that does not match your dongle "
            "(see Help, 'Only noise on FM'); (4) a genuinely weak area - try a high spot outdoors.";
        if (r.peakDb > -12.0)
            t += " Note: something very strong (" + fmt1(r.peakDb) + " dBFS) was seen, so the receiver may be overloaded - lower the gain.";
        A.cfg.freqHz = A.scanSavedCentre; A.cfg.vfoHz = A.scanSavedVfo;
    }
    A.scanText = t;
    applyTuning();
    setText(ID_FREQ, formatFrequency(A.cfg.vfoHz));
    updateInfo();
}

void startScan() {
    if (!A.dev) return;
    if (surveyRunning()) { stopSurvey("Sweep stopped."); return; }
    stopAdsb(true);
    if (A.scanning) { finishScan(true, "cancelled."); return; }
    if (A.dev->isPlayback()) { A.scanText = "A recording covers one slice of the band and cannot be scanned. Choose the live receiver."; updateInfo(); return; }
    stopRecording();
    if (A.dev->caps().minFreqHz > 88e6 || A.dev->caps().maxFreqHz < 108e6) {
        A.scanText = "This receiver cannot tune the FM broadcast band.";
        updateInfo();
        return;
    }
    A.scanSavedCentre = A.cfg.freqHz; A.scanSavedVfo = A.cfg.vfoHz;
    A.scanSavedLps = 30;
    A.scan.begin(BandScanner::fmPlan((double)A.cfg.rate, kFftSizes[A.cfg.fftIdx]));
    A.scanning = true;
    A.scanProgressMs = GetTickCount64();
    A.dm.setMute(true);
    A.proc.setLinesPerSecond(60);
    setText(ID_SCAN, "Stop scan");
    A.cfg.freqHz = A.scan.centre();
    A.dev->setCenterFrequency((uint32_t)std::llround(A.scan.centre()));
    A.proc.setTuning(A.scan.centre(), (double)A.cfg.rate);
    A.pipe.flush();
    A.view.clearHistory();
    A.scanText = "Scanning the FM band...";
    updateInfo();
}

// ------------------------------------------------------------------ Discover: sweep, list, auto-listen

struct DiscoverState {
    bool sweeping = false, quick = true;
    std::vector<SurveyRange> list; size_t idx = 0; int pass = 0;
    DiscoveryTable table;
    std::string status = "Press Quick sweep to look for signals around you. It checks the popular services and repeats until you press Stop.";
    std::string log;
    double savedCentre = 0, savedVfo = 0; int savedMode = 0;
    // auto-listen
    bool autoOn = false; int ast = 0; size_t aidx = 0;
    ULONGLONG t0 = 0, holdStart = 0, quietSince = 0, minHold = 0;
    double mn = 1e9, mx = -1e9;
    std::vector<DiscoveryRow> cand; std::map<long, double> base;
    bool recClips = false, clipOn = false, beepNew = false; std::set<long> told;
} D;

bool surveyRunning() { return D.sweeping; }
void autosaveDiscovery();
void stopClip();
std::string discoveryPath() {
    std::string p = settingsPath();
    const size_t k = p.find_last_of("\\/");
    return (k == std::string::npos ? std::string() : p.substr(0, k + 1)) + "discovery.tsv";
}
void autosaveDiscovery() { if (D.table.size()) D.table.save(discoveryPath()); }
void discoverPush() { discoverUpdate(D.table.rows(), D.status, D.sweeping, D.autoOn, D.log); }

void logActivity(const std::string& s) {
    SYSTEMTIME st; GetLocalTime(&st);
    char b[32]; std::snprintf(b, sizeof b, "%02d:%02d:%02d  ", st.wHour, st.wMinute, st.wSecond);
    D.log += b + s + "\r\n";
    if (D.log.size() > 6000) D.log.erase(0, D.log.size() - 6000);
}

void setAutoListen(bool on) {
    if (on == D.autoOn) return;
    if (on) {
        if (!A.dev || A.dev->isPlayback()) { D.status = "Auto-listen needs the live receiver."; discoverPush(); return; }
        if (D.table.rows().empty()) { D.status = "Run a sweep first so there is something to listen to."; discoverPush(); return; }
        if (D.sweeping) { D.status = "Stop the sweep first, then turn on Auto-listen."; discoverPush(); return; }
        D.autoOn = true; D.ast = 0; D.aidx = 0; D.cand.clear();
        A.dm.setMute(true);
        D.status = "Auto-listen is hopping between the channels found. It stops where it hears activity.";
        logActivity("Auto-listen started.");
    } else {
        stopClip();
        D.autoOn = false;
        A.dm.setMute(A.cfg.mute);
        D.status = "Auto-listen stopped.";
        logActivity("Auto-listen stopped.");
    }
    discoverPush();
}

// Say so (in the log) when a sweep finds a signal that was not in the scan we started from.
void announceNew() {
    int n = 0;
    for (auto& r : D.table.rows()) {
        if (!r.isNew) continue;
        const long key = std::lround(r.hz / 1000.0);
        if (!D.told.insert(key).second) continue;
        char b[160]; std::snprintf(b, sizeof b, "NEW signal: %s (%s, %.0f dB above the noise)", formatFrequency(r.hz).c_str(), r.where.c_str(), r.snrDb);
        logActivity(b); ++n;
    }
    if (n && D.beepNew) MessageBeep(MB_OK);
}

void stopClip() {
    if (!D.clipOn) return;
    D.clipOn = false;
    const std::string path = A.wav.path(); const double secs = A.wav.seconds();
    A.wav.stop();
    char b[64]; std::snprintf(b, sizeof b, "  ...clip saved (%.0f s): ", secs);
    D.log += std::string(b) + path + "\r\n";
}

void startClip(double hz) {
    if (!D.recClips || D.clipOn || A.wav.active()) return;
    std::string dir = A.cfg.recDir;
    if (dir.empty()) { const std::string p = settingsPath(); const size_t k = p.find_last_of("\\/"); dir = (k == std::string::npos ? std::string(".") : p.substr(0, k)) + "\\clips"; }
    CreateDirectoryA(dir.c_str(), nullptr);
    SYSTEMTIME t; GetLocalTime(&t); char f[MAX_PATH];
    std::snprintf(f, sizeof f, "%s\\clip_%.3fMHz_%04d-%02d-%02d_%02d%02d%02d.wav", dir.c_str(), hz / 1e6, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::string err;
    if (A.wav.start(f, (uint32_t)Demodulator::kAudioRate, 1ull << 25, err)) D.clipOn = true;
    else D.log += "  (could not save a clip: " + err + ")\r\n";
}

void autoTick() {
    const ULONGLONG now = GetTickCount64();
    if (D.aidx >= D.cand.size()) {                       // (re)build the list: voice-width channels first
        D.cand.clear(); D.aidx = 0;
        for (auto& r : D.table.rows()) if (r.widthHz < 120e3 && (r.mode == 1 || r.mode == 2) && D.cand.size() < 30) D.cand.push_back(r);
        if (D.cand.empty()) { setAutoListen(false); D.status = "Nothing voice-sized was found to listen to."; discoverPush(); return; }
    }
    const DiscoveryRow& r = D.cand[D.aidx];
    const long key = std::lround(r.hz / 1000.0);
    switch (D.ast) {
        case 0:
            if (A.cfg.mode != r.mode) setModeIndex(r.mode, false);
            listenTo(r.hz);
            D.t0 = now; D.ast = 1;
            { char b[160]; std::snprintf(b, sizeof b, "Auto-listen: checking %s (%s)...", formatFrequency(r.hz).c_str(), r.where.c_str()); D.status = b; }
            break;
        case 1:
            if (now - D.t0 >= 350) { D.t0 = now; D.mn = 1e9; D.mx = -1e9; D.ast = 2; }
            break;
        case 2: {
            const double lvl = A.dm.levelDb();
            D.mn = std::min(D.mn, lvl); D.mx = std::max(D.mx, lvl);
            if (now - D.t0 < 700) break;
            const bool first = !D.base.count(key);
            double b = first ? D.mn : std::min(D.base[key], D.mn);
            D.base[key] = b + 0.05;                      // the quiet level creeps up slowly so a changed environment is re-learned
            const bool jump = (D.mx - b > 6.0) || (D.mx - D.mn > 8.0);
            const bool steady = first && r.snrDb >= 15.0 && r.activeNow;   // a strong steady signal on the first visit
            if (jump || steady) {
                D.ast = 3; D.holdStart = now; D.quietSince = 0; D.minHold = steady && !jump ? 10000 : 4000;
                A.dm.setMute(A.cfg.mute);
                logActivity("Activity on " + formatFrequency(r.hz) + " (" + r.where + ")" + (steady && !jump ? " - a steady signal" : ""));
                startClip(r.hz);
                D.status = "Auto-listen: listening to " + formatFrequency(r.hz) + ". It moves on when the channel goes quiet.";
            } else { ++D.aidx; D.ast = 0; }
            break;
        }
        case 3: {
            const double lvl = A.dm.levelDb();
            const double b = D.base.count(key) ? D.base[key] : lvl;
            if (lvl > b + 3.0) D.quietSince = 0; else if (!D.quietSince) D.quietSince = now;
            const bool quiet = D.quietSince && now - D.quietSince > 3000 && now - D.holdStart >= D.minHold;
            if (quiet || now - D.holdStart > 90000) {
                char bb[96]; std::snprintf(bb, sizeof bb, "  ...ended after about %d s", (int)((now - D.holdStart) / 1000));
                D.log += std::string(bb) + "\r\n";
                stopClip();
                A.dm.setMute(true);
                ++D.aidx; D.ast = 0;
            }
            break;
        }
    }
}

void surveyBeginRange() {
    const SurveyRange& r = D.list[D.idx];
    BandScanner::Plan p;
    p.lo = r.lo; p.hi = r.hi; p.rate = (double)A.cfg.rate; p.fftSize = kFftSizes[A.cfg.fftIdx];
    p.chCount = 0; p.settleFrames = 10; p.collectFrames = 6;
    A.scan.begin(p);
    A.cfg.freqHz = A.scan.centre();
    A.dev->setCenterFrequency((uint32_t)std::llround(A.scan.centre()));
    A.proc.setTuning(A.scan.centre(), (double)A.cfg.rate);
    A.pipe.flush();
    A.view.clearHistory();
    A.scanProgressMs = GetTickCount64();
}

void stopSurvey(const std::string& why) {
    if (!D.sweeping) return;
    D.sweeping = false; A.scanning = false;
    autosaveDiscovery();
    A.proc.setLinesPerSecond(A.scanSavedLps);
    A.dm.setMute(A.cfg.mute);
    A.cfg.freqHz = A.scanSavedCentre; A.cfg.vfoHz = A.scanSavedVfo;
    applyTuning();
    setText(ID_FREQ, formatFrequency(A.cfg.vfoHz));
    D.status = why;
    discoverPush();
    updateInfo();
}

void startSurvey(bool quick) {
    if (!A.dev) return;
    if (D.sweeping) return;
    stopAdsb(true);
    if (A.scanning) finishScan(true, "cancelled.");
    if (A.dev->isPlayback()) { D.status = "A recording covers one slice of the band and cannot be swept. Choose the live receiver."; discoverPush(); return; }
    setAutoListen(false);
    stopRecording();
    D.list = surveyRanges(quick, (double)A.caps.minFreqHz, (double)A.caps.maxFreqHz);
    if (D.list.empty()) { D.status = "This receiver cannot tune the bands the sweep covers."; discoverPush(); return; }
    D.quick = quick; D.idx = 0; D.pass = 0;
    D.base.clear();
    A.scanSavedCentre = A.cfg.freqHz; A.scanSavedVfo = A.cfg.vfoHz; A.scanSavedLps = 30;
    D.sweeping = true; A.scanning = true;
    A.dm.setMute(true);
    A.proc.setLinesPerSecond(60);
    surveyBeginRange();
    D.status = quick ? "Quick sweep started..." : "Full sweep started: this takes several minutes.";
    discoverPush();
}

void surveyFeed(const std::vector<SpectrumFrame>& fr) {
    bool advanced = false;
    for (auto& f : fr) if (A.scan.addFrame(f)) { advanced = true; break; }
    const ULONGLONG now = GetTickCount64();
    if (advanced) A.scanProgressMs = now;
    if (A.scan.done()) {
        const ScanReport rep = A.scan.result();
        D.table.addSignals(findSignals(rep.spectrumDb, rep.spectrumLoHz, rep.spectrumBinHz));
        if (++D.idx >= D.list.size()) {
            D.table.endPass(); announceNew(); autosaveDiscovery(); D.idx = 0; ++D.pass;
            if (!D.quick) { stopSurvey("Full sweep finished: " + std::to_string(D.table.rows().size()) + " signal(s) found. Double-click one to listen."); return; }
        }
        surveyBeginRange();
        discoverPush();
    } else if (advanced) {
        A.cfg.freqHz = A.scan.centre();
        A.dev->setCenterFrequency((uint32_t)std::llround(A.scan.centre()));
        A.proc.setTuning(A.scan.centre(), (double)A.cfg.rate);
        A.pipe.flush();
    } else if (now - A.scanProgressMs > 4000) {
        stopSurvey("The receiver stopped delivering data.");
        return;
    }
    char b[200];
    const SurveyRange& r = D.list[std::min(D.idx, D.list.size() - 1)];
    std::snprintf(b, sizeof b, "Sweeping %s to %s (%s). Sweep %d, range %zu of %zu. %zu signal(s) so far.", formatFrequency(r.lo).c_str(), formatFrequency(r.hi).c_str(),
                  r.name, D.pass + 1, D.idx + 1, D.list.size(), D.table.rows().size());
    D.status = b;
}

void discoverListen(const DiscoveryRow& r) {
    setAutoListen(false);
    if (D.sweeping) stopSurvey("Stopped to listen.");
    tuneTo(r.hz);
    if (A.cfg.mode != r.mode) setModeIndex(r.mode, false);
    D.status = "Listening to " + formatFrequency(r.hz) + ". The main window shows it.";
    discoverPush();
}

void discoverFavourite(const DiscoveryRow& r) {
    for (auto& f : g_favs) if (std::fabs(f.hz - r.hz) < 5e3) { D.status = "That frequency is already a favourite."; discoverPush(); return; }
    if (g_favs.size() >= kMaxFavs) { D.status = "The favourites list is full (40)."; discoverPush(); return; }
    Fav f; f.hz = r.hz; f.mode = r.mode; f.name = formatFrequency(r.hz) + " " + r.where;
    for (char& c : f.name) if (c == '|' || (unsigned char)c < 32) c = ' ';
    g_favs.push_back(f); favouritesRefresh();
    D.status = "Saved " + formatFrequency(r.hz) + " as a favourite. Manage favourites under Tools, Favourite stations.";
    discoverPush();
}

std::string pickFile(bool save, const char* filter, const char* ext, const char* title, const std::string& initial) {
    char file[MAX_PATH] = {0};
    std::snprintf(file, sizeof file, "%s", initial.c_str());
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof ofn; ofn.hwndOwner = A.hwnd;
    ofn.lpstrFilter = filter; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrDefExt = ext; ofn.lpstrTitle = title;
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY);
    const BOOL ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    return ok ? std::string(file) : std::string();
}

void openDiscover() {
    static bool triedLoad = false;
    if (!discoverOpen()) { D.recClips = false; D.beepNew = false; }   // the window's tick boxes start cleared
    if (!triedLoad) {                                            // bring back the signals found last time
        triedLoad = true;
        if (D.table.size() == 0 && D.table.load(discoveryPath()))
            D.status = "Loaded " + std::to_string(D.table.size()) + " signal(s) from your last session. New sweeps add to them, and anything not seen before is marked NEW.";
    }
    DiscoverHost h;
    h.saveScan = [] {
        SYSTEMTIME t; GetLocalTime(&t); char n[64];
        std::snprintf(n, sizeof n, "scan_%04d-%02d-%02d", t.wYear, t.wMonth, t.wDay);
        const std::string f = pickFile(true, "OpenRadio Lab scan (*.tsv)\0*.tsv\0", "tsv", "Save this scan as...", n);
        if (f.empty()) return;
        D.status = D.table.save(f) ? "Saved " + std::to_string(D.table.size()) + " signal(s) to " + f : "That file could not be written.";
        discoverPush();
    };
    h.loadScan = [] {
        if (D.sweeping) { D.status = "Stop the sweep first, then open a scan."; discoverPush(); return; }
        setAutoListen(false);
        const std::string f = pickFile(false, "OpenRadio Lab scan (*.tsv)\0*.tsv\0All files\0*.*\0", "tsv", "Open a saved scan...", "");
        if (f.empty()) return;
        DiscoveryTable t;
        if (!t.load(f)) { D.status = "That file is not an OpenRadio Lab scan."; discoverPush(); return; }
        D.table = t; D.told.clear();
        D.status = "Opened " + std::to_string(D.table.size()) + " signal(s) from " + f + ". Run a sweep to see which are still there and what is new.";
        discoverPush();
    };
    h.exportCsv = [] {
        if (D.table.size() == 0) { D.status = "There is nothing to export yet. Run a sweep first."; discoverPush(); return; }
        const std::string f = pickFile(true, "CSV for a spreadsheet (*.csv)\0*.csv\0", "csv", "Export the list as...", "signals");
        if (f.empty()) return;
        const std::string csv = D.table.toCsv();
        std::FILE* fp = std::fopen(f.c_str(), "wb");
        const bool ok = fp && std::fwrite(csv.data(), 1, csv.size(), fp) == csv.size();
        if (fp) std::fclose(fp);
        D.status = ok ? "Exported " + std::to_string(D.table.size()) + " signal(s) to " + f : "That file could not be written.";
        discoverPush();
    };
    h.clearScan = [] {
        if (D.sweeping) { D.status = "Stop the sweep first."; discoverPush(); return; }
        setAutoListen(false);
        D.table.clear(); D.base.clear(); D.told.clear();
        DeleteFileA(discoveryPath().c_str());
        D.status = "The list is empty. Press Quick sweep to start again.";
        discoverPush();
    };
    h.recordClips = [](bool on) { D.recClips = on; };
    h.beepOnNew = [](bool on) { D.beepNew = on; };
    h.setNote = [](double hz, const std::string& note) {
        if (D.table.setNote(hz, note)) { autosaveDiscovery(); D.status = "Note saved."; } else D.status = "That signal is no longer in the list.";
        discoverPush();
    };
    h.start = [](bool q) { startSurvey(q); };
    h.stop = [] { stopSurvey("Sweep stopped. The signals found are listed below."); };
    h.listen = [](const DiscoveryRow& r) { discoverListen(r); };
    h.favourite = [](const DiscoveryRow& r) { discoverFavourite(r); };
    h.autoListen = [](bool on) { setAutoListen(on); };
    h.addToJournal = [] { openJournal(true); };
    showDiscover(A.hwnd, A.hi, A.font, h);
    discoverPush();
}

void openFavourites() {
    FavHost h;
    h.list = [] { std::vector<FavItem> v; for (auto& f : g_favs) { FavItem i; i.hz = f.hz; i.mode = f.mode; i.name = f.name; v.push_back(i); } return v; };
    h.listen = [](int i) {
        if (i < 0 || i >= (int)g_favs.size()) return;
        const Fav f = g_favs[(size_t)i];
        if (A.dev && A.dev->isPlayback()) { A.notice = "A recording has a fixed tuning. Choose the live receiver to tune to a favourite."; updateInfo(); return; }
        stopAdsb(true);
        tuneTo(f.hz);
        if (f.mode != A.cfg.mode) { setModeIndex(f.mode, true); updateInfo(); }
    };
    h.rename = [](int i, const std::string& n) {
        if (i < 0 || i >= (int)g_favs.size()) return;
        std::string t = n; for (char& c : t) if (c == '|' || (unsigned char)c < 32) c = ' ';
        g_favs[(size_t)i].name = t;
    };
    h.remove = [](int i) { if (i >= 0 && i < (int)g_favs.size()) g_favs.erase(g_favs.begin() + i); };
    h.move = [](int i, int d) {
        const int j = i + d;
        if (i < 0 || j < 0 || i >= (int)g_favs.size() || j >= (int)g_favs.size()) return;
        std::swap(g_favs[(size_t)i], g_favs[(size_t)j]);
    };
    h.addCurrent = [] { addFavourite(); favouritesRefresh(); };
    showFavourites(A.hwnd, A.hi, A.font, h);
}

// ------------------------------------------------------------------ Aircraft tracker (ADS-B, 1090 MHz)

struct AdsbState {
    AdsbDecoder dec;
    bool on = false;
    double savedFreq = 0, savedVfo = 0; uint32_t savedRate = 0; bool savedAuto = true;
} AD;


// restore = put the tuning, gain and sound back as they were before tracking began.
void stopAdsb(bool restore) {
    if (!AD.on) return;
    AD.on = false;
    A.pipe.setIqTap(nullptr);
    A.pipe.setAudioEnabled(true);
    if (restore && A.dev) {
        A.cfg.freqHz = AD.savedFreq; A.cfg.vfoHz = AD.savedVfo; A.cfg.rate = AD.savedRate; A.cfg.autoGain = AD.savedAuto;
        applyTuning();
        applyGain();
    }
}

bool startAdsb(std::string& why) {
    if (!A.dev) { why = "No receiver is running."; return false; }
    if (A.dev->isPlayback()) { why = "A recording cannot be retuned. Choose the live receiver to track aircraft."; return false; }
    if (A.caps.minFreqHz > 1090e6 || A.caps.maxFreqHz < 1090e6) { why = "This receiver cannot tune to 1090 MHz."; return false; }
    if (A.dev->isSimulated()) { why = "The simulator does not transmit aircraft reports, so there is nothing to track. Plug in the RTL-SDR and choose it as the receiver."; return false; }
    if (D.sweeping) stopSurvey("Sweep stopped for the aircraft tracker.");
    if (A.scanning) finishScan(true, "cancelled.");
    setAutoListen(false);
    stopRecording();
    AD.savedFreq = A.cfg.freqHz; AD.savedVfo = A.cfg.vfoHz; AD.savedRate = A.cfg.rate; AD.savedAuto = A.cfg.autoGain;
    A.cfg.freqHz = A.cfg.vfoHz = 1090e6;
    A.cfg.rate = 2048000;
    applyTuning();
    A.dev->setGainDb(40.0);                         // aircraft signals are short bursts; automatic gain does not suit them
    AD.dec.setRate((double)A.cfg.rate);
    AD.dec.reset();
    A.pipe.setAudioEnabled(false);
    A.pipe.setIqTap([](const float* iq, size_t n) { AD.dec.process(iq, n); });
    AD.on = true;
    return true;
}

void openAdsb() {
    AdsbHost h;
    h.start = [](std::string& why) { return startAdsb(why); };
    h.stop = [] { stopAdsb(true); };
    h.running = [] { return AD.on; };
    h.aircraft = [] { return AD.dec.aircraft(120.0); };
    h.stats = [] { return AD.dec.stats(); };
    h.clock = [] { return AD.dec.clock(); };
    h.note = [] { return std::string(); };
    h.saveHome = [](bool has, double la, double lo) {
        const std::string p = settingsPath(); char b[32];
        WritePrivateProfileStringA("adsb", "home", has ? "1" : "0", p.c_str());
        std::snprintf(b, sizeof b, "%.5f", la); WritePrivateProfileStringA("adsb", "lat", b, p.c_str());
        std::snprintf(b, sizeof b, "%.5f", lo); WritePrivateProfileStringA("adsb", "lon", b, p.c_str());
    };
    const std::string p = settingsPath(); char b[48];
    h.hasHome = GetPrivateProfileIntA("adsb", "home", 0, p.c_str()) != 0;
    GetPrivateProfileStringA("adsb", "lat", "0", b, sizeof b, p.c_str()); h.homeLat = std::atof(b);
    GetPrivateProfileStringA("adsb", "lon", "0", b, sizeof b, p.c_str()); h.homeLon = std::atof(b);
    showAdsbWindow(A.hwnd, A.hi, A.font, h);
}

// Called with every batch of spectrum frames while a scan runs.
void scanFeed(const std::vector<SpectrumFrame>& fr) {
    if (D.sweeping) { surveyFeed(fr); return; }
    bool advanced = false;
    for (auto& f : fr) if (A.scan.addFrame(f)) { advanced = true; break; }
    const ULONGLONG now = GetTickCount64();
    if (advanced || !fr.empty()) A.scanProgressMs = advanced ? now : A.scanProgressMs;
    if (A.scan.done()) { finishScan(false, ""); return; }
    if (advanced) {
        A.cfg.freqHz = A.scan.centre();
        A.dev->setCenterFrequency((uint32_t)std::llround(A.scan.centre()));
        A.proc.setTuning(A.scan.centre(), (double)A.cfg.rate);
        A.pipe.flush();
        char b[64]; std::snprintf(b, sizeof b, "Scanning the FM band... %d%%", (int)(A.scan.progress() * 100));
        A.scanText = b;
        updateInfo();
    } else if (now - A.scanProgressMs > 4000) {
        finishScan(true, "the receiver stopped delivering data.");
    }
}

// ------------------------------------------------------------------ report window

LRESULT CALLBACK reportProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            HWND ed = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                                      ES_READONLY | ES_AUTOVSCROLL, 0, 0, 10, 10, h, (HMENU)1, A.hi, nullptr);
            SendMessageA(ed, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), TRUE);
            HWND b = CreateWindowExA(0, "BUTTON", "Copy all", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, (HMENU)2, A.hi, nullptr);
            SendMessageA(b, WM_SETFONT, (WPARAM)A.font, TRUE);
            const std::string* txt = (const std::string*)((CREATESTRUCTA*)l)->lpCreateParams;
            SetWindowTextA(ed, txt->c_str());
            return 0;
        }
        case WM_SIZE: {
            RECT rc; GetClientRect(h, &rc);
            SetWindowPos(GetDlgItem(h, 1), nullptr, 0, 0, rc.right, rc.bottom - S(36), SWP_NOZORDER);
            SetWindowPos(GetDlgItem(h, 2), nullptr, S(8), rc.bottom - S(30), S(90), S(24), SWP_NOZORDER);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(w) == 2) {
                SendMessageA(GetDlgItem(h, 1), EM_SETSEL, 0, -1);
                SendMessageA(GetDlgItem(h, 1), WM_COPY, 0, 0);
            }
            return 0;
        case WM_CLOSE: DestroyWindow(h); return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

void showReport() {
    static std::string text;
    text = buildDeviceReport(A.usb, A.rtl);
    CreateWindowExA(0, kReportClass, "OpenRadio Lab - Device report", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                    CW_USEDEFAULT, CW_USEDEFAULT, S(760), S(520), A.hwnd, nullptr, A.hi, &text);
}

// Hover help for the controls, so nothing needs a manual to understand.
void addTooltips() {
    HWND tt = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, nullptr, WS_POPUP | TTS_ALWAYSTIP, 0, 0, 0, 0, A.hwnd, nullptr, A.hi, nullptr);
    if (!tt) return;
    SendMessageA(tt, TTM_SETMAXTIPWIDTH, 0, S(320));
    SendMessageA(tt, TTM_SETDELAYTIME, TTDT_INITIAL, 400);
    SendMessageA(tt, TTM_SETDELAYTIME, TTDT_AUTOPOP, 12000);
    struct T { int id; const char* text; };
    static const T tips[] = {
        {ID_DEVICE, "Which radio to use. 'Simulator' is a pretend radio so you can learn without any hardware."},
        {ID_RESCAN, "Look for plugged-in radios again."},
        {ID_REPORT, "A plain-language report of what was found and what is missing. Handy when something does not work."},
        {ID_PAUSE, "Freeze the picture so you can study it. The radio keeps running."},
        {ID_HELP, "Open the getting-started manual."},
        {ID_LEARN, "Short lessons and a dictionary of radio words."},
        {ID_REC, "Record the raw radio signal to a file you can replay later, even without the radio."},
        {ID_OPENREC, "Play back a recording made earlier."},
        {ID_FREQ, "The frequency you are listening to. Type a number in MHz (for example 98.1) and press Go."},
        {ID_GO, "Tune to the frequency typed in the box."},
        {ID_DOWN, "Step down by the amount chosen in the Step list."},
        {ID_UP, "Step up by the amount chosen in the Step list."},
        {ID_STEP, "How far the up and down buttons move. Use the small steps (100 Hz to 5 kHz) for fine tuning voice and Morse."},
        {ID_PRESET, "Jump straight to a popular place on the dial."},
        {ID_SCAN, "Sweep the FM broadcast band and list the stations that can be heard."},
        {ID_STATIONS, "Stations found by the scan. Pick one to listen."},
        {ID_LISTEN, "How the sound is carried. FM for broadcast radio, AM for aircraft, USB/LSB for amateur voice, CW for Morse. The program picks for you unless Advanced is on."},
        {ID_VOL, "Loudness of what you hear. Does not change the picture."},
        {ID_MUTE, "Silence the sound."},
        {ID_STEREO, "Play FM stereo when the station sends it. Weak stations sound better in mono."},
        {ID_WAV, "Save the sound you hear as a WAV audio file."},
        {ID_BW, "How wide a slice of the radio signal you listen to. Narrower cuts noise but can cut the voice."},
        {ID_SQL, "Silences the sound when the signal is weak. Raise it until the hiss stops between transmissions."},
        {ID_AUTOGAIN, "Let the radio choose its sensitivity. Leave on unless the picture looks crowded or empty."},
        {ID_GAIN, "Radio sensitivity. Too low hides weak stations; too high makes strong ones create ghosts."},
    };
    for (const auto& t : tips) {
        HWND c = GetDlgItem(A.hwnd, t.id);
        if (!c) continue;
        TOOLINFOA ti{}; ti.cbSize = sizeof ti; ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS; ti.hwnd = A.hwnd; ti.uId = (UINT_PTR)c; ti.lpszText = (LPSTR)t.text;
        SendMessageA(tt, TTM_ADDTOOLA, 0, (LPARAM)&ti);
    }
}

void showHelp() {
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir = exe;
    dir = dir.substr(0, dir.find_last_of("\\/") + 1);
    const std::string manual = dir + "docs\\getting-started.html";
    if (GetFileAttributesA(manual.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxA(A.hwnd, "The manual was not found.\n\nIt should be at:\n  docs\\getting-started.html\nnext to OpenRadioLab.exe.",
                    kAppName, MB_OK | MB_ICONINFORMATION);
        return;
    }
    ShellExecuteA(A.hwnd, "open", manual.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ------------------------------------------------------------------ layout & mode

void setMode(bool advanced) {
    A.cfg.advanced = advanced;
    setText(ID_MODE, advanced ? "Mode: Advanced" : "Mode: Beginner");
    applyGain();
    applyListen();                 // beginner mode ignores the squelch
    layout();
    updateInfo();
}

void layout() {
    if (!A.hwnd) return;
    RECT rc; GetClientRect(A.hwnd, &rc);
    const int W = rc.right, Hh = rc.bottom;
    const int m = S(8), rowH = S(24), gap = S(8);
    auto place = [&](int id, int x, int y, int w, int h) {
        SetWindowPos(H(id), nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    auto show = [&](int id, bool v) { ShowWindow(H(id), v ? SW_SHOW : SW_HIDE); };

    int y = m, x = m;
    // Row 1: device
    place(ID_L_DEVICE, x, y + S(4), S(50), rowH); x += S(52);
    place(ID_DEVICE, x, y, S(330), S(240)); x += S(330) + gap;
    place(ID_RESCAN, x, y, S(70), rowH); x += S(70) + gap;
    place(ID_REPORT, x, y, S(110), rowH); x += S(110) + gap * 3;
    place(ID_MODE, x, y, S(130), rowH); x += S(130) + gap;
    place(ID_PAUSE, x, y, S(80), rowH); x += S(80) + gap;
    place(ID_HELP, x, y, S(60), rowH); x += S(60) + gap;
    place(ID_LEARN, x, y, S(70), rowH); x += S(70) + gap * 3;
    place(ID_REC, x, y, S(90), rowH); x += S(90) + gap;
    place(ID_OPENREC, x, y, S(140), rowH);
    y += rowH + gap;

    // Row 2: frequency
    x = m;
    place(ID_L_FREQ, x, y + S(4), S(70), rowH); x += S(72);
    place(ID_FREQ, x, y, S(130), rowH); x += S(130) + S(4);
    place(ID_GO, x, y, S(40), rowH); x += S(40) + gap;
    place(ID_DOWN, x, y, S(30), rowH); x += S(30) + S(2);
    place(ID_UP, x, y, S(30), rowH); x += S(30) + gap;
    place(ID_L_STEP, x, y + S(4), S(36), rowH); x += S(38);
    place(ID_STEP, x, y, S(90), S(200)); x += S(90) + gap * 2;
    place(ID_PRESET, x, y, S(250), S(260)); x += S(250) + gap;
    place(ID_SCAN, x, y, S(80), rowH); x += S(80) + gap;
    place(ID_STATIONS, x, y, S(210), S(300));
    y += rowH + gap;

    // Row 3: listening
    const bool adv = A.cfg.advanced;
    x = m;
    place(ID_L_LISTEN, x, y + S(4), S(64), rowH); x += S(66);
    place(ID_LISTEN, x, y, S(250), S(200)); x += S(250) + gap * 2;
    place(ID_L_VOL, x, y + S(4), S(52), rowH); x += S(54);
    place(ID_VOL, x, y, S(120), rowH); x += S(120) + gap;
    place(ID_MUTE, x, y, S(64), rowH); x += S(64);
    place(ID_STEREO, x, y, S(70), rowH); x += S(70);
    place(ID_WAV, x, y, S(96), rowH); x += S(96) + gap * 2;
    const int advListen[] = {ID_L_BW, ID_BW, ID_L_BWV, ID_L_SQL, ID_SQL, ID_L_SQLV};
    for (int id : advListen) show(id, adv);
    if (adv) {
        place(ID_L_BW, x, y + S(4), S(62), rowH); x += S(64);
        place(ID_BW, x, y, S(110), rowH); x += S(110);
        place(ID_L_BWV, x, y + S(4), S(56), rowH); x += S(58) + gap;
        place(ID_L_SQL, x, y + S(4), S(62), rowH); x += S(64);
        place(ID_SQL, x, y, S(110), rowH); x += S(110);
        place(ID_L_SQLV, x, y + S(4), S(70), rowH);
    }
    y += rowH + gap;

    // Playback row (only while a recording is open)
    {
        const bool pb = player() != nullptr;
        const int pbIds[] = {ID_PB_PLAY, ID_PB_SEEK, ID_PB_TIME, ID_PB_LOOP, ID_PB_LIVE};
        for (int id : pbIds) show(id, pb);
        if (pb) {
            x = m;
            place(ID_PB_PLAY, x, y, S(90), rowH); x += S(90) + gap;
            place(ID_PB_SEEK, x, y, std::max(S(200), W - x - S(340) - m), rowH); x += std::max(S(200), W - x - S(340) - m) + gap;
            place(ID_PB_TIME, x, y + S(4), S(100), rowH); x += S(100) + gap;
            place(ID_PB_LOOP, x, y, S(80), rowH); x += S(80) + gap;
            place(ID_PB_LIVE, x, y, S(130), rowH);
            y += rowH + gap;
        }
    }

    const int advIds[] = {ID_L_RATE, ID_RATE, ID_L_FFT, ID_FFT, ID_L_WINDOW, ID_WINDOW, ID_L_AUDIO, ID_AUDIODEV,
                          ID_AUTOGAIN, ID_L_GAIN, ID_GAIN, ID_L_GAINV, ID_L_SMOOTH, ID_SMOOTH, ID_L_RANGE, ID_RANGE};
    for (int id : advIds) show(id, adv);

    if (adv) {
        x = m;
        place(ID_L_RATE, x, y + S(4), S(80), rowH); x += S(82);
        place(ID_RATE, x, y, S(110), S(200)); x += S(110) + gap * 2;
        place(ID_L_FFT, x, y + S(4), S(60), rowH); x += S(62);
        place(ID_FFT, x, y, S(80), S(200)); x += S(80) + gap * 2;
        place(ID_L_WINDOW, x, y + S(4), S(60), rowH); x += S(62);
        place(ID_WINDOW, x, y, S(140), S(200)); x += S(140) + gap * 2;
        place(ID_L_AUDIO, x, y + S(4), S(70), rowH); x += S(72);
        place(ID_AUDIODEV, x, y, S(240), S(200));
        y += rowH + gap;

        x = m;
        place(ID_AUTOGAIN, x, y, S(100), rowH); x += S(104);
        place(ID_L_GAIN, x, y + S(4), S(36), rowH); x += S(38);
        place(ID_GAIN, x, y, S(190), rowH); x += S(190);
        place(ID_L_GAINV, x, y + S(4), S(56), rowH); x += S(60) + gap;
        place(ID_L_SMOOTH, x, y + S(4), S(76), rowH); x += S(78);
        place(ID_SMOOTH, x, y, S(130), rowH); x += S(130) + gap;
        place(ID_L_RANGE, x, y + S(4), S(70), rowH); x += S(72);
        place(ID_RANGE, x, y, S(130), rowH);
        y += rowH + gap;
    }

    RECT sb; SendMessageA(H(ID_STATUS), WM_SIZE, 0, 0);
    GetWindowRect(H(ID_STATUS), &sb);
    const int sbH = sb.bottom - sb.top;
    const int infoH = S(adv ? 124 : 150);
    const bool st = stationPanelWanted();
    const int stW = st ? std::max(S(300), (W - 2 * m) * 34 / 100) : 0;
    place(ID_INFO, m, Hh - sbH - infoH - m, W - 2 * m - (st ? stW + gap : 0), infoH);
    ShowWindow(H(ID_STATION), st ? SW_SHOW : SW_HIDE);
    if (st) place(ID_STATION, W - m - stW, Hh - sbH - infoH - m, stW, infoH);
    place(ID_VIEW, m, y, W - 2 * m, std::max(S(120), Hh - sbH - infoH - m * 2 - y));
}

void updateStatus() {
    char b[160];
    const ULONGLONG now = GetTickCount64();
    if (now - A.lastStatusMs >= 1000) {
        const double dt = (now - A.lastStatusMs) / 1000.0;
        const uint64_t s = A.samples.load();
        A.msps = (double)(s - A.lastSamples) / dt / 1e6;
        A.fps = A.framesSince / dt;
        A.lastSamples = s; A.framesSince = 0; A.lastStatusMs = now;
    }
    HWND sb = H(ID_STATUS);
    SendMessageA(sb, SB_SETTEXTA, 0, (LPARAM)(A.dev ? A.dev->name().c_str() : "no device"));
    {
        const bool st = A.cfg.mode == 0 && A.cfg.stereo && A.dm.stereoLocked();
        std::string t = "Listening " + formatFrequency(A.cfg.vfoHz) + " (" + (st ? "WFM stereo" : kModeShort[A.cfg.mode]) + "), centre " +
                        formatFrequency(A.cfg.freqHz);
        SendMessageA(sb, SB_SETTEXTA, 1, (LPARAM)t.c_str());
    }
    const unsigned long long drops = (unsigned long long)A.pipe.droppedBlocks();
    if (drops) std::snprintf(b, sizeof b, "%.2f MS/s in, %.0f lines/s, FFT %d, DROPPED %llu", A.msps, A.fps, kFftSizes[A.cfg.fftIdx], drops);
    else       std::snprintf(b, sizeof b, "%.2f MS/s in, %.0f lines/s, FFT %d", A.msps, A.fps, kFftSizes[A.cfg.fftIdx]);
    SendMessageA(sb, SB_SETTEXTA, 2, (LPARAM)b);
    {
        std::string rec = A.rec.active() ? "REC " + megabytes(A.rec.bytes()) + " | " : "";
        if (A.wav.active()) { char wt[24]; const int ws = (int)A.wav.seconds(); std::snprintf(wt, sizeof wt, "WAV %d:%02d | ", ws / 60, ws % 60); rec += wt; }
        std::snprintf(b, sizeof b, "%sSignal %.0f dBFS%s | audio %s", rec.c_str(), A.dm.levelDb(), A.dm.squelchOpen() ? "" : " (squelched)",
                      A.audio.isOpen() ? (A.cfg.mute ? "muted" : "on") : "OFF");
    }
    SendMessageA(sb, SB_SETTEXTA, 3, (LPARAM)b);
    SendMessageA(sb, SB_SETTEXTA, 4, (LPARAM)(A.dev && A.dev->isPlayback() ? (A.dev->isSimulated() ? "PLAYBACK (simulated)" : "PLAYBACK") : (A.dev && A.dev->isSimulated() ? "SIMULATED DATA" : "Live")));
}

// Smoothed copy of the latest spectrum, so a snapshot is not one noisy line.
struct SpecAvg { std::vector<float> db; double centre = 0, rate = 0; int n = 0; } g_spec;
void updateSpecAvg(const std::vector<SpectrumFrame>& fr) {
    for (const auto& f : fr) {
        if (f.db.size() != g_spec.db.size() || f.centerHz != g_spec.centre || f.sampleRate != g_spec.rate) {
            g_spec.db = f.db; g_spec.centre = f.centerHz; g_spec.rate = f.sampleRate; g_spec.n = 1; continue;
        }
        for (size_t i = 0; i < f.db.size(); ++i) g_spec.db[i] += 0.2f * (f.db[i] - g_spec.db[i]);
        ++g_spec.n;
    }
}
void onTimer() {
    std::vector<SpectrumFrame> fr;
    A.proc.drain(fr);
    A.framesSince += (int)fr.size();
    A.view.addFrames(fr);
    updateSpecAvg(fr);
    morseSetCwMode(A.cfg.mode == 5);
    if (AD.on && (std::fabs(A.cfg.freqHz - 1090e6) > 1.0 || !A.dev || !A.dev->isRunning())) stopAdsb(false);   // the user retuned: stop tracking, keep their tuning
    if (D.autoOn && !D.sweeping && A.dev && A.dev->isRunning() && !A.dev->isPlayback()) autoTick();
    if (discoverOpen()) { static ULONGLONG lastD = 0; const ULONGLONG t = GetTickCount64(); if (t - lastD >= 500) { lastD = t; discoverPush(); } }
    {   // station data changes slowly; refresh its card about once a second, and re-lay-out when the card appears or goes
        static ULONGLONG lastRdsMs = 0; static bool shown = false;
        const ULONGLONG nowMs = GetTickCount64();
        if (stationPanelWanted() != shown) { shown = stationPanelWanted(); layout(); updateStation(); }
        else if (nowMs - lastRdsMs >= 1000) { lastRdsMs = nowMs; updateStation(); }
    }
    if (A.scanning) scanFeed(fr);
    if (A.recWas && !A.rec.active()) { stopRecording(true); updateInfo(); }
    if (A.wavWas && !A.wav.active()) { stopAudioCapture(true); updateInfo(); }
    updatePlaybackBar();
    if (signalLabOpen()) {
        static ULONGLONG lastLab = 0;
        const ULONGLONG nowMs = GetTickCount64();
        if (nowMs - lastLab >= 100) {
            lastLab = nowMs;
            IqFileDevice* pl = player();
            if (!A.dev || !A.dev->isRunning()) signalLabSetStatus("no receiver is running");
            else if (A.scanning) signalLabSetStatus("the FM scan is using the receiver");
            else if (pl && pl->paused()) signalLabSetStatus("the recording playback is paused");
            else signalLabPush(A.dm.levelDb(), A.cfg.vfoHz, kModeShort[A.cfg.mode], 1e9);
        }
    }
    updateStatus();

    if (A.dev && !A.dev->isRunning()) {
        if (D.sweeping) { D.sweeping = false; D.status = "The device stopped delivering data."; }
        if (D.autoOn) { D.autoOn = false; }
        if (A.scanning) { A.scanning = false; setText(ID_SCAN, "Scan FM"); A.proc.setLinesPerSecond(A.scanSavedLps); A.dm.setMute(A.cfg.mute); }
        A.notice = "The device stopped delivering data (unplugged?). Switched to the simulated SDR.";
        stopDevice();
        rescan(false);
        if (!A.dev) startEntry((int)A.entries.size() - 1, false);
        else if (!A.dev->isSimulated()) {
            for (size_t i = 0; i < A.entries.size(); ++i) if (A.entries[i].sim) startEntry((int)i, false);
        }
    }
}

// ------------------------------------------------------------------ window procedure

WNDPROC g_oldEdit = nullptr;
LRESULT CALLBACK editProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CHAR && w == VK_RETURN) {
        PostMessageA(GetParent(h), WM_COMMAND, MAKEWPARAM(ID_GO, BN_CLICKED), 0);
        return 0;
    }
    return CallWindowProcA(g_oldEdit, h, m, w, l);
}

void openAudio() {
    std::string err;
    A.audioNotice.clear();
    if (!A.audio.open(A.cfg.audioDev, err)) A.audioNotice = err + " (the display still works; check Audio out in Advanced mode)";
}

const size_t kNumPresets = sizeof(kPresets) / sizeof(kPresets[0]);
void fillPresets() {
    SendMessageA(H(ID_PRESET), CB_RESETCONTENT, 0, 0);
    for (auto& p : kPresets) cbAdd(ID_PRESET, p.label);
    cbSel(ID_PRESET, 0);
}
// Name a new favourite after what the station says it is, else by its frequency.
void addFavourite() {
    for (auto& f : g_favs) if (std::fabs(f.hz - A.cfg.vfoHz) < 5e3) { A.notice = "That frequency is already in your favourites."; updateInfo(); return; }
    if (g_favs.size() >= kMaxFavs) { A.notice = "The favourites list is full (40). Remove one first."; updateInfo(); return; }
    Fav f; f.hz = A.cfg.vfoHz; f.mode = A.cfg.mode; f.name = formatFrequency(f.hz);
    if (A.cfg.mode == 0) {
        std::string ps = A.dm.rdsInfo().ps;
        while (!ps.empty() && ps.back() == ' ') ps.pop_back();
        if (!ps.empty()) f.name = ps;
    }
    for (char& c : f.name) if (c == '|' || (unsigned char)c < 32) c = ' ';
    g_favs.push_back(f);
    favouritesRefresh();
    A.notice = "Saved " + formatFrequency(f.hz) + " as a favourite ('" + f.name + "'). Manage your favourites under Tools, Favourite stations.";
    updateInfo();
}

void createControls() {
    NONCLIENTMETRICSA ncm{};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    A.font = CreateFontIndirectA(&ncm.lfMessageFont);

    mk("STATIC", "Device:", 0, ID_L_DEVICE);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_DEVICE);
    mk("BUTTON", "Rescan", BS_PUSHBUTTON | WS_TABSTOP, ID_RESCAN);
    mk("BUTTON", "Device report...", BS_PUSHBUTTON | WS_TABSTOP, ID_REPORT);
    mk("BUTTON", "Mode: Beginner", BS_PUSHBUTTON | WS_TABSTOP, ID_MODE);
    mk("BUTTON", "Pause", BS_PUSHBUTTON | WS_TABSTOP, ID_PAUSE);
    mk("BUTTON", "Help", BS_PUSHBUTTON | WS_TABSTOP, ID_HELP);
    mk("BUTTON", "Learn", BS_PUSHBUTTON | WS_TABSTOP, ID_LEARN);
    mk("BUTTON", "Record", BS_PUSHBUTTON | WS_TABSTOP, ID_REC);
    mk("BUTTON", "Open recording...", BS_PUSHBUTTON | WS_TABSTOP, ID_OPENREC);

    mk("BUTTON", "Pause", BS_PUSHBUTTON | WS_TABSTOP, ID_PB_PLAY);
    mk(TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, ID_PB_SEEK);
    mk("STATIC", "0:00 / 0:00", 0, ID_PB_TIME);
    mk("BUTTON", "Repeat", BS_AUTOCHECKBOX | WS_TABSTOP, ID_PB_LOOP);
    mk("BUTTON", "Back to live radio", BS_PUSHBUTTON | WS_TABSTOP, ID_PB_LIVE);
    tbRange(ID_PB_SEEK, 0, 1000);

    mk("STATIC", "Frequency:", 0, ID_L_FREQ);
    mk("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, ID_FREQ, WS_EX_CLIENTEDGE);
    g_oldEdit = (WNDPROC)SetWindowLongPtrA(H(ID_FREQ), GWLP_WNDPROC, (LONG_PTR)editProc);
    mk("BUTTON", "Go", BS_PUSHBUTTON | WS_TABSTOP, ID_GO);
    mk("BUTTON", "-", BS_PUSHBUTTON | WS_TABSTOP, ID_DOWN);
    mk("BUTTON", "+", BS_PUSHBUTTON | WS_TABSTOP, ID_UP);
    mk("STATIC", "Step:", 0, ID_L_STEP);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_STEP);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_PRESET);
    SendMessageA(H(ID_PRESET), CB_SETDROPPEDWIDTH, S(320), 0);
    mk("BUTTON", "Scan FM", BS_PUSHBUTTON | WS_TABSTOP, ID_SCAN);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_STATIONS);

    mk("STATIC", "Listen as:", 0, ID_L_LISTEN);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_LISTEN);
    mk("STATIC", "Volume:", 0, ID_L_VOL);
    mk(TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, ID_VOL);
    mk("BUTTON", "Mute", BS_AUTOCHECKBOX | WS_TABSTOP, ID_MUTE);
    mk("BUTTON", "Stereo", BS_AUTOCHECKBOX | WS_TABSTOP, ID_STEREO);
    mk("BUTTON", "Save audio", BS_PUSHBUTTON | WS_TABSTOP, ID_WAV);
    mk("STATIC", "Bandwidth:", 0, ID_L_BW);
    mk(TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, ID_BW);
    mk("STATIC", "", 0, ID_L_BWV);
    mk("STATIC", "Squelch:", 0, ID_L_SQL);
    mk(TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, ID_SQL);
    mk("STATIC", "", 0, ID_L_SQLV);

    mk("STATIC", "Sample rate:", 0, ID_L_RATE);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_RATE);
    mk("STATIC", "FFT size:", 0, ID_L_FFT);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_FFT);
    mk("STATIC", "Window:", 0, ID_L_WINDOW);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_WINDOW);
    mk("STATIC", "Audio out:", 0, ID_L_AUDIO);
    mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_AUDIODEV);

    mk("BUTTON", "Auto gain", BS_AUTOCHECKBOX | WS_TABSTOP, ID_AUTOGAIN);
    mk("STATIC", "Gain:", 0, ID_L_GAIN);
    mk(TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, ID_GAIN);
    mk("STATIC", "", 0, ID_L_GAINV);
    mk("STATIC", "Smoothing:", 0, ID_L_SMOOTH);
    mk(TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, ID_SMOOTH);
    mk("STATIC", "Contrast:", 0, ID_L_RANGE);
    mk(TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, ID_RANGE);

    mk("EDIT", "", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, ID_INFO, WS_EX_CLIENTEDGE);
    mk("EDIT", "", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, ID_STATION, WS_EX_CLIENTEDGE);
    mk(STATUSCLASSNAMEA, "", SBARS_SIZEGRIP, ID_STATUS);
    int parts[5] = {S(200), S(540), S(780), S(1090), -1};
    SendMessageA(H(ID_STATUS), SB_SETPARTS, 5, (LPARAM)parts);

    A.view.create(A.hwnd, A.hi, ID_VIEW);

    fillPresets();
    fillStations();
    for (auto n : kStepNames) cbAdd(ID_STEP, n);
    cbSel(ID_STEP, A.cfg.stepIdx);
    for (int n : kFftSizes) cbAdd(ID_FFT, std::to_string(n));
    cbSel(ID_FFT, A.cfg.fftIdx);
    for (auto w : kWindows) cbAdd(ID_WINDOW, windowName(w));
    cbSel(ID_WINDOW, A.cfg.windowIdx);
    for (auto n : kModeNames) cbAdd(ID_LISTEN, n);
    cbSel(ID_LISTEN, A.cfg.mode);
    {
        const auto devs = AudioOutput::listDevices();
        for (auto& d : devs) cbAdd(ID_AUDIODEV, d);
        if (A.cfg.audioDev >= (int)devs.size()) A.cfg.audioDev = 0;
        cbSel(ID_AUDIODEV, A.cfg.audioDev);
    }

    tbRange(ID_SMOOTH, 0, 90); tbSet(ID_SMOOTH, A.cfg.smoothPct);
    tbRange(ID_RANGE, 30, 100); tbSet(ID_RANGE, A.cfg.rangeDb);
    tbRange(ID_VOL, 0, 100); tbSet(ID_VOL, A.cfg.volume);
    tbRange(ID_SQL, 0, 81); tbSet(ID_SQL, A.cfg.squelchPos);
    SendMessageA(H(ID_AUTOGAIN), BM_SETCHECK, A.cfg.autoGain ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(H(ID_MUTE), BM_SETCHECK, A.cfg.mute ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(H(ID_STEREO), BM_SETCHECK, A.cfg.stereo ? BST_CHECKED : BST_UNCHECKED, 0);
    A.view.setSmoothing(A.cfg.smoothPct / 100.0);
    A.view.setSpanDb(A.cfg.rangeDb);
    syncBwControl();
    setText(ID_MODE, A.cfg.advanced ? "Mode: Advanced" : "Mode: Beginner");
    char b[32]; std::snprintf(b, sizeof b, "%.0f dB", A.cfg.gainDb);
    setText(ID_L_GAINV, b);
    updateListenLabels();
}

IqFileDevice* player() { return A.dev && A.dev->isPlayback() ? dynamic_cast<IqFileDevice*>(A.dev.get()) : nullptr; }
bool g_seeking = false;

std::string clockText(double s) {
    const int t = (int)std::floor(s + 0.5);
    char b[24]; std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
    return b;
}

void updatePlaybackBar(bool force) {
    IqFileDevice* p = player();
    if (!p || !A.hwnd) return;
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (!force && now - last < 100) return;
    last = now;
    const double dur = std::max(0.001, p->durationSeconds()), pos = std::min(dur, p->positionSeconds());
    if (!g_seeking) tbSet(ID_PB_SEEK, (int)std::lround(pos / dur * 1000.0));
    setText(ID_PB_TIME, clockText(pos) + " / " + clockText(dur));
    setText(ID_PB_PLAY, p->paused() ? (p->atEnd() ? "Play again" : "Play") : "Pause");
    if (force) SendMessageA(H(ID_PB_LOOP), BM_SETCHECK, p->loop() ? BST_CHECKED : BST_UNCHECKED, 0);
}

void playbackSeek(double fraction) {
    IqFileDevice* p = player();
    if (!p) return;
    p->seekSeconds(fraction * p->durationSeconds());
    A.pipe.flush();                  // drop audio and spectrum queued from the old position
    A.proc.setTuning(A.cfg.freqHz, (double)A.cfg.rate);
}

void playbackPlayPause() {
    IqFileDevice* p = player();
    if (!p) return;
    if (p->paused()) {
        if (p->atEnd()) playbackSeek(0.0);
        p->setPaused(false);
    } else {
        p->setPaused(true);
    }
    updatePlaybackBar(true);
}

// Leave a recording and return to the live receiver (the dongle if one is plugged in, otherwise the simulator).
void backToLive() {
    int live = -1, sim = -1;
    for (size_t i = 0; i < A.entries.size(); ++i) {
        if (!A.entries[i].path.empty()) continue;
        if (A.entries[i].sim) { if (sim < 0) sim = (int)i; }
        else if (live < 0) live = (int)i;
    }
    const int idx = live >= 0 ? live : sim;
    if (idx < 0) return;
    A.notice.clear();
    startEntry(idx, true);
}

JournalEntry captureEntry() {
    JournalEntry e;
    SYSTEMTIME t; GetLocalTime(&t);
    char b[64]; std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    e.when = b;
    e.vfoHz = A.cfg.vfoHz; e.centreHz = A.cfg.freqHz; e.rateHz = A.cfg.rate;
    e.mode = kModeShort[A.cfg.mode];
    e.title = formatFrequency(A.cfg.vfoHz) + " " + e.mode;
    if (const Band* band = findBand(A.cfg.vfoHz)) e.band = band->name;
    if (A.dev) { e.device = A.dev->name(); e.simulated = A.dev->isSimulated(); }
    const bool autoG = A.cfg.autoGain || !A.cfg.advanced;
    if (A.dev && A.dev->isPlayback()) e.gain = "as recorded";
    else if (autoG) e.gain = "auto";
    else { std::snprintf(b, sizeof b, "%.0f dB", A.cfg.gainDb); e.gain = b; }
    e.levelDb = A.dm.levelDb();
    e.measurement = A.view.observation();
    e.notes = rdsSection(true);                       // the station's own name and text, if it sent any
    e.recording = (A.dev && A.dev->isPlayback()) ? A.filePath : A.lastRec;
    return e;
}

void openJournal(bool newEntry) {
    JournalHost host; host.capture = [] { return captureEntry(); };
    showJournal(A.hwnd, A.hi, A.font, host, newEntry);
}

void openSignalLab() {
    showSignalLab(A.hwnd, A.hi, A.font, [] { openJournal(true); },
                  [](std::vector<float>& db, double& c, double& r) {
                      if (g_spec.n < 5 || g_spec.db.empty()) return false;
                      db = g_spec.db; c = g_spec.centre; r = g_spec.rate; return true;
                  });
}

void toggleAudioCapture() {
    if (A.wav.active()) { stopAudioCapture(true); updateInfo(); return; }
    if (!A.audio.isOpen()) { A.notice = "There is no audio output to save from."; updateInfo(); return; }
    std::string dir = A.cfg.recDir;
    if (dir.empty() || GetFileAttributesA(dir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        char docs[MAX_PATH] = {0};
        dir = SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs) == S_OK ? std::string(docs) + "\\OpenRadioLab" : ".";
        CreateDirectoryA(dir.c_str(), nullptr);
        dir += "\\Recordings";
        CreateDirectoryA(dir.c_str(), nullptr);
    }
    SYSTEMTIME t; GetLocalTime(&t);
    char file[MAX_PATH];
    std::snprintf(file, sizeof file, "audio_%.3fMHz_%04d-%02d-%02d_%02d%02d%02d", A.cfg.vfoHz / 1e6, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof ofn; ofn.hwndOwner = A.hwnd;
    ofn.lpstrFilter = "WAV audio (*.wav)\0*.wav\0";
    ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrInitialDir = dir.c_str(); ofn.lpstrDefExt = "wav";
    ofn.lpstrTitle = "Save what you hear as an audio file...";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameA(&ofn)) return;
    {
        const std::string chosen = file;
        const size_t slash = chosen.find_last_of("\\/");
        if (slash != std::string::npos) A.cfg.recDir = chosen.substr(0, slash);
    }
    std::string err;
    if (!A.wav.start(file, (uint32_t)Demodulator::kAudioRate, 1ull << 30, err)) { A.notice = err; updateInfo(); return; }
    A.wavWas = true;
    SetWindowTextA(H(ID_WAV), "Stop audio");
    CheckMenuItem(GetMenu(A.hwnd), ID_M_WAV, MF_BYCOMMAND | MF_CHECKED);
    A.notice = "Saving the audio you hear to " + A.wav.path() + ". It keeps going while you retune. Press Stop audio when you are done (muting records silence).";
    updateInfo();
}

void toggleRecording() {
    if (A.rec.active()) { stopRecording(true); updateInfo(); return; }
    if (!A.dev) return;
    if (A.dev->isPlayback()) { A.notice = "This is already a recording. Choose the live receiver or the simulator to record."; updateInfo(); return; }
    if (A.scanning) return;
    // Ask where to save. The dialog starts in the last folder used (first time: Documents\\OpenRadioLab\\Recordings).
    std::string dir = A.cfg.recDir;
    if (dir.empty() || GetFileAttributesA(dir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        char docs[MAX_PATH] = {0};
        dir = SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs) == S_OK ? std::string(docs) + "\\OpenRadioLab" : ".";
        CreateDirectoryA(dir.c_str(), nullptr);
        dir += "\\Recordings";
        CreateDirectoryA(dir.c_str(), nullptr);
    }
    SYSTEMTIME t; GetLocalTime(&t);
    char file[MAX_PATH];
    std::snprintf(file, sizeof file, "%.3fMHz_%04d-%02d-%02d_%02d%02d%02d", A.cfg.freqHz / 1e6, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = A.hwnd;
    ofn.lpstrFilter = "OpenRadio Lab recordings (*.orliq)\0*.orliq\0";
    ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = dir.c_str();
    ofn.lpstrDefExt = "orliq";
    ofn.lpstrTitle = "Save the recording as...  (it records the whole band you see, not just the audio)";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameA(&ofn)) return;                         // cancelled: nothing is recorded
    {
        std::string chosen = file;
        const size_t slash = chosen.find_last_of("\\/");
        if (slash != std::string::npos) A.cfg.recDir = chosen.substr(0, slash);
    }
    IqHeader h; h.rate = A.cfg.rate; h.centreHz = (uint64_t)std::llround(A.cfg.freqHz); h.simulated = A.dev->isSimulated();
    std::string err;
    if (!A.rec.start(file, h, 1ull << 30, err)) { A.notice = err; updateInfo(); return; }   // 1 GiB limit (about 4 minutes at 2.048 MS/s)
    A.recWas = true;
    setText(ID_REC, "Stop rec");
    A.notice = "Recording the whole captured band to " + A.rec.path() + " (stops by itself at 1 GB, about 4 minutes). Changing the centre frequency also stops it. Press Stop rec when you are done.";
    updateInfo();
}

void openRecording() {
    char file[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = A.hwnd;
    ofn.lpstrFilter = "OpenRadio Lab recordings (*.orliq)\0*.orliq\0All files\0*.*\0";
    ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Open a recording";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&ofn)) return;
    const std::string previous = A.filePath;
    A.filePath = file;
    A.notice.clear();
    rescan(false);
    int idx = -1;
    for (size_t i = 0; i < A.entries.size(); ++i) if (A.entries[i].path == A.filePath) idx = (int)i;
    if (idx < 0 || !startEntry(idx, false)) {
        const std::string why = A.notice;
        A.filePath = previous;
        rescan(false);
        A.notice = why.empty() ? "That file could not be opened." : why;
        updateInfo();
    }
}

void onCommand(int id, int code) {
    switch (id) {
        case ID_DEVICE:
            if (code == CBN_SELCHANGE) { A.notice.clear(); startEntry(cbGet(ID_DEVICE), true); }
            break;
        case ID_RESCAN: A.notice.clear(); rescan(true); break;
        case ID_REPORT: showReport(); break;
        case ID_HELP: showHelp(); break;
        case ID_LEARN: showLearnWindow(A.hwnd, A.hi, A.font); break;
        case ID_REC: toggleRecording(); break;
        case ID_WAV: case ID_M_WAV: toggleAudioCapture(); break;
        case ID_PB_LIVE: case ID_M_LIVE: backToLive(); break;
        case ID_M_JOURNAL: openJournal(false); break;
        case ID_M_NEWENTRY: openJournal(true); break;
        case ID_M_LAB: openSignalLab(); break;
        case ID_M_ATLAS: showAtlasWindow(A.hwnd, A.hi, A.font); break;
        case ID_M_DISCOVER: openDiscover(); break;
        case ID_M_ADSB: openAdsb(); break;
        case ID_M_FAVADD: addFavourite(); break;
        case ID_M_FAVLIST: openFavourites(); break;
        case ID_M_MORSE: showMorseWindow(A.hwnd, A.hi, A.font); break;
        case ID_M_ANTENNA: showAntennaWorkshop(A.hwnd, A.hi, A.font, A.cfg.vfoHz); break;
        case ID_M_EXIT: SendMessageA(A.hwnd, WM_CLOSE, 0, 0); break;
        case ID_PB_PLAY: playbackPlayPause(); break;
        case ID_PB_LOOP:
            if (IqFileDevice* p = player()) {
                p->setLoop(SendMessageA(H(ID_PB_LOOP), BM_GETCHECK, 0, 0) == BST_CHECKED);
                if (p->loop() && p->atEnd()) playbackPlayPause();
            }
            break;
        case ID_OPENREC: openRecording(); break;
        case ID_MODE: setMode(!A.cfg.advanced); break;
        case ID_PAUSE:
            A.paused = !A.paused;
            A.view.setPaused(A.paused);
            setText(ID_PAUSE, A.paused ? "Resume" : "Pause");
            break;
        case ID_GO: {
            char buf[128]; GetWindowTextA(H(ID_FREQ), buf, sizeof buf);
            double hz;
            if (parseFrequencyHz(buf, hz)) tuneTo(hz);
            else { A.notice = std::string("Could not understand \"") + buf + "\" as a frequency. Try 100.5, 145 MHz or 433920 kHz."; updateInfo(); }
            break;
        }
        case ID_DOWN: if (kSteps[A.cfg.stepIdx] < 100e3) listenTo(A.cfg.vfoHz - kSteps[A.cfg.stepIdx]); else tuneTo(A.cfg.vfoHz - kSteps[A.cfg.stepIdx]); break;
        case ID_UP:   if (kSteps[A.cfg.stepIdx] < 100e3) listenTo(A.cfg.vfoHz + kSteps[A.cfg.stepIdx]); else tuneTo(A.cfg.vfoHz + kSteps[A.cfg.stepIdx]); break;
        case ID_SCAN: startScan(); break;
        case ID_STATIONS:
            if (code == CBN_SELCHANGE) {
                const int i = cbGet(ID_STATIONS);
                if (i > 0 && i <= (int)A.stations.size() && !A.scanning) { A.scanText.clear(); tuneTo(A.stations[i - 1].hz); }
                cbSel(ID_STATIONS, 0);
            }
            break;
        case ID_STEP: if (code == CBN_SELCHANGE) A.cfg.stepIdx = cbGet(ID_STEP); break;
        case ID_PRESET:
            if (code == CBN_SELCHANGE) {
                const int i = cbGet(ID_PRESET);
                if (i > 0 && (size_t)i < kNumPresets) tuneTo(kPresets[i].hz);
                else if ((size_t)i >= kNumPresets && (size_t)i - kNumPresets < g_favs.size()) {
                    const Fav f = g_favs[(size_t)i - kNumPresets];
                    tuneTo(f.hz);
                    if (f.mode != A.cfg.mode) { setModeIndex(f.mode, true); updateInfo(); }
                }
                cbSel(ID_PRESET, 0);
            }
            break;
        case ID_LISTEN:
            if (code == CBN_SELCHANGE) { setModeIndex(cbGet(ID_LISTEN), true); updateInfo(); }
            break;
        case ID_MUTE:
            A.cfg.mute = SendMessageA(H(ID_MUTE), BM_GETCHECK, 0, 0) == BST_CHECKED;
            applyListen();
            break;
        case ID_STEREO:
            A.cfg.stereo = SendMessageA(H(ID_STEREO), BM_GETCHECK, 0, 0) == BST_CHECKED;
            applyListen();
            break;
        case ID_RATE:
            if (code == CBN_SELCHANGE) {
                const int i = cbGet(ID_RATE);
                if (i >= 0 && i < (int)A.caps.sampleRates.size()) { A.cfg.rate = A.caps.sampleRates[i]; applyTuning(); }
            }
            break;
        case ID_FFT:
            if (code == CBN_SELCHANGE) { A.cfg.fftIdx = cbGet(ID_FFT); applyDsp(); }
            break;
        case ID_WINDOW:
            if (code == CBN_SELCHANGE) { A.cfg.windowIdx = cbGet(ID_WINDOW); applyDsp(); }
            break;
        case ID_AUDIODEV:
            if (code == CBN_SELCHANGE) { A.cfg.audioDev = cbGet(ID_AUDIODEV); openAudio(); updateInfo(); }
            break;
        case ID_AUTOGAIN:
            A.cfg.autoGain = SendMessageA(H(ID_AUTOGAIN), BM_GETCHECK, 0, 0) == BST_CHECKED;
            applyGain();
            break;
    }
}

LRESULT CALLBACK mainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            A.hwnd = h;
            HDC dc = GetDC(nullptr); A.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
            createControls();
            addTooltips();
            layout();
            return 0;
        }
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = (MINMAXINFO*)l;
            mm->ptMinTrackSize.x = S(1240);
            mm->ptMinTrackSize.y = S(640);
            return 0;
        }
        case WM_COMMAND: onCommand(LOWORD(w), HIWORD(w)); return 0;
        case WM_HSCROLL: {
            HWND src = (HWND)l;
            if (src == H(ID_GAIN)) {
                A.cfg.gainDb = tbGet(ID_GAIN);
                char b[32]; std::snprintf(b, sizeof b, "%.0f dB", A.cfg.gainDb);
                setText(ID_L_GAINV, b);
                applyGain();
            } else if (src == H(ID_SMOOTH)) {
                A.cfg.smoothPct = tbGet(ID_SMOOTH);
                A.view.setSmoothing(A.cfg.smoothPct / 100.0);
            } else if (src == H(ID_RANGE)) {
                A.cfg.rangeDb = tbGet(ID_RANGE);
                A.view.setSpanDb(A.cfg.rangeDb);
            } else if (src == H(ID_VOL)) {
                A.cfg.volume = tbGet(ID_VOL);
                applyListen();
            } else if (src == H(ID_SQL)) {
                A.cfg.squelchPos = tbGet(ID_SQL);
                applyListen();
            } else if (src == H(ID_PB_SEEK)) {
                const int code = LOWORD(w);
                if (code == TB_THUMBTRACK) {
                    g_seeking = true;
                    if (IqFileDevice* p = player()) setText(ID_PB_TIME, clockText(tbGet(ID_PB_SEEK) / 1000.0 * p->durationSeconds()) + " / " + clockText(p->durationSeconds()));
                } else if (code == TB_ENDTRACK) {
                    g_seeking = false;
                    playbackSeek(tbGet(ID_PB_SEEK) / 1000.0);
                }
            } else if (src == H(ID_BW)) {
                (A.cfg.mode == 1 ? A.cfg.nfmBwKhz : A.cfg.amBwKhz) = tbGet(ID_BW);
                applyListen();
            }
            return 0;
        }
        case WM_ORL_MARKER: updateInfo(); return 0;
        case WM_ORL_TRYIT:
            if (!A.scanning) {
                tuneTo((double)(unsigned long)l);
                if (A.cfg.advanced) setModeIndex((int)w, true);      // Beginner mode already picks the band's usual mode
                updateInfo();
            }
            return 0;
        case WM_ORL_RETUNE: listenTo((double)(uint32_t)l); return 0;
        case WM_TIMER:
            if (w == TIMER_UI) onTimer();
            else if (w == TIMER_RESCAN) { KillTimer(h, TIMER_RESCAN); rescan(true); }
            return 0;
        case WM_DEVICECHANGE:
            if (w == DBT_DEVNODES_CHANGED || w == DBT_DEVICEARRIVAL || w == DBT_DEVICEREMOVECOMPLETE)
                SetTimer(h, TIMER_RESCAN, 1500, nullptr);
            return TRUE;
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            KillTimer(h, TIMER_UI);
            stopAudioCapture(false);
            closeAdsbWindow();
            stopAdsb(true);
            autosaveDiscovery();
            saveSettings();
            stopDevice();          // device first, so nothing is still feeding the DSP thread
            A.pipe.stop();
            A.audio.close();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

} // namespace

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int show) {
    A.hi = hi;
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    SpectrumView::registerClass(hi);
    registerLearnClass(hi);
    registerLabClasses(hi);
    registerAtlasClass(hi);
    registerAntennaClass(hi);
    registerMorseClass(hi);
    registerDiscoverClass(hi);
    registerAdsbClass(hi);
    registerFavouritesClass(hi);
    loadSettings();

    WNDCLASSA wc{};
    wc.lpfnWndProc = mainProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kMainClass;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassA(&wc);

    WNDCLASSA rc{};
    rc.lpfnWndProc = reportProc;
    rc.hInstance = hi;
    rc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    rc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    rc.lpszClassName = kReportClass;
    RegisterClassA(&rc);

    HDC dc = GetDC(nullptr); A.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    HMENU menu = CreateMenu(), file = CreatePopupMenu(), tools = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuA(file, MF_STRING, ID_OPENREC, "Open a recording...");
    AppendMenuA(file, MF_STRING, ID_M_WAV, "Save what I hear as audio (WAV)...");
    AppendMenuA(file, MF_STRING, ID_M_LIVE, "Back to live radio");
    AppendMenuA(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(file, MF_STRING, ID_M_EXIT, "Exit");
    AppendMenuA(tools, MF_STRING, ID_M_DISCOVER, "Discover: find what is on the air around me...");
    AppendMenuA(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(tools, MF_STRING, ID_M_FAVADD, "Save this station as a favourite");
    AppendMenuA(tools, MF_STRING, ID_M_FAVLIST, "Favourite stations (manage)...");
    AppendMenuA(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(tools, MF_STRING, ID_M_JOURNAL, "Experiment Journal...");
    AppendMenuA(tools, MF_STRING, ID_M_NEWENTRY, "Add what I'm listening to into the journal");
    AppendMenuA(tools, MF_STRING, ID_M_LAB, "Signal Lab: watch a signal over time...");
    AppendMenuA(tools, MF_STRING, ID_M_ATLAS, "Frequency Atlas: what is where...");
    AppendMenuA(tools, MF_STRING, ID_M_ADSB, "Aircraft tracker (ADS-B, 1090 MHz)...");
    AppendMenuA(tools, MF_STRING, ID_M_MORSE, "Morse Decoder (CW text)...");
    AppendMenuA(tools, MF_STRING, ID_M_ANTENNA, "Antenna Workshop: lengths and build guides...");
    AppendMenuA(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(tools, MF_STRING, ID_REPORT, "Device report...");
    AppendMenuA(help, MF_STRING, ID_HELP, "Getting-started manual");
    AppendMenuA(help, MF_STRING, ID_LEARN, "Learning Center (lessons and dictionary)");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)file, "File");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)tools, "Tools");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)help, "Help");
    HWND w = CreateWindowExA(0, kMainClass, kAppName, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             S(1300), S(860), nullptr, menu, hi, nullptr);
    if (!w) return 1;
    ShowWindow(w, show);
    UpdateWindow(w);

    // Audio and the DSP thread come up before the first device so no samples are ever lost.
    openAudio();
    A.pipe.setAudioSink([](const float* a, size_t n, int ch) { A.audio.write(a, n, ch); morseFeed(a, n, ch); if (A.wav.active()) A.wav.write(a, n, ch); });
    A.pipe.setAudioEnabled(true);
    A.pipe.start();

    if (!A.cfg.welcomed) {
        A.cfg.welcomed = true;
        MessageBoxA(w,
            "Welcome to OpenRadio Lab!\n\n"
            "This is a receive-only radio for learning. It cannot transmit.\n\n"
            "- No radio plugged in? The Simulator works with no hardware.\n"
            "- Press Scan to find FM stations, or pick one from 'Quick jump to'.\n"
            "- Hover the mouse over any control to see what it does.\n"
            "- Learn (top right) has short lessons; Tools has the Frequency Atlas and Antenna Workshop.\n\n"
            "Simulated signals are always labelled as simulated.",
            "Welcome", MB_OK | MB_ICONINFORMATION);
    }
    rescan(true);                          // automatic detection + start
    SetTimer(w, TIMER_UI, 33, nullptr);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
