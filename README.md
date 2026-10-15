# Artilody Soundsculp

A simple multi-track audio editor built with C++ and [JUCE](https://juce.com).  
*En simpel lydredigerings-app med flere spor, bygget med C++ og JUCE.*

![Screenshot](docs/images/screenshot_en.png)

## Features
- Open several audio files (WAV, AIFF, FLAC, OGG, MP3, and via ffmpeg M4A/Opus/WMA …) – each becomes a track
- Create empty tracks, insert audio files, copy pieces between tracks, merge tracks into one
- Mouse selection, trim, cut/copy/paste, fade in/out, normalize, silence trim, loop, reverse
- Pitch shift without changing the speed (phase vocoder)
- Parametric 4-band EQ (live preview + apply) and spectral noise remover with spectrum display
- Per-track volume envelope with mouse-editable points
- Text markers on a marker line below the waveform, edited in the Markers panel
- Colourful transport: record, go to start, play, stop, loop (record/play light up)
- Export to WAV (16/24/32f), AIFF, FLAC, OGG Vorbis, MP3, M4A (AAC/ALAC), Opus – track, selection or all tracks mixed
- Undo/redo, zoom, English and Danish user interface

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
