## Artilody Soundsculp 1.1.4

### Download
| System | File |
|---|---|
| Windows 10/11 (64-bit) | `ArtilodySoundsculp-1.1.4-Windows-Setup.exe` (installer) or `…-Windows-Portable.zip` |
| macOS 11+ (Intel + Apple Silicon) | `ArtilodySoundsculp-1.1.4-macOS-Universal.dmg` |
| Debian 12+ | `artilody-soundsculp_1.1.4_debian-12_amd64.deb` |
| Ubuntu 22.04 / 24.04 | `artilody-soundsculp_1.1.4_ubuntu-22.04-24.04_amd64.deb` |

User manuals: `Artilody-Soundsculp-Manual-EN.pdf` (English) and `Artilody-Soundsculp-Manual-DA.pdf` (Danish).

Linux: `sudo apt install ./artilody-soundsculp_*.deb`

macOS (the app is not notarised): on macOS 15 Sequoia and later, open the app once, then go to **System Settings → Privacy & Security** and click **Open Anyway** next to the message about Artilody Soundsculp (on macOS 11–14 you can instead right-click the app → Open). Alternatively run `xattr -dr com.apple.quarantine "/Applications/Artilody Soundsculp.app"` in Terminal. macOS 11 Big Sur and later are fully supported.

Licenses: Artilody Soundsculp is GPL-3.0, uses JUCE under the AGPLv3 and the LAME MP3 encoder under the LGPL. License texts and third-party notices are included in every package (see `THIRD_PARTY_NOTICES.md`).

### Fixed in 1.1.4
- **Volume tool no longer locks the editor**: click Volume again (or press Esc, or use any other tool) to leave volume mode; in volume mode, clicks away from the orange line still make a normal selection
- **The volume line is always visible**, also on tracks without points (dashed line at 0 dB) and when you are not editing it

### Changes in 1.1.3
- **MP3 export with the built-in LAME encoder** (320 or 192 kbps, with LAME tag for exact length). The bundled ffmpeg program was removed, so the packages contain no external programs and **macOS 11+ is fully supported** (Universal: Intel + Apple Silicon)
- **Export formats**: WAV (16/24-bit, 32-bit float), AIFF (24-bit), FLAC (16/24-bit), MP3 (320/192 kbps) and Ogg Vorbis (192 kbps). M4A (AAC / Apple Lossless) and Opus export were removed
- **Opening files**: WAV, AIFF, FLAC, Ogg Vorbis and MP3 (JUCE's built-in decoder) on all systems; M4A/AAC/CAF additionally on macOS. Opus files can no longer be opened, and M4A files only on macOS
- **Export runs in the background** with a progress window, so the program no longer freezes while exporting
- Windows: no Visual C++ Redistributable is needed any more

### Fixed in 1.1.4
- Crashes when editing (cut, paste, split, undo) during playback, when merging or dragging clips while recording, and when closing a track from the Tracks panel
- Exporting over an existing file no longer deletes it if the export fails
- Undo uses far less memory: marker and volume-line edits no longer store a copy of the audio, and the total undo memory is limited
- EQ: no more clicks or level jumps between clips when applying, switching a band off no longer disturbs the other bands, the curve shows the track's real sample rate, and moving the EQ during playback no longer causes dropouts
- Noise remover: switching off the learned profile now really uses the automatic estimate; the spectrum shows the correct frequencies for all sample rates
- Better quality when inserting audio with a different sample rate and when pitch-shifting long selections; the loop tool crossfades at each joint
- Stopping a recording no longer clears the undo history of another track; recording overflow is reported
- Seeking during playback is no longer lost; loop range follows edits and undo
- Export dialog: always opens with a valid choice, file names with dots are kept, size estimate per choice, Mono/Stereo/N ch labels
- Many smaller fixes (empty undo steps, frequency input like "1,000 Hz", Q display, pitch preview stops when the dialog is closed, solo dimming, …)
- macOS: the minimum version (11) is now really applied, so the app starts on macOS 11–14

### Fixed in 1.1.2
- Linux packages are **self-contained**: nothing has to be installed from third parties
- Linux packages no longer install the JUCE development files into /usr/include
- The app icon (from the new Artilody Soundsculp logo) now shows correctly in the Linux application menu; icons are installed in 48–512 px and the menu/icon caches are refreshed on install

### New in 1.1 (since 1.0)
- All tracks are shown below each other on a shared timeline and **play together**
- **Clips** like in Audacity: cut/delete leave a gap, Split (Ctrl+I), Join (Ctrl+J), drag clips by their name bar – also onto other tracks, Delete and close gap (Shift+Delete), right-click menu
- **Mute / Solo** per track
- **Merge** button in the Tracks panel (volume lines are included in the merged track)
- **Markers per track** – only the number is shown under the track, text in the Markers panel, **custom marker colours**
- Zoom bar (zoom in/out, zoom to selection, show all) and adjustable **track height**
- Clearer **Open** buttons
- New, cleaner **Export** dialog
- English user interface by default (Danish available under Settings → Language)
- New Artilody logo and app icon
- The overview strip was removed
