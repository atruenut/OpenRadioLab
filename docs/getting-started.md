# OpenRadio Lab - Getting Started

*For complete beginners. No radio or computer-driver knowledge needed.*

**You can start in 10 seconds with no hardware at all.** Run `OpenRadioLab.exe` and it starts with a
built-in **simulated radio** (clearly labelled "SIMULATED"). Everything on this page about dongles, drivers
and DLL files is only needed when you want to listen to **real** radio.

## 1. The big picture

A **Software Defined Radio (SDR)** is a radio where most of the work is done by software instead of by
dedicated circuits. A tiny USB "dongle" catches radio waves from an antenna and turns them into a stream of
numbers. A program on your computer (this one) turns those numbers into pictures and sound.

```
 Antenna  ->  USB dongle  ->  USB cable  ->  Windows  ->  OpenRadio Lab
 (catches     (tuner chip +   (carries      (needs to      (draws the
  waves)       converter)      the numbers)   let us in)     spectrum, plays audio)
```

The most popular dongles are called **RTL-SDR**. They were originally cheap TV receivers; hobbyists
discovered that the chip inside, an **RTL2832U**, can hand over the raw radio data. That is why they cost
about the same as a lunch and why this project starts with them.

## 2. Why do I need extra files and a driver tool?

This is the question everyone asks, so here is the honest answer. Getting data from the dongle needs three
small helpers. Each one solves a specific problem:

| Helper | Plain-English job | Why it is needed |
|---|---|---|
| **WinUSB driver** (installed with a free tool called **Zadig**) | Lets a program talk *directly* to one specific USB device. | Windows decides which "driver" (translator) handles each USB device. For a TV-stick-style dongle it either has none or the wrong kind, and programs are not allowed to talk to the device through it. WinUSB is Microsoft's own generic driver that gives programs direct access. |
| **libusb-1.0.dll** | The USB plumbing. | It handles the low-level conversation between a program and a WinUSB device. |
| **rtlsdr.dll** (the *librtlsdr* library) | Knows the dongle's secret language. | The RTL2832U chip has its own commands: how to tune, set the sample rate, start streaming. The open-source community figured these out and packaged them in this library. OpenRadio Lab asks the library instead of re-inventing it. |

Put together:

```
OpenRadio Lab  ->  rtlsdr.dll  ->  libusb-1.0.dll  ->  WinUSB driver  ->  your dongle
 (the app)         (speaks RTL)     (speaks USB)        (Windows lets us in)
```

**What is a DLL?** A "dynamic link library" is a shared toolbox of code. A program can borrow tools from a
DLL that sits in the same folder, which is why the two `.dll` files must be placed **next to
`OpenRadioLab.exe`**.

**What does Zadig actually change?** It tells Windows: "for *this one device*, use the WinUSB driver".
It changes nothing else and you can undo it (see section 7). Always check the device name in the Zadig
dropdown before clicking, and never change a keyboard, mouse or USB hub.

**Why isn't all of this bundled in the download?** They are separate open-source projects with their own
licences and release schedules, and the right version matters: **RTL-SDR Blog V4 dongles need the RTL-SDR
Blog build of the library.** Future OpenRadio Lab releases may bundle them; for now you fetch them once.

## 3. Step by step

### Step 1 - Try the simulator first (2 minutes)
1. Start `OpenRadioLab.exe`. The banner says **SIMULATED SDR**.
2. You will see peaks in the top graph (radio stations) and a scrolling picture below (the waterfall).
3. Click a big peak: the app measures it. Double-click it to **listen** (red marker). Turn up Volume.
4. Press **Help** any time to reopen this page.

### Step 2 - Connect the antenna and dongle
1. Screw the antenna onto the dongle's connector (usually SMA). Extend a telescopic antenna fully.
2. Put the antenna near a window, away from computers and chargers (they make radio noise).
3. Plug the dongle into a USB port. Avoid unpowered hubs. Windows may say it is "setting up a device" and
   may find nothing: that is expected.

### Step 3 - Get the two DLL files
1. Download the Windows package for the **RTL-SDR Blog** drivers (see the RTL-SDR Blog Quick Start Guide at
   rtl-sdr.com). Make sure you are on the real site.
2. Open the **64-bit (x64)** folder. Copy **`rtlsdr.dll`** and **`libusb-1.0.dll`**.
3. Paste both into the same folder as `OpenRadioLab.exe`.

*Use the 64-bit files with this 64-bit program. A 32/64-bit mismatch is the most common reason the app says
the DLL "was loaded but is missing functions".*

### Step 4 - Install the WinUSB driver with Zadig
1. Download **Zadig** from its official page, zadig.akeo.ie, and run it.
2. Menu **Options > List All Devices**.
3. In the dropdown choose your dongle. It is usually called **"Bulk-In, Interface (Interface 0)"**
   (sometimes "RTL2832U"). *Pick Interface 0, not Interface 1.*
4. In the driver box on the right make sure the target says **WinUSB**.
5. Click **Replace Driver** (or **Install Driver**) and wait for "installed successfully".

### Step 5 - Start OpenRadio Lab
It detects the dongle automatically and switches from the simulator. The banner now says
**LIVE RECEIVER** and the status bar says **Live**. If not, press **Rescan**, then **Device report...**: it
tells you exactly what is missing and what to do.

### Step 6 - Your first real listen
1. Use **Quick jump to... > FM broadcast** (or type `98.5` and press Enter).
2. Look for tall peaks about 0.2 MHz apart. **Double-click** one. The red marker shows where you are listening.
3. Turn up **Volume**. If you only hear hiss, double-click a taller peak, or raise the antenna and move it
   closer to a window.

**Keeping the DLLs when you update:** OpenRadio Lab also looks for the two DLLs in a `libs` folder next to
the .exe and in `%APPDATA%\\OpenRadioLab\\libs` (paste that into the Windows Explorer address bar). Put them in
the second place once and every future version will find them, so you never have to download them again.
(They are not bundled in the download because they are separate projects with their own licences; see THIRD-PARTY.md.)

## 4. Words you will see

| Term | Meaning |
|---|---|
| **Spectrum** | A graph of signal strength at each frequency right now. |
| **Waterfall** | The spectrum over time, newest at the top; brighter means stronger. |
| **Centre frequency** | The middle of what the dongle is capturing. |
| **Listening frequency** | The one you hear (red marker). It can be anywhere inside the capture. |
| **Sample rate (MS/s)** | Millions of samples per second; also how wide a slice of spectrum you see. |
| **dBFS** | Signal level where 0 is the strongest the receiver can represent. Weaker is more negative. |
| **Noise floor** | The ragged baseline of background noise. Signals must rise above it. |
| **WFM / NFM / AM** | Ways of carrying sound: wide FM (music stations), narrow FM (voice, weather), amplitude (aircraft, AM radio). |
| **Squelch** | Mutes the audio when the signal is below a level, so you do not hear hiss. |

## 5. Tips for good reception

* The antenna matters more than the dongle. Higher, near a window, away from electronics.
* A telescopic antenna works best when its length suits the frequency (the Antenna Workshop will teach this).
* Too much **gain** makes strong signals smear across the screen; too little hides weak ones. Beginner mode
  chooses automatic gain for you.
* A narrow spike dead in the middle of the screen is not a station. It is a harmless artefact of the receiver.

## 6. Troubleshooting

| What you see | Likely cause and fix |
|---|---|
| "rtlsdr.dll is missing" | Copy `rtlsdr.dll` and `libusb-1.0.dll` next to the .exe (step 3). |
| "loaded but missing functions" | Wrong DLL or a 32/64-bit mismatch. Use the x64 files. |
| "wrong driver" / "no working driver" | Run Zadig and install WinUSB for the dongle (step 4). |
| Found but "could not be opened" | Close other SDR programs; try another USB port; unplug and replug. |
| Dongle not found at all | Different USB port or cable; avoid hubs; open the Device report. |
| Works, but only noise | Normal indoors. Try the FM preset, a window and a longer antenna. |
| No sound | Check Volume and Mute; in Advanced mode check **Audio out**; check the squelch is off. |
| Windows or antivirus warns about a DLL | Download only from official project pages and scan the files. |
| V4 dongle misbehaves | Use the RTL-SDR Blog build of the library. HF (below about 24 MHz) is not built in yet. |

## 6b. Only noise on FM (no stations)

First press **Scan FM**. The app sweeps 88-108 MHz, lists every station it finds in the **Stations** box,
and if it finds none it tells you how far above the noise the best channel was. Then work down this list:

1. **Antenna.** Is it screwed in tightly? Extend the telescopic antenna to about 75 cm and put the dongle near a window.
   No antenna = noise only. This is the most common cause.
2. **Gain.** Press *Mode: Beginner* to switch to Advanced, untick **Auto gain**, and try 30-45 dB.
   If the picture fills with wide smeared humps everywhere, you went too high; back off.
3. **Library mismatch.** An RTL-SDR Blog **V4** dongle (the scan banner says tuner *R828D*) needs a recent
   RTL-SDR Blog build of `rtlsdr.dll`; an old generic one can show noise, wrong frequencies or corrupted signals.
   Get the library from the RTL-SDR Blog's official driver package and replace the old one.
4. **Weak area / indoors.** Try a high spot outdoors. Rural FM is often only a few strong stations.
5. **Do not use direct sampling** for FM. That mode is for HF and is not what you want here.

## 7. Undoing the Zadig change

Open **Device Manager**, find the dongle (under *Universal Serial Bus devices*), right-click it,
choose **Uninstall device**, tick **Delete the driver software** if offered, then unplug and replug it.

## 8. Using radio responsibly

OpenRadio Lab is **receive-only**: it cannot transmit. Listening to broadcasts, aircraft, weather and
amateur radio is a popular hobby, but rules differ by country (in Canada they come from ISED and the
Radiocommunication Act). In general: do not share, record or act on **private** communications, do not
try to defeat encryption, and check your local rules. This is general information, not legal advice.

## 9. Licence

OpenRadio Lab is free software released under the **GNU General Public License, version 2** (see `LICENSE`).
You may use, study, change and share it under those terms. The helper libraries and Zadig are separate
projects with their own licences; see `docs/THIRD-PARTY.md`.
