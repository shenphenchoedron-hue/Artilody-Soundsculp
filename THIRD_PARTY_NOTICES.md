# Third-party notices – Artilody Soundsculp

Artilody Soundsculp is © Artilody and licensed under the **GNU General Public
License v3.0** (see `LICENSE`). It incorporates the third-party components listed
below. Their license texts are shipped in the
`licenses/` folder of every package (Windows: installation folder / portable zip,
macOS: `Artilody Soundsculp.app/Contents/Resources/licenses` and the `Licenses`
folder in the DMG, Linux: `/usr/share/doc/artilody-soundsculp/`).

## JUCE 8.0.4 – AGPLv3

The application is built with the [JUCE framework](https://juce.com) 8.0.4, whose
modules are dual-licensed under the **GNU Affero General Public License v3**
(AGPLv3) and the commercial JUCE licence. Artilody Soundsculp uses JUCE under the
**AGPLv3** (`licenses/AGPL-3.0.txt`, `licenses/JUCE-LICENSE.md`).

The executable is therefore a combined work of GPL-3.0 code (this project) and
AGPL-3.0 code (JUCE). This combination is permitted by section 13 of both
licenses: the project's own source remains under GPL-3.0, while the AGPL-3.0
terms (including its section 13) continue to apply to the JUCE part and so to the
combined work as a whole. The complete corresponding source code of the
application is available at
<https://github.com/shenphenchoedron-hue/Artilody-Soundsculp> (JUCE 8.0.4 source:
<https://github.com/juce-framework/JUCE/tree/8.0.4>).

JUCE contains the following third-party code that is compiled into the
application (license texts in `licenses/juce/`):

| Component | License | File |
|---|---|---|
| FLAC (libFLAC) | BSD-3-Clause | `FLAC-LICENSE.txt` |
| Ogg Vorbis (libogg, libvorbis) | BSD-3-Clause | `OggVorbis-LICENSE.txt` |
| jpeglib (Independent JPEG Group) | IJG License | `jpeglib-README.txt` |
| libpng | libpng License | `libpng-LICENSE.txt` |
| zlib | zlib License | `zlib-README.txt` |
| HarfBuzz | Old MIT | `HarfBuzz-COPYING.txt` |
| SheenBidi | Apache-2.0 | `SheenBidi-LICENSE.txt` |

“This software is based in part on the work of the Independent JPEG Group.”

Other JUCE dependencies (VST3/AAX/LV2/AudioUnit SDKs, Oboe, GLEW, Box2D, CHOC,
QuickJS) belong to modules or features that this application does not compile.

## LAME 3.100 – LGPL-2.0-or-later

MP3 export uses the **LAME** MP3 encoder (<https://lame.sourceforge.io>, formerly
www.mp3dev.org), version 3.100, licensed under the **GNU Library General Public
License version 2 or later** (`licenses/LAME-LGPL-2.0.txt`). The unmodified LAME
encoder sources are included in the application's source repository in
[`third_party/lame`](third_party/lame) and are compiled into the application;
the complete source code of the application, including LAME and the build
scripts needed to rebuild and relink it, is available at
<https://github.com/shenphenchoedron-hue/Artilody-Soundsculp>.

## Decoding

MP3 files are decoded with JUCE's own built-in MP3 decoder
(`JUCE_USE_MP3AUDIOFORMAT`), which is part of JUCE and covered by the AGPLv3 notice
above. On macOS, M4A/AAC/CAF files are opened with the operating system's Core
Audio framework. No external programs are included.
