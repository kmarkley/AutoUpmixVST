# AutoUpmix

A VST3 audio plugin that automatically upmixes a stereo (2-channel) signal to 5.1 or 3-channel output. Designed for use on a Raspberry Pi hosted by the **Hang Loose Host** application.

---

## Features

- **Auto-detection** — monitors all 8 input channels; upmix only activates when a stereo signal is present and no multi-channel signal is already in the stream
- **Two upmix modes**:
  - *Surround* — derives FL, FR, CC, SL, SR from the stereo source
  - *Wide stereo* — derives FL, FR, CC with enhanced stereo separation (no surround channels)
- **Bypass** — hard passthrough, zero processing
- **Upmix gain** — trim the upmixed signal from −24 dB to 0 dB
- **16 level meters** — input and output for each of the 8 channels, with peak-hold display
- **Persistent settings** — all parameters survive host/system restarts via VST3 state

---

## I/O Layout

| Ch | Name                | Input | Output |
|----|---------------------|-------|--------|
| 0  | Front Left          | FLi   | FLo    |
| 1  | Front Right         | FRi   | FRo    |
| 2  | Low Frequency Eff.  | LFEi  | LFEo   |
| 3  | Center              | CCi   | CCo    |
| 4  | Surround Left       | SLi   | SLo    |
| 5  | Surround Right      | SRi   | SRo    |
| 6  | Channel 6           | CH6i  | CH6o   |
| 7  | Channel 7           | CH7i  | CH7o   |

---

## Upmix Logic

```
if bypass:
    in → out (passthrough)
elif signal on any channel 2-7:
    in → out (passthrough, upmix inactive)
elif signal on channel 0 and/or 1:
    apply upmix matrix (see below), then scale by upmix gain
else:
    in → out (passthrough, upmix inactive)
```

### Upmix Matrix — Surround mode

| Output | Formula |
|--------|---------|
| FLo    | FLi × 5/6            |
| FRo    | FRi × 5/6            |
| LFEo   | 0                    |
| CCo    | (FLi + FRi) × √10/6  |
| SLo    | −FRi × 1/6           |
| SRo    | −FLi × 1/6           |
| CH6o   | 0                    |
| CH7o   | 0                    |

### Upmix Matrix — Wide stereo mode

| Output | Formula |
|--------|---------|
| FLo    | FLi × 5/6 − FRi × 1/6  |
| FRo    | FRi × 5/6 − FLi × 1/6  |
| LFEo   | 0                       |
| CCo    | (FLi + FRi) × √10/6     |
| SLo    | 0                       |
| SRo    | 0                       |
| CH6o   | 0                       |
| CH7o   | 0                       |

Matrix based on: <http://elias.altervista.org/html/3_speaker_matrix.html>  
Inspired by: <https://github.com/itsalic/StereoToSurroundUpmixer>

---

## Building

### Prerequisites

- CMake ≥ 3.22
- A C++17 compiler (`g++` ≥ 9 on Raspberry Pi OS)
- JUCE (added as a Git submodule)

```bash
# 1. Clone the repo (with JUCE submodule)
git clone --recurse-submodules https://github.com/kmarkley/AutoUpmix.git
cd AutoUpmix

# 2. Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. Build
cmake --build build --config Release -j$(nproc)
```

The compiled `.vst3` bundle appears under `build/AutoUpmix_artefacts/Release/VST3/`.

### Installing on Raspberry Pi

Copy the `.vst3` bundle to the VST3 search path used by Hang Loose Host:

```bash
cp -r build/AutoUpmix_artefacts/Release/VST3/AutoUpmix.vst3 ~/.vst3/
```

Then rescan plugins in Hang Loose Host.

---

## Adding JUCE as a Submodule

If you cloned without `--recurse-submodules`:

```bash
git submodule add https://github.com/juce-framework/JUCE.git JUCE
git submodule update --init --recursive
```

---

## Project Structure

```
AutoUpmix/
├── CMakeLists.txt          ← build system
├── Source/
│   ├── PluginProcessor.h   ← audio engine (signal detection, upmix matrix)
│   ├── PluginProcessor.cpp
│   ├── PluginEditor.h      ← UI (controls, level meters, status)
│   └── PluginEditor.cpp
├── JUCE/                   ← JUCE submodule (not committed, see above)
├── .gitignore
└── README.md
```

---

## License

MIT — see `LICENSE` for details.
