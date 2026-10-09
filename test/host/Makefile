# Host (PC) tests of the firmware's portable code, built with AddressSanitizer + UBSan.
# Run from this directory:  make [T=filter]   (Linux, or inside the Docker image)
# On Windows:               powershell -File test\host\run.ps1 [filter]
# WAV decoder (WA8LMF):     make wav WAV=track.wav ARGS="1200 -v"

ROOT    := ../..
LIB     := $(ROOT)/lib/LibAPRS_ESP32
FEC     := $(ROOT)/lib/lwfec
OUT     := build

# ArduinoJson (header only): from a previous PlatformIO build, or downloaded once
AJSON_VER   := 7.4.3
AJSON_LOCAL := $(firstword $(wildcard $(ROOT)/.pio/libdeps/*/ArduinoJson/src/ArduinoJson.h))
ifeq ($(AJSON_LOCAL),)
AJSON_HDR := $(OUT)/ajson/ArduinoJson.h
else
AJSON_HDR := $(AJSON_LOCAL)
endif

CXX      ?= g++
CXXFLAGS := -std=gnu++17 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined \
            -fno-sanitize-recover=undefined -DENABLE_FX25 \
            -Ishim -I$(LIB) -I$(FEC) -I$(ROOT)/include -I$(dir $(AJSON_HDR)) -I.
LDFLAGS  := -fsanitize=address,undefined

MODEM_SRC    := $(LIB)/AX25.cpp $(LIB)/modem.cpp $(LIB)/fx25.cpp $(FEC)/rs.cpp $(FEC)/gf.cpp
FIRMWARE_SRC := $(MODEM_SRC) $(ROOT)/src/config.cpp $(ROOT)/src/digirepeater.cpp
FIRMWARE_C   := $(LIB)/CRC-CCIT.c
HARNESS_SRC  := host_stubs.cpp modem_harness.cpp wav_decode.cpp
TEST_SRC     := host_main.cpp $(HARNESS_SRC) $(wildcard test_*.cpp)
DEPS         := $(wildcard *.h) $(wildcard shim/*.h)

all: $(OUT)/host_tests
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ./$(OUT)/host_tests $(T)

wav: $(OUT)/wav_decode
	./$(OUT)/wav_decode "$(WAV)" $(ARGS)

$(OUT)/ajson/ArduinoJson.h:
	@mkdir -p $(dir $@)
	curl -sSL -o $@ https://github.com/bblanchon/ArduinoJson/releases/download/v$(AJSON_VER)/ArduinoJson-v$(AJSON_VER).h

$(OUT)/host_tests: $(AJSON_HDR) $(FIRMWARE_SRC) $(FIRMWARE_C) $(TEST_SRC) $(DEPS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) $(FIRMWARE_SRC) -x c++ $(FIRMWARE_C) -x none $(TEST_SRC) $(LDFLAGS) -o $@

# Optimized, no sanitizers: fast enough for 40-minute recordings
$(OUT)/wav_decode: $(MODEM_SRC) $(FIRMWARE_C) $(HARNESS_SRC) $(DEPS)
	@mkdir -p $(OUT)
	$(CXX) -std=gnu++17 -O2 -DENABLE_FX25 -DWAV_DECODE_MAIN -Ishim -I$(LIB) -I$(FEC) -I. \
	    $(MODEM_SRC) -x c++ $(FIRMWARE_C) -x none $(HARNESS_SRC) -o $@

clean:
	rm -rf $(OUT)

.PHONY: all clean wav
