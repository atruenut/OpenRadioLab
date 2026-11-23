// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

#include "Content.h"

namespace orl {

static const Lesson kLessons[] = {
{ "1. What is radio, really?",
"Radio is invisible energy that travels through the air as a wave, like ripples on a pond. A transmitter shakes electricity back and forth very fast in a metal rod (an antenna) and that sends ripples out. Your antenna catches a tiny bit of them.\n"
"FREQUENCY is how many ripples pass per second. It is measured in hertz (Hz). 1 kHz is a thousand per second, 1 MHz (megahertz) is a million per second, 1 GHz is a billion. An FM station called '98.1' is shaking at 98.1 million times per second.\n"
"Every kind of radio lives at its own frequencies, like houses on a street: FM music at 88-108 MHz, aircraft voice at 118-137 MHz, boats and the VHF weather channels around 156-162 MHz. Radios do not mix them up because each one only listens at one address at a time.\n"
"WAVELENGTH is the distance between ripples. Higher frequency = shorter wavelength. At 98 MHz one wave is about 3 metres long. This matters for antennas (lesson 4).\n"
"A Software Defined Radio (SDR) like your dongle does not tune one station. It grabs a whole slice of the radio world at once, about 2 MHz wide, and the computer sorts it out. That is why you can SEE many stations at the same time.",
0, 0, "" },

{ "2. Reading the screen",
"The top graph is the SPECTRUM: left to right is frequency, up is how strong the radio energy is at that frequency right now. A tall narrow peak is a transmission. The ragged low carpet is NOISE, which is always there.\n"
"The bottom graph is the WATERFALL: the same thing scrolling downward over time. Bright = strong. A steady station draws a bright vertical stripe. Something that comes and goes draws dashes.\n"
"Strength is shown in dBFS (decibels relative to full scale). It is always negative here: -20 is very strong, -60 is a weak signal, -70 to -65 is usually just the noise floor. Every 10 dB is ten times the power, so +3 dB is double.\n"
"The NOISE FLOOR is the carpet. A signal is only useful if it rises clearly above it. The number that matters is how many dB above the floor it is, called SNR (signal to noise ratio). 20+ dB sounds clear; 5-10 dB sounds hissy; under 3 dB is hard to find.\n"
"Click the graph to place a marker; the app measures the signal nearest to it. Double-click to listen to it. The red band shows what you are listening to and how wide the filter is.\n"
"The app only ever reports what it can measure (strength, width, steady or on-and-off). It does not claim to know WHO is transmitting.",
0, 0, "" },

{ "3. Your first station",
"Press 'Scan FM'. The app hops across the whole FM band, which takes a few seconds, then lists every station it can hear in the Stations box. Pick one and it tunes there and you hear it.\n"
"You can also type a frequency like 98.1 and press Enter. A bare number means MHz.\n"
"Why does FM sound good? Broadcast FM is WIDE: each station uses about 200 kHz of space, which is why the bumps on the graph are fat. That width is what carries music in good quality. Stations are spaced 200 kHz apart in Canada and the US, always on odd tenths (88.1, 88.3, 88.5 ...). If you see a peak on a .0 or .2 it is probably not a normal FM station.\n"
"Only noise between stations is NORMAL: it is the receiver turning up the volume on nothing. Go back to a station and the hiss should vanish.\n"
"If the scan finds nothing, read lessons 4 and 5. The antenna is the usual reason.\n"
"Curious what else is out there? Tools -> Frequency Atlas lists well-known bands (FM, airband, marine, amateur and more) with a plain-language note on each and a button to jump to an example. Tools -> File -> Save what I hear as audio saves a station as a normal sound file.",
98.1e6, 0, "Go to the FM band (98.1 MHz)" },

{ "4. Antennas for beginners",
"The antenna matters more than the dongle. An antenna is a piece of metal that is the right size to 'resonate' with the waves, like pushing a swing at the right moment. The best simple size is a QUARTER of the wavelength.\n"
"Quick rule: quarter-wave length in centimetres = 7500 divided by the frequency in MHz.\n"
"  FM broadcast, 98 MHz:   about 77 cm\n"
"  Aircraft, 123 MHz:       about 61 cm\n"
"  Amateur 2 m, 145 MHz:    about 52 cm\n"
"  VHF marine/weather, 162.5 MHz: about 46 cm\n"
"  Remote controls, 433 MHz: about 17 cm\n"
"So a telescopic antenna pulled out to about 75 cm is nearly right for FM, and a paperclip is far too short, which is why it only catches the strongest stations. Still, it proves the idea: more metal in the right shape means more signal.\n"
"Easy upgrades, cheapest first: (1) straighten a paperclip or a 75 cm piece of wire and connect it to the antenna centre pin; (2) a telescopic antenna; (3) move it near a window, or higher up; (4) keep it away from computers, chargers and the USB cable itself, which make their own radio noise; (5) a USB extension cable so the dongle can sit by the window.\n"
"Outdoors and higher is always better. Never put an antenna near power lines, and never use it in a thunderstorm.\n"
"This is a receive-only program. It cannot transmit, and an antenna on the dongle will not either.",
0, 0, "" },

{ "5. Gain: how loud to turn the receiver up",
"GAIN is amplification inside the dongle before the computer sees the signal. Too little and weak stations stay buried in the noise. Too much and the receiver is OVERLOADED: strong stations smear into fat humps and ghost copies of themselves show up at frequencies where nothing is really transmitting.\n"
"Beginner mode uses automatic gain, which is a sensible choice. If you want to experiment, press 'Mode: Beginner' to switch to Advanced, untick 'Auto gain', and move the Gain slider while watching the graph. Aim for the strongest stations to peak around -30 to -20 dBFS without the whole band lifting up.\n"
"A good experiment: find a strong station, raise the gain slowly, and watch the noise carpet rise and then watch new ghost signals appear. That is overload, and it is why 'more' is not always better.\n"
"If the Scan FM report warns of overload, lower the gain.",
0, 0, "" },

{ "6. AM, FM and why modes matter",
"The signal is just a wave. To get sound out of it you need to know how the sound was put in, called the MODE.\n"
"AM (amplitude modulation) puts sound in the wave's strength. Used by AM radio (530-1710 kHz) and aircraft voice (118-137 MHz).\n"
"FM (frequency modulation) puts sound in tiny changes of the wave's frequency. WFM (wide FM) is broadcast music. NFM (narrow FM) is the same idea but skinnier, used for two-way voice, boats and amateur radio.\n"
"Try it: go to a station and change 'Listen as' to the wrong mode. Wide FM played as AM is faint and buzzy. Voice played as the wrong width sounds thin or distorted. Beginner mode picks the usual mode for each band; Advanced mode leaves it to you.\n"
"Also try the Bandwidth slider in Advanced mode on NFM. Too narrow muffles the voice, too wide lets in hiss and neighbours.",
123.0e6, 2, "Go to the airband (123.0 MHz, AM)" },

{ "7. Strange things on the screen",
"A NARROW SPIKE IN THE CENTRE of the screen that never moves is not a station. It is a harmless artefact of how these receivers work (DC offset). The app marks it and ignores it when measuring.\n"
"GHOSTS: weaker copies of a strong station at the wrong frequency. They usually mean overload (lower the gain) or the receiver's mirror frequency. If it vanishes when you reduce gain it was a ghost.\n"
"A COMB of evenly spaced thin spikes is usually electrical interference from a computer, LED lights or a charger. Move the antenna away from them.\n"
"A signal that appears for a second and disappears is not broken; many transmitters (remote controls, sensors, aircraft) only talk now and then. That is called INTERMITTENT, and the waterfall is the best place to catch it.\n"
"The app says 'it looks like' and gives measurements. Identifying who sent what is a skill you build over time from the lessons and the Frequency Atlas coming later.",
0, 0, "" },

{ "8. Keeping a capture",
"A radio signal is gone the moment it passes, unless you record it. Press 'Record', choose where to save the file, and OpenRadio Lab saves everything the dongle is seeing, about 2 MHz of the radio world at once. This is the raw radio, not just the sound, which is why a minute is roughly 240 MB. It stops by itself at 1 GB. When you press 'Stop rec' the app offers to show you the file in its folder.\\n"
"Later, press 'Open recording...' to play it back. It behaves like a receiver that is stuck on one slice of the band: you can click, measure, and listen to any signal inside it, as often as you like, without needing the dongle. A bar appears with Play/Pause, a slider to jump around, the time, and a Repeat box (untick it to play once and stop at the end).\\n"
"Good uses: catching a rare signal so you can study it, comparing the same band at different times of day, and sharing a puzzle with a friend.\\n"
"A recording cannot be retuned, because the radio waves it holds were captured at one centre frequency. Think of it as a window the recording looked through.",
0, 0, "" },

{ "9. Keeping notes, and watching a signal over time",
"Real radio skill comes from noticing patterns, and patterns need notes. Open Tools -> Experiment Journal and press 'New entry from current radio'. The journal fills in the frequency, mode, gain and what the app measured; you add what you heard and what your antenna was. Next month you can press 'Tune to this' and be right back there.\\n"
"Tools -> Signal Lab draws the strength of the signal you are listening to, over minutes. A steady broadcast station is a flat line. A push-to-talk radio is a series of bursts. A signal fading in and out wanders. The lab counts the bursts and tells you how often the signal is 'on'.\\n"
"Try it: listen to a strong FM station for a minute, then a quiet frequency, and compare the two traces. Then compare the same station at different times of day.\\n"
"Keep one thing in mind: the lab describes how the level behaves. It cannot tell you who is transmitting; that is a conclusion you draw from several clues, and the journal is where you collect them.",
0, 0, "" },

{ "10. Listening responsibly (Canada)",
"Listening to broadcast radio (AM, FM, aircraft, marine and amateur radio) is a normal, well-loved hobby and this program is receive-only.\n"
"In Canada, radio is regulated by Innovation, Science and Economic Development Canada (ISED). The general idea in the rules is that radio meant for the public (broadcast, aviation and marine information) is fine to listen to, while private communications (telephone calls, private messages) should not be listened to, written down, used or passed on. Do not decode or act on anything private you happen to hear.\n"
"This is general information and not legal advice. If you are unsure, check the ISED rules for your situation.\n"
"Courtesy: never use what you hear to harm, track or bother people. Never interfere with aircraft or emergency radio. This program cannot transmit, and it will stay that way.",
0, 0, "" },

{ "11. Sideband voice and Morse code",
"FM and AM are easy to hear: tune and listen. Amateur (ham) radio on the shortwave bands mostly uses SINGLE SIDEBAND (SSB). An AM signal is a carrier in the middle with a copy of the sound on each side (the sidebands). SSB throws away the carrier and one sideband, so the same voice takes about a quarter of the space and travels further on the same power.\n"
"The catch: with no carrier, the receiver has to put one back. If the frequency is a little off, voices sound too high or too low, like Donald Duck. That is normal. Use the fine Step sizes (100 Hz, 500 Hz) and the up and down buttons to nudge until the voice sounds natural.\n"
"UPPER sideband (USB) is the habit above 10 MHz, LOWER sideband (LSB) below 10 MHz. Pick the wrong one and the voice is garbled, so if it sounds wrong, switch.\n"
"CW is Morse code: a plain carrier switched on and off. You hear it as beeps because the receiver adds its own 700 Hz tone. Tune until the pitch of the beeps is comfortable.\n"
"Most of these signals live on HF (below 30 MHz), which an ordinary RTL-SDR cannot tune without direct sampling or an upconverter. The Frequency Atlas shows which bands use which mode. The program only decodes the sound. It does not name or identify stations.",
0, 0, "" },

{ "12. Better antennas and comparing results",
"Open Tools, then Antenna Workshop. Type the frequency you care about and it gives the lengths to cut for a whip or a dipole, with step-by-step guides. This is the cheapest way to improve reception.\n"
"To find out if a change really helped, use the Signal Lab. Press 'Take snapshot' with the old antenna, change the antenna (or move it, or change the gain), wait a few seconds, and press 'Take snapshot' again. The lab lists which signals appeared, disappeared or changed strength in dB.\n"
"Keep the conditions fair: same place, same gain, same time of day if you can. Weak signals fade and drift on their own, so a difference of 1 or 2 dB means nothing. A change of 5 dB or more is real.\n"
"Write what you did in the Experiment Journal so you can repeat it. Radio is an experimental hobby: change one thing at a time.",
0, 0, "" },
};

static const Term kTerms[] = {
{ "Amplitude", "How strong a wave is. AM changes it to carry sound." },
{ "Antenna", "A piece of metal that turns radio waves into a tiny electrical signal (or the reverse). Its size should suit the frequency." },
{ "Artefact", "Something that appears on the screen because of the receiver, not because of a real transmission (for example the centre spike)." },
{ "Bandwidth", "How much frequency space a signal or filter takes up. FM broadcast ~200 kHz, voice radio ~12 kHz." },
{ "dB (decibel)", "A way to compare strengths. +10 dB = 10 times the power, +3 dB = double." },
{ "dBFS", "Decibels relative to 'full scale': the loudest signal the receiver can represent. Always zero or negative." },
{ "Demodulation", "Turning a radio wave back into sound. The mode (AM, FM...) says how." },
{ "Dongle", "The small USB receiver. Here, an RTL-SDR." },
{ "FFT", "The maths that turns raw samples into the spectrum graph. Bigger FFT = finer detail but slower updates." },
{ "Frequency", "Waves per second. Measured in Hz, kHz, MHz, GHz." },
{ "Gain", "Amplification inside the receiver. Too little buries weak signals, too much overloads." },
{ "Hertz (Hz)", "One wave per second. 1 MHz = one million." },
{ "I/Q", "The two numbers per sample the dongle sends. Together they describe the wave completely so software can tune anywhere in the slice." },
{ "Intermittent", "A signal that comes and goes rather than staying on." },
{ "Marker", "The spot you clicked. The app measures the signal nearest to it." },
{ "Mode", "How sound is carried: AM, NFM, WFM, SSB (USB/LSB), CW and others." },
{ "Noise floor", "The always-present background level. Signals must rise above it to be heard." },
{ "Overload", "When a signal is too strong for the receiver to handle cleanly, producing ghosts and smear." },
{ "Receive-only", "Can listen but never transmit. This program is receive-only." },
{ "Sample rate", "How many measurements per second the dongle makes. 2.048 MS/s means about 2 MHz of the radio world is visible at once." },
{ "SDR", "Software Defined Radio: a receiver where software, not fixed electronics, does the tuning and decoding." },
{ "Sideband (SSB)", "A way of sending voice that uses half the space of AM by dropping the carrier and one side. Upper sideband (USB) is the convention above 10 MHz, lower (LSB) below. Tune a little off and voices sound like Donald Duck: that is normal, nudge the frequency until it sounds natural." },
{ "CW (Morse)", "Morse code sent by switching a plain carrier on and off. A receiver has to add its own pitch to turn the carrier into a beep you can hear. Here that pitch is 700 Hz." },
{ "Spectrum", "A graph of signal strength against frequency." },
{ "Squelch", "A gate that silences audio when no signal is strong enough, so you do not hear hiss between transmissions." },
{ "SNR", "Signal to noise ratio: how far a signal stands above the noise, in dB. More is clearer." },
{ "Tuner", "The chip in the dongle that selects the frequency (for example R820T2). The program reports which one you have." },
{ "Waterfall", "The scrolling picture of the spectrum over time. Brighter is stronger." },
{ "Wavelength", "Distance between waves. About 300 divided by the frequency in MHz, in metres." },
{ "WinUSB", "The Windows driver that lets programs talk to the dongle. Installed with Zadig." },
};

const Lesson* lessons(size_t& count) { count = sizeof kLessons / sizeof kLessons[0]; return kLessons; }
const Term* dictionary(size_t& count) { count = sizeof kTerms / sizeof kTerms[0]; return kTerms; }

} // namespace orl
