# OpenRadio Lab

**A beginner-first, receive-only Software Defined Radio (SDR) learning environment for Windows.**

Plug in a cheap RTL-SDR USB dongle, open OpenRadio Lab, and in a few minutes you are looking at the radio world
around you, listening to FM stations, and learning what you are seeing. No radio background assumed. The app
explains itself, measures what it can, and never pretends to know more than it does.

Native Win32 / C++17, built with MinGW-w64. No Qt, no Electron, no .NET, no installer. One small `.exe`.

> **Receive-only.** OpenRadio Lab cannot transmit, and it will stay that way.

Licence: **GPL-2.0-only** (see [LICENSE](LICENSE)).

## What it does

| | |
|---|---|
| **Spectrum + waterfall** | Custom-drawn, double-buffered. Click to place a marker and get an *estimate* of centre, level, width, and whether the signal is steady or intermittent. Double-click to listen. |
| **Listening** | Wide FM (with **stereo**), narrow FM and AM, with volume, squelch and filter width. A small DSP chain written from scratch (FFT, windowed-sinc decimators, discriminator, 19 kHz pilot PLL). |
| **FM band scanner** | One button sweeps 88-108 MHz, lists the stations it finds, and says why if it finds none (antenna, gain, driver mismatch). |
| **Automatic hardware detection** | Scans USB, diagnoses a missing driver in plain language, loads `rtlsdr.dll` at run time, and falls back to a labelled simulator when nothing is plugged in. |
| **Simulator** | A fictional radio world with stations, bursts, a DC spike and ADC clipping, so everything works with no hardware. Always labelled **SIMULATED**. |
| **Recording and playback** | Save the whole captured band to a `.orliq` file; replay it later with play/pause/seek/repeat, or share it with someone who has no dongle. |
| **Discover** | Sweeps the whole tunable range, lists every signal found (sortable; saved between sessions and as files; CSV export; personal notes), marks signals that are new since the last scan, shows when each is usually on the air, compares two scans, can re-sweep on a schedule (Watch), and can auto-listen: hop between active channels, stop where something is being said and optionally save a clip. |
| **Favourites** | A window to sort, rename, reorder, remove and tune to your saved stations. |
| **Aircraft tracker** | Decodes ADS-B on 1090 MHz: lists nearby aircraft with flight, height, speed, heading and position, and plots them on a simple radar view. |
| **Morse and RDS** | Morse text decoder for CW; station name, song text and programme type from FM RDS/RBDS. |
| **Frequency Atlas** | Well-known bands in plain language, each with its information source and a button to jump to an example. A curated summary, not the official ISED table. |
| **Save audio** | Save what you hear as a standard WAV file. |
| **Learning Center** | Ten short lessons for total beginners (antennas, gain, reading the waterfall, modes, responsible listening in Canada) plus an A-Z radio dictionary. |
| **Experiment Journal** | A plain-text notebook that captures frequency, mode, gain, measurement and recording with each entry. Exports to Markdown. |
| **Signal Lab** | Plots a signal's level over minutes and reports duty cycle and bursts. Exports CSV. |
| **Beginner / Advanced modes** | Beginner picks gain and mode for you; Advanced exposes sample rate, FFT size, window, gain, bandwidth and squelch. |

### Design principles

1. **Observation, not identification.** The app says *"it is in the airband, voice-sized, and comes and goes, which fits short radio calls"*, never *"this is a plane"*.
2. **Simulated data is always labelled.** Nothing fake is ever shown as live.
3. **Plain language first.** Every number should be explainable to someone who has never touched a radio.
4. **Receive-only and responsible.** See lesson 10 in the Learning Center.
5. **Small and dependency-free.** Win32 + the C++ standard library. The hardware library is loaded at run time.

## Status

Written and tested together with a real RTL-SDR dongle on Windows. Honest summary:

* Verified on real hardware by the author: detection, spectrum/waterfall, listening (FM, stereo FM), audio, scanner, recording and playback, journal, Signal Lab.
* Verified only by unit tests (no hardware or Windows needed): FFT, filters, demodulators, stereo decoder, band scanner, IQ file format, hints, journal storage, level statistics.
* **Not verified:** other tuners/dongles than the author's, RTL-SDR Blog V4 specifics, HiDPI scaling beyond 100-125%, Windows versions other than recent 10/11. Reports welcome.

## Quick start (users)

1. Download `OpenRadioLab.exe` from the [Releases](../../releases) page (or build it, below).
2. Read **[docs/getting-started.html](docs/getting-started.html)** (also: **Help -> Getting-started manual** in the app). It explains, for complete newbies, what an SDR is and why you need a driver tool (Zadig) and two DLL files (`rtlsdr.dll`, `libusb-1.0.dll`).
3. With no dongle attached the app starts on the simulator, so you can explore straight away.

The DLLs are separate projects with their own licences and are **not bundled**. Put them next to the exe, in a `libs` folder next to it, or in `%APPDATA%\OpenRadioLab\libs` (the last survives updates). See [docs/THIRD-PARTY.md](docs/THIRD-PARTY.md).

## Building

You need a MinGW-w64 toolchain (g++ with C++17).

```
# Windows (MSYS2 MinGW-w64 shell)
build.bat                                # -> OpenRadioLab.exe, or:
mingw32-make

# Linux / macOS cross-compile
make CROSS=x86_64-w64-mingw32-

# Portable unit tests: any OS with g++, no Windows needed
make test
```

`make test` builds and runs nine test programs (core DSP, demodulators, stereo, scanner, IQ files, WAV files, learning content, hints and the atlas table, journal/statistics). The Makefile tracks header dependencies, so incremental builds are safe.

## Project layout

```
src/
  main.cpp              Main window, controls, settings, glue
  Sync.h                Mutex / Thread / Event on raw Win32 (pthread fallback), no std::thread
  Util.h  Bands.h  Hints.h   Frequency parsing/formatting, band table, "what could this be?" hints
  sdr/                  SDRDevice interface; SimulatedSDR; RTLSDRDevice (runtime-loaded librtlsdr); DeviceDetect (USB scan, driver diagnosis)
  dsp/                  FFT, SpectrumProcessor, Filters, Demodulator (WFM/NFM/AM/stereo), DspPipeline (thread), BandScanner, LevelStats
  io/IqFile.*           .orliq recorder (writer thread) and IqFileDevice (a recording as a "receiver")
  audio/                waveOut stereo output
  ui/                   SpectrumView (spectrum + waterfall), Learn / Journal / Signal Lab windows
  learn/Content.*       The lessons and dictionary (plain data)
  journal/Journal.*     Journal storage (plain text) and Markdown export
tests/                  Portable tests (no Windows APIs)
docs/                   Getting-started manual (md + html), reference guide, third-party notes
```

**Data flow:** device thread -> bounded queue (drops oldest, never blocks USB) -> DSP thread -> spectrum frames (drained by a 33 ms UI timer) and demodulator -> audio thread. Anything that is not a Windows API lives in portable code that the tests exercise.

## Forking and contributing

Forks are welcome; that is why it is GPL. Start with **[CONTRIBUTING.md](CONTRIBUTING.md)**. Good first ideas:

* **More lessons or translations.** Edit `src/learn/Content.cpp` (plain ASCII data; a test checks it).
* **Better frequency data.** `src/Bands.h` is a curated summary with a source note on each entry; checking entries against the official Canadian Table of Frequency Allocations (ISED), or adding more, is very welcome.
* HF direct sampling, more SDR backends.
* **More SDR backends** (Airspy, HackRF, SDRplay, Soapy). Implement `SDRDevice`; `IqFileDevice` is the shortest example.
* **Testing on other dongles and Windows versions**, and reporting what you find.

See [ROADMAP.md](ROADMAP.md) for where it is heading.

## Third-party software and licence

OpenRadio Lab is free software under the **GNU General Public License, version 2** (`GPL-2.0-only`). It contains no code from librtlsdr, libusb or Zadig; it loads the first two at run time. Details and a redistribution checklist: [docs/THIRD-PARTY.md](docs/THIRD-PARTY.md). Source files carry the copyright line "The OpenRadio Lab authors"; add your name if you contribute.

## Responsible use

Listening to broadcast, weather, aviation and amateur radio is a normal hobby. In Canada, radio rules are set by ISED; private communications should not be listened to, used or passed on. This is general information, not legal advice. Check the rules where you live before recording or sharing captures.
