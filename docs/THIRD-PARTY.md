# Third-party software

OpenRadio Lab itself is licensed under **GPL-2.0-only** (see `LICENSE`). It contains its own FFT, filters
and demodulators and does not include code from the projects below.

| Component | Used for | How it is used | Licence (check the files shipped with the version you download) |
|---|---|---|---|
| **librtlsdr** (`rtlsdr.dll`) | Talking to RTL2832U dongles | Loaded at runtime with `LoadLibrary`; not linked or bundled | GPL-2.0 family |
| **libusb** (`libusb-1.0.dll`) | USB access used by librtlsdr | Loaded indirectly by `rtlsdr.dll`; not bundled | LGPL-2.1 family |
| **Zadig** | Installing the WinUSB driver | Separate tool the user runs; not bundled | See the Zadig project |
| **WinUSB** | Windows' generic USB driver | Part of Windows | Microsoft |

## Redistributing the DLLs with OpenRadio Lab

Because OpenRadio Lab is GPL-2.0, shipping `rtlsdr.dll` and `libusb-1.0.dll` next to it is compatible in
principle. If you do, you must also:

1. include each library's licence text,
2. provide (or point to) the exact source for the DLL versions you ship,
3. keep the user able to replace the DLLs with their own build (they are loaded at runtime, so they can).

Use the RTL-SDR Blog build if you want RTL-SDR Blog V4 support. This note is a practical checklist, not
legal advice.
