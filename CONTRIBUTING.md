# Contributing / forking guide

Thanks for looking. OpenRadio Lab is GPL-2.0-only; by contributing you agree your work is released under the same licence.

## Ground rules (these are the soul of the project)

1. **Receive-only.** No transmit code, ever.
2. **Observation, not identification.** UI text may say what a signal *looks like or fits*; it must not claim to know who or what is transmitting.
3. **Label simulated data**, always, everywhere it appears (status bar, banner, journal, recordings).
4. **Beginner-first wording.** Explain terms where they appear; avoid jargon without a plain-language sentence.
5. **No heavy dependencies.** Win32 + standard C++17. Hardware libraries are loaded at run time, not linked.
6. **Never block the device thread.** USB callbacks must return fast; heavy work belongs on the DSP thread.

## Set up

```
make test                                  # portable tests, any OS with g++
make CROSS=x86_64-w64-mingw32-             # Windows exe from Linux
build.bat                                  # Windows, MSYS2 MinGW-w64
```

Wine does not run the GUI reliably, so GUI, audio and hardware paths can only be checked on Windows. Keep logic out of `main.cpp` and the windows where you can, and put it in portable code with a test.

## Conventions

* C++17, `-Wall -Wextra` clean. No exceptions are relied on; no `std::thread` (use `src/Sync.h`, which works on every MinGW thread model).
* Every source file starts with the 4-line SPDX / GPL-2.0 header (copy from any file).
* UI text is ANSI/ASCII (the app uses the `A` Win32 APIs). `tests/test_content.cpp` enforces this for lessons.
* Settings live in `%APPDATA%\OpenRadioLab\settings.ini`; the journal in `journal.txt`.
* Build artefacts (`build/`, `*.exe`) are not committed. Releases go on the Releases page.

## Recipes

**Add a lesson or term.** Edit `src/learn/Content.cpp` (a `Lesson` has title, body, and an optional "Try it" frequency/mode). Run `make test`.

**Add an SDR backend.** Implement `SDRDevice` (`src/sdr/SDRDevice.h`): `open/close`, `setCenterFrequency`, `setSampleRate`, gain, `start(sink)` which calls the sink with interleaved float I/Q in [-1, 1] from your own thread, `stop`, `isRunning`, `caps`. `src/io/IqFile.cpp` (`IqFileDevice`) is the smallest working example, `SimulatedSDR` the most complete. Register it where `rescan()` / `startEntry()` build device entries in `main.cpp`, and add detection to `src/sdr/DeviceDetect.cpp` if it is hardware.

**Add a demodulator mode.** Add to `Demodulator::Mode` and its `process()`, extend the mode tables in `main.cpp` (`kModeShort`, the "Listen as" combo) and `Bands.h` (band default mode), and write a test that modulates a known tone and checks the decoded tone (see `tests/test_demod.cpp` and `tests/test_stereo.cpp`).

**Improve the band table.** `src/Bands.h` is deliberately simplified. If you replace it with official ISED allocation data, cite the source and date in the file and in `docs/THIRD-PARTY.md`.

## Pull requests

* Keep changes focused, run `make test`, and say what you verified on real hardware (and which dongle/tuner) versus what only the tests cover.
* New user-visible features: add a line to `docs/GUIDE.md`; add or adjust a lesson if beginners need to understand it.
* Be kind in issues and reviews. Many users here are brand new to radio.

## Reporting bugs

Open an issue with: Windows version, dongle model and tuner (shown in the banner and **Tools -> Device report**), the app version (window title), what you did, and what you expected.
