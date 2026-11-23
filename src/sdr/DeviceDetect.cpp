// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "DeviceDetect.h"

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace orl {

namespace {

struct Known { uint16_t vid, pid; const char* name; SdrFamily fam; };

// RTL2832U-based dongles: mirrors the table librtlsdr itself uses to recognise devices.
const Known kKnown[] = {
    {0x0bda, 0x2832, "RTL-SDR (Generic RTL2832U)", SdrFamily::RTLSDR},
    {0x0bda, 0x2838, "RTL-SDR (Generic RTL2832U OEM)", SdrFamily::RTLSDR},
    {0x0413, 0x6680, "RTL-SDR (DigitalNow Quad DVB-T PCI-E)", SdrFamily::RTLSDR},
    {0x0413, 0x6f0f, "RTL-SDR (Leadtek WinFast DTV2000 DS)", SdrFamily::RTLSDR},
    {0x0458, 0x707f, "RTL-SDR (Genius TVGo DVB-T03)", SdrFamily::RTLSDR},
    {0x0ccd, 0x00a9, "RTL-SDR (Terratec Cinergy T Stick Black)", SdrFamily::RTLSDR},
    {0x0ccd, 0x00b3, "RTL-SDR (Terratec NOXON DAB/DAB+ USB)", SdrFamily::RTLSDR},
    {0x0ccd, 0x00b4, "RTL-SDR (Terratec Deutschlandradio DAB Stick)", SdrFamily::RTLSDR},
    {0x0ccd, 0x00b5, "RTL-SDR (Terratec NOXON DAB Stick - Radio Energy)", SdrFamily::RTLSDR},
    {0x0ccd, 0x00b7, "RTL-SDR (Terratec Media Broadcast DAB Stick)", SdrFamily::RTLSDR},
    {0x0ccd, 0x00b8, "RTL-SDR (Terratec BR DAB Stick)", SdrFamily::RTLSDR},
    {0x0ccd, 0x00b9, "RTL-SDR (Terratec WDR DAB Stick)", SdrFamily::RTLSDR},
    {0x0ccd, 0x00c0, "RTL-SDR (Terratec MuellerVerlag DAB Stick)", SdrFamily::RTLSDR},
    {0x0ccd, 0x00c6, "RTL-SDR (Terratec Fraunhofer DAB Stick)", SdrFamily::RTLSDR},
    {0x0ccd, 0x00d3, "RTL-SDR (Terratec Cinergy T Stick RC)", SdrFamily::RTLSDR},
    {0x0ccd, 0x00d7, "RTL-SDR (Terratec T Stick PLUS)", SdrFamily::RTLSDR},
    {0x0ccd, 0x00e0, "RTL-SDR (Terratec NOXON DAB/DAB+ USB v2)", SdrFamily::RTLSDR},
    {0x1554, 0x5020, "RTL-SDR (PixelView PV-DT235U)", SdrFamily::RTLSDR},
    {0x15f4, 0x0131, "RTL-SDR (Astrometa DVB-T/DVB-T2)", SdrFamily::RTLSDR},
    {0x15f4, 0x0133, "RTL-SDR (HanfTek DAB+FM+DVB-T)", SdrFamily::RTLSDR},
    {0x185b, 0x0620, "RTL-SDR (Compro Videomate U620F)", SdrFamily::RTLSDR},
    {0x185b, 0x0650, "RTL-SDR (Compro Videomate U650F)", SdrFamily::RTLSDR},
    {0x185b, 0x0680, "RTL-SDR (Compro Videomate U680F)", SdrFamily::RTLSDR},
    {0x1b80, 0xd393, "RTL-SDR (GIGABYTE GT-U7300)", SdrFamily::RTLSDR},
    {0x1b80, 0xd394, "RTL-SDR (DIKOM USB-DVBT HD)", SdrFamily::RTLSDR},
    {0x1b80, 0xd395, "RTL-SDR (Peak 102569AGPK)", SdrFamily::RTLSDR},
    {0x1b80, 0xd397, "RTL-SDR (KWorld KW-UB450-T USB DVB-T Pico TV)", SdrFamily::RTLSDR},
    {0x1b80, 0xd398, "RTL-SDR (Zaapa ZT-MINDVBZP)", SdrFamily::RTLSDR},
    {0x1b80, 0xd39d, "RTL-SDR (SVEON STV20 DVB-T USB & FM)", SdrFamily::RTLSDR},
    {0x1b80, 0xd3a4, "RTL-SDR (Twintech UT-40)", SdrFamily::RTLSDR},
    {0x1b80, 0xd3a8, "RTL-SDR (ASUS U3100MINI_PLUS_V2)", SdrFamily::RTLSDR},
    {0x1b80, 0xd3af, "RTL-SDR (SVEON STV27 DVB-T USB & FM)", SdrFamily::RTLSDR},
    {0x1b80, 0xd3b0, "RTL-SDR (SVEON STV21 DVB-T USB & FM)", SdrFamily::RTLSDR},
    {0x1d19, 0x1101, "RTL-SDR (Dexatek DK DVB-T Dongle)", SdrFamily::RTLSDR},
    {0x1d19, 0x1102, "RTL-SDR (Dexatek DK DVB-T Dongle, rev 2)", SdrFamily::RTLSDR},
    {0x1d19, 0x1103, "RTL-SDR (Dexatek Technology Ltd. DK 5217 DVB-T Dongle)", SdrFamily::RTLSDR},
    {0x1d19, 0x1104, "RTL-SDR (MSI DigiVox Micro HD)", SdrFamily::RTLSDR},

    // Other SDR families: detected and identified, but no streaming backend yet.
    {0x1d50, 0x60a1, "Airspy", SdrFamily::Airspy},
    {0x03eb, 0x800c, "Airspy HF+", SdrFamily::Airspy},
    {0x1d50, 0x6089, "HackRF One", SdrFamily::HackRF},
    {0x1d50, 0x604b, "HackRF Jawbreaker", SdrFamily::HackRF},
    {0x1d50, 0xcc15, "rad1o badge (HackRF)", SdrFamily::HackRF},
    {0x1d50, 0x6108, "LimeSDR", SdrFamily::LimeSDR},
    {0x04b4, 0x00f1, "BladeRF (Cypress bootloader)", SdrFamily::BladeRF},
    {0x2cf0, 0x5246, "BladeRF", SdrFamily::BladeRF},
    {0x2cf0, 0x5250, "BladeRF 2.0", SdrFamily::BladeRF},
    {0x1df7, 0x2500, "SDRplay RSP1", SdrFamily::SDRplay},
    {0x1df7, 0x3000, "SDRplay RSP1A/RSP2", SdrFamily::SDRplay},
    {0x1df7, 0x3010, "SDRplay RSP2", SdrFamily::SDRplay},
    {0x1df7, 0x3020, "SDRplay RSPduo", SdrFamily::SDRplay},
    {0x1df7, 0x3030, "SDRplay RSPdx", SdrFamily::SDRplay},
    {0x1df7, 0x3050, "SDRplay RSP1B", SdrFamily::SDRplay},
    {0x0456, 0xb673, "ADALM-PLUTO", SdrFamily::PlutoSDR},
    {0x2500, 0x0020, "Ettus USRP B200", SdrFamily::USRP},
    {0x2500, 0x0021, "Ettus USRP B210", SdrFamily::USRP},
    {0x04d8, 0xfb31, "FUNcube Dongle Pro+", SdrFamily::Funcube},
};

const Known* lookup(uint16_t vid, uint16_t pid) {
    for (const auto& k : kKnown) if (k.vid == vid && k.pid == pid) return &k;
    return nullptr;
}

std::string getProp(HDEVINFO set, SP_DEVINFO_DATA& d, DWORD prop) {
    DWORD type = 0, need = 0;
    SetupDiGetDeviceRegistryPropertyA(set, &d, prop, &type, nullptr, 0, &need);
    if (need == 0) return "";
    std::vector<char> buf(need + 2, 0);
    if (!SetupDiGetDeviceRegistryPropertyA(set, &d, prop, &type, (PBYTE)buf.data(), (DWORD)buf.size(), &need))
        return "";
    return std::string(buf.data());     // for REG_MULTI_SZ this is the first (most specific) entry
}

bool parseVidPid(const std::string& hwid, uint16_t& vid, uint16_t& pid) {
    const size_t v = hwid.find("VID_"), p = hwid.find("PID_");
    if (v == std::string::npos || p == std::string::npos) return false;
    vid = (uint16_t)std::strtoul(hwid.substr(v + 4, 4).c_str(), nullptr, 16);
    pid = (uint16_t)std::strtoul(hwid.substr(p + 4, 4).c_str(), nullptr, 16);
    return true;
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Drivers librtlsdr (via libusb) can talk to.
bool isUsableService(const std::string& svcLower) {
    return svcLower == "winusb" || svcLower == "libusbk" || svcLower == "libusb0" ||
           svcLower == "libusb0" || svcLower == "libusb-win32";
}

} // namespace

const char* familyName(SdrFamily f) {
    switch (f) {
        case SdrFamily::RTLSDR:   return "RTL-SDR";
        case SdrFamily::Airspy:   return "Airspy";
        case SdrFamily::HackRF:   return "HackRF";
        case SdrFamily::LimeSDR:  return "LimeSDR";
        case SdrFamily::BladeRF:  return "BladeRF";
        case SdrFamily::SDRplay:  return "SDRplay";
        case SdrFamily::PlutoSDR: return "PlutoSDR";
        case SdrFamily::USRP:     return "USRP";
        case SdrFamily::Funcube:  return "FUNcube";
        default:                  return "SDR";
    }
}

std::vector<DetectedDevice> detectUsbSdrs() {
    std::vector<DetectedDevice> result;
    std::vector<bool> goodDriver;

    HDEVINFO set = SetupDiGetClassDevsA(nullptr, "USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) return result;

    SP_DEVINFO_DATA d;
    d.cbSize = sizeof(d);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &d); ++i) {
        const std::string hwid = getProp(set, d, SPDRP_HARDWAREID);
        uint16_t vid, pid;
        if (!parseVidPid(hwid, vid, pid)) continue;
        const Known* k = lookup(vid, pid);
        if (!k) continue;

        const bool isInterface = hwid.find("&MI_") != std::string::npos;
        const std::string svc = getProp(set, d, SPDRP_SERVICE);
        ULONG status = 0, problem = 0;
        CM_Get_DevNode_Status(&status, &problem, d.DevInst, 0);
        const bool hasProblem = (status & DN_HAS_PROBLEM) != 0;

        // Composite devices show up as a parent plus one child per interface, and the
        // useful driver (WinUSB) is often bound to a child, so merge by VID/PID.
        size_t idx = result.size();
        for (size_t j = 0; j < result.size(); ++j)
            if (result[j].vid == vid && result[j].pid == pid) { idx = j; break; }
        if (idx == result.size()) {
            DetectedDevice dd;
            dd.vid = vid; dd.pid = pid; dd.name = k->name; dd.family = k->fam;
            dd.count = 0;
            dd.streamingSupported = (k->fam == SdrFamily::RTLSDR);
            result.push_back(dd);
            goodDriver.push_back(false);
        }
        DetectedDevice& e = result[idx];
        if (!isInterface) ++e.count;
        if (!svc.empty() && e.services.find(svc) == std::string::npos)
            e.services += (e.services.empty() ? "" : ", ") + svc;
        if (isUsableService(lower(svc))) goodDriver[idx] = true;
        if (hasProblem && e.driver == DriverState::Unknown) e.driver = DriverState::NotInstalled;
    }
    SetupDiDestroyDeviceInfoList(set);

    for (size_t j = 0; j < result.size(); ++j) {
        DetectedDevice& e = result[j];
        if (e.count < 1) e.count = 1;
        if (goodDriver[j]) e.driver = DriverState::Ok;
        else if (e.driver != DriverState::NotInstalled && !e.services.empty()) e.driver = DriverState::WrongDriver;
    }
    return result;
}

std::string buildDeviceReport(const std::vector<DetectedDevice>& usb, const RtlProbe& rtl) {
    std::string r;
    r += "OpenRadio Lab - hardware report\r\n";
    r += "================================\r\n\r\n";

    r += "USB scan (Windows device list)\r\n";
    if (usb.empty()) {
        r += "  No known SDR hardware found on the USB bus.\r\n"
             "  - Is the dongle plugged in (try a different USB port, avoid unpowered hubs)?\r\n"
             "  - Some cheap dongles use an unusual USB ID; if it shows in Device Manager but not\r\n"
             "    here, tell us its Hardware ID (Device Manager > Details) so it can be added.\r\n";
    }
    for (const auto& d : usb) {
        char head[256];
        std::snprintf(head, sizeof head, "  * %s  [USB %04X:%04X]%s\r\n", d.name.c_str(), d.vid, d.pid,
                      d.count > 1 ? "  (x multiple)" : "");
        r += head;
        r += "      Windows driver: " + (d.services.empty() ? std::string("(none)") : d.services) + "\r\n";
        switch (d.driver) {
            case DriverState::Ok:
                r += "      Driver status: OK (a libusb-compatible driver is bound).\r\n"; break;
            case DriverState::WrongDriver:
                r += "      Driver status: PROBLEM - Windows is using a different driver, so SDR software cannot open it.\r\n"
                     "      Fix: run Zadig (https://zadig.akeo.ie), Options > List All Devices, choose\r\n"
                     "      'Bulk-In, Interface (Interface 0)' (or the device by name), pick WinUSB, click Replace Driver.\r\n"
                     "      Then press Rescan. Only replace the driver on the dongle - never on keyboards, mice or hubs.\r\n";
                break;
            case DriverState::NotInstalled:
                r += "      Driver status: PROBLEM - Windows reports the device has no working driver.\r\n"
                     "      Fix: install WinUSB with Zadig (https://zadig.akeo.ie) as described above, then Rescan.\r\n";
                break;
            default:
                r += "      Driver status: could not be determined.\r\n"; break;
        }
        if (!d.streamingSupported)
            r += std::string("      Support: ") + familyName(d.family) +
                 " devices are recognised, but OpenRadio Lab has no streaming backend for them yet\r\n"
                 "      (the simulated SDR is available meanwhile).\r\n";
    }

    r += "\r\nRTL-SDR driver library (rtlsdr.dll)\r\n";
    if (!rtl.libraryFound) {
        r += "  Not usable: " + rtl.error + "\r\n"
             "  Fix: download a Windows build of librtlsdr (the 'RTL-SDR Blog' package is recommended; it is\r\n"
             "  REQUIRED for RTL-SDR Blog V4 dongles) and copy rtlsdr.dll and libusb-1.0.dll into the same\r\n"
             "  folder as OpenRadioLab.exe (or in %APPDATA%\\OpenRadioLab\\libs, which survives updates). Use the 64-bit DLLs with a 64-bit build of this program.\r\n";
    } else {
        r += "  Loaded: " + rtl.libraryPath + "\r\n";
        if (rtl.devices.empty()) {
            r += "  The library works but reports 0 devices. If the USB scan above shows an RTL-SDR,\r\n"
                 "  the driver is most likely not WinUSB (see Zadig note), or another program holds the dongle.\r\n";
        }
        for (const auto& d : rtl.devices) {
            r += "  * #" + std::to_string(d.index) + ": " + d.name;
            if (!d.manufacturer.empty() || !d.product.empty())
                r += "  (" + d.manufacturer + " / " + d.product + ")";
            if (!d.serial.empty()) r += "  serial " + d.serial;
            r += "\r\n";
        }
    }
    return r;
}

} // namespace orl
