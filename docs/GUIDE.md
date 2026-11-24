# OpenRadio Lab - Guide

## 1. What you are looking at

* **Top graph - spectrum.** Signal strength (dBFS, 0 = the strongest the receiver can represent) at every
  frequency in view. A transmission is a hill; the ragged floor is noise.
* **Bottom graph - waterfall.** The same data over time, newest line at the top, brighter = stronger.
  A steady station is a vertical streak, a burst is a dash, drifting signals lean.
* **Centre spike.** A narrow spike exactly in the middle is a receiver artefact (DC offset), not a station.
* **Dotted line** marks the centre (tuned) frequency. The visible span equals the sample rate.

## 2. Worked example (works with no hardware, using the simulator)

1. Start the app: the simulator starts at 100.000 MHz.
2. In the spectrum you will see a wide hill near 100.5 MHz. Hover over it: the cursor shows the frequency.
3. **Click** the hill. A marker appears and a box reports the signal centre, level, how far it is above the noise,
   and an estimated width (~180 kHz - typical of broadcast FM).
4. **Double-click** it: the receiver re-centres on the signal.
5. Use **Quick jump to... > 2 m amateur radio (145.0 MHz)**. A narrow signal appears and disappears.
   Click it: the measurement reports "intermittent" and a width of roughly 10-20 kHz.
6. Switch to **Mode: Advanced**. Raise the gain to 50 dB: the noise floor rises and strong signals flatten and
   spread as the receiver overloads (the simulator clips like a real 8-bit ADC).
   Change **FFT size** and **Window** and watch resolution versus leakage trade off.

## 2b. Listening (new)

1. Pick **Listen as**: WFM for broadcast FM, NFM for two-way voice, AM for aircraft and AM radio.
   In Beginner mode the mode follows the band automatically; in Advanced mode it is yours to choose,
   so you can try the wrong mode on purpose and hear what that does.
2. **Double-click** any signal on the graphs to listen to it. The red marker and strip show the frequency
   and the width of the channel filter. The frequency box, +/- buttons and presets move the whole view.
3. Advanced: **Bandwidth** sets the channel filter (NFM 3-30 kHz, AM 3-15 kHz; WFM is fixed at 180 kHz),
   **Squelch** mutes the audio below a chosen channel level (shown as "Signal ... dBFS" in the status bar),
   **Audio out** picks the sound device.
4. Hiss on an empty channel is normal: it is the receiver amplifying noise.

## 3. Controls

| Control | What it does |
|---|---|
| Device | Pick a detected RTL-SDR or the simulator. Rescan re-detects (also automatic on USB hot-plug). |
| Frequency | Type `100.5`, `145 MHz`, `433920 kHz`, `1.09g`; Enter or Go. +/- step by the Step size. |
| Quick jump | Common services. Real tuners cannot reach HF (below ~24 MHz) yet. |
| Listen as / Volume / Mute | Demodulation mode and output level. Quadratic volume curve, hard-limited so it cannot clip the speakers. |
| Pause | Freezes the display so you can inspect it. Hardware keeps running. |
| Beginner / Advanced | Beginner uses automatic gain and hides technical controls. Switching never loses settings. |
| Sample rate | Width of the view. Higher = wider but heavier. 2.048 MS/s is a safe default. |
| FFT size / Window | Frequency resolution vs speed; window shape controls spectral leakage. |
| Gain | Receiver sensitivity (Advanced; real dongles snap to their supported steps). |
| Smoothing / Contrast | Averaging of the trace; dynamic range shown by the colours. |

## 4. Automatic hardware detection

1. **USB scan (SetupAPI):** looks for ~60 known SDR USB IDs (RTL-SDR variants, Airspy, HackRF, LimeSDR, BladeRF,
   SDRplay, PlutoSDR, USRP, FUNcube) and checks which Windows driver each is using. This works even when no
   driver is installed, so the app can explain *why* a dongle is unusable.
2. **Driver library:** loads `rtlsdr.dll` and asks it which devices it can open and which tuner chip is inside
   (E4000, FC0012/13, FC2580, R820T/R820T2, R828D). Tuning range is set per tuner chip.

Only RTL-SDR (RTL2832U) devices can stream today. Other families are recognised and reported as "not supported
yet". Adding one means writing a class that implements `SDRDevice` (see `src/sdr/SDRDevice.h`).

### Troubleshooting

| Symptom | Cause / fix |
|---|---|
| "rtlsdr.dll is missing" | Copy 64-bit `rtlsdr.dll` + `libusb-1.0.dll` next to the .exe. |
| "wrong driver" | Run Zadig, Options > List All Devices, select the dongle (Bulk-In, Interface 0), choose WinUSB, Replace Driver. Never replace drivers on keyboards/mice/hubs. |
| Found but cannot open | Close SDR#, rtl_tcp or other programs using it; try another USB port; avoid unpowered hubs. |
| V4 dongle misbehaves | Use the RTL-SDR Blog build of librtlsdr. |
| Nothing but noise | Normal indoors with the stock antenna: try a window, a different frequency, a longer antenna. |

## 5. Architecture

```
SDRDevice (interface)  <-  SimulatedSDR | RTLSDRDevice      (device thread produces IQ)
        |  float IQ
DspPipeline   (own thread; bounded queue so the device thread never waits)
   |--> SpectrumProcessor  (window -> FFT -> power average -> dBFS lines)  -> UI timer -> SpectrumView
   '--> Demodulator       (mixer -> channel FIR+decimate -> WFM/NFM/AM -> 48 kHz audio) -> AudioOutput (waveOut thread)
```
The UI never touches hardware directly and never blocks on USB or DSP. Threads use raw Win32 primitives
(`src/Sync.h`), so the build works with both the "win32" and "posix" MinGW thread models.

## 6. Roadmap (next)

1. HF direct sampling, more SDR backends.
2. Learning Center (lesson engine, the six starter lessons) and Radio Dictionary.
3. Signal Lab: generator, AM/FM modulation playground, FFT visualiser.
4. Experiment Journal (SQLite), Frequency Atlas with Canadian (ISED) data, HF via direct sampling.

## 7. Tests

`make test` runs two suites. The demodulator suite renders simulator stations and checks the AUDIO holds exactly the embedded tones (WFM, NFM, AM, on and off centre), FIR alias rejection (> 100 dB), channel selectivity, squelch, mute, audio length at all six sample rates, CPU cost (~5% of a core at 2.4 MS/s) and the threaded pipeline. The core suite runs: FFT vs a naive DFT, tone-in-correct-bin level check (-6.0 dBFS), the simulator + DSP pipeline
(finds the expected station, noise floor, DC spike, CPU cost ~6% of one core), retune behaviour, ADC clipping,
and frequency parsing/formatting.

## Scan FM (0.3.0)

**Scan FM** hops the receiver across 87.8-108 MHz (about 17 hops at 2.048 MS/s), averages the spectrum at each
hop, and checks the 101 broadcast channels (87.9-107.9 MHz, 200 kHz apart). A channel counts as a station when its
core is at least 8 dB above the noise floor (taken as a quiet channel's level) and it is not just the skirt of a
much stronger neighbour. Results appear in **Stations**; picking one tunes to it. If nothing is found the app
reports the noise floor and the best margin so you can tell "no antenna" from "weak area". It warns when something
is strong enough to overload the receiver. Audio is muted while scanning. This is an observation of signal
strength, not an identification of what is broadcast.

## Learn (0.4.0)

The **Learn** button opens the Learning Center: eight short lessons for complete beginners (what radio is, reading
the screen, your first station, antennas including a quarter-wave length table, gain and overload, AM vs FM, strange
things on the screen, listening responsibly in Canada) and an A-Z radio dictionary. Some lessons have a **Try it**
button that tunes the main window to a good example. The text lives in `src/learn/Content.cpp`; contributions welcome.

## Stereo FM and signal hints (0.5.0)

**Stereo** (tick box next to Mute, on by default): broadcast stereo hides a 19 kHz "pilot" tone in the signal. When the
app finds it, it rebuilds left and right and the status bar says "WFM stereo". Weak or mono stations stay mono, with
a short fade between the two so you do not hear a click. A pure-tone test gives about 38 dB of left/right separation.
**Click a signal** and the info panel now combines where it is (band), how wide it is and whether it is steady or
on-and-off into a cautious hint, for example "sits on the 200 kHz FM channel grid" or "comes and goes, which fits
short radio calls". It is a possibility, never an identification.

## Recording and playback (0.6.0)

**Record** asks where to save (the dialog starts in the folder you used last, first time `Documents\OpenRadioLab\Recordings`), then saves the whole captured band, about 2 MHz of radio at once. When you press **Stop rec** it offers to open the file's folder. It stops by itself at 1 GB (about 4 minutes at
2.048 MS/s) and also stops if you change the centre frequency, because a recording has one fixed centre.
**Open recording...** adds a saved file to the device list and plays it back in real time, looping. The screen, listening,
stereo and click-to-measure all work on it, so you can study an interesting signal as often as you like, or share
the file with someone who has no dongle. A recording cannot be retuned or scanned, only listened into.
File format (`.orliq`): a 32-byte header (magic `ORLIQ001`, sample rate, format, centre frequency, flags) followed by
8-bit unsigned I/Q pairs, the same data an RTL-SDR produces. Recordings of the simulator are flagged as simulated and
always labelled that way. Only record things you are allowed to receive, and see lesson 10 before sharing captures.

### Playback controls (0.7.0)
While a recording is open a bar appears under the listening row: **Play/Pause**, a **seek slider** (drag or click to jump), the
time, and a **Repeat** box. With Repeat off the recording plays once and stops at the end (**Play again** restarts it).

## Experiment Journal, Signal Lab, menus (0.8.0)

**Menu bar.** File: open a recording, **back to live radio**. Tools: Experiment Journal, add the current radio to the
journal, Signal Lab, Device report. Help: manual and Learning Center. While a recording plays there is also a
**Back to live radio** button in the playback bar (the dongle if one is plugged in, otherwise the simulator).

**Experiment Journal** (`%APPDATA%\OpenRadioLab\journal.txt`, plain text you can read and back up). *New entry from current
radio* captures the frequency, mode, gain, receiver, band, what the app measured, signal level and the last recording, and
you add a title, your antenna/setup and notes. Edits save automatically. *Tune to this* returns the radio to that
frequency and mode, *Export...* writes everything as a Markdown file anyone can read.

**Signal Lab** plots the level of the signal you are listening to, ten readings per second, for up to ten minutes. It
reports the strongest, average and weakest level, the background, how much of the time the signal is "on" (more than
6 dB above the background), the number of bursts and their typical length, and gives a cautious plain-language reading:
steady carrier, comes-and-goes, or empty. *Save as CSV* opens in any spreadsheet. The plot restarts when you change
frequency. Note: a perfectly steady signal and an empty channel look alike in the level alone, so Signal Lab says so
instead of guessing; compare with the noise floor on the spectrum.

## Frequency Atlas, Save audio (0.9.0)

**Tools -> Frequency Atlas** lists well-known bands sorted by frequency, with a search box. Each entry says what the band is,
what mode it is usually listened to in, and **where that information comes from**. Many have a **Tune** button that jumps
to a good example. It is a plain-language *summary*, not the official table: the authority is the Canadian Table of
Frequency Allocations (ISED). The weather entry records that **Weatheradio Canada was permanently shut down on 16 March
2026** (Environment and Climate Change Canada notice), so an empty 162 MHz channel is now normal in Canada.
Private services (cell phones, police and fire, business radio) are deliberately not described. The same table drives the
"which mode fits" suggestions and the click-a-signal hints.

**Save audio** (button next to Stereo, or File menu) saves what you hear as a normal 16-bit stereo 48 kHz WAV file, up to
1 GB. Unlike **Record**, which saves the raw radio for replay in this app, a WAV plays anywhere. It keeps going while you retune,
and muting records silence.

## SSB, CW, Antenna Workshop, spectrum snapshots (0.10.0)

**Sideband and Morse modes.** The Listen as list now has USB, LSB and CW. Amateur radio voice on HF uses single sideband
(LSB below 10 MHz, USB above), which has no carrier and sounds like Donald Duck until the frequency is within about
50 Hz of right: nudge the tuning. CW is Morse: a plain carrier switched on and off. The receiver adds a 700 Hz beat tone so you hear beeps.
Bandwidths are fixed (2.7 kHz for SSB, 400 Hz for CW). Both have an automatic gain control so weak and strong stations sound similar.
HF still needs a dongle with HF capability (direct sampling or an upconverter).

**Fine tuning.** The Step list now has 100 Hz, 500 Hz, 1 kHz, 5 kHz and 25 kHz steps. Small steps move the listening frequency inside the current view without retuning the dongle, so they are quick for nudging SSB voices into tune.

**Antenna Workshop** (Tools menu). Type a frequency or pick a band: it shows the wavelength and the lengths to cut for a whip, a dipole and ground-plane radials,
plus seven short build guides (dipole, V for weather satellites, ground plane, long wire for HF, safety).

**Spectrum snapshots** (Signal Lab). *Take snapshot* stores the current spectrum (smoothed over about a second). With two snapshots the lab lists which signals appeared,
disappeared or changed strength by 3 dB or more. Both must use the same centre frequency and sample rate. Useful for comparing two antennas, two gain settings or morning against evening.

**Signal Lab** now refreshes on its own timer and says why it is not recording (no receiver, scanning, playback paused, Pause pressed).

## HF on the RTL-SDR Blog V4 (0.10.1)

If the dongle is an RTL-SDR Blog V4 (its USB name contains "V4") and you use the Blog's driver files, the tuning range now starts at 0.5 MHz, so the
shortwave and amateur presets (40 m, 20 m, 40 m Morse) work. This is untested on real hardware by the author: please report what you see.
Other dongles still stop at about 24 MHz; direct sampling is not built in. The Atlas has Morse segments for 40 m and 20 m that select CW mode automatically.
HF needs a long wire antenna (see Antenna Workshop, guide 6) and is noisy indoors; expect to hear mostly hiss without one.

## Morse Decoder (0.11.0)

Tools, then *Morse Decoder (CW text)*. With Listen as set to CW (Morse), the window shows the letters it hears, the estimated speed in words per minute, and whether
it hears a clear tone. It measures the length of each beep and gap, so it adapts to any speed. It reads clean, steady signals well; weak, fading or overlapping
stations produce errors, and a * marks a pattern it could not read. *Copy text* puts the text on the clipboard. The decoder writes down what it hears and never says who is sending.
It is checked against synthetic keyed tones (10 to 25 wpm, with noise) but not yet against real on-air signals.

## Hover help and welcome (0.11.1)

Hold the mouse over any button, list or slider in the main window for a short plain-language explanation. The first time the program runs it shows a short welcome message
pointing at the Simulator, Scan, the Learn lessons and the Tools menu.
