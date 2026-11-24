# OpenRadio Lab - GNU Make build for MinGW-w64.
#
#   Windows (MSYS2/MinGW shell, or mingw32-make):   make
#   Linux cross-compile:                             make CROSS=x86_64-w64-mingw32-
#   Run the portable tests on any OS with g++:       make test
#
CROSS   ?=
CXX      = $(CROSS)g++
WINDRES  = $(CROSS)windres
CXXFLAGS = -std=gnu++17 -O2 -Wall -Wextra -MMD -MP    # -MMD: rebuild objects when a header they use changes
LDFLAGS  = -mwindows -static -static-libgcc -static-libstdc++
LIBS     = -lcomctl32 -lgdi32 -lsetupapi -lcfgmgr32 -lshell32 -luser32 -lole32 -lwinmm -lcomdlg32

SRC = $(wildcard src/*.cpp src/*/*.cpp)
OBJ = $(SRC:%.cpp=build/%.o)

all: OpenRadioLab.exe

OpenRadioLab.exe: $(OBJ) build/app_res.o
	$(CXX) -o $@ $^ $(LDFLAGS) $(LIBS)

build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(OBJ:.o=.d)

build/app_res.o: resources/app.rc resources/app.manifest
	@mkdir -p build
	cd resources && $(WINDRES) app.rc -O coff -o ../build/app_res.o

CORE_SRC = src/dsp/FFT.cpp src/dsp/BandScanner.cpp src/dsp/Filters.cpp src/dsp/Demodulator.cpp src/dsp/DspPipeline.cpp \
           src/dsp/SpectrumProcessor.cpp src/sdr/SimulatedSDR.cpp src/io/IqFile.cpp src/io/WavWriter.cpp src/dsp/RdsDecoder.cpp
test:
	@mkdir -p build
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_core tests/test_core.cpp $(CORE_SRC) -lpthread
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_demod tests/test_demod.cpp $(CORE_SRC) -lpthread
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_scanner tests/test_scanner.cpp $(CORE_SRC) -lpthread
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_content tests/test_content.cpp src/learn/Content.cpp
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_stereo tests/test_stereo.cpp $(CORE_SRC) -lpthread
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_iq tests/test_iq.cpp $(CORE_SRC) -lpthread
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_lab tests/test_lab.cpp src/journal/Journal.cpp src/dsp/LevelStats.cpp
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_wav tests/test_wav.cpp src/io/WavWriter.cpp -lpthread
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_hints tests/test_hints.cpp
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_morse tests/test_morse.cpp src/dsp/MorseDecoder.cpp
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_antenna tests/test_antenna.cpp src/learn/Antenna.cpp
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_rds tests/test_rds.cpp $(CORE_SRC) -lpthread
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_ssb tests/test_ssb.cpp $(CORE_SRC) -lpthread
	g++ -std=gnu++17 -O2 -Wall -Wextra -o build/test_compare tests/test_compare.cpp src/dsp/SpectrumCompare.cpp
	./build/test_core && ./build/test_demod && ./build/test_scanner && ./build/test_content && ./build/test_stereo && ./build/test_hints && ./build/test_iq && ./build/test_lab && ./build/test_wav && ./build/test_ssb && ./build/test_compare && ./build/test_antenna && ./build/test_morse && ./build/test_rds

clean:
	rm -rf build OpenRadioLab.exe
