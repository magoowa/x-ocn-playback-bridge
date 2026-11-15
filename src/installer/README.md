# X-OCN Playback Bridge

Installer version 2026-11-14 18:22 · Authored by magoowa

Run the **X-OCN Playback Bridge Setup.exe** installer. Windows will request administrator permission, which is required for the Explorer thumbnail handler and installations of mpv under Program Files.

The installer detects MPC-HC x64, mpv x64 and installed SonyRawDev libraries. It lists the DLL version and path, including versions from Adobe Premiere Pro; only 64-bit SonyRawDev 6.0 or later can be selected for installation. The installer searches common machine and per-user locations and application registration entries; unusual paths and portable MPC-HC configurations may not be detected.

Select playback for MPC-HC, playback for mpv, Explorer thumbnails, or a combination. Playback requires the locally installed SonyRawDev DLL and the relevant player. MPC-HC currently provides the best performance; mpv uses a half-resolution 4K-class preview for 8K material. The installer does not redistribute SonyRawDev or change file associations.

The installer saves prior values and replaced files before making changes. Run the same installer EXE again and select **Restore previous state** or **Uninstall** to undo that installation; both restore the saved state. Keep the installer EXE to access these actions. Changes made by other applications after installation are left untouched and reported as conflicts. Close MPC-HC and mpv before installing or restoring. Explorer may need to be restarted before a thumbnail handler change becomes visible.

The thumbnail provider falls back to Icaros for other MXF formats when Icaros is installed. To avoid disrupting other software, setup refuses to replace an unknown MXF thumbnail provider. It does not clear the thumbnail cache.

## Credits and dependencies

- Project and filter: **magoowa**.
- [fx5-xocn-tools](https://github.com/Dec18studios/fx5-xocn-tools), Dec18 Studios: X-OCN parsing and SonyRawDev reference, MIT; see `LICENSE-fx5-xocn-tools.txt`.
- DirectShow BaseClasses, Microsoft: filter framework, MIT; see `LICENSE-DirectShow-baseclasses.txt`.
- AviSynth+ and DirectShowSource contributors: mpv bridge; see `LICENSE-AviSynth.txt`.
- SonyRawDev, DaVinci Resolve, Sony software, MPC-HC and mpv are separately installed and are not redistributed.

The requested startup/seek prebuffer for 8K is not implemented; dropped frames can still occur. The filter outputs an approximate 8-bit camera-native S-Log3 preview, not calibrated S-Gamut3.Cine.

## MPC Video Renderer

For GPU playback in MPC-HC x64, choose MPC Video Renderer under Options > Playback > Output, then enable Use Direct3D 11 in the renderer settings. Installer setup shows the detected player version and can apply both settings for the current user. For portable MPC-HC, change the settings inside MPC-HC.

The filter can pass shared Direct3D 11 textures directly to MPC Video Renderer on supported GPUs. This avoids sending uncompressed frames through system RAM and improves headroom for full-resolution 8K.
