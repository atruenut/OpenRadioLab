// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "AudioOutput.h"

#include <algorithm>
#include <cstring>

namespace orl {

std::vector<std::string> AudioOutput::listDevices() {
    std::vector<std::string> v;
    v.push_back("System default");
    const UINT n = waveOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        WAVEOUTCAPSA caps;
        if (waveOutGetDevCapsA(i, &caps, sizeof caps) == MMSYSERR_NOERROR) v.push_back(caps.szPname);
        else v.push_back("Audio device " + std::to_string(i));
    }
    return v;
}

bool AudioOutput::open(int deviceIndex, std::string& error) {
    close();
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = 48000;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = 192000;

    ev_ = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    const UINT dev = deviceIndex <= 0 ? WAVE_MAPPER : (UINT)(deviceIndex - 1);
    const MMRESULT r = waveOutOpen(&hwo_, dev, &fmt, (DWORD_PTR)ev_, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR) {
        char b[160]; waveOutGetErrorTextA(r, b, sizeof b);
        error = std::string("Could not open the audio device: ") + b;
        CloseHandle(ev_); ev_ = nullptr; hwo_ = nullptr;
        return false;
    }

    for (int i = 0; i < kBufs; ++i) {
        data_[i].assign(kFrames * 2, 0);
        std::memset(&hdr_[i], 0, sizeof(WAVEHDR));
        hdr_[i].lpData = (LPSTR)data_[i].data();
        hdr_[i].dwBufferLength = kFrames * 4;
        waveOutPrepareHeader(hwo_, &hdr_[i], sizeof(WAVEHDR));
        waveOutWrite(hwo_, &hdr_[i], sizeof(WAVEHDR));      // start with silence
    }
    stop_ = false;
    th_.start([this] { run(); });
    return true;
}

void AudioOutput::close() {
    if (!hwo_) return;
    stop_ = true;
    if (ev_) SetEvent(ev_);
    th_.join();
    waveOutReset(hwo_);
    for (int i = 0; i < kBufs; ++i) waveOutUnprepareHeader(hwo_, &hdr_[i], sizeof(WAVEHDR));
    waveOutClose(hwo_);
    hwo_ = nullptr;
    if (ev_) { CloseHandle(ev_); ev_ = nullptr; }
    clear();
}

void AudioOutput::clear() {
    Lock l(m_);
    ring_.clear();
}

// The ring always holds interleaved stereo; mono input is duplicated to both ears.
void AudioOutput::write(const float* data, size_t frames, int channels) {
    Lock l(m_);
    if (channels == 2) ring_.insert(ring_.end(), data, data + 2 * frames);
    else for (size_t k = 0; k < frames; ++k) { ring_.push_back(data[k]); ring_.push_back(data[k]); }
    if (ring_.size() > 24000)                                   // more than 250 ms queued: trim to 80 ms
        ring_.erase(ring_.begin(), ring_.end() - 7680);
}

void AudioOutput::fill(int i) {
    int16_t* d = data_[i].data();
    {
        Lock l(m_);
        const size_t n = std::min<size_t>(ring_.size() & ~(size_t)1, (size_t)kFrames * 2);
        for (size_t k = 0; k < n; ++k) {
            const float v = std::max(-1.0f, std::min(1.0f, ring_[k]));
            d[k] = (int16_t)(v * 32767.0f);
        }
        ring_.erase(ring_.begin(), ring_.begin() + (long)n);
        for (size_t k = n; k < (size_t)kFrames * 2; ++k) d[k] = 0;   // underrun: silence
    }
}

void AudioOutput::run() {
    while (!stop_) {
        WaitForSingleObject(ev_, 50);
        for (int i = 0; i < kBufs && !stop_; ++i) {
            if (hdr_[i].dwFlags & WHDR_DONE) {
                fill(i);
                waveOutWrite(hwo_, &hdr_[i], sizeof(WAVEHDR));
            }
        }
    }
}

} // namespace orl
