// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Custom-drawn spectrum analyser + waterfall (GDI, double buffered).
//
// Interaction:
//   hover          - shows the frequency/level under the cursor
//   click          - places a marker and measures the nearest signal (an
//                    OBSERVATION, never an identification)
//   double-click   - asks the parent to LISTEN to that signal (WM_ORL_RETUNE)
#pragma once

#include <windows.h>

#include <deque>
#include <string>
#include <vector>

#include "../dsp/SpectrumProcessor.h"

#define WM_ORL_MARKER (WM_APP + 1)   // marker moved / measurement updated
#define WM_ORL_RETUNE (WM_APP + 2)   // lParam = target frequency in Hz (fits in 32 bits)

namespace orl {

class SpectrumView {
public:
    static bool registerClass(HINSTANCE hi);
    HWND create(HWND parent, HINSTANCE hi, int id);
    HWND hwnd() const { return hwnd_; }
    ~SpectrumView();

    void addFrames(const std::vector<SpectrumFrame>& frames);
    void clearHistory();

    void setPaused(bool p);
    bool paused() const { return paused_; }
    void setBanner(const std::string& s) { banner_ = s; invalidate(); }
    // The frequency being listened to and the width of its channel filter, drawn over both graphs.
    void setListen(double hz, double bandwidthHz, bool visible) { listenHz_ = hz; listenBw_ = bandwidthHz; listenOn_ = visible; invalidate(); }
    void setSmoothing(double a) { smooth_ = a < 0 ? 0 : (a > 0.95 ? 0.95 : a); }
    void setSpanDb(double s)    { spanDb_ = s < 20 ? 20 : s; rebuildLut(); invalidate(); }

    double noiseDb() const { return haveNoise_ ? noiseDb_ : 1e9; }
    const std::string& observation() const { return obs_; }
    bool hasMarker() const { return hasMarker_; }
    double markerHz() const { return markerHz_; }
    double suggestedTuneHz() const { return signalFound_ ? signalHz_ : markerHz_; }

    void invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);

    int S(int v) const { return MulDiv(v, dpi_, 96); }
    void computeLayout();
    void ensureBuffers();
    void paint(HDC dc);
    void pushRow(const std::vector<float>& db);
    void rebuildLut();
    void binRange(int x, int& lo, int& hi) const;
    float maxInRange(const std::vector<float>& v, int lo, int hi) const;
    double pixelToHz(int x) const;
    int hzToPixel(double hz) const;
    double floorDb() const { return noiseDb_ - 8.0; }
    void placeMarker(int x);
    void analyse();

    HWND hwnd_ = nullptr;
    int dpi_ = 96;

    // layout (client pixels)
    int cw_ = 0, ch_ = 0, L_ = 0, R_ = 0, T_ = 0, axisH_ = 0, specH_ = 0, wfTop_ = 0, wfH_ = 0, plotW_ = 0;

    // data
    std::vector<float> disp_, raw_;
    std::deque<std::vector<float>> hist_;
    std::deque<ULONGLONG> histT_;
    double center_ = 0, rate_ = 0;
    double noiseDb_ = -65.0;
    bool haveNoise_ = false;
    double smooth_ = 0.6, spanDb_ = 60.0;
    bool paused_ = false;
    std::string banner_;
    double listenHz_ = 0, listenBw_ = 0;
    bool listenOn_ = false;

    // marker / measurement
    bool hasMarker_ = false, signalFound_ = false;
    double markerHz_ = 0, signalHz_ = 0;
    std::string obs_;
    std::vector<std::string> readout_;
    int hoverX_ = -1;
    bool tracking_ = false;

    // GDI resources
    HDC wfDC_ = nullptr;     HBITMAP wfBmp_ = nullptr, wfOld_ = nullptr;  uint32_t* wfBits_ = nullptr;
    int wfW_ = 0, wfHcur_ = 0;
    HDC bbDC_ = nullptr;     HBITMAP bbBmp_ = nullptr, bbOld_ = nullptr;  int bbW_ = 0, bbH_ = 0;
    HFONT font_ = nullptr, fontSmall_ = nullptr;
    uint32_t lut_[256];
};

} // namespace orl
