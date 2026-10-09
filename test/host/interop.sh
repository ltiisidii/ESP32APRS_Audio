#!/bin/sh
# Interoperability with Dire Wolf (independent, widely used software TNC), in both directions:
#   RX: Dire Wolf's gen_packets writes 100 frames with increasing noise; they are decoded by this
#       firmware's demodulator (wav_decode) and, as the reference, by Dire Wolf's own atest.
#   TX: this firmware's modulator writes 100 frames (wav_decode --tx); Dire Wolf's atest decodes them.
# Runs inside the gcc:13 Docker image (installs direwolf). Results: build/interop.csv
#   powershell -File test\host\interop.ps1
set -e
cd "$(dirname "$0")"
command -v atest >/dev/null 2>&1 || { apt-get -qq update >/dev/null && apt-get -qq install -y direwolf >/dev/null; }
make -s build/wav_decode >/dev/null
mkdir -p build/interop
W=build/interop
N=100

ours() { ./build/wav_decode "$@" | sed -n 's/.*: \([0-9]*\) frames decoded/\1/p'; }
dw()   { atest "$@" 2>/dev/null | sed -n 's/^\([0-9]*\) packets decoded.*/\1/p' | tail -1; }
# V.23 (1300/2100 Hz): atest only knows the standard tones, so run the full direwolf with a custom
# MODEM line, feeding the WAV's samples on stdin. Args: wav-file sample-rate
dwv23() {
    printf 'ADEVICE stdin null\nARATE %s\nCHANNEL 0\nMODEM 1200 1300:2100\n' "$2" > $W/v23.conf
    tail -c +45 "$1" | timeout 300 direwolf -c $W/v23.conf -t 0 -q hd - 2>/dev/null | grep -c '^\[0' || true
}

echo "mode,direction,frames,direwolf,firmware" > build/interop.csv
row() { printf "%-14s %-26s %3s sent | Dire Wolf %3s | firmware %3s\n" "$1" "$2" "$3" "$4" "$5"; echo "$1,$2,$3,$4,$5" >> build/interop.csv; }

# --- RX: Dire Wolf transmits (100 frames, noise increasing frame by frame)
gen_packets -B 1200 -n $N -o $W/dw1200.wav >/dev/null 2>&1
row "1200 AFSK" "RX (Dire Wolf -> firmware)" $N "$(dw -B 1200 $W/dw1200.wav)" "$(ours $W/dw1200.wav 1200 flat)"
gen_packets -B 300 -n $N -o $W/dw300.wav >/dev/null 2>&1
row "300 AFSK" "RX (Dire Wolf -> firmware)" $N "$(dw -B 300 $W/dw300.wav)" "$(ours $W/dw300.wav 300 flat)"
gen_packets -B 9600 -n $N -o $W/dw9600.wav >/dev/null 2>&1
row "9600 G3RUH" "RX (Dire Wolf -> firmware)" $N "$(dw -B 9600 $W/dw9600.wav)" "$(ours $W/dw9600.wav 9600)"
gen_packets -B 1200 -X 32 -n $N -o $W/dwfx25.wav >/dev/null 2>&1
row "1200 FX.25" "RX (Dire Wolf -> firmware)" $N "$(dw -B 1200 $W/dwfx25.wav)" "$(ours $W/dwfx25.wav 1200 flat)"
gen_packets -b 1200 -m 1300 -s 2100 -n $N -o $W/dwv23.wav >/dev/null 2>&1
row "1200 V.23" "RX (Dire Wolf -> firmware)" $N "$(dwv23 $W/dwv23.wav 44100)" "$(ours $W/dwv23.wav v23 flat)"

# --- TX: the firmware transmits (100 clean frames), Dire Wolf decodes
./build/wav_decode --tx $W/fw1200.wav 1200 $N >/dev/null
row "1200 AFSK" "TX (firmware -> Dire Wolf)" $N "$(dw -B 1200 $W/fw1200.wav)" "$(ours $W/fw1200.wav 1200 flat)"
./build/wav_decode --tx $W/fw300.wav 300 $N >/dev/null
row "300 AFSK" "TX (firmware -> Dire Wolf)" $N "$(dw -B 300 $W/fw300.wav)" "$(ours $W/fw300.wav 300 flat)"
./build/wav_decode --tx $W/fw9600.wav 9600 $N >/dev/null
row "9600 G3RUH" "TX (firmware -> Dire Wolf)" $N "$(dw -B 9600 $W/fw9600.wav)" "$(ours $W/fw9600.wav 9600)"
./build/wav_decode --tx $W/fwv23.wav v23 $N >/dev/null
row "1200 V.23" "TX (firmware -> Dire Wolf)" $N "$(dwv23 $W/fwv23.wav 38400)" "$(ours $W/fwv23.wav v23 flat)"
./build/wav_decode --tx $W/fwfx25.wav 1200 fx25 $N >/dev/null
row "1200 FX.25" "TX (firmware -> Dire Wolf)" $N "$(dw -B 1200 $W/fwfx25.wav)" "$(ours $W/fwfx25.wav 1200 flat)"
