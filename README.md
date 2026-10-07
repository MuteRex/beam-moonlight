# Moonlight PC — Beam fork

This is a fork of [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt)
used by **[Beam](https://github.com/MuteRex/beam)**, a Parsec-style GTK4 launcher
for Moonlight + Sunshine. It adds an in-stream menu, a redesigned stats overlay
and a few Linux fixes. All changes live on the `beam-overlay` branch; `master`
is upstream, unchanged. Everything upstream Moonlight does still works, and the
fork runs fine on its own without Beam.

Tested on Linux (Wayland, EGL/VAAPI) against Sunshine. The in-stream menu is
Linux-only.

## What's changed

### In-stream Beam menu
A clickable pill at the top centre of the stream opens a menu drawn over the
video, so stream controls don't need memorised shortcuts.

- **Idle:** a slim handle on the top edge that expands into a "Beam" pill on hover.
- **Menu items:** Mouse mode (Desktop/Game), Fullscreen, Full immersion,
  Screen, Performance stats (cycles Off → Basic → Standard → Advanced), Paste
  clipboard (Stop pasting while one is typing), Release/Capture mouse,
  Minimize, Disconnect.
- **Full immersion** captures system keys in any window mode, so Super,
  Alt+Tab and the rest go to the host (GNOME asks once to allow it; Super+Esc
  is GNOME's own way out). Off returns them to the local desktop.
- **Screen** buttons switch which host monitor is streamed, by sending
  Sunshine's Ctrl+Alt+Shift+F1..F12 shortcut as real key presses.
- **Ctrl+Alt+Shift+B** opens/closes the menu from anywhere, including Game
  mode, where it temporarily releases mouse capture and restores it on close.
- Items call Moonlight's existing shortcut handlers, so no stream logic is duplicated.
- Clicks on the pill or menu never reach the host. Buttons held when the menu
  opens are released on the host, so nothing gets stuck down.
- The real cursor is shown over the pill and menu (in Desktop mouse mode the
  host-drawn cursor would otherwise be hidden under the overlay).
- Scales with display height (1×–2.5×) and re-places itself on window resize.

Files: `app/streaming/input/controls.cpp` (layout, drawing, hit-testing, menu
state), `app/streaming/beamdraw.{h,cpp}` (anti-aliased rounded shapes and text),
hooks in `input.cpp`, `mouse.cpp`, `keyboard.cpp`, `session.cpp`.

### Performance stats panel (replaces the plain-text stats)
The stats overlay (Ctrl+Alt+Shift+S, the menu, or the gamepad combo) is now a
styled panel with three levels, colour-coded green / amber / red against
healthy-LAN targets:

| Level | Shows |
|-------|-------|
| **Basic** | FPS, frametime, total latency, in one compact pill |
| **Standard** | + latency breakdown (host / network / decode / render), stream format, bitrate, dropped frames |
| **Advanced** | + frametime min/max and jitter, per-stage FPS (host › network › decode › render), network RTT and variance, peak bitrate, display mode, V-Sync, GPU, decoder, CPU, Moonlight's own CPU use |

Frametime is measured as the real interval between presented frames, recorded
in the pacer; gaps over one second, such as a minimised window, are left out.
Moonlight's end-of-session stats log is unchanged.

Files: `app/streaming/beamstats.{h,cpp}`, `publishBeamStats()` in
`app/streaming/video/ffmpeg.cpp`, frame-interval tracking in `pacer.cpp` and
`decoder.h`.

### Paste typed as real keystrokes
Upstream sends the clipboard as a Unicode text event, which Sunshine on Linux
turns into Ctrl+Shift+U sequences that terminals and many apps ignore. The fork
types it as US-layout key presses instead, falling back to text events only for
characters that have no key.

- Typed on a background thread with a 5 ms gap between events (Sunshine on
  Linux drops back-to-back key events, and a lost key-up auto-repeats on the
  host), so the stream keeps running while it types.
- Paste again to stop. A stopped paste releases any held key or Shift on the host.
- Capped at 16 KB (cut at a UTF-8 character boundary); CRLF becomes a single Enter.

### Window placement on Wayland
- **`BEAM_DISPLAY_POS=x,y`** opens the stream on the monitor at that layout
  position. On Wayland the Qt window can't report its screen, so streams
  otherwise open on display 0.
- Windowed streams are capped at 90% of the display, because a display-sized
  window gets maximised on Wayland.
- When started from the command line, the progress window opens as a normal
  window rather than taking the saved maximised/fullscreen UI mode, which
  also leaked into the stream window state.

### Overlay plumbing
`OverlayManager` gains custom-surface overlays, which keep their surface and re-send it to a
new renderer (`updateOverlaySurface`), and recorded top-centre placement for
hit-testing (`placeTopCenterOverlay`, `windowPointToOverlay`, which also maps
HiDPI window points to drawable pixels). The DRM, EGL, libplacebo/Vulkan, SDL,
VAAPI and VDPAU renderers place the new overlay; VAAPI also reports the GPU name
for the stats panel.

## Environment variables

| Variable | Effect |
|----------|--------|
| `BEAM_HIDE_PILL=1` | Hide the pill; Ctrl+Alt+Shift+B still opens the menu |
| `BEAM_STATS_LEVEL=basic\|standard\|advanced` | Starting stats level (default standard) |
| `BEAM_DISPLAY_POS=x,y` | Monitor to open the stream on |
| `BEAM_SCREENS=n` | Screen buttons in the menu, 1–12 (default 2) |

Beam sets these from its settings.

## Building

Same as upstream (see below), from this branch:

```bash
git clone --recursive -b beam-overlay https://github.com/MuteRex/beam-moonlight.git
cd beam-moonlight && qmake6 && make -j"$(nproc)" release   # → app/moonlight
```

`SDL2_ttf` is required, as upstream already needs it. Fonts are taken from the system
(Ubuntu, Cantarell or DejaVu).

## License

GPL-3.0, like upstream. Original README follows.

---

# Moonlight PC

[Moonlight PC](https://moonlight-stream.org) is an open source PC client for NVIDIA GameStream and [Sunshine](https://github.com/LizardByte/Sunshine).

Moonlight also has mobile versions for [Android](https://github.com/moonlight-stream/moonlight-android) and [iOS](https://github.com/moonlight-stream/moonlight-ios).

You can follow development on our [Discord server](https://moonlight-stream.org/discord) and help translate Moonlight into your language on [Weblate](https://hosted.weblate.org/projects/moonlight/moonlight-qt/).

 [![Build](https://img.shields.io/github/actions/workflow/status/moonlight-stream/moonlight-qt/build.yml?branch=master)](https://github.com/moonlight-stream/moonlight-qt/actions/workflows/build.yml?query=branch%3Amaster)
 [![Downloads](https://img.shields.io/github/downloads/moonlight-stream/moonlight-qt/total)](https://github.com/moonlight-stream/moonlight-qt/releases)
 [![Translation Status](https://hosted.weblate.org/widgets/moonlight/-/moonlight-qt/svg-badge.svg)](https://hosted.weblate.org/projects/moonlight/moonlight-qt/)

## Features
 - Hardware accelerated video decoding on Windows, Mac, and Linux
 - H.264, HEVC, and AV1 codec support (AV1 requires Sunshine and a supported host GPU)
 - YUV 4:4:4 support (Sunshine only)
 - HDR streaming support
 - 7.1 surround sound audio support
 - 10-point multitouch support (Sunshine only)
 - Gamepad support with force feedback and motion controls for up to 16 players
 - Support for both pointer capture (for games) and direct mouse control (for remote desktop)
 - Support for passing system-wide keyboard shortcuts like Alt+Tab to the host
 
## Downloads
- [Windows, macOS, and Steam Link](https://github.com/moonlight-stream/moonlight-qt/releases)
- [Snap (for Ubuntu-based Linux distros)](https://snapcraft.io/moonlight)
- [Flatpak (for other Linux distros)](https://flathub.org/apps/details/com.moonlight_stream.Moonlight)
- [AppImage](https://github.com/moonlight-stream/moonlight-qt/releases)
- [Raspberry Pi 4 and 5](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-Raspberry-Pi-4)
- [Generic ARM 32-bit and 64-bit Debian packages](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-ARM%E2%80%90based-Single-Board-Computers) (not for Raspberry Pi)
- [Experimental RISC-V Debian packages](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-RISC%E2%80%90V-Single-Board-Computers)
- [NVIDIA Jetson and Nintendo Switch (Ubuntu L4T)](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-Linux4Tegra-(L4T)-Ubuntu)

### Nightly Builds
- [Downloads](https://nightly.link/moonlight-stream/moonlight-qt/workflows/build/master)

#### Special Thanks

[![Hosted By: Cloudsmith](https://img.shields.io/badge/OSS%20hosting%20by-cloudsmith-blue?logo=cloudsmith&style=flat-square)](https://cloudsmith.com)

Hosting for Moonlight's Debian and L4T package repositories is graciously provided for free by [Cloudsmith](https://cloudsmith.com).

## Building

### Windows Build Requirements
* Qt 6.11 SDK or later (earlier versions may work but are not officially supported)
* [Visual Studio 2026](https://visualstudio.microsoft.com/downloads/) (Community edition is fine)
* Select **MSVC** option during Qt installation. MinGW is not supported.
* [7-Zip](https://www.7-zip.org/) (only if building installers for non-development PCs)
* Graphics Tools (only if running debug builds)
  * Install "Graphics Tools" in the Optional Features page of the Windows Settings app.
  * Alternatively, run `dism /online /add-capability /capabilityname:Tools.Graphics.DirectX~~~~0.0.1.0` and reboot.

### macOS Build Requirements
* Qt 6.11 SDK or later (earlier versions may work but are not officially supported)
* Xcode 15 or later (earlier versions may work but are not officially supported)
* [create-dmg](https://github.com/sindresorhus/create-dmg) (only if building DMGs for use on non-development Macs)

### Linux/Unix Build Requirements
* Qt 6 is recommended, but Qt 5.12 or later is also supported (replace `qmake6` with `qmake` when using Qt 5).
* GCC or Clang
* FFmpeg 4.0 or later
* Install the required packages:
  * Debian/Ubuntu:
    * Base Requirements: `libegl1-mesa-dev libgl1-mesa-dev libopus-dev libsdl2-dev libsdl2-ttf-dev libssl-dev libavcodec-dev libavformat-dev libswscale-dev libva-dev libvdpau-dev libxkbcommon-dev wayland-protocols libdrm-dev`
    * Qt 6 (Recommended): `qt6-base-dev qt6-declarative-dev libqt6svg6-dev qt6-wayland qml6-module-qtquick-controls qml6-module-qtquick-templates qml6-module-qtquick-layouts qml6-module-qtqml-workerscript qml6-module-qtquick-window qml6-module-qtquick`
    * Qt 5: `qtbase5-dev qt5-qmake qtdeclarative5-dev qtquickcontrols2-5-dev qml-module-qtquick-controls2 qml-module-qtquick-layouts qml-module-qtquick-window2 qml-module-qtquick2 qtwayland5`
  * RedHat/Fedora (RPM Fusion repo required):
    * Base Requirements: `openssl-devel SDL2-devel SDL2_ttf-devel ffmpeg-devel libva-devel libvdpau-devel opus-devel pulseaudio-libs-devel alsa-lib-devel libdrm-devel`
    * Qt 6 (Recommended): `qt6-qtsvg-devel qt6-qtdeclarative-devel`
    * Qt 5: `qt5-qtsvg-devel qt5-qtquickcontrols2-devel`
* Building the Vulkan renderer requires a `libplacebo-dev`/`libplacebo-devel` version of at least v7.349.0 and FFmpeg 6.1 or later.

### Steam Link Build Requirements
* [Steam Link SDK](https://github.com/ValveSoftware/steamlink-sdk) cloned on your build system
* STEAMLINK_SDK_PATH environment variable set to the Steam Link SDK path

**Steam Link Hardware Limitations**  
Moonlight builds for Steam Link are subject to hardware limitations of the Steam Link device:
* Maximum resolution: **1080p (1920x1080)**
* Maximum framerate: **60 FPS**
* Maximum video bitrate: **40 Mbps**
* **HDR streaming is not supported** on the original hardware

### Docker containers
If you want to use Docker for building, look at [this repo](https://github.com/cgutman/moonlight-packaging) containing canonical containers
for different architectures, which handle building deps and extra linking for you.

### Build Setup Steps
1. Install the latest Qt SDK (and optionally, the Qt Creator IDE) from https://www.qt.io/download
    * You can install Qt via Homebrew on macOS, but you will need to use `brew install qt --with-debug` to be able to create debug builds of Moonlight.
    * You may also use your Linux distro's package manager for the Qt SDK as long as the packages are Qt 5.12 or later.
    * This step is not required for building on Steam Link, because the Steam Link SDK includes Qt 5.14.
2. Download submodules and dependencies
    * Run `git submodule update --init --recursive` from within `moonlight-qt/`.
    * On Windows and macOS, you must also run `setup-deps.ps1` (Windows) or `setup-deps.py` (macOS).
    * Perform these steps each time you pull new changes from the Git repository.
3. Open the project in Qt Creator or build from qmake on the command line.
    * To build a binary for use on non-development machines, use the scripts in the `scripts` folder.
        * For Windows builds, use `scripts\build-arch.bat` and `scripts\generate-bundle.bat`. Execute these scripts from the root of the repository within a Qt command prompt. Ensure  7-Zip binary directory is on your `%PATH%`.
        * For macOS builds, use `scripts/generate-dmg.sh`. Execute this script from the root of the repository and ensure Qt's `bin` folder is in your `$PATH`.
        * For Steam Link builds, run `scripts/build-steamlink-app.sh` from the root of the repository.
    * To build from the command line for development use on macOS or Linux, run `qmake6 moonlight-qt.pro` then `make debug` or `make release`.
        * The final binary will be placed in `app/moonlight`.
    * To create an embedded build for a single-purpose device, use `qmake6 "CONFIG+=embedded" moonlight-qt.pro` and build normally.
        * This build will lack windowed mode, Discord/Help links, and other features that don't make sense on an embedded device.
        * For platforms with poor GPU performance, add `"CONFIG+=gpuslow"` to prefer direct KMSDRM rendering over GL/Vulkan renderers. Direct KMSDRM rendering can use dedicated YUV/RGB conversion and scaling hardware rather than slower GPU shaders for these operations.

## Contribute
1. Fork us
2. Write code
3. Send Pull Requests

Check out our [website](https://moonlight-stream.org) for project links and information.
