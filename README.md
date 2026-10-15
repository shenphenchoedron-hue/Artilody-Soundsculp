# Artilody Soundsculp

A simple multi-track audio editor built with C++ and [JUCE](https://juce.com).  
*En simpel lydredigerings-app med flere spor, bygget med C++ og JUCE.*

![Screenshot](docs/images/screenshot_en.png)

## Features
- Several audio tracks below each other on a shared timeline – all tracks play together, with Mute/Solo per track
- Audacity-style clips: cut/delete leave gaps, split, join, drag clips around and onto other tracks
- Open WAV, AIFF, FLAC, OGG, MP3 (and via ffmpeg M4A/Opus/WMA …); create empty tracks, insert files, merge tracks into one
- Trim, cut/copy/paste, fade in/out, normalize, silence trim, loop, reverse
- Pitch shift without changing the speed (phase vocoder)
- Parametric 4-band EQ (live preview + apply) and spectral noise remover with spectrum display
- Per-track volume line with mouse-editable points
- Numbered, coloured markers per track with text in the Markers panel
- Colourful transport: record, go to start, play, stop, loop (record/play light up)
- Export dialog: WAV (16/24/32f), AIFF, FLAC, Apple Lossless, OGG, MP3, M4A/AAC, Opus – track, selection or all tracks mixed
- Zoom bar, track height, undo/redo; English UI (Danish optional)

## Download
See [Releases](https://github.com/shenphenchoedron-hue/Artilody-Soundsculp/releases) for installers for Windows, macOS, Debian and Ubuntu,
plus the manuals in Danish and English (PDF).

## Building
Requirements: CMake ≥ 3.22, a C++17 compiler. JUCE is downloaded automatically.

```sh
# Linux dependencies (Debian/Ubuntu)
sudo apt install cmake g++ libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libgl1-mesa-dev

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/ArtilodySoundsculp_artefacts/Release/Artilody\ Soundsculp
```

Self tests: run the program with `--selftest` (DSP + file formats) or `--uitest` (editing workflow).  
Manuals: `tools/build_manuals.sh` renders `docs/manual/*.html` to `docs/pdf/*.pdf`.

MP3/M4A/Opus export uses `ffmpeg` (bundled with the Windows and macOS installers; on Linux install the `ffmpeg` package).

## License
GPL-3.0 – see [LICENSE](LICENSE).
