## Artilody Soundsculp 1.1.2

### Download
| System | File |
|---|---|
| Windows 10/11 (64-bit) | `ArtilodySoundsculp-1.1.2-Windows-Setup.exe` (installer) or `…-Windows-Portable.zip` |
| macOS 11+ (Intel + Apple Silicon) | `ArtilodySoundsculp-1.1.2-macOS-Universal.dmg` |
| Debian 12+ | `artilody-soundsculp_1.1.2_debian-12_amd64.deb` |
| Ubuntu 22.04 / 24.04 | `artilody-soundsculp_1.1.2_ubuntu-22.04-24.04_amd64.deb` |

User manuals: `Artilody-Soundsculp-Manual-EN.pdf` (English) and `Artilody-Soundsculp-Manual-DA.pdf` (Danish).

Linux: `sudo apt install ./artilody-soundsculp_*.deb` · macOS: first launch with right-click → Open (the app is not notarised).

### Fixed in 1.1.2
- Linux packages are now **self-contained**: ffmpeg (MP3/M4A/Opus export) is included, nothing has to be installed from third parties
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
