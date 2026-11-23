@echo off
rem ------------------------------------------------------------------
rem  OpenRadio Lab - one-click build for Windows (MinGW-w64 / MSYS2).
rem  Needs g++ and windres on PATH (e.g. C:\msys64\mingw64\bin).
rem ------------------------------------------------------------------
setlocal
where g++ >nul 2>nul || (echo [ERROR] g++ not found on PATH. Install MSYS2 MinGW-w64 and add its bin folder to PATH. & exit /b 1)
where windres >nul 2>nul || (echo [ERROR] windres not found on PATH. & exit /b 1)

if not exist build mkdir build
if not exist build\src mkdir build\src
if not exist build\src\ui mkdir build\src\ui
if not exist build\src\sdr mkdir build\src\sdr
if not exist build\src\dsp mkdir build\src\dsp
if not exist build\src\audio mkdir build\src\audio

set CXXFLAGS=-std=gnu++17 -O2 -Wall -Wextra
set FILES=src\main.cpp src\ui\SpectrumView.cpp src\ui\LearnWindow.cpp src\learn\Content.cpp src\io\IqFile.cpp src\io\WavWriter.cpp src\ui\LabWindows.cpp src\ui\AtlasWindow.cpp src\ui\AntennaWindow.cpp src\ui\MorseWindow.cpp src\dsp\MorseDecoder.cpp src\learn\Antenna.cpp src\dsp\SpectrumCompare.cpp src\journal\Journal.cpp src\dsp\LevelStats.cpp src\audio\AudioOutput.cpp src\sdr\SimulatedSDR.cpp src\sdr\RTLSDRDevice.cpp src\sdr\DeviceDetect.cpp src\dsp\FFT.cpp src\dsp\Filters.cpp src\dsp\BandScanner.cpp src\dsp\Demodulator.cpp src\dsp\DspPipeline.cpp src\dsp\SpectrumProcessor.cpp

echo Compiling resources...
pushd resources
windres app.rc -O coff -o ..\build\app_res.o || (popd & exit /b 1)
popd

set OBJS=build\app_res.o
for %%F in (%FILES%) do (
  echo Compiling %%F
  g++ %CXXFLAGS% -c %%F -o build\%%~dpnF.o || exit /b 1
  call set OBJS=%%OBJS%% build\%%~dpnF.o
)
echo Linking...
g++ -o OpenRadioLab.exe %OBJS% -mwindows -static -static-libgcc -static-libstdc++ -lcomctl32 -lgdi32 -lsetupapi -lcfgmgr32 -lshell32 -luser32 -lole32 -lwinmm -lcomdlg32 || exit /b 1
echo.
echo Built OpenRadioLab.exe
endlocal
