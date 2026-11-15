# Changelog

The DirectShow filter, thumbnail provider and installer have separate version numbers. Earlier development builds were not all published as releases.

## 1.23.2026.1115 — 2026-11-15

- Show the detected MPC-HC version and MPC Video Renderer status in setup.
- Offer to select MPC Video Renderer and enable Direct3D 11 for the current user during installation. Portable installations show manual instructions.
- Document why shared Direct3D 11 textures avoid copying full decoded frames through system memory.

## 1.22.2026.1115 — 2026-11-15

- Prefer shared Direct3D 11 textures for all supported X-OCN clips in Automatic mode when the GPU and renderer support sharing. Verified on FX5, BURANO and VENICE 2 footage.
- Fix an out-of-range error when setup closes with an empty PowerShell error stream.

## 1.21.2026.1115 — 2026-11-15

- Add full-resolution GPU playback for supported 8192 × 4320 X-OCN XT clips in MPC-HC with NVIDIA OpenCL and MPC Video Renderer.
- Rename the filter to X-OCN Playback Bridge, standardize camera labels and remove the incorrect PMW-F55 inference from the codec tag.
- Show system OpenCL GPU detection in setup and the filter properties.

## Installer 1.13.2026.1114 — 2026-11-14

- Shows the detected installation status of MPC-HC, mpv and Explorer thumbnails, and provides links to Sony RAW Viewer and Catalyst Prepare.
- Detects compatible SonyRawDev libraries from DaVinci Resolve, Sony applications and Adobe Premiere Pro.

## Installer 1.10–1.12.2026.1114 — 2026-11-14

- Introduced the Windows installer with selectable MPC-HC, mpv and thumbnail components.
- Added SonyRawDev and player detection, backup of previous settings, restore and uninstall.

## Filter 1.10.2026.1114 — 2026-11-14

- Added support for the SonyRawDev library selected by the installer, including compatible DaVinci Resolve installations.

## Filter 1.8.2026.1114 — 2026-11-14

- Added an approximate camera-native S-Log3 preview and made display LUT changes apply during playback.

## Filter 1.5–1.6.2026.1114 — 2026-11-14

- Extended the filter panel with file metadata and made it accessible when opening MXF files from Windows Explorer.
- Added filter version and author information to the panel.

## Filter 1.4.2026.1113 — 2026-11-13

- Added the mpv playback bridge and an optional half-resolution preview for 8K clips in mpv.

## Filter 1.2–1.3.2026.1113 — 2026-11-13

- Added the filter properties panel with automatic and manual anamorphic desqueeze controls.
- Added English panel text and versioned filter components.
- Introduced Windows Explorer thumbnail support for X-OCN MXF files.

## Filter 1.1.2026.1113 and earlier builds — 2026-11-13

- Established versioned DirectShow filter builds for MPC-HC.
- Added Sony X-OCN MXF decoding through the installed SonyRawDev library, including C1, C2, LT, ST and XT variants supported by the library.
- Added MXF image dimensions, display aspect and multichannel audio handling, plus performance work for high-resolution footage.
