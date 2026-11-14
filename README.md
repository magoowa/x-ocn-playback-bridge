<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="assets/logo-dark.png"><img src="assets/logo.png" alt="X-OCN Playback Bridge" width="620"></picture>
</p>

# X-OCN Playback Bridge

Play Sony X-OCN MXF clips in **MPC-HC** or **mpv**, and generate Explorer thumbnails on Windows. The bridge uses your locally installed Sony RAW development library; Sony software and codecs are not bundled.

## Download and install

1. Download the [Windows x64 installer](installer/X-OCN-Playback-Bridge-Setup-1.13.2026.1114.exe?raw=true).
2. Close MPC-HC and mpv, then run the installer. Windows will request administrator permission for system integration.
3. Select MPC-HC, mpv, Explorer thumbnails, or a combination. Open an X-OCN MXF in your player.

The installer detects MPC-HC, mpv and **SonyRawDev 6.0 or later**. It looks in installed DaVinci Resolve, Sony RAW Viewer, Sony Catalyst and Adobe Premiere Pro locations, including common per-user paths. If needed, install [Sony RAW Viewer](https://www.sony.com/electronics/support/software/00408235) or [Catalyst Prepare](https://support.d-imaging.sony.co.jp/disoft_DL/CatalystPrepareV3_DL/win?fm=en). The installer does not change Windows file associations. Portable installations and unusual paths may require manual setup.

## Formats and playback

The source filter supports Sony X-OCN MXF variants **Compact 1 (C1), Compact 2 (C2), LT, ST and XT**, using a compatible SonyRawDev library.

- **MPC-HC**: full active raster, embedded PCM audio, MXF display aspect and anamorphic desqueeze. Filter properties show file metadata and allow a live display LUT toggle.
- **mpv**: playback through the DirectShow and AviSynth+ bridge. Clips at least 7680 pixels wide use a half-resolution preview to improve performance.
- **Explorer**: optional X-OCN thumbnail provider. For other MXF formats it can fall back to Icaros when installed; setup refuses to replace an unknown existing MXF thumbnail provider.

The filter produces 8-bit RGB with an approximate camera-native S-Log3 preview. It is not a calibrated S-Gamut3.Cine transform. Desqueeze settings require reopening the clip. Full-resolution 8K may drop frames, especially at startup or after seeking; prebuffering is not implemented.

## Restore or uninstall

Run the same installer again and select **Restore previous state** or **Uninstall**. The installer saves previous files and settings, and leaves later external changes untouched if they conflict. Keep the installer EXE for this purpose. Close MPC-HC and mpv first; Explorer may need a restart after a thumbnail change.

## Project structure

- `installer/` — self-contained Windows installer.
- `src/filter/` — DirectShow source and Explorer thumbnail provider.
- `src/installer/` and `src/launcher/` — installer and single-file launcher sources, with build payload.
- `src/mpv/` — mpv bridge and clip probe sources.
- `licenses/` — third-party license notices.

The filter build uses Visual Studio C++ tools and the Microsoft Windows Classic Samples DirectShow BaseClasses. See [`src/filter/build.cmd`](src/filter/build.cmd). The installer launcher can be rebuilt with [`src/launcher/Build-Installer.ps1`](src/launcher/Build-Installer.ps1). The SST font used for the logo is not redistributed.

## Credits and dependencies

Created by **magoowa**. Thanks to [Dec18 Studios and fx5-xocn-tools](https://github.com/Dec18studios/fx5-xocn-tools), Microsoft DirectShow BaseClasses, and the AviSynth+ and DirectShowSource contributors. Their license notices are in [`licenses/`](licenses/). SonyRawDev, DaVinci Resolve, Sony applications, MPC-HC and mpv are separate dependencies and are not redistributed.

The installer has been checked with isolated install/restore tests and local clip tests. A full clean-machine installation has not yet been verified.
