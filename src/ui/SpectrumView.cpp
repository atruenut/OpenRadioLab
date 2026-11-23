// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "SpectrumView.h"
#include "../Hints.h"

#include "../Util.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace orl {

namespace {

const char* kClassName = "ORLSpectrumView";

const COLORREF kBg      = RGB(12, 14, 20);
const COLORREF kPlotBg  = RGB(7, 9, 13);
const COLORREF kGrid    = RGB(34, 40, 52);
const COLORREF kText    = RGB(170, 180, 195);
const COLORREF kTrace   = RGB(90, 220, 160);
const COLORREF kFill    = RGB(14, 56, 50);
const COLORREF kMarker  = RGB(255, 205, 60);
const COLORREF kHover   = RGB(150, 160, 175);
const COLORREF kAmber   = RGB(255, 170, 40);

inline uint32_t rgb32(int r, int g, int b) { return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b; }

} // namespace

bool SpectrumView::registerClass(HINSTANCE hi) {
    WNDCLASSA wc{};
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &SpectrumView::wndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    return RegisterClassA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND SpectrumView::create(HWND parent, HINSTANCE hi, int id) {
    HDC sdc = GetDC(nullptr);
    dpi_ = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(nullptr, sdc);
    rebuildLut();
    hwnd_ = CreateWindowExA(WS_EX_CLIENTEDGE, kClassName, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                            0, 0, 100, 100, parent, (HMENU)(INT_PTR)id, hi, this);
    return hwnd_;
}

SpectrumView::~SpectrumView() {
    if (wfDC_)  { SelectObject(wfDC_, wfOld_); DeleteDC(wfDC_); }
    if (wfBmp_) DeleteObject(wfBmp_);
    if (bbDC_)  { SelectObject(bbDC_, bbOld_); DeleteDC(bbDC_); }
    if (bbBmp_) DeleteObject(bbBmp_);
    if (font_) DeleteObject(font_);
    if (fontSmall_) DeleteObject(fontSmall_);
}

LRESULT CALLBACK SpectrumView::wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    SpectrumView* self;
    if (m == WM_NCCREATE) {
        self = (SpectrumView*)((CREATESTRUCTA*)l)->lpCreateParams;
        self->hwnd_ = h;
        SetWindowLongPtrA(h, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (SpectrumView*)GetWindowLongPtrA(h, GWLP_USERDATA);
    }
    if (!self) return DefWindowProcA(h, m, w, l);
    return self->handle(m, w, l);
}

LRESULT SpectrumView::handle(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: invalidate(); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            paint(dc);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            hoverX_ = GET_X_LPARAM(l);
            if (!tracking_) {
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&t);
                tracking_ = true;
            }
            invalidate();
            return 0;
        }
        case WM_MOUSELEAVE: hoverX_ = -1; tracking_ = false; invalidate(); return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd_);
            placeMarker(GET_X_LPARAM(l));
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            placeMarker(GET_X_LPARAM(l));
            if (hasMarker_)
                PostMessageA(GetParent(hwnd_), WM_ORL_RETUNE, 0, (LPARAM)(uint32_t)std::llround(suggestedTuneHz()));
            return 0;
        }
    }
    return DefWindowProcA(hwnd_, m, w, l);
}

// ---------------------------------------------------------------- layout / buffers

void SpectrumView::computeLayout() {
    RECT rc; GetClientRect(hwnd_, &rc);
    cw_ = rc.right; ch_ = rc.bottom;
    L_ = S(58); R_ = S(10); T_ = S(8);
    axisH_ = S(20);
    const int avail = ch_ - T_ - axisH_ - S(6);
    specH_ = std::max(S(40), avail * 38 / 100);
    wfTop_ = T_ + specH_ + axisH_;
    wfH_ = std::max(S(20), ch_ - wfTop_ - S(6));
    plotW_ = std::max(50, cw_ - L_ - R_);
}

void SpectrumView::ensureBuffers() {
    HDC sdc = GetDC(hwnd_);

    if (!font_) {
        font_ = CreateFontA(-S(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                            FIXED_PITCH | FF_MODERN, "Consolas");
        fontSmall_ = CreateFontA(-S(11), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, "Consolas");
    }

    if (!bbDC_ || bbW_ != cw_ || bbH_ != ch_) {
        if (bbDC_) { SelectObject(bbDC_, bbOld_); DeleteDC(bbDC_); bbDC_ = nullptr; }
        if (bbBmp_) { DeleteObject(bbBmp_); bbBmp_ = nullptr; }
        bbDC_ = CreateCompatibleDC(sdc);
        bbBmp_ = CreateCompatibleBitmap(sdc, std::max(1, cw_), std::max(1, ch_));
        bbOld_ = (HBITMAP)SelectObject(bbDC_, bbBmp_);
        bbW_ = cw_; bbH_ = ch_;
    }

    if (!wfDC_ || wfW_ != plotW_ || wfHcur_ != wfH_) {
        if (wfDC_) { SelectObject(wfDC_, wfOld_); DeleteDC(wfDC_); wfDC_ = nullptr; }
        if (wfBmp_) { DeleteObject(wfBmp_); wfBmp_ = nullptr; }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = plotW_;
        bi.bmiHeader.biHeight = -wfH_;          // top-down: row 0 is the newest line
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        wfBmp_ = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        wfBits_ = (uint32_t*)bits;
        wfDC_ = CreateCompatibleDC(sdc);
        wfOld_ = (HBITMAP)SelectObject(wfDC_, wfBmp_);
        wfW_ = plotW_; wfHcur_ = wfH_;
        if (wfBits_) std::fill(wfBits_, wfBits_ + (size_t)wfW_ * wfHcur_, lut_[0]);
    }
    ReleaseDC(hwnd_, sdc);
}

void SpectrumView::rebuildLut() {
    struct Stop { double t; int r, g, b; };
    static const Stop st[] = {
        {0.00, 3, 3, 20},   {0.25, 15, 40, 150}, {0.50, 0, 170, 190},
        {0.70, 240, 220, 50}, {0.88, 240, 70, 30}, {1.00, 255, 245, 235},
    };
    for (int i = 0; i < 256; ++i) {
        const double t = i / 255.0;
        size_t k = 0;
        while (k + 2 < sizeof st / sizeof st[0] && t > st[k + 1].t) ++k;
        const Stop& a = st[k]; const Stop& b = st[k + 1];
        const double u = std::min(1.0, std::max(0.0, (t - a.t) / (b.t - a.t)));
        lut_[i] = rgb32((int)(a.r + (b.r - a.r) * u), (int)(a.g + (b.g - a.g) * u), (int)(a.b + (b.b - a.b) * u));
    }
}

// ---------------------------------------------------------------- data

void SpectrumView::clearHistory() {
    hist_.clear(); histT_.clear();
    disp_.clear(); raw_.clear();
    if (wfBits_) std::fill(wfBits_, wfBits_ + (size_t)wfW_ * wfHcur_, lut_[0]);
    invalidate();
}

void SpectrumView::setPaused(bool p) { paused_ = p; invalidate(); }

void SpectrumView::binRange(int x, int& lo, int& hi) const {
    const int N = (int)disp_.size();
    const double ratio = (double)N / plotW_;
    const double c = x * ratio;                       // continuous bin index under pixel x
    lo = (int)std::floor(c - ratio * 0.5 + 0.5);
    hi = (int)std::floor(c + ratio * 0.5 + 0.5);
    lo = std::max(0, std::min(N - 1, lo));
    hi = std::max(lo, std::min(N - 1, hi));
}

float SpectrumView::maxInRange(const std::vector<float>& v, int lo, int hi) const {
    float m = v[lo];
    for (int i = lo + 1; i <= hi; ++i) if (v[i] > m) m = v[i];
    return m;
}

double SpectrumView::pixelToHz(int x) const { return (center_ - rate_ / 2) + (double)(x - L_) / plotW_ * rate_; }
int SpectrumView::hzToPixel(double hz) const {
    return L_ + (int)std::lround((hz - (center_ - rate_ / 2)) / rate_ * plotW_);
}

void SpectrumView::addFrames(const std::vector<SpectrumFrame>& frames) {
    if (frames.empty() || paused_) return;
    computeLayout();
    ensureBuffers();
    for (const auto& f : frames) {
        if (f.db.empty()) continue;
        const bool changed = disp_.size() != f.db.size() || std::fabs(f.centerHz - center_) > 0.5 ||
                             std::fabs(f.sampleRate - rate_) > 0.5;
        if (changed) {
            if (wfBits_) std::fill(wfBits_, wfBits_ + (size_t)wfW_ * wfHcur_, lut_[0]);
            hist_.clear(); histT_.clear();
            disp_ = f.db;
            center_ = f.centerHz; rate_ = f.sampleRate;
        } else {
            for (size_t i = 0; i < disp_.size(); ++i)
                disp_[i] = (float)(smooth_ * disp_[i] + (1.0 - smooth_) * f.db[i]);
        }
        raw_ = f.db;

        // Noise floor estimate: 20th percentile of the bins, smoothed over time.
        std::vector<float> tmp = f.db;
        std::nth_element(tmp.begin(), tmp.begin() + tmp.size() / 5, tmp.end());
        const double n = tmp[tmp.size() / 5];
        noiseDb_ = haveNoise_ && !changed ? 0.95 * noiseDb_ + 0.05 * n : n;
        haveNoise_ = true;

        pushRow(f.db);

        hist_.push_back(f.db);
        histT_.push_back(GetTickCount64());
        while (hist_.size() > 240) { hist_.pop_front(); histT_.pop_front(); }
    }
    if (hasMarker_ && (hist_.size() % 15) == 0) analyse();   // keep measurement fresh while running
    invalidate();
}

void SpectrumView::pushRow(const std::vector<float>& db) {
    if (!wfBits_ || wfW_ <= 0 || wfHcur_ <= 1) return;
    std::memmove(wfBits_ + wfW_, wfBits_, (size_t)(wfHcur_ - 1) * wfW_ * sizeof(uint32_t));
    const double lo = floorDb();
    for (int x = 0; x < wfW_; ++x) {
        int a, b;
        binRange(x, a, b);
        const double t = (maxInRange(db, a, b) - lo) / spanDb_;
        const int idx = (int)std::lround(std::min(1.0, std::max(0.0, t)) * 255.0);
        wfBits_[x] = lut_[idx];
    }
}

// ---------------------------------------------------------------- measurement

void SpectrumView::placeMarker(int x) {
    if (disp_.empty() || x < L_ || x > L_ + plotW_) return;
    markerHz_ = pixelToHz(x);
    hasMarker_ = true;
    analyse();
    invalidate();
    PostMessageA(GetParent(hwnd_), WM_ORL_MARKER, 0, 0);
}

void SpectrumView::analyse() {
    obs_.clear(); readout_.clear(); signalFound_ = false;
    const int N = (int)disp_.size();
    if (N < 16 || rate_ <= 0) return;
    const double binHz = rate_ / N;
    const double f0 = center_ - rate_ / 2;
    const int mb = std::max(0, std::min(N - 1, (int)std::lround((markerHz_ - f0) / binHz)));

    const int search = std::max(2, std::min(N / 8, (int)(15000.0 / binHz)));
    int pk = mb;
    for (int i = std::max(0, mb - search); i <= std::min(N - 1, mb + search); ++i)
        if (disp_[i] > disp_[pk]) pk = i;
    const double pkDb = disp_[pk];
    const double snr = pkDb - noiseDb_;

    char b[256];
    std::snprintf(b, sizeof b, "Marker: %s", formatFrequency(markerHz_).c_str());
    readout_.push_back(b);
    obs_ += std::string("Marker at ") + formatFrequency(markerHz_) + ".\r\n";

    if (std::abs(pk - N / 2) <= std::max(3, N / 512)) {
        obs_ += "This is the spike at the exact centre of the screen. It is an artefact of the receiver "
                "(a DC offset), not a radio signal. Tune the centre frequency a little away to see past it.";
        readout_.push_back("centre spike: receiver artefact");
        return;
    }
    if (snr < 6.0) {
        std::snprintf(b, sizeof b, "Nothing clearly stands out here: the strongest point nearby is only %.0f dB "
                      "above the noise floor.", snr);
        obs_ += b;
        std::snprintf(b, sizeof b, "no clear signal (%.0f dB over noise)", snr);
        readout_.push_back(b);
        return;
    }

    const double thr = std::max(noiseDb_ + 6.0, pkDb - 20.0);
    const int gapMax = std::max(2, (int)(4000.0 / binHz));
    int l = pk, r = pk, gap = 0;
    for (int i = pk - 1; i >= 0; --i) { if (disp_[i] >= thr) { l = i; gap = 0; } else if (++gap >= gapMax) break; }
    gap = 0;
    for (int i = pk + 1; i < N; ++i)  { if (disp_[i] >= thr) { r = i; gap = 0; } else if (++gap >= gapMax) break; }

    const double widthHz = (r - l + 1) * binHz;
    signalHz_ = f0 + 0.5 * (l + r) * binHz;
    signalFound_ = true;

    std::snprintf(b, sizeof b, "Signal centre approx. %s, peak level %.0f dBFS, %.0f dB above the noise floor. ",
                  formatFrequency(signalHz_).c_str(), pkDb, snr);
    obs_ += b;
    std::snprintf(b, sizeof b, "Width approx. %s (where the signal is within 20 dB of its peak). ",
                  formatBandwidth(widthHz).c_str());
    obs_ += b;

    std::snprintf(b, sizeof b, "signal at ~%s", formatFrequency(signalHz_).c_str());
    readout_.push_back(b);
    std::snprintf(b, sizeof b, "%.0f dBFS, %.0f dB over noise", pkDb, snr);
    readout_.push_back(b);
    std::snprintf(b, sizeof b, "width ~%s (estimate)", formatBandwidth(widthHz).c_str());
    readout_.push_back(b);

    // Activity over the recent history (continuous or intermittent?).
    double activity = -1.0;
    if (hist_.size() >= 30 && hist_.front().size() == (size_t)N) {
        int on = 0;
        for (const auto& h : hist_) {
            float v = h[pk];
            if (pk > 0) v = std::max(v, h[pk - 1]);
            if (pk + 1 < N) v = std::max(v, h[pk + 1]);
            if (v >= noiseDb_ + 6.0) ++on;
        }
        const double frac = (double)on / hist_.size();
        activity = frac;
        const double secs = (double)(histT_.back() - histT_.front()) / 1000.0;
        if (frac >= 0.9)
            std::snprintf(b, sizeof b, "Activity: present for essentially all of the last %.0f s (continuous). ", secs);
        else if (frac <= 0.05)
            std::snprintf(b, sizeof b, "Activity: almost absent in the last %.0f s. ", secs);
        else
            std::snprintf(b, sizeof b, "Activity: intermittent - above the noise for %.0f%% of the last %.0f s. ",
                          frac * 100.0, secs);
        obs_ += b;
        readout_.push_back(frac >= 0.9 ? "continuous" : "intermittent / bursty");
    }

    // A hint, deliberately worded as a possibility: a spectrum shape alone cannot identify a transmission.
    obs_ += signalHint(signalHz_, widthHz, activity);
}

// ---------------------------------------------------------------- painting

namespace {
struct GdiPen {
    HDC dc; HPEN pen, old;
    GdiPen(HDC d, COLORREF c, int w = 1, int style = PS_SOLID) : dc(d) { pen = CreatePen(style, w, c); old = (HPEN)SelectObject(dc, pen); }
    ~GdiPen() { SelectObject(dc, old); DeleteObject(pen); }
};
void fillRect(HDC dc, int x0, int y0, int x1, int y1, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    RECT r{x0, y0, x1, y1};
    FillRect(dc, &r, br);
    DeleteObject(br);
}
void text(HDC dc, int x, int y, const std::string& s, COLORREF c, UINT align = TA_LEFT | TA_TOP) {
    SetTextColor(dc, c);
    SetTextAlign(dc, align);
    TextOutA(dc, x, y, s.c_str(), (int)s.size());
}
} // namespace

void SpectrumView::paint(HDC dc) {
    computeLayout();
    ensureBuffers();
    HDC m = bbDC_;

    fillRect(m, 0, 0, cw_, ch_, kBg);
    SetBkMode(m, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(m, font_);

    const int specT = T_, specB = T_ + specH_;
    fillRect(m, L_, specT, L_ + plotW_, specB, kPlotBg);
    fillRect(m, L_, wfTop_, L_ + plotW_, wfTop_ + wfH_, kPlotBg);

    const bool haveData = !disp_.empty() && rate_ > 0;
    const double lo = floorDb(), hi = lo + spanDb_;
    auto yOf = [&](double db) {
        const double t = std::min(1.0, std::max(0.0, (db - lo) / spanDb_));
        return specB - (int)std::lround(t * (specH_ - 1)) - 1;
    };

    // dB grid + labels
    if (haveData) {
        GdiPen gp(m, kGrid);
        for (int d = (int)std::ceil(lo / 10.0) * 10; d <= hi; d += 10) {
            const int y = yOf(d);
            MoveToEx(m, L_, y, nullptr); LineTo(m, L_ + plotW_, y);
            char b[16]; std::snprintf(b, sizeof b, "%d", d);
            text(m, L_ - S(6), y - S(7), b, kText, TA_RIGHT | TA_TOP);
        }
        text(m, S(6), specT, "dBFS", kText);
    }

    // frequency grid + labels
    if (haveData) {
        const double f0 = center_ - rate_ / 2;
        const double raw = rate_ / std::max(2.0, (double)plotW_ / S(110));
        const double p10 = std::pow(10.0, std::floor(std::log10(raw)));
        double step = p10;
        for (double mult : {1.0, 2.0, 5.0, 10.0}) { step = p10 * mult; if (step >= raw) break; }
        int dec = step >= 1e6 ? 0 : (int)std::ceil(-std::log10(step / 1e6));
        dec = std::max(0, std::min(6, dec));

        GdiPen gp(m, kGrid);
        for (double f = std::ceil(f0 / step) * step; f <= f0 + rate_; f += step) {
            const int x = L_ + (int)std::lround((f - f0) / rate_ * plotW_);
            if (x < L_ || x > L_ + plotW_) continue;
            MoveToEx(m, x, specT, nullptr); LineTo(m, x, specB);
            MoveToEx(m, x, wfTop_, nullptr); LineTo(m, x, wfTop_ + wfH_);
            char b[32]; std::snprintf(b, sizeof b, "%.*f", dec, f / 1e6);
            text(m, x, specB + S(3), b, kText, TA_CENTER | TA_TOP);
        }
        text(m, S(6), specB + S(3), "MHz", kText);
    }

    // spectrum trace
    if (haveData) {
        std::vector<POINT> pts;
        pts.reserve(plotW_ + 3);
        for (int x = 0; x < plotW_; ++x) {
            int a, b;
            binRange(x, a, b);
            pts.push_back({L_ + x, yOf(maxInRange(disp_, a, b))});
        }
        std::vector<POINT> poly = pts;
        poly.push_back({L_ + plotW_ - 1, specB - 1});
        poly.push_back({L_, specB - 1});
        HBRUSH fb = CreateSolidBrush(kFill);
        HBRUSH ob = (HBRUSH)SelectObject(m, fb);
        HPEN np = (HPEN)SelectObject(m, GetStockObject(NULL_PEN));
        Polygon(m, poly.data(), (int)poly.size());
        SelectObject(m, np); SelectObject(m, ob); DeleteObject(fb);
        GdiPen tp(m, kTrace);
        Polyline(m, pts.data(), (int)pts.size());

        // Centre-frequency marker (the tuned frequency).
        GdiPen cp(m, RGB(80, 90, 110), 1, PS_DOT);
        const int cx = L_ + plotW_ / 2;
        MoveToEx(m, cx, specT, nullptr); LineTo(m, cx, specB);
        MoveToEx(m, cx, wfTop_, nullptr); LineTo(m, cx, wfTop_ + wfH_);
    } else {
        text(m, L_ + plotW_ / 2, specT + specH_ / 2 - S(8), "Waiting for samples...", kText, TA_CENTER | TA_TOP);
    }

    // waterfall
    BitBlt(m, L_, wfTop_, wfW_, wfHcur_, wfDC_, 0, 0, SRCCOPY);

    // plot frames
    {
        GdiPen fp(m, RGB(70, 80, 96));
        HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH ob = (HBRUSH)SelectObject(m, nb);
        Rectangle(m, L_ - 1, specT - 1, L_ + plotW_ + 1, specB + 1);
        Rectangle(m, L_ - 1, wfTop_ - 1, L_ + plotW_ + 1, wfTop_ + wfH_ + 1);
        SelectObject(m, ob);
    }
    text(m, S(6), wfTop_, "time", kText);
    text(m, S(6), wfTop_ + S(14), "(newest", kText);
    text(m, S(6), wfTop_ + S(28), " at top)", kText);

    // listening frequency: channel-width strip plus a solid line
    if (haveData && listenOn_) {
        const int lx = hzToPixel(listenHz_);
        const int x0 = std::max(L_, hzToPixel(listenHz_ - listenBw_ / 2));
        const int x1 = std::min(L_ + plotW_, hzToPixel(listenHz_ + listenBw_ / 2));
        const COLORREF red = RGB(235, 70, 70);
        if (x1 > x0) fillRect(m, x0, specT, x1, specT + S(5), red);
        {
            GdiPen ep(m, RGB(190, 60, 60), 1, PS_DOT);
            for (int ex : {x0, x1}) {
                if (ex <= L_ || ex >= L_ + plotW_) continue;
                MoveToEx(m, ex, specT, nullptr); LineTo(m, ex, specB);
                MoveToEx(m, ex, wfTop_, nullptr); LineTo(m, ex, wfTop_ + wfH_);
            }
        }
        if (lx >= L_ && lx <= L_ + plotW_) {
            GdiPen lp(m, red);
            MoveToEx(m, lx, specT, nullptr); LineTo(m, lx, specB);
            MoveToEx(m, lx, wfTop_, nullptr); LineTo(m, lx, wfTop_ + wfH_);
            text(m, lx + S(4), specT + S(7), "listening", red);
        }
    }

    // marker
    if (haveData && hasMarker_) {
        const int mx = hzToPixel(markerHz_);
        if (mx >= L_ && mx <= L_ + plotW_) {
            GdiPen mp(m, kMarker);
            MoveToEx(m, mx, specT, nullptr); LineTo(m, mx, specB);
            MoveToEx(m, mx, wfTop_, nullptr); LineTo(m, mx, wfTop_ + wfH_);
        }
        if (signalFound_) {
            const int sx = hzToPixel(signalHz_);
            if (sx >= L_ && sx <= L_ + plotW_) {
                GdiPen sp(m, RGB(255, 140, 60), 1, PS_DASH);
                MoveToEx(m, sx, specT, nullptr); LineTo(m, sx, specB);
            }
        }
    }

    // hover readout
    if (haveData && hoverX_ >= L_ && hoverX_ <= L_ + plotW_) {
        GdiPen hp(m, kHover, 1, PS_DOT);
        MoveToEx(m, hoverX_, specT, nullptr); LineTo(m, hoverX_, specB);
        int a, b;
        binRange(hoverX_ - L_, a, b);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s  %.0f dBFS", formatFrequency(pixelToHz(hoverX_)).c_str(), maxInRange(disp_, a, b));
        const int tw = S(8) * (int)std::strlen(buf) / 1 / 1;
        int tx = hoverX_ + S(8);
        if (tx + tw > L_ + plotW_) tx = hoverX_ - S(8);
        text(m, tx, specB - S(18), buf, kText, tx < hoverX_ ? (TA_RIGHT | TA_TOP) : (TA_LEFT | TA_TOP));
    }

    // measurement box (top right of the spectrum)
    if (haveData && hasMarker_ && !readout_.empty()) {
        int w = 0;
        for (auto& s : readout_) w = std::max(w, (int)s.size());
        const int bw = w * S(7) + S(14), bh = (int)readout_.size() * S(15) + S(8);
        const int bx = L_ + plotW_ - bw - S(6), by = specT + S(6);
        fillRect(m, bx, by, bx + bw, by + bh, RGB(22, 26, 36));
        int y = by + S(4);
        for (size_t i = 0; i < readout_.size(); ++i, y += S(15))
            text(m, bx + S(7), y, readout_[i], i == 0 ? kMarker : kText);
    }

    // banners: simulated data is always labelled as such
    if (!banner_.empty()) {
        const int bw = (int)banner_.size() * S(8) + S(16);
        fillRect(m, L_ + S(6), specT + S(6), L_ + S(6) + bw, specT + S(24), RGB(60, 40, 8));
        text(m, L_ + S(14), specT + S(8), banner_, kAmber);
    }
    if (paused_) {
        fillRect(m, L_ + S(6), wfTop_ + S(6), L_ + S(6) + S(76), wfTop_ + S(24), RGB(60, 40, 8));
        text(m, L_ + S(14), wfTop_ + S(8), "PAUSED", kAmber);
    }

    SelectObject(m, oldFont);
    BitBlt(dc, 0, 0, cw_, ch_, m, 0, 0, SRCCOPY);
}

} // namespace orl
